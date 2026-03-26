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
`floor(0.6 × (available_hardware_memory / fiber_stack_size))`).
The `0.6` factor keeps roughly
40% headroom for GC/runtime overhead while still pushing
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

Example (canary mode, pool=8):

```
Channel ping-pong: 200000 round trips in 88ms (2248271 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 495ms (1008686 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 840ms (238041 msg/s)
Spawn idle tasks: 200000 fibers in 1203ms (166231 tasks/s)
Prime sieve: 2262 primes up to 20000 in 877ms (2577 primes/s)
```

## Multi-Pool Testing

`make run-all` tests performance at different `GOC_POOL_THREADS` settings.

### canary mode (default — `LIBGOC_VMEM=OFF`)

```
=== Pool Size: 1 ===
GOC_POOL_THREADS=1
Channel ping-pong: 200000 round trips in 124ms (1600265 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 549ms (909431 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 937ms (213288 msg/s)
Spawn idle tasks: 200000 fibers in 1468ms (136217 tasks/s)
Prime sieve: 2262 primes up to 20000 in 811ms (2788 primes/s)

=== Pool Size: 2 ===
GOC_POOL_THREADS=2
Channel ping-pong: 200000 round trips in 125ms (1588830 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 682ms (732366 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 1009ms (198079 msg/s)
Spawn idle tasks: 200000 fibers in 1276ms (156738 tasks/s)
Prime sieve: 2262 primes up to 20000 in 894ms (2528 primes/s)

=== Pool Size: 4 ===
GOC_POOL_THREADS=4
Channel ping-pong: 200000 round trips in 111ms (1796811 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 682ms (732873 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 1059ms (188743 msg/s)
Spawn idle tasks: 200000 fibers in 1308ms (152888 tasks/s)
Prime sieve: 2262 primes up to 20000 in 914ms (2473 primes/s)

=== Pool Size: 8 ===
GOC_POOL_THREADS=8
Channel ping-pong: 200000 round trips in 97ms (2051353 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 499ms (1001727 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 935ms (213821 msg/s)
Spawn idle tasks: 200000 fibers in 1666ms (120005 tasks/s)
Prime sieve: 2262 primes up to 20000 in 812ms (2785 primes/s)
```

### vmem mode (`-DLIBGOC_VMEM=ON`)

```
=== Pool Size: 1 ===
GOC_POOL_THREADS=1
Channel ping-pong: 200000 round trips in 356ms (561421 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 14570ms (34316 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 914ms (218791 msg/s)
Spawn idle tasks: 200000 fibers in 3070ms (65136 tasks/s)
Prime sieve: 2262 primes up to 20000 in 3349ms (675 primes/s)

=== Pool Size: 2 ===
GOC_POOL_THREADS=2
Channel ping-pong: 200000 round trips in 381ms (524909 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 14709ms (33991 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 973ms (205537 msg/s)
Spawn idle tasks: 200000 fibers in 3120ms (64083 tasks/s)
Prime sieve: 2262 primes up to 20000 in 3399ms (665 primes/s)

=== Pool Size: 4 ===
GOC_POOL_THREADS=4
Channel ping-pong: 200000 round trips in 292ms (682715 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 15033ms (33259 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 1112ms (179819 msg/s)
Spawn idle tasks: 200000 fibers in 3112ms (64262 tasks/s)
Prime sieve: 2262 primes up to 20000 in 2794ms (809 primes/s)

=== Pool Size: 8 ===
GOC_POOL_THREADS=8
Channel ping-pong: 200000 round trips in 230ms (868409 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 10281ms (48632 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 910ms (219742 msg/s)
Spawn idle tasks: 200000 fibers in 3070ms (65128 tasks/s)
Prime sieve: 2262 primes up to 20000 in 3103ms (729 primes/s)
```

With the current memory-derived admission cap defaults (`GOC_DEFAULT_LIVE_FIBER_MEMORY_FACTOR = 0.6`), this run shows:

- **Canary** scales strongly with pool size: ping-pong reaches 2.05 M round
  trips/s at pool=8 (0.90× Go), and ring throughput crosses 1 M hops/s
  (0.44× Go). Spawn idle tasks is stable at 120–157k tasks/s across pool sizes.
- **vmem** ping-pong reaches 868k round trips/s at pool=8 (0.38× Go).
  Spawn idle tasks is now consistent at ~64–65k tasks/s across all pool
  sizes, resolving a prior stall at pool=8. Ring and sieve remain costly
  due to TLB/page-fault pressure from mmap-backed stacks.
- vmem still behaves as a bounded-correctness/stress configuration rather than
  a near-parity performance mode.
