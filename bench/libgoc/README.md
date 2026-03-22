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

### canary mode (default — fixed stacks with stack-overflow detection)

```sh
# Build
make build

# Single run
make run

# Multi-pool testing (GOC_POOL_THREADS = 1, 2, 4, 8)
make run-all
```

### vmem mode (virtual-memory-backed stacks — opt-in)

```sh
# Build and run benchmarks against a vmem libgoc build
make LIBGOC_VMEM=ON BUILD_DIR=../../build-bench-vmem build run-all
```

In all builds, libgoc now throttles the number of simultaneously materialised
fibers per pool by default (`GOC_MAX_LIVE_FIBERS`, default
`floor(0.7 × (available_hardware_memory / fiber_stack_size)
  × (pool_threads / hardware_threads))`). The `0.7` factor keeps roughly
30% headroom for GC/runtime overhead while still pushing
high throughput. Set `GOC_MAX_LIVE_FIBERS=0` to disable the throttle, or pick
an explicit positive cap for repeatable stress testing.

## Benchmarks

All five benchmarks are **implemented and enabled** in `bench.c`.

| # | Name | Status |
|---|------|--------|
| 1 | **Channel ping-pong** — two fibers exchange a token back and forth | ✅ enabled |
| 2 | **Ring** — a token is forwarded around a ring of N fibers | ✅ enabled |
| 3 | **Selective receive / fan-out / fan-in** — producer → N workers → `goc_alts` collector | ✅ enabled |
| 4 | **Spawn idle tasks** — spawn many fibers that park immediately, then wake them | ✅ enabled |
| 5 | **Prime sieve** — concurrent Eratosthenes pipeline | ✅ enabled |

## Output Format

All benchmarks produce a single line per run:

```
<description>: <count> <unit> in <ms>ms (<rate> <unit>/s)
```

Example (canary mode, pool=1):

```
Channel ping-pong: 200000 round trips in 197ms (1010144 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 1170ms (427214 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 727ms (274870 msg/s)
Spawn idle tasks: 200000 fibers in 4915ms (40688 tasks/s)
Prime sieve: 2262 primes up to 20000 in 1312ms (1724 primes/s)
```

## Multi-Pool Testing

`make run-all` tests performance at different `GOC_POOL_THREADS` settings.

### canary mode (default — `LIBGOC_VMEM=OFF`)

```
=== Pool Size: 1 ===
GOC_POOL_THREADS=1
Channel ping-pong: 200000 round trips in 237ms (841585 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 1133ms (441182 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 753ms (265515 msg/s)
Spawn idle tasks: 200000 fibers in 17888ms (11180 tasks/s)
Prime sieve: 2262 primes up to 20000 in 1114ms (2029 primes/s)

=== Pool Size: 2 ===
GOC_POOL_THREADS=2
Channel ping-pong: 200000 round trips in 123ms (1613367 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 740ms (674984 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 484ms (412647 msg/s)
Spawn idle tasks: 200000 fibers in 15300ms (13071 tasks/s)
Prime sieve: 2262 primes up to 20000 in 1218ms (1857 primes/s)

=== Pool Size: 4 ===
GOC_POOL_THREADS=4
Channel ping-pong: 200000 round trips in 135ms (1475172 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 779ms (641142 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 577ms (346416 msg/s)
Spawn idle tasks: 200000 fibers in 15953ms (12536 tasks/s)
Prime sieve: 2262 primes up to 20000 in 1239ms (1825 primes/s)

=== Pool Size: 8 ===
GOC_POOL_THREADS=8
Channel ping-pong: 200000 round trips in 120ms (1656638 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 633ms (789484 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 624ms (320277 msg/s)
Spawn idle tasks: 200000 fibers in 16270ms (12292 tasks/s)
Prime sieve: 2262 primes up to 20000 in 1186ms (1906 primes/s)
```

### vmem mode (`-DLIBGOC_VMEM=ON`)

```
=== Pool Size: 1 ===
GOC_POOL_THREADS=1
Channel ping-pong: 200000 round trips in 537ms (372188 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 14197ms (35218 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 1120ms (178561 msg/s)
Spawn idle tasks: 200000 fibers in 15492ms (12910 tasks/s)
Prime sieve: 2262 primes up to 20000 in 7613ms (297 primes/s)

=== Pool Size: 2 ===
GOC_POOL_THREADS=2
Channel ping-pong: 200000 round trips in 265ms (752747 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 11927ms (41921 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 1326ms (150720 msg/s)
Spawn idle tasks: 200000 fibers in 17046ms (11732 tasks/s)
Prime sieve: 2262 primes up to 20000 in 7953ms (284 primes/s)

=== Pool Size: 4 ===
GOC_POOL_THREADS=4
Channel ping-pong: 200000 round trips in 290ms (689219 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 12327ms (40559 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 1691ms (118259 msg/s)
Spawn idle tasks: 200000 fibers in 21884ms (9139 tasks/s)
Prime sieve: 2262 primes up to 20000 in 11204ms (202 primes/s)

=== Pool Size: 8 ===
GOC_POOL_THREADS=8
Channel ping-pong: 200000 round trips in 237ms (843651 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 9165ms (54555 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 1746ms (114523 msg/s)
Spawn idle tasks: 200000 fibers in 23049ms (8677 tasks/s)
Prime sieve: 2262 primes up to 20000 in 11499ms (197 primes/s)
```

With the current memory-derived admission cap defaults, the latest run shows:

- **Canary** is strongest on ping-pong (near/parity with Go at pool=8, and
  slight lead at pool=2), while `ring` and `fan-out/fan-in` remain below Go.
  `prime sieve` is best at pool=1 and then mostly flat. `spawn idle` is now
  tightly bounded by admission and no longer spikes with thread count.
- **vmem** remains significantly slower than canary on all five benchmarks,
  especially `ring` and `prime sieve`, and should still be treated primarily as
  a bounded-correctness / stress configuration.
