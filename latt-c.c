/*
    latt-c.c

    LATT-C: a recursive C lattice game using only the digits 1..9.

    Idea:
      - C is the order constant: C = 3.
      - One local game is a C x C matrix, which has C*C = 9 cells.
      - Every cell recursively expands into another C x C game.
      - Therefore depth d produces a matrix of matrices of size C^d x C^d.
      - Every visible value is wrapped into the digit field {1,2,3,4,5,6,7,8,9}.

    Proof sketch of the simulation invariant:
      1. Size invariant:
         depth 0 is a single seed. Each step expands one cell by C rows and C columns.
         By induction, depth d has C^d rows and C^d columns.

      2. Digit closure invariant:
         wrap9(n) = 1 + (n mod 9), normalized to {1..9}.
         Since every generated payoff passes through wrap9, no value can leave {1..9}.

      3. Recursive termination:
         latt_c_at(x, y, depth, parent) calls itself with depth-1.
         Since depth is finite and nonnegative, it reaches depth 0.

      4. Game-theory trace:
         Each local C x C block is a two-strategy-space payoff matrix.
         The r == c branch receives an added +C, marking a Nash-like self-consistent
         diagonal preference inside the lattice. The full diagonal across recursive
         layers is the ascension trace.
*/

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* The letter C is the order constant of the whole simulation. */
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

/*
   A local C x C game.

   row_strategy and col_strategy are in {0,1,2}.
   local_order is the row-major C-order operation: 1..9.
   interaction is the payoff pressure between the two selected strategies: 1..9.
   nash_bias adds C when the strategy pair lies on the local diagonal.
*/
static int local_game_digit(int row_strategy, int col_strategy, int parent, int level) {
    int local_order = row_strategy * C + col_strategy + 1;
    int interaction = (row_strategy + 1) * (col_strategy + 1);
    int nash_bias = (row_strategy == col_strategy) ? C : 0;
    int counterplay = (row_strategy + col_strategy == C - 1) ? 1 : 0;

    return wrap9(parent + local_order + interaction + nash_bias - counterplay + level * C);
}

/*
   Recursively evaluate the visible digit at coordinate (x,y).
   The coordinate is decomposed into ternary strategy choices because C = 3.
*/
static int latt_c_at(int x, int y, int depth, int parent) {
    if (depth == 0) return wrap9(parent);

    int stride = ipow(C, depth - 1);
    int row_strategy = y / stride;
    int col_strategy = x / stride;
    int next_parent = local_game_digit(row_strategy, col_strategy, parent, depth);

    return latt_c_at(x % stride, y % stride, depth - 1, next_parent);
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

static void print_lattice(int depth) {
    int size = ipow(C, depth);

    for (int y = 0; y < size; ++y) {
        int rr = boundary_rank(y, depth);
        if (y > 0 && rr > 0) print_break(size, depth, rr);

        printf("  ");
        for (int x = 0; x < size; ++x) {
            int br = boundary_rank(x, depth);
            if (x > 0 && br > 0) printf(br > 1 ? "| " : ": ");
            printf("%d ", latt_c_at(x, y, depth, C));
        }
        putchar('\n');
    }
}

static void print_potential(int depth) {
    int size = ipow(C, depth);
    uint64_t total = 0;
    uint64_t nash_trace = 0;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            int v = latt_c_at(x, y, depth, C);
            total += (uint64_t)v;
            if (x == y) nash_trace += (uint64_t)v;
        }
    }

    printf("  payoff-potential = %" PRIu64 " | diagonal-ascension-trace = %" PRIu64 "\n",
           total, nash_trace);
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

    puts("LATT-C :: recursive latticework game matrix");
    printf("C = %d | C*C = %d | digit field = {1..9}\n", C, DIGITS);
    puts("operator: parent -> C-order -> interaction -> diagonal bias -> wrap9");

    for (int depth = 1; depth <= max_depth; ++depth) {
        int size = ipow(C, depth);
        printf("\nASCENSION LAYER %d :: %d x %d matrix of matrices\n", depth, size, size);
        print_potential(depth);
        print_lattice(depth);
    }

    return 0;
}
