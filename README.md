# Limit Order Book

Two implementations of a price-time-priority limit order book in C++17, held to
identical behavior by a differential test, and benchmarked against each other.

The question the project answers is not "can I write an order book." It is
"what does the optimized representation actually buy, and what does it cost."

## Build and run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/test_correctness    # hand-written matching rules
./build/test_differential   # 1M ops, both books must agree exactly
./build/bench               # dense vs sparse price domain
```

No dependencies beyond a C++17 compiler and CMake. Runs on macOS and Linux.

## The two implementations

**MapBook** is the obvious version. `std::map<Price, std::list<Order>>` keeps
levels sorted so the best price is `begin()`, a `std::list` per level preserves
FIFO time priority, and an `unordered_map` from order id to list iterator makes
cancel a lookup instead of a scan.

**ArrayBook** changes three things:

| | MapBook | ArrayBook |
|---|---|---|
| find a price level | red-black tree descent, O(log n) pointer chasing | array index, O(1) |
| store an order | one heap allocation per order | slot in a preallocated pool, no allocation |
| find best bid/ask | `map.begin()` | cached offset, repaired only when that level empties |

The cost is a bounded, dense price domain. The level array is sized for the
whole configured tick range up front.

## Results

Measured on the machine that produced this README. Ratios are the portable
number, absolute nanoseconds are not.

Dense book, 2001 tick range, orders clustered near the touch:

| operation | MapBook mean | ArrayBook mean | speedup |
|---|---:|---:|---:|
| add | 60.6 ns | 27.6 ns | 2.20x |
| cancel | 39.0 ns | 16.7 ns | 2.34x |
| market | 55.8 ns | 45.4 ns | 1.23x |

Same order flow, price domain widened 100x to 200001 ticks:

| | reserved level slots | occupied | memory for levels |
|---|---:|---:|---:|
| dense | 4,002 | 180 (4.50%) | 31 KB |
| sparse | 400,002 | 180 (0.04%) | 3,125 KB |

Latency barely moves, because the resting orders still cluster in the same
narrow band and the cached best-price offsets keep repair scans short. What
degrades is memory: the array reserves 100x more space to hold the same 180
occupied levels. That is the entire tradeoff, and it is why a real venue with a
wide or unbounded tick range does not use a flat array naively.

Cancel shows the largest speedup because it is the operation where the two
designs differ most: a tree lookup plus a list node free, versus an array index
plus an unlink into a free list. Market orders show the smallest speedup
because most of their cost is the matching loop and trade recording, which both
implementations share.

## Layout

```
include/ob/types.hpp       Price, Quantity, OrderId, Side, Trade
include/ob/book.hpp        shared interface both implementations satisfy
include/ob/map_book.hpp    baseline
include/ob/array_book.hpp  optimized
include/ob/generator.hpp   deterministic seeded order flow
src/                       implementations
tests/test_correctness.cpp matching rules, run against both books
tests/test_differential.cpp 50 seeds x 20k ops, output must be identical
bench/bench_main.cpp       benchmark harness
docs/design.md             decisions and rejected alternatives
docs/INTERVIEW.md          questions this project invites, with answers
JOURNAL.md                 what went wrong while building it
```

## Matching rules

- Price priority first, then time priority within a level (FIFO).
- A trade prints at the resting order's price, not the incoming order's.
- A limit order matches while the book's best price is acceptable, then rests
  whatever remains.
- A market order sweeps levels with no price bound and never rests. Unfilled
  quantity is discarded.
- Cancelling an order that has already filled returns false rather than
  erroring, because that race is normal.
