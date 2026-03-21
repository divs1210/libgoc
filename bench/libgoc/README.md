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
# Rebuild libgoc with -DLIBGOC_VMEM=ON, then run
cmake -S ../.. -B ../../build-bench-vmem -DCMAKE_BUILD_TYPE=Release -DLIBGOC_VMEM=ON
cmake --build ../../build-bench-vmem --target goc
make BUILD_DIR=../../build-bench-vmem build run-all
```

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
Channel ping-pong: 200000 round trips in 40ms (4979968 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 107ms (4661227 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 361ms (553123 msg/s)
Spawn idle tasks: 200000 fibers in 3754ms (53268 tasks/s)
Prime sieve: 2262 primes up to 20000 in 534ms (4236 primes/s)
```

## Multi-Pool Testing

`make run-all` tests performance at different `GOC_POOL_THREADS` settings.

### canary mode (default — `LIBGOC_VMEM=OFF`)

```
=== Pool Size: 1 ===
GOC_POOL_THREADS=1
Channel ping-pong: 200000 round trips in 40ms (4979968 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 107ms (4661227 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 361ms (553123 msg/s)
Spawn idle tasks: 200000 fibers in 3754ms (53268 tasks/s)
Prime sieve: 2262 primes up to 20000 in 534ms (4236 primes/s)

=== Pool Size: 2 ===
GOC_POOL_THREADS=2
Channel ping-pong: 200000 round trips in 102ms (1959635 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 261ms (1915119 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 372ms (537148 msg/s)
Spawn idle tasks: 200000 fibers in 4708ms (42473 tasks/s)
Prime sieve: 2262 primes up to 20000 in 405ms (5579 primes/s)

=== Pool Size: 4 ===
GOC_POOL_THREADS=4
Channel ping-pong: 200000 round trips in 104ms (1909436 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 261ms (1908874 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 446ms (447785 msg/s)
Spawn idle tasks: 200000 fibers in 4833ms (41379 tasks/s)
Prime sieve: 2262 primes up to 20000 in 528ms (4281 primes/s)

=== Pool Size: 8 ===
GOC_POOL_THREADS=8
Channel ping-pong: 200000 round trips in 116ms (1710556 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 305ms (1634155 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 576ms (347022 msg/s)
Spawn idle tasks: 200000 fibers in 4546ms (43992 tasks/s)
Prime sieve: 2262 primes up to 20000 in 687ms (3292 primes/s)
```

### vmem mode (`-DLIBGOC_VMEM=ON`)

```
=== Pool Size: 1 ===
GOC_POOL_THREADS=1
Channel ping-pong: 200000 round trips in 75ms (2659572 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 191ms (2607129 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 609ms (328056 msg/s)
Spawn idle tasks: 200000 fibers in 6604ms (30281 tasks/s)
Prime sieve: 2262 primes up to 20000 in 547ms (4134 primes/s)

=== Pool Size: 2 ===
GOC_POOL_THREADS=2
Channel ping-pong: 200000 round trips in 103ms (1927229 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 273ms (1831236 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 376ms (530599 msg/s)
Spawn idle tasks: 200000 fibers in 5122ms (39043 tasks/s)
Prime sieve: 2262 primes up to 20000 in 453ms (4987 primes/s)

=== Pool Size: 4 ===
GOC_POOL_THREADS=4
Channel ping-pong: 200000 round trips in 109ms (1825689 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 268ms (1862514 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 466ms (429146 msg/s)
Spawn idle tasks: 200000 fibers in 5662ms (35319 tasks/s)
Prime sieve: 2262 primes up to 20000 in 566ms (3992 primes/s)

=== Pool Size: 8 ===
GOC_POOL_THREADS=8
Channel ping-pong: 200000 round trips in 118ms (1688129 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 307ms (1625095 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 562ms (355636 msg/s)
Spawn idle tasks: 200000 fibers in 9314ms (21472 tasks/s)
Prime sieve: 2262 primes up to 20000 in 1363ms (1659 primes/s)
```

Note that pool=1 typically shows the best channel throughput for libgoc because
all fibers run cooperatively on one OS thread with no cross-thread wakeup cost.
The spawn idle benchmark benefits from both **deferred fiber materialisation**
(stack allocation is deferred to the first worker dispatch) and **coroutine stack
pooling** (dead `mco_coro` allocations are reused across fibers, eliminating
malloc/free round-trips and TLB/page-fault cost on the bulk of the stack).
Both optimisations are canary-mode only; vmem stacks are excluded from the pool
due to variable committed size.
