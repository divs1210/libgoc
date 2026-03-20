# libgoc Benchmarks

Standalone CSP benchmarks implemented in C using libgoc.

Built separately from the main libgoc library; no changes to the main CMake
build are required to run them.

## Prerequisites

- C11 compiler (GCC or Clang)
- CMake ≥ 3.20
- pkg-config
- libuv
- Threaded Boehm GC (`bdw-gc-threaded`)

## Building and Running

```sh
# Build (compiles libgoc.a via CMake, then compiles bench-libgoc)
make build

# Single run (uses current GOC_POOL_THREADS, or the default pool size)
make run

# Multi-pool testing — runs with GOC_POOL_THREADS = 1, 2, 4, 8
make run-all
```

## Benchmarks

All five benchmarks are **implemented** in `bench.c`.  Benchmarks 1 and 2 are
currently enabled; the remaining three are compiled but disabled in `main()`
while the benchmark suite is being finalised.

| # | Name | Status |
|---|------|--------|
| 1 | **Channel ping-pong** — two fibers exchange a token back and forth | ✅ enabled |
| 2 | **Ring** — a token is forwarded around a ring of N fibers | ✅ enabled |
| 3 | **Selective receive / fan-out / fan-in** — producer → N workers → `goc_alts` collector | 🚧 disabled |
| 4 | **Spawn idle tasks** — spawn many fibers that park immediately, then wake them | 🚧 disabled |
| 5 | **Prime sieve** — concurrent Eratosthenes pipeline | 🚧 disabled |

## Output Format

All benchmarks produce a single line per run:

```
<description>: <count> <unit> in <ms>ms (<rate> <unit>/s)
```

Example:

```
Channel ping-pong: 200000 round trips in 89ms (2244633 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 656ms (761308 hops/s)
```

## Multi-Pool Testing

`make run-all` tests performance at different `GOC_POOL_THREADS` settings:

```
=== Pool Size: 1 ===
GOC_POOL_THREADS=1
Channel ping-pong: 200000 round trips in 89ms (2244633 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 656ms (761308 hops/s)

=== Pool Size: 8 ===
GOC_POOL_THREADS=8
Channel ping-pong: 200000 round trips in 320ms (624659 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 1019ms (490455 hops/s)
```

Note that single-threaded (pool size 1) performance can be higher than
multi-threaded for benchmarks that are inherently sequential (e.g. ping-pong),
because cross-thread wakeups add latency when the two fibers are forced onto
different OS threads.
