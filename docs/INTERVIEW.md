# Interview notes

## Short explanation

The project implements the same order-book rules with two storage layouts.
MapBook uses sorted maps and FIFO lists. ArrayBook uses tick-indexed arrays
and an order pool. A million-operation differential test compares observable
behavior, and a separate benchmark measures their costs.

## Walk through an order

For a buy limit order, visit the lowest ask while its price is acceptable.
Fill the oldest resting order first, record the trade at its price, and remove
fully filled orders. Place any remaining buy quantity at the back of its bid
level. Market orders use the same matching rules but never rest.

## Tradeoffs to explain

- The map stores occupied levels and supports a wide price range. Level lookup
  costs `O(log L)` for `L` occupied levels.
- The array provides direct level lookup but reserves every tick. A wider
  configured range increases memory even when few levels are occupied.
- Cancellation uses an expected constant-time hash lookup in both designs.
  MapBook also finds the level in a tree; ArrayBook unlinks a pool slot.
- ArrayBook may scan `O(R)` ticks after the best level empties. Matching work
  also grows with the number of resting orders filled. Add and cancel are
  therefore not unconditionally constant-time operations.
- Indices survive pool-vector relocation; raw pointers would not. Pool growth
  and hash-map allocations still have costs.

## Evidence and limits

The scenario tests check matching rules independently of the differential test.
Agreement between implementations is useful evidence, but both can share a bug.
Inputs use unique IDs and prices inside the array's configured domain.

Benchmark samples average 256 operations. Their p99 is not per-order p99 latency,
and speedup ratios are not portable guarantees. The wide-domain workload keeps
orders clustered, so it does not measure widely separated occupied levels.

A useful next experiment would spread orders across the price range and measure
best-price repair scans before introducing another data structure.
