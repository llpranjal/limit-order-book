# Limit Order Book

Two C++17 implementations of a price-time-priority order book: a map of FIFO
queues and an array of price levels with pooled order storage. The project
compares their behavior, operation timings, and memory tradeoffs.

## Build and run

Requires a C++17 compiler and CMake.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/test_correctness
./build/test_differential
./build/bench
```

The differential test runs 50 seeds of 20,000 operations, comparing trades,
best prices, and resting order counts after each operation.

## Data structures

| | MapBook | ArrayBook |
|---|---|---|
| Price levels | Sorted maps | Arrays indexed by integer tick |
| Orders at a level | `std::list`, in arrival order | Doubly linked pool slots |
| Cancel lookup | Order ID to list iterator and level | Order ID to pool index |
| Best price | First map entry | Cached offset; scan when its level empties |

ArrayBook reserves storage for every tick in a bounded range. Its pool uses
32-bit indices so links survive vector reallocation. Pool growth, the order-ID
hash map, and trade recording can still allocate memory.

## Matching rules

- Best price first, then FIFO within that price level.
- Trades use the resting order's price.
- A limit order matches acceptable prices and rests any remaining quantity.
- A market order sweeps available levels and discards unfilled quantity.
- Cancelling an absent or filled order returns false.

Inputs assume unique order IDs. ArrayBook ignores prices outside its configured
range; comparisons use prices within that range. There is no feed handling,
order amendment, persistence, or multi-symbol routing.

## Benchmarks

The harness times chunks of 256 operations and reports medians across five
runs. Percentiles describe chunk-average cost, not individual-operation tail
latency. Both absolute timings and speedup ratios depend on hardware, compiler,
and workload.

The original recorded run reported mean add/cancel speedups of 2.20x/2.34x for
ArrayBook in a 2,001-tick domain. Widening the domain to 200,001 ticks increased
reserved level storage from about 31 KiB to 3,125 KiB. These are level-array
sizes, not total process memory or a memory ratio against MapBook.

Orders remain clustered in a narrow band in both scenarios. The wide-domain
case measures the cost of reserving unused levels; it does not test order flow
spread throughout that domain. Emptying the best level can require a scan
across the configured range. Run the benchmark locally before quoting timings.

## Code map

- `src/map_book.cpp`: map and list implementation.
- `src/array_book.cpp`: level arrays, order pool, and matching.
- `include/ob/generator.hpp`: seeded order stream.
- `tests/`: matching scenarios and differential checks.
- `bench/bench_main.cpp`: timing and level-storage comparison.
- [Design notes](docs/design.md) and [interview notes](docs/INTERVIEW.md).
