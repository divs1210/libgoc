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

## Benchmark Status

| # | Benchmark | Go | libgoc |
|---|-----------|:--:|:------:|
| 1 | Channel ping-pong | ✅ | ✅ |
| 2 | Ring | ✅ | ✅ |
| 3 | Selective receive / fan-out / fan-in | ✅ | ✅ |
| 4 | Spawn idle tasks | ✅ | 🚧 |
| 5 | Prime sieve | ✅ | 🚧 |

🚧 — Implemented in `bench/libgoc/bench.c` but disabled in `main()`.

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
Channel ping-pong: 200000 round trips in 120ms (1658644 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 751ms (665623 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 716ms (279139 msg/s)

=== Pool Size: 2 ===
GOC_POOL_THREADS=2
Channel ping-pong: 200000 round trips in 294ms (679017 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 1537ms (325279 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 822ms (243094 msg/s)

=== Pool Size: 4 ===
GOC_POOL_THREADS=4
Channel ping-pong: 200000 round trips in 445ms (448940 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 1839ms (271767 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 921ms (216986 msg/s)

=== Pool Size: 8 ===
GOC_POOL_THREADS=8
Channel ping-pong: 200000 round trips in 588ms (339584 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 2037ms (245396 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 1142ms (175014 msg/s)
```

## Report

### Chart

Throughput comparison at GOMAXPROCS / GOC_POOL_THREADS = 1 and 8
(higher is better; libgoc results shown where available).

#### Channel ping-pong (round trips/s)

```
Benchmark           Pool  Go (ops/s)   libgoc (ops/s)  Ratio (libgoc/Go)
------------------  ----  -----------  --------------  -----------------
Channel ping-pong      1  2,280,645      1,658,644          0.73×
Channel ping-pong      2  2,224,597        679,017          0.31×
Channel ping-pong      4  2,228,437        448,940          0.20×
Channel ping-pong      8  2,257,564        339,584          0.15×
```

#### Ring (hops/s)

```
Benchmark  Pool  Go (ops/s)   libgoc (ops/s)  Ratio (libgoc/Go)
---------  ----  -----------  --------------  -----------------
Ring          1  2,243,222        665,623          0.30×
Ring          2  2,284,381        325,279          0.14×
Ring          4  2,240,562        271,767          0.12×
Ring          8  2,250,942        245,396          0.11×
```

#### Selective receive / fan-out / fan-in (msg/s)

```
Benchmark  Pool  Go (ops/s)  libgoc (ops/s)  Ratio (libgoc/Go)
---------  ----  ----------  --------------  -----------------
Fan-in        1    599,056       279,139          0.47×
Fan-in        2    650,773       243,094          0.37×
Fan-in        4    661,967       216,986          0.33×
Fan-in        8    657,846       175,014          0.27×
```

### Summary

**Ping-pong — overhead of GC stack redirect.**  With a single pool thread,
libgoc achieves ~1.66 M round trips/s vs Go's ~2.28 M (0.73×).  Each
`mco_resume` call is bracketed by `GC_set_stackbottom` to redirect BDW-GC's
stack scan to the fiber's stack — this prevents SIGSEGV when the GC fires
mid-resume, but adds measurable overhead at POOL=1.  As pool threads increase,
cross-thread wakeups dominate and throughput falls to ~0.34 M at POOL=8
(0.15×).  Go's work-stealing scheduler keeps communicating goroutines on the
same thread naturally; libgoc's current pool scheduler does not yet implement
this locality optimisation.

**Ring — significant gap across all thread counts.**  Ring throughput is
~0.11–0.30× of Go's.  The ring places each message on a different pair of
fibers for every hop, amplifying the cross-thread wakeup cost.

**Fan-in — best relative performance.**  Selective receive / fan-out / fan-in
throughput is ~0.27–0.47× of Go's.  The pattern is naturally parallel (8
sender workers feeding one consumer) and benefits from multiple OS threads
less adversely than ping-pong or ring.

**What the numbers do and do not say.**  All three benchmarks measure
inter-fiber communication latency, not CPU computation throughput.  libgoc's
coroutine stack model is heavier than Go's goroutine stacks (minicoro uses a
fixed or virtual-memory-backed stack per fiber vs. Go's automatically-growing
stacks), and the current pool scheduler lacks work-stealing.  These are known
areas of improvement tracked in `TODO.md`.  Benchmarks 4–5 (spawn, sieve)
are implemented in `bench/libgoc/bench.c` and will be enabled in a future
release.

**Go scalability.**  Go scales well on CPU-bound workloads (spawn: 188 K/s →
492 K/s; prime sieve: 1919/s → 14136/s from POOL=1 to POOL=8) while
communication-bound workloads (ping-pong, ring) remain flat because the
bottleneck is channel round-trip latency, not CPU availability.
