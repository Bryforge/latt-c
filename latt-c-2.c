#define _POSIX_C_SOURCE 200809L
#define SDL_MAIN_HANDLED

/*
    latt-c.c

    LATT-C :: Ascension Layer 3

    Clean continuous SDL2 animation.   
    Records the first 60 seconds to latt-c-sample.mp4 using ffmpeg.

    Compile:
      gcc -std=c11 -Wall -Wextra -O2 latt-c.c -o latt-c $(sdl2-config --cflags --libs) -lm

    Run:
      ./latt-c

    Controls:
      F      toggle fullscreen
      Q/ESC  quit
*/

#include <SDL2/SDL.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    C = 3,
    DIGITS = C * C,
    DEPTH = 3,
    CELLS = 27,

    FRAME_W = 1980,
    FRAME_H = 1080,

    VIDEO_FPS = 30,
    RECORD_SECONDS = 60
};

static const double DIGIT_TICKS_PER_SECOND = 3.14;
static const char *VIDEO_OUT = "latt-c-sample.mp4";

typedef struct {
    uint64_t payoff;
    uint64_t diagonal;
} Stats;

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

static int wrap9(int n) {
    int z = n % DIGITS;
    if (z < 0) z += DIGITS;
    return z + 1;
}

/*
    Local C-order game:

    C = 3.
    Each local game is a 3x3 matrix.
    Each visible value is wrapped into {1..9}.
    Diagonal cells receive Nash-like self-consistency pressure.
*/
static int local_game_digit(int row, int col, int parent, int level, int tick) {
    int local_order = row * C + col + 1;
    int interaction = (row + 1) * (col + 1);
    int nash_bias = (row == col) ? C : 0;
    int counterplay = (row + col == C - 1) ? 1 : 0;
    int time_order = wrap9(tick + level * C + row - col);

    return wrap9(parent + local_order + interaction + nash_bias - counterplay + time_order);
}

/*
    Recursive lattice evaluation.

    Invariant:
      depth d creates C^d x C^d cells.
      depth 3 creates 27 x 27 cells.
      every value remains inside {1..9}.
*/
static int latt_c_at(int x, int y, int depth, int parent, int tick) {
    if (depth == 0) return wrap9(parent + tick);

    int stride = ipow(C, depth - 1);
    int row = y / stride;
    int col = x / stride;

    int next = local_game_digit(row, col, parent, depth, tick);

    return latt_c_at(
        x % stride,
        y % stride,
        depth - 1,
        next,
        tick + row + col + 1
    );
}

static unsigned char clamp_u8(double x) {
    if (x < 0.0) return 0;
    if (x > 255.0) return 255;
    return (unsigned char)x;
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

static int digit_mask(int d) {
    enum {
        A = 1 << 0,
        B = 1 << 1,
        CSEG = 1 << 2,
        D = 1 << 3,
        E = 1 << 4,
        F = 1 << 5,
        G = 1 << 6
    };

    switch (d) {
        case 1: return B | CSEG;
        case 2: return A | B | G | E | D;
        case 3: return A | B | CSEG | D | G;
        case 4: return F | G | B | CSEG;
        case 5: return A | F | G | CSEG | D;
        case 6: return A | F | E | D | CSEG | G;
        case 7: return A | B | CSEG;
        case 8: return A | B | CSEG | D | E | F | G;
        case 9: return A | B | CSEG | D | F | G;
        default: return A | B | CSEG | D | E | F | G;
    }
}

static void draw_digit(SDL_Renderer *r, int d, int x, int y, int w, int h,
                       unsigned char red,
                       unsigned char green,
                       unsigned char blue,
                       unsigned char alpha) {
    int mask = digit_mask(d);

    int t = h / 8;
    if (t < 2) t = 2;

    int mid = y + h / 2;

    enum {
        A = 1 << 0,
        B = 1 << 1,
        CSEG = 1 << 2,
        D = 1 << 3,
        E = 1 << 4,
        F = 1 << 5,
        G = 1 << 6
    };

    if (mask & A) fill_rect(r, x + t, y, w - 2 * t, t, red, green, blue, alpha);
    if (mask & G) fill_rect(r, x + t, mid - t / 2, w - 2 * t, t, red, green, blue, alpha);
    if (mask & D) fill_rect(r, x + t, y + h - t, w - 2 * t, t, red, green, blue, alpha);

    if (mask & F) fill_rect(r, x, y + t, t, h / 2 - t, red, green, blue, alpha);
    if (mask & B) fill_rect(r, x + w - t, y + t, t, h / 2 - t, red, green, blue, alpha);

    if (mask & E) fill_rect(r, x, mid, t, h / 2 - t, red, green, blue, alpha);
    if (mask & CSEG) fill_rect(r, x + w - t, mid, t, h / 2 - t, red, green, blue, alpha);
}

static int boundary_rank(int i) {
    if (i <= 0 || i >= CELLS) return 0;
    if (i % 9 == 0) return 2;
    if (i % 3 == 0) return 1;
    return 0;
}

static void draw_diagonal(SDL_Renderer *r, int x0, int y0, int x1, int y1) {
    SDL_SetRenderDrawColor(r, 225, 250, 255, 145);

    for (int o = -2; o <= 2; ++o) {
        SDL_RenderDrawLine(r, x0 + o, y0, x1 + o, y1);
        SDL_RenderDrawLine(r, x0, y0 + o, x1, y1 + o);
    }
}

static Stats render_scene(SDL_Renderer *r, int ww, int wh, double seconds) {
    Stats stats = { 0, 0 };

    int tick = (int)floor(seconds * DIGIT_TICKS_PER_SECOND);

    SDL_SetRenderDrawColor(r, 2, 4, 10, 255);
    SDL_RenderClear(r);

    double canvas_w = ww * 0.985;
    double canvas_h = wh * 0.965;

    double ox = (ww - canvas_w) * 0.5;
    double oy = (wh - canvas_h) * 0.5;

    double cell_w = canvas_w / CELLS;
    double cell_h = canvas_h / CELLS;

    for (int y = 0; y < CELLS; ++y) {
        for (int x = 0; x < CELLS; ++x) {
            int x0 = (int)floor(ox + x * cell_w);
            int y0 = (int)floor(oy + y * cell_h);
            int x1 = (int)floor(ox + (x + 1) * cell_w);
            int y1 = (int)floor(oy + (y + 1) * cell_h);

            int cw = x1 - x0;
            int ch = y1 - y0;

            int v = latt_c_at(x, y, DEPTH, C, tick);

            stats.payoff += (uint64_t)v;
            if (x == y) stats.diagonal += (uint64_t)v;

            int diag = (x == y);
            int subdiag = ((x % C) == (y % C));
            int phase = (x * 7 + y * 11 + tick) % DIGITS;

            unsigned char cr = clamp_u8(4 + v * 9 + phase * 3 + diag * 80 + subdiag * 12);
            unsigned char cg = clamp_u8(16 + v * 13 + phase * 4 + diag * 110 + subdiag * 18);
            unsigned char cb = clamp_u8(38 + v * 18 + phase * 5 + diag * 125 + subdiag * 24);

            int pad = diag ? 1 : 2;

            fill_rect(r, x0 + pad, y0 + pad, cw - 2 * pad, ch - 2 * pad, cr, cg, cb, 220);

            int dh = (int)(ch * 0.68);
            int dw = (int)(dh * 0.52);

            if (dw < 8) dw = 8;
            if (dh < 14) dh = 14;

            int dx = x0 + (cw - dw) / 2;
            int dy = y0 + (ch - dh) / 2;

            unsigned char dr = clamp_u8(150 + v * 6 + diag * 60);
            unsigned char dg = clamp_u8(210 + v * 4 + diag * 40);
            unsigned char db = clamp_u8(235 + v * 2 + diag * 20);

            draw_digit(r, v, dx, dy, dw, dh, dr, dg, db, 235);
        }
    }

    for (int i = 0; i <= CELLS; ++i) {
        int x = (int)floor(ox + i * cell_w);
        int y = (int)floor(oy + i * cell_h);
        int rank = boundary_rank(i);

        if (rank == 2) {
            fill_rect(r, x - 1, (int)oy, 2, (int)canvas_h, 120, 220, 255, 120);
            fill_rect(r, (int)ox, y - 1, (int)canvas_w, 2, 120, 220, 255, 120);
        } else if (rank == 1) {
            fill_rect(r, x, (int)oy, 1, (int)canvas_h, 70, 160, 210, 70);
            fill_rect(r, (int)ox, y, (int)canvas_w, 1, 70, 160, 210, 70);
        } else {
            fill_rect(r, x, (int)oy, 1, (int)canvas_h, 35, 80, 120, 24);
            fill_rect(r, (int)ox, y, (int)canvas_w, 1, 35, 80, 120, 24);
        }
    }

    draw_diagonal(
        r,
        (int)ox,
        (int)oy,
        (int)(ox + canvas_w),
        (int)(oy + canvas_h)
    );

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
        fprintf(stderr, "recording disabled: ffmpeg was not found in PATH\n");
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
        "-an -c:v libx264 -preset veryfast -crf 18 "
        "-pix_fmt yuv420p -movflags +faststart \"%s\"",
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

    fprintf(stderr, "recording first %d seconds to %s\n", RECORD_SECONDS, VIDEO_OUT);

    return 1;
}

static void recorder_close(Recorder *rec) {
    if (rec->pipe) {
        fflush(rec->pipe);
        pclose(rec->pipe);
        rec->pipe = NULL;
    }

    if (rec->active) {
        fprintf(stderr, "saved video sample: %s\n", VIDEO_OUT);
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
        "LATT-C :: ASCENSION LAYER 3 :: 27x27",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        FRAME_W,
        FRAME_H,
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

    Recorder recorder;
    recorder_start(&recorder);

    int running = 1;
    int fullscreen = 0;

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
                }

                if (key == SDLK_f) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(
                        window,
                        fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0
                    );
                }
            }
        }

        double seconds = (double)(SDL_GetTicks64() - start_ticks) / 1000.0;

        SDL_SetRenderTarget(renderer, scene);
        Stats stats = render_scene(renderer, FRAME_W, FRAME_H, seconds);

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
                "LATT-C :: ASCENSION LAYER 3 :: 27x27 :: recording %.1fs left :: payoff=%llu :: diagonal=%llu",
                left,
                (unsigned long long)stats.payoff,
                (unsigned long long)stats.diagonal
            );
        } else {
            snprintf(
                title,
                sizeof(title),
                "LATT-C :: ASCENSION LAYER 3 :: 27x27 :: saved %s :: payoff=%llu :: diagonal=%llu",
                VIDEO_OUT,
                (unsigned long long)stats.payoff,
                (unsigned long long)stats.diagonal
            );
        }

        SDL_SetWindowTitle(window, title);
    }

    recorder_destroy(&recorder);

    SDL_DestroyTexture(scene);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
