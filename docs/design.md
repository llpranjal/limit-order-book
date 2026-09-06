# Design notes

## Price and time priority

Integer ticks make price equality exact. Each price level keeps a FIFO queue.
A list or linked pool can remove a cancelled order without reordering the
remaining orders; vector swap-and-pop would break time priority.

## Shared interface

Both implementations expose the `Book` interface so tests can replay the same
operations. The benchmark includes interface dispatch. Shared overhead affects
measured ratios as well as absolute timings.

## Pool indices

ArrayBook links orders with 32-bit indices into a vector. Indices survive vector
relocation and use less link storage than 64-bit pointers. The pool doubles
when exhausted. Hash-map nodes and trade storage can allocate separately.

## Best-price cache

Adding a resting order can improve the cached best price. Removing the last
order at the best level triggers a scan for the next occupied level. Reads are
constant-time; repair can scan the whole tick range.

Separate bid and ask arrays keep each level's side explicit, at the cost of
two arrays spanning the configured domain.

## Measurement

Timing chunks reduces the relative cost of reading the clock. Five repetitions
provide a median for each statistic. Results depend on the operation mix,
allocation behavior, compiler, and machine. The reserved-level memory statistic
excludes the order pool, ID index, and recorded trades.
