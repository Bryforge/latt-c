#define _POSIX_C_SOURCE 200809L
#define SDL_MAIN_HANDLED

#include <SDL2/SDL.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    C = 3,
    DIGITS = C * C,
    DEPTH = 4,
    GRID = 81,

    WINDOW_W = 1980,
    WINDOW_H = 1080,

    FRAME_W = 1980,
    FRAME_H = 1080,

    VIDEO_FPS = 30,
    RECORD_SECONDS = 60
};

static const double TAU = 6.28318530717958647692;
static const char *VIDEO_OUT = "latt-reality-sample.mp4";

typedef struct {
    double amp;
    double phase;
    double energy;
    double life;
} Cell;

typedef struct {
    double ox;
    double oy;
    double w;
    double h;
    double cw;
    double ch;
} Layout;

typedef struct {
    double avg_energy;
    double avg_life;
    uint64_t expressed_cells;
} RenderStats;

typedef struct {
    FILE *pipe;
    uint8_t *pixels;
    int active;
    int done;
    int frames_written;
    int target_frames;
} Recorder;

static int ipow(int base, int exp) {
    int out = 1;
    while (exp-- > 0) out *= base;
    return out;
}

static double clamp01(double x) {
    if (x < 0.0) return 0.0;
    if (x > 1.0) return 1.0;
    return x;
}

static unsigned char clamp_u8(double x) {
    if (x < 0.0) return 0;
    if (x > 255.0) return 255;
    return (unsigned char)x;
}

static double mix(double a, double b, double t) {
    return a * (1.0 - t) + b * t;
}

static int wrap9(int n) {
    int z = n % DIGITS;
    if (z < 0) z += DIGITS;
    return z + 1;
}

static int inside_equilateral_plane(int x, int y) {
    double fy = ((double)y + 0.5) / (double)GRID;
    double fx = (((double)x + 0.5) / (double)GRID) * 2.0 - 1.0;

    return fabs(fx) <= fy;
}

static int recursive_digit(int x, int y, int depth, int parent, int tick) {
    if (depth == 0) {
        return wrap9(parent + tick);
    }

    int stride = ipow(C, depth - 1);

    int row = y / stride;
    int col = x / stride;

    int local_order = row * C + col + 1;
    int interaction = (row + 1) * (col + 1);
    int diagonal_bias = (row == col) ? C : 0;
    int counterplay = (row + col == C - 1) ? 1 : 0;

    int next = wrap9(
        parent +
        local_order +
        interaction +
        diagonal_bias -
        counterplay +
        depth +
        tick
    );

    return recursive_digit(
        x % stride,
        y % stride,
        depth - 1,
        next,
        tick + row + col + 1
    );
}

static int idx(int x, int y) {
    return y * GRID + x;
}

static void seed_field(Cell *field, int seed_tick) {
    for (int y = 0; y < GRID; ++y) {
        for (int x = 0; x < GRID; ++x) {
            Cell *c = &field[idx(x, y)];

            if (!inside_equilateral_plane(x, y)) {
                c->amp = 0.0;
                c->phase = 0.0;
                c->energy = 0.0;
                c->life = 0.0;
                continue;
            }

            int d = recursive_digit(x, y, DEPTH, C, seed_tick);

            double fx = ((double)x + 0.5) / (double)GRID;
            double fy = ((double)y + 0.5) / (double)GRID;

            c->amp = 0.35 + 0.45 * ((double)d / 9.0);
            c->phase = TAU * ((double)d / 9.0) + fx * TAU;
            c->energy = 0.25 + 0.55 * fy + 0.08 * sin((double)d);
            c->life = 0.20 + 0.60 * fabs(sin((fx + fy) * TAU + d));
        }
    }
}

static void update_field(const Cell *cur, Cell *next, int tick) {
    static const int dirs[6][2] = {
        { -1,  0 },
        {  1,  0 },
        {  0, -1 },
        {  0,  1 },
        { -1,  1 },
        {  1, -1 }
    };

    for (int y = 0; y < GRID; ++y) {
        for (int x = 0; x < GRID; ++x) {
            Cell *out = &next[idx(x, y)];

            if (!inside_equilateral_plane(x, y)) {
                out->amp = 0.0;
                out->phase = 0.0;
                out->energy = 0.0;
                out->life = 0.0;
                continue;
            }

            const Cell *self = &cur[idx(x, y)];

            double avg_energy = self->energy;
            double avg_life = self->life;

            double wave_x = cos(self->phase) * self->amp;
            double wave_y = sin(self->phase) * self->amp;

            int n = 1;

            for (int k = 0; k < 6; ++k) {
                int nx = x + dirs[k][0];
                int ny = y + dirs[k][1];

                if (nx < 0 || nx >= GRID || ny < 0 || ny >= GRID) continue;
                if (!inside_equilateral_plane(nx, ny)) continue;

                const Cell *v = &cur[idx(nx, ny)];

                avg_energy += v->energy;
                avg_life += v->life;

                wave_x += cos(v->phase) * v->amp;
                wave_y += sin(v->phase) * v->amp;

                n++;
            }

            avg_energy /= (double)n;
            avg_life /= (double)n;

            double mean_phase = atan2(wave_y, wave_x);
            double coherence = sqrt(wave_x * wave_x + wave_y * wave_y) / (double)n;

            int d = recursive_digit(x, y, DEPTH, C, tick / 3);

            double q = sin(self->phase - mean_phase + ((double)d / 9.0) * TAU);
            double mutation = 0.008 * sin(0.031 * tick + x * 0.17 - y * 0.13 + d);
            double growth = self->life * (1.0 - self->life) * (self->energy - 0.40);

            double life2 =
                self->life +
                0.100 * (avg_life - self->life) +
                0.090 * growth +
                0.018 * q +
                mutation;

            double energy2 =
                self->energy +
                0.160 * (avg_energy - self->energy) +
                0.024 * coherence -
                0.015 * (life2 - 0.5);

            double amp2 =
                self->amp +
                0.130 * (coherence - self->amp) +
                0.018 * sin(mean_phase + d);

            double phase2 =
                self->phase +
                0.050 +
                0.020 * d +
                0.030 * sin(avg_energy * TAU) +
                0.020 * q;

            life2 = mix(life2, 0.50, 0.0025);
            energy2 = mix(energy2, 0.50, 0.0040);
            amp2 = mix(amp2, 0.55, 0.0030);

            while (phase2 >= TAU) phase2 -= TAU;
            while (phase2 < 0.0) phase2 += TAU;

            out->life = clamp01(life2);
            out->energy = clamp01(energy2);
            out->amp = clamp01(amp2);
            out->phase = phase2;
        }
    }
}

static Layout compute_layout(int ww, int wh) {
    Layout l;

    l.h = wh * 0.92;
    l.w = l.h * 2.0 / sqrt(3.0);

    if (l.w > ww * 0.94) {
        l.w = ww * 0.94;
        l.h = l.w * sqrt(3.0) / 2.0;
    }

    l.ox = ((double)ww - l.w) * 0.5;
    l.oy = ((double)wh - l.h) * 0.5;
    l.cw = l.w / (double)GRID;
    l.ch = l.h / (double)GRID;

    return l;
}

static int boundary_rank(int i) {
    if (i <= 0 || i >= GRID) return 0;
    if (i % 27 == 0) return 3;
    if (i % 9 == 0) return 2;
    if (i % 3 == 0) return 1;
    return 0;
}

static void fill_rect(SDL_Renderer *r, int x, int y, int w, int h,
                      unsigned char red,
                      unsigned char green,
                      unsigned char blue,
                      unsigned char alpha) {
    if (w <= 0 || h <= 0) return;

    SDL_Rect rect = { x, y, w, h };
    SDL_SetRenderDrawColor(r, red, green, blue, alpha);
    SDL_RenderFillRect(r, &rect);
}

static void draw_line_thick(SDL_Renderer *r,
                            int x1, int y1,
                            int x2, int y2,
                            int thickness,
                            unsigned char red,
                            unsigned char green,
                            unsigned char blue,
                            unsigned char alpha) {
    SDL_SetRenderDrawColor(r, red, green, blue, alpha);

    for (int o = -thickness; o <= thickness; ++o) {
        SDL_RenderDrawLine(r, x1 + o, y1, x2 + o, y2);
        SDL_RenderDrawLine(r, x1, y1 + o, x2, y2 + o);
    }
}

static RenderStats draw_field(SDL_Renderer *r, const Cell *field, int tick, int ww, int wh) {
    RenderStats stats;
    stats.avg_energy = 0.0;
    stats.avg_life = 0.0;
    stats.expressed_cells = 0;

    Layout l = compute_layout(ww, wh);

    SDL_SetRenderDrawColor(r, 1, 3, 8, 255);
    SDL_RenderClear(r);

    double total_energy = 0.0;
    double total_life = 0.0;

    for (int y = 0; y < GRID; ++y) {
        for (int x = 0; x < GRID; ++x) {
            if (!inside_equilateral_plane(x, y)) continue;

            const Cell *c = &field[idx(x, y)];

            stats.expressed_cells++;
            total_energy += c->energy;
            total_life += c->life;

            int d = recursive_digit(x, y, DEPTH, C, tick / 3);

            double interference = 0.5 + 0.5 * sin(c->phase);
            double diagonal = (x == y) ? 1.0 : 0.0;
            double recursive_glow = ((double)d / 9.0);

            unsigned char red = clamp_u8(
                12 +
                85 * c->life +
                35 * c->energy +
                45 * diagonal
            );

            unsigned char green = clamp_u8(
                35 +
                125 * c->life +
                55 * recursive_glow +
                60 * diagonal
            );

            unsigned char blue = clamp_u8(
                75 +
                130 * c->amp +
                55 * interference +
                45 * diagonal
            );

            int x0 = (int)floor(l.ox + x * l.cw);
            int y0 = (int)floor(l.oy + y * l.ch);
            int x1 = (int)ceil(l.ox + (x + 1) * l.cw);
            int y1 = (int)ceil(l.oy + (y + 1) * l.ch);

            int w = x1 - x0;
            int h = y1 - y0;

            fill_rect(r, x0, y0, w, h, red, green, blue, 230);

            int brx = boundary_rank(x);
            int bry = boundary_rank(y);

            if (brx || bry) {
                unsigned char alpha = 45;
                unsigned char gr = 80;
                unsigned char gg = 190;
                unsigned char gb = 230;

                if (brx == 2 || bry == 2) alpha = 75;
                if (brx == 3 || bry == 3) alpha = 120;

                if (brx) fill_rect(r, x0, y0, 1 + brx, h, gr, gg, gb, alpha);
                if (bry) fill_rect(r, x0, y0, w, 1 + bry, gr, gg, gb, alpha);
            }
        }
    }

    int top_x = (int)(l.ox + l.w * 0.5);
    int top_y = (int)l.oy;

    int left_x = (int)l.ox;
    int left_y = (int)(l.oy + l.h);

    int right_x = (int)(l.ox + l.w);
    int right_y = (int)(l.oy + l.h);

    draw_line_thick(r, top_x, top_y, left_x, left_y, 2, 170, 235, 255, 150);
    draw_line_thick(r, top_x, top_y, right_x, right_y, 2, 170, 235, 255, 150);
    draw_line_thick(r, left_x, left_y, right_x, right_y, 2, 170, 235, 255, 150);

    draw_line_thick(r, top_x, top_y, (left_x + right_x) / 2, left_y, 1, 235, 255, 255, 85);

    if (stats.expressed_cells > 0) {
        stats.avg_energy = total_energy / (double)stats.expressed_cells;
        stats.avg_life = total_life / (double)stats.expressed_cells;
    }

    return stats;
}

static int ffmpeg_available(void) {
    int rc = system("command -v ffmpeg >/dev/null 2>&1");
    return rc == 0;
}

static int recorder_start(Recorder *rec) {
    memset(rec, 0, sizeof(*rec));

    rec->target_frames = VIDEO_FPS * RECORD_SECONDS;
    rec->pixels = malloc((size_t)FRAME_W * (size_t)FRAME_H * 4);

    if (!rec->pixels) {
        fprintf(stderr, "recording disabled: could not allocate pixel buffer\n");
        return 0;
    }

    if (!ffmpeg_available()) {
        fprintf(stderr, "recording disabled: ffmpeg was not found\n");
        free(rec->pixels);
        rec->pixels = NULL;
        return 0;
    }

    char cmd[1024];

    snprintf(
        cmd,
        sizeof(cmd),
        "ffmpeg -y -hide_banner -loglevel error "
        "-f rawvideo -pix_fmt rgba -s %dx%d -r %d -i - "
        "-an -c:v mpeg4 -q:v 3 -pix_fmt yuv420p \"%s\"",
        FRAME_W,
        FRAME_H,
        VIDEO_FPS,
        VIDEO_OUT
    );

    rec->pipe = popen(cmd, "w");

    if (!rec->pipe) {
        fprintf(stderr, "recording disabled: could not start ffmpeg\n");
        free(rec->pixels);
        rec->pixels = NULL;
        return 0;
    }

    rec->active = 1;
    rec->done = 0;
    rec->frames_written = 0;

    fprintf(stderr, "recording up to %d seconds to %s\n", RECORD_SECONDS, VIDEO_OUT);

    return 1;
}

static void recorder_close(Recorder *rec) {
    if (!rec->pipe) return;

    fflush(rec->pipe);
    int rc = pclose(rec->pipe);
    rec->pipe = NULL;

    if (rc == 0 && rec->frames_written > 0) {
        fprintf(stderr, "saved video sample: %s (%d frames)\n", VIDEO_OUT, rec->frames_written);
    } else {
        fprintf(stderr, "recording ended with an ffmpeg error; video may not exist\n");
    }

    rec->active = 0;
    rec->done = 1;
}

static void recorder_destroy(Recorder *rec) {
    if (rec->active) recorder_close(rec);

    free(rec->pixels);
    rec->pixels = NULL;
}

static void recorder_capture(Recorder *rec, SDL_Renderer *r, double seconds) {
    if (!rec->active || rec->done) return;

    int should_have = (int)floor(seconds * VIDEO_FPS);

    if (should_have > rec->target_frames) {
        should_have = rec->target_frames;
    }

    int missing = should_have - rec->frames_written;

    if (missing <= 0) return;

    if (SDL_RenderReadPixels(
            r,
            NULL,
            SDL_PIXELFORMAT_RGBA32,
            rec->pixels,
            FRAME_W * 4
        ) != 0) {
        fprintf(stderr, "recording stopped: SDL_RenderReadPixels failed: %s\n", SDL_GetError());
        recorder_close(rec);
        return;
    }

    size_t bytes = (size_t)FRAME_W * (size_t)FRAME_H * 4;

    for (int i = 0; i < missing; ++i) {
        size_t written = fwrite(rec->pixels, 1, bytes, rec->pipe);

        if (written != bytes) {
            fprintf(stderr, "recording stopped: ffmpeg pipe closed early\n");
            recorder_close(rec);
            return;
        }

        rec->frames_written++;
    }

    if (rec->frames_written >= rec->target_frames) {
        recorder_close(rec);
    }
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "LATTICRA REALITY SUBSTRATE",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WINDOW_W,
        WINDOW_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_ACCELERATED |
        SDL_RENDERER_PRESENTVSYNC |
        SDL_RENDERER_TARGETTEXTURE
    );

    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    SDL_Texture *scene = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        FRAME_W,
        FRAME_H
    );

    if (!scene) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Cell *cur = calloc((size_t)GRID * GRID, sizeof(Cell));
    Cell *next = calloc((size_t)GRID * GRID, sizeof(Cell));

    if (!cur || !next) {
        fprintf(stderr, "allocation failed\n");
        free(cur);
        free(next);
        SDL_DestroyTexture(scene);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Recorder recorder;
    recorder_start(&recorder);

    int tick = 0;
    int running = 1;
    int paused = 0;
    int fullscreen = 0;

    seed_field(cur, 0);

    uint64_t start_ticks = SDL_GetTicks64();

    while (running) {
        SDL_Event e;

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = 0;
            }

            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode key = e.key.keysym.sym;

                if (key == SDLK_ESCAPE || key == SDLK_q) {
                    running = 0;
                } else if (key == SDLK_SPACE) {
                    paused = !paused;
                } else if (key == SDLK_r) {
                    seed_field(cur, tick);
                } else if (key == SDLK_f) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(
                        window,
                        fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0
                    );
                }
            }
        }

        if (!paused) {
            update_field(cur, next, tick);

            Cell *tmp = cur;
            cur = next;
            next = tmp;

            tick++;
        }

        double seconds = (double)(SDL_GetTicks64() - start_ticks) / 1000.0;

        SDL_SetRenderTarget(renderer, scene);
        RenderStats stats = draw_field(renderer, cur, tick, FRAME_W, FRAME_H);

        recorder_capture(&recorder, renderer, seconds);

        SDL_SetRenderTarget(renderer, NULL);

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, scene, NULL, NULL);
        SDL_RenderPresent(renderer);

        char title[256];

        if (recorder.active) {
            double left = RECORD_SECONDS - seconds;
            if (left < 0.0) left = 0.0;

            snprintf(
                title,
                sizeof(title),
                "LATTICRA REALITY SUBSTRATE :: recording %.1fs left :: energy %.3f :: life %.3f :: tick %d",
                left,
                stats.avg_energy,
                stats.avg_life,
                tick
            );
        } else if (recorder.done) {
            snprintf(
                title,
                sizeof(title),
                "LATTICRA REALITY SUBSTRATE :: saved %s :: energy %.3f :: life %.3f :: tick %d",
                VIDEO_OUT,
                stats.avg_energy,
                stats.avg_life,
                tick
            );
        } else {
            snprintf(
                title,
                sizeof(title),
                "LATTICRA REALITY SUBSTRATE :: energy %.3f :: life %.3f :: tick %d",
                stats.avg_energy,
                stats.avg_life,
                tick
            );
        }

        SDL_SetWindowTitle(window, title);
    }

    recorder_destroy(&recorder);

    free(cur);
    free(next);

    SDL_DestroyTexture(scene);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
