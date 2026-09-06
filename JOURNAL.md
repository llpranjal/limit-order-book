# Benchmark notes

The harness measures 256 operations per timing interval, then divides by the
number of operations. This reduces timer overhead relative to the measured work.
It also prints a clock-resolution probe and takes the median of five runs.

The dense and wide scenarios use the same price dispersion around their
respective midpoints. Widening the configured domain increases reserved array
memory without spreading the occupied levels. That explains why the original
run showed a much larger memory difference than latency difference.

The original recorded add/cancel speedups were about 2.2x/2.3x. Re-run on the
target machine before using those figures. The current harness does not measure
individual-operation tail latency or total process memory.
