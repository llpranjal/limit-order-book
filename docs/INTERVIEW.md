# Questions this project invites

Answers you should be able to give without the code in front of you.

## Explain the project in 60 seconds

I built a limit order book twice. The first version is the textbook one,
`std::map` of price levels with a list per level. The second replaces the tree
with a flat array indexed by price and puts every order in a preallocated pool
linked by 32-bit indices. A differential test replays a million seeded
operations through both and asserts the trade output is identical, which is what
makes the benchmark trustworthy. The array version is about 2.2x faster on adds
and 2.3x on cancels, and it costs 100x more memory when the price domain is
wide, which is the actual point of the project.

## Why is cancel the biggest speedup?

Cancel is where the two designs differ most and share least. In the map version
it is a hash lookup to find the order, a tree lookup to find its level, a list
node erase which frees memory, and possibly a tree node erase if the level
emptied. In the array version it is a hash lookup, an array index, two pointer
writes to unlink, and a push onto the free list. No tree, no allocator.

Add is close behind for the same reason. Market orders show the least
improvement because most of their cost is the matching loop and appending
trades, which is identical code in both.

## Why not just use `std::map` in production?

Because a tree descent is pointer chasing through nodes scattered across the
heap, and every one of those hops is a potential cache miss. An array index is
one multiply-add into memory that is likely already resident.

But `std::map` is the right answer when the price domain is unbounded or very
sparse, which is exactly what the second benchmark scenario shows.

## Where does the array version lose?

Memory, and it is not close. Widening the tick range 100x grows the reserved
level storage from 31 KB to 3.1 MB while the number of occupied levels stays at
180. Utilization drops to 0.04%.

Latency barely moved in that test, because the resting orders still cluster in
a narrow band, so the working set is unchanged and the cached best-price
offsets keep repair scans short. If the flow itself spread across the whole
range, the repair scans would lengthen and the cache behavior would degrade.

The general rule is that the flat array wins when the price domain is bounded
and dense, and loses when it is wide or sparse. Real venues with fine tick
sizes across a huge price range use hybrid structures for this reason.

## What is the worst case for the cached best price?

A cancel that empties the best level forces a linear scan outward to find the
next occupied one. If the book is empty on that side, the scan covers the whole
domain. It is linear over contiguous memory so it is cheap per element, but it
is unbounded in the tick range.

The fix, if it mattered, is a bitset of occupied levels plus a find-first-set
instruction, which turns the scan into a word-at-a-time search. I did not do it
because the measurements did not justify it.

## Why 32-bit indices instead of pointers?

Two reasons. The node gets smaller, so more of them fit in a cache line. And
the pool stays relocatable: when the pool vector grows, its storage moves, which
would dangle every raw pointer. Indices survive a reallocation.

## How do you know the optimized version is correct?

I do not trust it on its own. The differential test runs 50 seeds of 20,000
operations each and compares, after every single operation, the full trade
list, both best prices, and the resting order count. Comparing after each op
rather than at the end means a divergence is reported at the operation that
caused it.

Separately there are hand-written scenarios pinning the matching rules: price
priority beats time priority, FIFO within a level, trades print at the maker's
price, partial fills rest the remainder, market orders discard rather than
rest, and cancelling a filled order returns false.

## What benchmark result initially misled you?

The first version timed every individual operation. The p50 came out identical
for both implementations, which looked like the optimization did nothing.

The cause was clock resolution. `steady_clock` on that machine ticks about every
41 ns, and the operations take 15 to 60 ns, so nearly every sample quantized to
one clock tick. I was measuring the timer.

The harness now times chunks of 256 operations and divides, which puts the
measured interval well above the clock's resolution. It also prints the measured
clock resolution at startup so the reader can check that assumption. After the
fix the difference showed up immediately.

## Why report ratios instead of nanoseconds?

Absolute nanoseconds depend on the CPU, the clock source, and the timer
overhead, so a number from my laptop does not describe your machine. The ratio
between two implementations measured in the same process on the same input
does transfer, and it is the number that actually answers whether the
optimization was worth it.

## What would you change to make this production-realistic?

In rough priority order: order modify and amend, multiple symbols, a
self-trade prevention rule, an event log so the book state can be rebuilt
deterministically after a crash, and a real feed parser instead of a synthetic
generator. I would keep the matching core single-threaded and put a lock-free
queue in front of it rather than locking the book, because the book is a
sequential state machine and making it concurrent buys complexity rather than
throughput.

## What breaks at 10x scale?

Nothing structural on the array side as long as the tick range stays bounded,
because both add and cancel are already O(1). The pool grows by doubling, which
is amortized fine. The parts that would need attention first are the
`unordered_map` from order id to slot, which is the remaining hash lookup on
every operation and could become an open-addressed table, and the trades vector,
which grows without bound and should be a ring buffer that a consumer drains.
