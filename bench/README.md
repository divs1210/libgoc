# Benchmarks

This directory contains standalone CSP benchmarks implemented in Go and in C
using libgoc.

1. **Channel ping-pong** — Two tasks pass a single message back and forth,
   measuring the basic cost of a send/receive and the context switch it causes.
2. **Ring** — Many tasks are arranged in a circle and pass a token around,
   stressing scheduling and handoff overhead across a larger group.
3. **Selective receive / fan-out / fan-in** — One producer feeds many workers
   and a collector selects across multiple output channels, stressing select
   logic and load distribution.
4. **Spawn idle tasks** — Create many tasks that immediately block, highlighting
   creation time and memory overhead for lightweight tasks.
5. **Prime sieve** — A pipeline of filters passes numbers through channels to
   find primes, stressing long chains of tasks and sustained channel traffic.

## Running

From this directory:

### Go

```sh
# Single run (uses current GOMAXPROCS)
make -C go run

# Multi-pool testing (runs with GOMAXPROCS = 1, 2, 4, 8)
make -C go run-all
```

### libgoc

```sh
# Single run (uses current GOC_POOL_THREADS)
make -C libgoc run

# Multi-pool testing (runs with GOC_POOL_THREADS = 1, 2, 4, 8)
make -C libgoc run-all
```

## Output Format

Both implementations use consistent integer millisecond formatting:

- **Time**: Integer milliseconds (e.g., `234ms`, `1567ms`)
- **Rates**: Floating-point operations per second (e.g., `1234567 ops/s`)
- **Organization**: Clear section headers for multi-pool runs

## Benchmark Status

| # | Benchmark | Go | libgoc |
|---|-----------|:--:|:------:|
| 1 | Channel ping-pong | ✅ | ✅ |
| 2 | Ring | ✅ | ✅ |
| 3 | Selective receive / fan-out / fan-in | ✅ | 🚧 |
| 4 | Spawn idle tasks | ✅ | 🚧 |
| 5 | Prime sieve | ✅ | 🚧 |

🚧 — Implemented in `bench/libgoc/bench.c` but disabled in `main()` while the
benchmark suite is being finalised.

## Runs

### Benchmark Environment

| Property        | Value                          |
|-----------------|--------------------------------|
| **CPU**         | AMD Ryzen 7 5800H              |
| **Cores**       | 8 cores / 16 threads (SMT)     |
| **Max Clock**   | 4463 MHz                       |
| **L1d / L1i**   | 256 KiB each (per core)        |
| **L2 Cache**    | 4 MiB (per core)               |
| **L3 Cache**    | 16 MiB (shared)                |
| **RAM**         | 13 GiB                         |
| **OS**          | Ubuntu 24.04.4 LTS             |
| **Kernel**      | Linux 6.11.0 x86_64            |

### Go (`make run-all`)

```
=== Pool Size: 1 ===
GOMAXPROCS=1
Channel ping-pong: 200000 round trips in 87ms (2280645 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 222ms (2243222 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 333ms (599056 msg/s)
Spawn idle tasks: 200000 goroutines in 1062ms (188282 tasks/s)
Prime sieve: 2262 primes up to 20000 in 1178ms (1919 primes/s)

=== Pool Size: 2 ===
GOMAXPROCS=2
Channel ping-pong: 200000 round trips in 89ms (2224597 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 218ms (2284381 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 307ms (650773 msg/s)
Spawn idle tasks: 200000 goroutines in 570ms (350786 tasks/s)
Prime sieve: 2262 primes up to 20000 in 570ms (3962 primes/s)

=== Pool Size: 4 ===
GOMAXPROCS=4
Channel ping-pong: 200000 round trips in 89ms (2228437 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 223ms (2240562 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 302ms (661967 msg/s)
Spawn idle tasks: 200000 goroutines in 480ms (416456 tasks/s)
Prime sieve: 2262 primes up to 20000 in 295ms (7647 primes/s)

=== Pool Size: 8 ===
GOMAXPROCS=8
Channel ping-pong: 200000 round trips in 88ms (2257564 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 222ms (2250942 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 304ms (657846 msg/s)
Spawn idle tasks: 200000 goroutines in 406ms (492388 tasks/s)
Prime sieve: 2262 primes up to 20000 in 160ms (14136 primes/s)
```

### libgoc (`make run-all`)

```
=== Pool Size: 1 ===
GOC_POOL_THREADS=1
Channel ping-pong: 200000 round trips in 89ms (2244633 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 656ms (761308 hops/s)

=== Pool Size: 2 ===
GOC_POOL_THREADS=2
Channel ping-pong: 200000 round trips in 253ms (788650 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 1325ms (377239 hops/s)

=== Pool Size: 4 ===
GOC_POOL_THREADS=4
Channel ping-pong: 200000 round trips in 384ms (519721 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 1639ms (305005 hops/s)

=== Pool Size: 8 ===
GOC_POOL_THREADS=8
Channel ping-pong: 200000 round trips in 320ms (624659 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 1019ms (490455 hops/s)
```

## Report

### Chart

Throughput comparison at GOMAXPROCS / GOC_POOL_THREADS = 1 and 8
(higher is better; libgoc results shown where available).

#### Channel ping-pong (round trips/s)

```
              POOL=1            POOL=8
Go        ██████████████████  2,280,645
libgoc    ██████████████████  2,244,633   (≈ parity at POOL=1)

Go        ██████████████████  2,257,564
libgoc    █████████████       624,659     (−72% at POOL=8)
```

```
Benchmark           Pool  Go (ops/s)   libgoc (ops/s)  Ratio (libgoc/Go)
------------------  ----  -----------  --------------  -----------------
Channel ping-pong      1  2,280,645      2,244,633          0.98×
Channel ping-pong      2  2,224,597        788,650          0.35×
Channel ping-pong      4  2,228,437        519,721          0.23×
Channel ping-pong      8  2,257,564        624,659          0.28×
```

#### Ring (hops/s)

```
Benchmark  Pool  Go (ops/s)   libgoc (ops/s)  Ratio (libgoc/Go)
---------  ----  -----------  --------------  -----------------
Ring          1  2,243,222        761,308          0.34×
Ring          2  2,284,381        377,239          0.17×
Ring          4  2,240,562        305,005          0.14×
Ring          8  2,250,942        490,455          0.22×
```

### Summary

**Ping-pong — single-threaded parity; multi-threaded regression.**  With a
single pool thread, libgoc and Go achieve nearly identical ping-pong throughput
(~2.24 M round trips/s vs ~2.28 M).  Both runtimes must perform the same two
rendezvous channel operations per round trip, and the coroutine/goroutine
switch overhead is comparable.

As pool threads increase, libgoc's ping-pong throughput *drops* to ~0.6–0.8 M
round trips/s while Go's stays flat near 2.25 M.  The cause: a ping-pong
pattern forces two fibers to alternate ownership of the token, which means
every send/receive pair involves a cross-thread wakeup when the two fibers
land on different OS threads.  Go's work-stealing scheduler keeps communicating
goroutines on the same thread naturally; libgoc's current pool scheduler does
not yet implement this locality optimisation.

**Ring — significant gap across all thread counts.**  Ring throughput is
~0.14–0.34× of Go's.  The ring places each message on a different pair of
fibers for every hop, amplifying the cross-thread wakeup cost that also
affects ping-pong.  The gap is widest at POOL=4 (0.14×) and partially
recovers at POOL=8 (0.22×), likely because more threads allow more concurrent
forwarding.

**What the numbers do and do not say.**  Both benchmarks measure only
inter-fiber communication latency, not CPU computation throughput.  libgoc's
coroutine stack model is heavier than Go's goroutine stacks (minicoro uses a
fixed or virtual-memory-backed stack per fiber vs. Go's automatically-growing
stacks), and the current pool scheduler lacks work-stealing.  These are known
areas of improvement tracked in `TODO.md`.  Benchmarks 3–5 (fan-in, spawn,
sieve) are implemented in `bench/libgoc/bench.c` and will be enabled once
multi-threaded cross-fiber communication performance improves.

**Go scalability.**  Go scales well on CPU-bound workloads (spawn: 188 K/s →
492 K/s; prime sieve: 1919/s → 14136/s from POOL=1 to POOL=8) while
communication-bound workloads (ping-pong, ring) remain flat because the
bottleneck is channel round-trip latency, not CPU availability.
