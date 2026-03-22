# Benchmarks

This directory contains standalone CSP benchmarks implemented in Go, in C
using libgoc, and in Clojure using core.async.

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

### Clojure

Requires [Clojure CLI tools](https://clojure.org/guides/install_clojure).

```sh
# Single run (uses default pool size = 8)
make -C clojure run

# Multi-pool testing (runs with CLOJURE_POOL_THREADS = 1, 2, 4, 8)
make -C clojure run-all
```

## Benchmark Status

| # | Benchmark | Go | libgoc | Clojure |
|---|-----------|:--:|:------:|:-------:|
| 1 | Channel ping-pong | ✅ | ✅ | ✅ |
| 2 | Ring | ✅ | ✅ | ✅ |
| 3 | Selective receive / fan-out / fan-in | ✅ | ✅ | ✅ |
| 4 | Spawn idle tasks | ✅ | ✅ | ✅ |
| 5 | Prime sieve | ✅ | ✅ | ✅ |

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

### Go (`make -C go run-all`)

```
=== Pool Size: 1 ===
GOMAXPROCS=1
Channel ping-pong: 200000 round trips in 119ms (1668462 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 308ms (1619555 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 502ms (398338 msg/s)
Spawn idle tasks: 200000 goroutines in 1423ms (140512 tasks/s)
Prime sieve: 2262 primes up to 20000 in 1687ms (1340 primes/s)

=== Pool Size: 2 ===
GOMAXPROCS=2
Channel ping-pong: 200000 round trips in 128ms (1554949 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 334ms (1493978 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 469ms (426361 msg/s)
Spawn idle tasks: 200000 goroutines in 811ms (246540 tasks/s)
Prime sieve: 2262 primes up to 20000 in 873ms (2591 primes/s)

=== Pool Size: 4 ===
GOMAXPROCS=4
Channel ping-pong: 200000 round trips in 131ms (1519533 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 343ms (1455247 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 436ms (458290 msg/s)
Spawn idle tasks: 200000 goroutines in 633ms (315799 tasks/s)
Prime sieve: 2262 primes up to 20000 in 448ms (5041 primes/s)

=== Pool Size: 8 ===
GOMAXPROCS=8
Channel ping-pong: 200000 round trips in 120ms (1657939 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 285ms (1748604 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 395ms (506307 msg/s)
Spawn idle tasks: 200000 goroutines in 609ms (328174 tasks/s)
Prime sieve: 2262 primes up to 20000 in 237ms (9539 primes/s)
```

### libgoc canary — (default) — (`make -C libgoc run-all`)

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

### libgoc vmem (`make -C libgoc LIBGOC_VMEM=ON BUILD_DIR=../../build-bench-vmem build run-all`)

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

### Clojure core.async (`make -C clojure run-all`)

```
=== Pool Size: 1 ===
CLOJURE_POOL_THREADS=1
Channel ping-pong: 200000 round trips in 167ms (1192028 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 173ms (2883906 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 642ms (311273 msg/s)
Spawn idle tasks: 200000 go-blocks in 280ms (711755 tasks/s)
Prime sieve: 2262 primes up to 20000 in 1202ms (1881 primes/s)

=== Pool Size: 2 ===
CLOJURE_POOL_THREADS=2
Channel ping-pong: 200000 round trips in 232ms (861049 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 266ms (1874808 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 568ms (351951 msg/s)
Spawn idle tasks: 200000 go-blocks in 253ms (788886 tasks/s)
Prime sieve: 2262 primes up to 20000 in 1213ms (1864 primes/s)

=== Pool Size: 4 ===
CLOJURE_POOL_THREADS=4
Channel ping-pong: 200000 round trips in 283ms (706072 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 253ms (1976053 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 586ms (340867 msg/s)
Spawn idle tasks: 200000 go-blocks in 260ms (767344 tasks/s)
Prime sieve: 2262 primes up to 20000 in 1323ms (1709 primes/s)

=== Pool Size: 8 ===
CLOJURE_POOL_THREADS=8
Channel ping-pong: 200000 round trips in 344ms (581133 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 342ms (1460544 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 764ms (261466 msg/s)
Spawn idle tasks: 200000 go-blocks in 327ms (610215 tasks/s)
Prime sieve: 2262 primes up to 20000 in 5700ms (397 primes/s)
```

## Report: libgoc vs. Go Baseline (+ Clojure)

This report evaluates the performance of **libgoc (post-optimization)**, **libgoc vmem**, and **Clojure core.async** relative to the **Go** runtime. All figures represent operations per second; the multiplier in parentheses indicates performance relative to the Go baseline (e.g., **1.10x** represents 10% faster, while **0.50x** represents half the speed).

---

### Channel ping-pong (round trips/s)
*Measures overhead of basic synchronization and context switching.*

| Pool | Go (Baseline) | libgoc | libgoc vmem | Clojure |
| :--- | :--- | :--- | :--- | :--- |
| **1** | 1,668,462 | 1,728,969 **(1.04x)** | 717,257 **(0.43x)** | 1,192,028 **(0.71x)** |
| **2** | 1,554,949 | 1,746,742 **(1.12x)** | 719,098 **(0.46x)** | 861,049 **(0.55x)** |
| **4** | 1,519,533 | 2,108,007 **(1.39x)** | 676,953 **(0.45x)** | 706,072 **(0.46x)** |
| **8** | 1,657,939 | 1,783,970 **(1.08x)** | 820,580 **(0.49x)** | 581,133 **(0.35x)** |

### Ring (hops/s)
*Measures message passing latency across a circular topology.*

| Pool | Go (Baseline) | libgoc | libgoc vmem | Clojure |
| :--- | :--- | :--- | :--- | :--- |
| **1** | 1,619,555 | 726,077 **(0.45x)** | 41,994 **(0.03x)** | 2,883,906 **(1.78x)** |
| **2** | 1,493,978 | 771,101 **(0.52x)** | 41,738 **(0.03x)** | 1,874,808 **(1.25x)** |
| **4** | 1,455,247 | 726,145 **(0.50x)** | 40,047 **(0.03x)** | 1,976,053 **(1.36x)** |
| **8** | 1,748,604 | 819,886 **(0.47x)** | 53,585 **(0.03x)** | 1,460,544 **(0.84x)** |

### Selective receive / fan-out / fan-in (msg/s)
*Evaluates complex orchestration and selection logic.*

| Pool | Go (Baseline) | libgoc | libgoc vmem | Clojure |
| :--- | :--- | :--- | :--- | :--- |
| **1** | 398,338 | 475,822 **(1.19x)** | 214,599 **(0.54x)** | 311,273 **(0.78x)** |
| **2** | 426,361 | 467,108 **(1.10x)** | 147,001 **(0.34x)** | 351,951 **(0.83x)** |
| **4** | 458,290 | 369,904 **(0.81x)** | 114,368 **(0.25x)** | 340,867 **(0.74x)** |
| **8** | 506,307 | 344,187 **(0.68x)** | 114,665 **(0.23x)** | 261,466 **(0.52x)** |

### Spawn idle tasks (tasks/s)
*Tests the efficiency of task creation and scheduling.*

| Pool | Go (Baseline) | libgoc | libgoc vmem | Clojure |
| :--- | :--- | :--- | :--- | :--- |
| **1** | 140,512 | 14,607 **(0.10x)** | 14,102 **(0.10x)** | 711,755 **(5.07x)** |
| **2** | 246,540 | 14,403 **(0.06x)** | 13,771 **(0.06x)** | 788,886 **(3.20x)** |
| **4** | 315,799 | 14,020 **(0.04x)** | 13,194 **(0.04x)** | 767,344 **(2.43x)** |
| **8** | 328,174 | 14,573 **(0.04x)** | 13,576 **(0.04x)** | 610,215 **(1.86x)** |

### Prime sieve (primes/s)
*High-concurrency filtering test.*

| Pool | Go (Baseline) | libgoc | libgoc vmem | Clojure |
| :--- | :--- | :--- | :--- | :--- |
| **1** | 1,340 | 2,136 **(1.59x)** | 304 **(0.23x)** | 1,881 **(1.40x)** |
| **2** | 2,591 | 2,235 **(0.86x)** | 283 **(0.11x)** | 1,864 **(0.72x)** |
| **4** | 5,041 | 2,193 **(0.44x)** | 281 **(0.06x)** | 1,709 **(0.34x)** |
| **8** | 9,539 | 2,237 **(0.23x)** | 285 **(0.03x)** | 397 **(0.04x)** |

---

## Summary

### Go

Go remains the reference baseline and scales the most predictably in this run.
Ping-pong stays in a tight 1.52–1.67 M round-trips/s band, ring improves to
1.75 M hops/s at pool=8, and the fan-out benchmark climbs steadily to 506k
msg/s. Prime sieve shows the clearest scheduler scaling, rising from 1,340
primes/s at pool=1 to 9,539 primes/s at pool=8. Spawn idle remains far faster
than libgoc, though still below Clojure's stackless go-block model.

### libgoc

#### canary

This run reflects the current memory-derived bounded-admission policy
(`GOC_DEFAULT_LIVE_FIBER_MEMORY_FACTOR = 0.67`).

**Ping-pong: beats Go at all pool sizes.** Canary ranges from 1.04× Go at
pool=1 to 1.39× at pool=4, where it peaks at 2.1 M round trips/s. pool=2 and
pool=8 also exceed Go (1.12× and 1.08× respectively).

**Ring: still below Go.** Throughput is 0.45–0.52× Go across pool sizes,
with best performance at pool=2 (771 k hops/s, 0.52× Go).

**Fan-out/fan-in: competitive at pool=1 and pool=2.** Canary reaches 1.19×
Go at pool=1 and 1.10× at pool=2. Falls to 0.81× at pool=4 and 0.68× at pool=8.

**Spawn idle tasks: bounded by stackful fiber cost.** canary stays at
~14 k tasks/s across all pool sizes — far below Go's goroutine model which
benefits from smaller stacks and a lighter scheduler.

**Prime sieve: strong at pool=1, stable at higher pools.** Canary is 1.59×
Go at pool=1 and holds near 2.2 k primes/s across pool=2/4/8 (0.23–0.86× Go).

#### vmem

vmem remains substantially slower than canary on ring, fan-out, and prime sieve.

**Ping-pong improved substantially.** vmem now reaches 717–821 k round trips/s
(0.43–0.49× Go) across all pool sizes — compared to 0.23–0.52× Go in prior
runs — reflecting scheduler hot-path optimisations landing in this branch.

**Ring and prime sieve remain the main bottlenecks.** Ring stays near
42–54 k hops/s (~0.03× Go), dominated by mmap page-fault overhead. Prime
sieve remains 0.03–0.23× Go.

**Spawn idle tasks: similar to canary.** vmem stays at ~13–14 k tasks/s,
comparable to canary, as the admission cap limits concurrent live fibers.

vmem should still be treated as a bounded-correctness/stress configuration
rather than a near-parity performance mode.

### Clojure

**Ring: Clojure still shines.** It leads the field at pool=1 and remains above
Go through pool=4, topping out at 2.88 M hops/s at pool=1.

**Ping-pong: consistently below Go and canary's best case.** Clojure ranges
from 0.35× to 0.71× Go here.

**Fan-out/fan-in: steady but sub-Go.** The benchmark stays in a 0.52–0.83× Go
band, ahead of current vmem but generally behind Go and canary pool=2.

**Spawn idle: still the easiest winner.** go-block creation remains by far the
cheapest model in this comparison, at 1.86–5.07× Go and an order of magnitude
above libgoc.

**Prime sieve: only pool=1 is competitive.** Clojure slightly beats Go at
pool=1, then falls off quickly as the JVM pool size increases.
