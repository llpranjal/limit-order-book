# Design decisions

Each section states the decision, the alternative that was rejected, and why.

## Prices are integers, not doubles

Two orders at "the same price" must compare exactly equal, because that
equality decides whether they share a queue. Floating point does not promise
that. Real venues quote in ticks anyway, so an integer tick index is both
faster and more faithful.

Rejected: `double` prices with an epsilon comparison. Epsilon comparison is not
transitive, which breaks the ordering `std::map` depends on.

## One interface, two implementations

`Book` is an abstract base class. Both books satisfy it, and the differential
test drives the same order stream through both.

This is the load-bearing decision in the project. Without it, the optimized
book is just code that appears to work. With it, every claim about the fast
version is backed by a million operations of agreement with a simple version
that is easy to read and hard to get wrong.

Rejected: templating on the book type. It would remove one virtual call from
the benchmark, but the virtual call is identical for both implementations and
therefore cancels out of the ratio. Readability won.

## Trades print at the maker's price

If a buyer bids 105 and the best offer is 100, the trade happens at 100. The
maker's price was the one displayed, and the taker crossed to reach it.

This is asserted directly in `test_trade_prints_at_maker_price`, because it is
the single most common thing people get wrong when writing a first order book.

## MapBook: `std::list` per level, not `std::deque` or `std::vector`

Time priority means the queue is FIFO, and cancels remove from the middle.
`std::list` gives O(1) erase given an iterator, and iterators stay valid when
other nodes are removed. That iterator stability is what makes the id index
work.

Rejected: `std::vector<Order>` per level with swap-and-pop on cancel. It
destroys time priority, because swapping the last order into the cancelled
slot moves it to the front of the queue.

Rejected: `std::deque`. Middle erase is O(n) and iterators are invalidated.

## ArrayBook: 32-bit indices, not pointers

Pool nodes link to each other with `std::int32_t` indices into the pool vector
rather than raw pointers. Two reasons:

1. The node shrinks. Two 32-bit links instead of two 64-bit pointers.
2. The pool stays relocatable. When the pool grows, a `std::vector` resize
   moves the storage, which would dangle every pointer. Indices survive it.

Rejected: `std::deque` for the pool to get pointer stability. It adds a second
level of indirection on every node access, which is the exact cost we are
trying to remove.

## ArrayBook: cached best bid and ask

`best_bid_off_` and `best_ask_off_` hold the offset of the best occupied level.
Adding an order can only improve the best price, which is a comparison. Only
when the level at the cached offset empties do we scan outward to repair.

Worst case is a full sweep of the price domain. That happens when the book is
genuinely empty near the touch, which is rare and cheap in absolute terms
because the sweep is a linear scan over contiguous memory.

Rejected: recomputing the best on every read. That turns an O(1) read into an
O(range) scan and would make the benchmark meaningless.

Rejected: maintaining a separate sorted structure of occupied levels. That is
just reintroducing the `std::map` we were trying to remove.

## Separate level arrays for bids and asks

Both arrays cover the same tick range. A single shared array would work, since
a crossed book is transient, but two arrays make the invariant "a level holds
only one side" structural instead of something you have to remember.

The cost is double the memory for the price domain, which is already the design
weakness. It is measured and reported rather than hidden.

## The pool grows by doubling

If the free list empties, the pool doubles and the new slots are chained onto
the free list. Amortized O(1), and in steady state it never happens because the
book reaches a working size and stays there.

The benchmark reserves enough up front that growth does not occur during
measurement. A growth event inside a timed region would show up as a tail
outlier that has nothing to do with the design being tested.

## Cancelling a filled order returns false

It does not throw and it is not an error. In any real system a cancel can be in
flight when the order fills. Both implementations must agree on this, so the
generator deliberately emits cancels for ids that may already be gone.
