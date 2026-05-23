#define _POSIX_C_SOURCE 199309L

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum { C = 3, DIGITS = C * C, DEFAULT_DEPTH = 3, MAX_DEPTH = 5 };

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

static void sleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static int local_game_digit(int row, int col, int parent, int level, int tick) {
    int local_order = row * C + col + 1;
    int interaction = (row + 1) * (col + 1);
    int nash_bias = (row == col) ? C : 0;
    int counterplay = (row + col == C - 1) ? 1 : 0;
    int pulse = wrap9(tick + row - col + level);

    return wrap9(parent + local_order + interaction + nash_bias - counterplay + pulse);
}

static int latt_c_at(int x, int y, int depth, int parent, int tick) {
    if (depth == 0) return wrap9(parent + tick);

    int stride = ipow(C, depth - 1);
    int row = y / stride;
    int col = x / stride;
    int next = local_game_digit(row, col, parent, depth, tick);

    return latt_c_at(x % stride, y % stride, depth - 1, next, tick + row + col + 1);
}

static int boundary_rank(int index, int depth) {
    if (index <= 0) return 0;

    for (int p = depth - 1; p >= 1; --p) {
        int stride = ipow(C, p);
        if (index % stride == 0) return p;
    }

    return 0;
}

static void print_break(int size, int depth, int rank) {
    printf("  ");
    for (int x = 0; x < size; ++x) {
        int br = boundary_rank(x, depth);
        if (x > 0 && br > 0) printf(rank > 1 ? "+=" : "+-");
        printf(rank > 1 ? "==" : "--");
    }
    putchar('\n');
}

static void print_lattice(int depth, int tick) {
    int size = ipow(C, depth);

    for (int y = 0; y < size; ++y) {
        int rr = boundary_rank(y, depth);
        if (y > 0 && rr > 0) print_break(size, depth, rr);

        printf("  ");
        for (int x = 0; x < size; ++x) {
            int br = boundary_rank(x, depth);
            if (x > 0 && br > 0) printf(br > 1 ? "| " : ": ");

            int v = latt_c_at(x, y, depth, C, tick);

            if (x == y)
                printf("[%d]", v);
            else
                printf(" %d ", v);
        }
        putchar('\n');
    }
}

static void print_potential(int depth, int tick) {
    int size = ipow(C, depth);
    uint64_t total = 0;
    uint64_t diagonal = 0;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            int v = latt_c_at(x, y, depth, C, tick);
            total += (uint64_t)v;
            if (x == y) diagonal += (uint64_t)v;
        }
    }

    printf("  payoff-potential = %" PRIu64
           " | diagonal-ascension-trace = %" PRIu64 "\n",
           total, diagonal);
}

static int parse_depth(int argc, char **argv) {
    if (argc < 2) return DEFAULT_DEPTH;

    errno = 0;
    char *end = NULL;
    long d = strtol(argv[1], &end, 10);

    if (errno != 0 || end == argv[1] || *end != '\0' || d < 1 || d > MAX_DEPTH) {
        fprintf(stderr, "usage: %s [depth 1..%d]\n", argv[0], MAX_DEPTH);
        exit(1);
    }

    return (int)d;
}

int main(int argc, char **argv) {
    int max_depth = parse_depth(argc, argv);

    uint64_t tick = 0;

    while (1) {
        printf("\033[2J\033[H");

        puts("LATT-C :: looping recursive latticework game matrix");
        printf("C = %d | C*C = %d | digit field = {1..9} | tick = %" PRIu64 "\n",
               C, DIGITS, tick);
        puts("diagonal cells are wrapped in [ ] as the Nash ascension trace");
        puts("press CTRL+C to stop");

        for (int depth = 1; depth <= max_depth; ++depth) {
            int size = ipow(C, depth);

            printf("\nASCENSION LAYER %d :: %d x %d matrix of matrices\n",
                   depth, size, size);

            print_potential(depth, (int)tick);
            print_lattice(depth, (int)tick);
        }

        fflush(stdout);
        sleep_ms(160);
        tick++;
    }

    return 0;
}
