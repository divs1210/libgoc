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

All five benchmarks are **implemented** in `bench.c`.  Benchmarks 1–3 are
currently enabled; benchmarks 4 and 5 are compiled but disabled in `main()`.

| # | Name | Status |
|---|------|--------|
| 1 | **Channel ping-pong** — two fibers exchange a token back and forth | ✅ enabled |
| 2 | **Ring** — a token is forwarded around a ring of N fibers | ✅ enabled |
| 3 | **Selective receive / fan-out / fan-in** — producer → N workers → `goc_alts` collector | ✅ enabled |
| 4 | **Spawn idle tasks** — spawn many fibers that park immediately, then wake them | 🚧 disabled |
| 5 | **Prime sieve** — concurrent Eratosthenes pipeline | 🚧 disabled |

## Output Format

All benchmarks produce a single line per run:

```
<description>: <count> <unit> in <ms>ms (<rate> <unit>/s)
```

Example:

```
Channel ping-pong: 200000 round trips in 120ms (1658644 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 751ms (665623 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 716ms (279139 msg/s)
```

## Multi-Pool Testing

`make run-all` tests performance at different `GOC_POOL_THREADS` settings:

```
=== Pool Size: 1 ===
GOC_POOL_THREADS=1
Channel ping-pong: 200000 round trips in 120ms (1658644 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 751ms (665623 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 716ms (279139 msg/s)

=== Pool Size: 8 ===
GOC_POOL_THREADS=8
Channel ping-pong: 200000 round trips in 588ms (339584 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 2037ms (245396 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 1142ms (175014 msg/s)
```

Note that single-threaded (pool size 1) performance can be higher than
multi-threaded for benchmarks that are inherently sequential (e.g. ping-pong),
because cross-thread wakeups add latency when the two fibers are forced onto
different OS threads.  Each `mco_resume` call is bracketed by
`GC_set_stackbottom` to redirect BDW-GC's stack scan to the active fiber stack,
which adds a small but measurable per-resume cost.
