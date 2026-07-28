# Build journal

Kept during development. Rejected designs, bugs, and results that surprised me.

## Interface before implementations

Wrote `Book` as an abstract base first, before either implementation. The
differential test is the reason this project is worth anything, and it only
works if both books are externally identical. Designing the interface first
forced that.

## Baseline before optimization

Deliberately wrote the `std::map` version first and made it fully correct with
hand-written scenario tests, before touching the fast version. Two reasons: it
gives the differential test something to compare against, and it means the
speedup number is measured against something that actually works rather than
against a strawman.

## Rejected: vector per price level

First instinct for the level container was `std::vector<Order>` with
swap-and-pop on cancel. Realized while writing the time-priority test that this
destroys FIFO ordering. Swapping the last order into the cancelled slot moves a
late arrival to the front of the queue. Went with `std::list`, which also gives
iterator stability so the id index can hold iterators directly.

## Rejected: pointers in the pool

Started with raw pointers linking pool nodes. Realized the pool has to be able
to grow, and a `std::vector` resize relocates storage, which would dangle every
pointer at once. Switched to 32-bit indices. Side benefit: the node got smaller.

## Bug: erasing the map level while holding its iterator

In `match_side`, the inner loop drains a level and the outer loop erases it if
it emptied. First version called `levels.erase(levels.begin())` after the inner
loop, which is fine, but I had also captured `level_price` from the iterator
before the erase. Reordering that read after the erase would have been a
use-after-free. Now the price is read once at the top of the loop, before
anything can invalidate the iterator.

## The benchmark was measuring the clock

This was the big one.

First benchmark timed each operation individually with `steady_clock`. Result:
p50 of 42.0 ns for both implementations, on every operation type. Suspiciously
round, and suspiciously identical.

The clock ticks about every 41 ns on this machine. Operations take 15 to 60 ns.
So nearly every sample rounded to exactly one clock tick, and the measurement
was reporting the timer's resolution rather than the code's cost.

Fix: time chunks of 256 operations and divide by 256. The measured interval is
then several microseconds, far above clock resolution. Also added a startup
probe that measures and prints the clock resolution, so the assumption is
visible rather than buried.

After the fix the difference appeared immediately: 2.2x on adds, 2.3x on
cancels. The optimization had been working the whole time. The instrument was
too coarse to see it.

Lesson: check that your measurement interval is well above your clock's
resolution before believing any result, especially a null result.

## Surprise: the sparse scenario did not slow down

Expected the wide price domain to hurt latency, since the level array becomes
100x larger and should thrash cache. It did not. Add and cancel timings were
unchanged.

Reason: the order flow still clusters in a narrow band around the mid, so the
working set of occupied levels is the same 180 either way. The array is huge but
almost all of it is untouched, so it never enters cache. The cached best-price
offsets also keep the repair scans short, since the next occupied level is
usually adjacent.

What does degrade is memory: 3.1 MB reserved to hold 180 occupied levels, 0.04%
utilization. So the honest conclusion is that this design trades memory for
speed, not that it trades sparsity for speed. Rewrote the README to say that
instead of what I assumed going in.

## Added 5 repetitions with median

Single runs varied by 10 to 15% between invocations. Running the whole
measurement 5 times and reporting the median per statistic made the numbers
stable enough to quote.
