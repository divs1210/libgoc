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
`floor(0.67 × (available_hardware_memory / fiber_stack_size))`).
The `0.67` factor keeps roughly
33% headroom for GC/runtime overhead while still pushing
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
Channel ping-pong: 200000 round trips in 115ms (1728969 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 688ms (726077 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 420ms (475822 msg/s)
Spawn idle tasks: 200000 fibers in 13692ms (14607 tasks/s)
Prime sieve: 2262 primes up to 20000 in 1059ms (2136 primes/s)
```

## Multi-Pool Testing

`make run-all` tests performance at different `GOC_POOL_THREADS` settings.

### canary mode (default — `LIBGOC_VMEM=OFF`)

```
=== Pool Size: 1 ===
GOC_POOL_THREADS=1
Channel ping-pong: 200000 round trips in 115ms (1728969 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 688ms (726077 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 420ms (475822 msg/s)
Spawn idle tasks: 200000 fibers in 13692ms (14607 tasks/s)
Prime sieve: 2262 primes up to 20000 in 1059ms (2136 primes/s)

=== Pool Size: 2 ===
GOC_POOL_THREADS=2
Channel ping-pong: 200000 round trips in 114ms (1746742 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 648ms (771101 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 428ms (467108 msg/s)
Spawn idle tasks: 200000 fibers in 13886ms (14403 tasks/s)
Prime sieve: 2262 primes up to 20000 in 1012ms (2235 primes/s)

=== Pool Size: 4 ===
GOC_POOL_THREADS=4
Channel ping-pong: 200000 round trips in 94ms (2108007 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 688ms (726145 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 540ms (369904 msg/s)
Spawn idle tasks: 200000 fibers in 14265ms (14020 tasks/s)
Prime sieve: 2262 primes up to 20000 in 1031ms (2193 primes/s)

=== Pool Size: 8 ===
GOC_POOL_THREADS=8
Channel ping-pong: 200000 round trips in 112ms (1783970 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 609ms (819886 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 581ms (344187 msg/s)
Spawn idle tasks: 200000 fibers in 13724ms (14573 tasks/s)
Prime sieve: 2262 primes up to 20000 in 1011ms (2237 primes/s)
```

### vmem mode (`-DLIBGOC_VMEM=ON`)

```
=== Pool Size: 1 ===
GOC_POOL_THREADS=1
Channel ping-pong: 200000 round trips in 278ms (717257 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 11906ms (41994 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 931ms (214599 msg/s)
Spawn idle tasks: 200000 fibers in 14182ms (14102 tasks/s)
Prime sieve: 2262 primes up to 20000 in 7448ms (304 primes/s)

=== Pool Size: 2 ===
GOC_POOL_THREADS=2
Channel ping-pong: 200000 round trips in 278ms (719098 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 11979ms (41738 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 1360ms (147001 msg/s)
Spawn idle tasks: 200000 fibers in 14522ms (13771 tasks/s)
Prime sieve: 2262 primes up to 20000 in 7994ms (283 primes/s)

=== Pool Size: 4 ===
GOC_POOL_THREADS=4
Channel ping-pong: 200000 round trips in 295ms (676953 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 12485ms (40047 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 1748ms (114368 msg/s)
Spawn idle tasks: 200000 fibers in 15158ms (13194 tasks/s)
Prime sieve: 2262 primes up to 20000 in 8061ms (281 primes/s)

=== Pool Size: 8 ===
GOC_POOL_THREADS=8
Channel ping-pong: 200000 round trips in 243ms (820580 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 9331ms (53585 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 1744ms (114665 msg/s)
Spawn idle tasks: 200000 fibers in 14732ms (13576 tasks/s)
Prime sieve: 2262 primes up to 20000 in 7927ms (285 primes/s)
```

With the current memory-derived admission cap defaults (`GOC_DEFAULT_LIVE_FIBER_MEMORY_FACTOR = 0.67`), this run shows:

- **Canary** beats Go on ping-pong at all pool sizes (1.04–1.39×), peaking at
  2.1 M round trips/s at pool=4. Fan-out/fan-in exceeds Go at pool=1/2.
  Ring and prime sieve remain below Go at higher pool sizes.
- **vmem** shows improved ping-pong (0.43–0.49× Go) compared to prior runs.
  Ring and prime sieve are dominated by mmap overhead and remain ~0.03× Go.
- vmem still behaves as a bounded-correctness/stress configuration rather than
  a near-parity performance mode.
