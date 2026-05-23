# latt-c
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
