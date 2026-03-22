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

### libgoc vmem — `-DLIBGOC_VMEM=ON` — (`make -C libgoc BUILD_DIR=../../build-bench-vmem build run-all`)

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
| **1** | 1,668,462 | 841,585 **(0.50x)** | 372,188 **(0.22x)** | 1,192,028 **(0.71x)** |
| **2** | 1,554,949 | 1,613,367 **(1.04x)** | 752,747 **(0.48x)** | 861,049 **(0.55x)** |
| **4** | 1,519,533 | 1,475,172 **(0.97x)** | 689,219 **(0.45x)** | 706,072 **(0.46x)** |
| **8** | 1,657,939 | 1,656,638 **(1.00x)** | 843,651 **(0.51x)** | 581,133 **(0.35x)** |

### Ring (hops/s)
*Measures message passing latency across a circular topology.*

| Pool | Go (Baseline) | libgoc | libgoc vmem | Clojure |
| :--- | :--- | :--- | :--- | :--- |
| **1** | 1,619,555 | 441,182 **(0.27x)** | 35,218 **(0.02x)** | 2,883,906 **(1.78x)** |
| **2** | 1,493,978 | 674,984 **(0.45x)** | 41,921 **(0.03x)** | 1,874,808 **(1.25x)** |
| **4** | 1,455,247 | 641,142 **(0.44x)** | 40,559 **(0.03x)** | 1,976,053 **(1.36x)** |
| **8** | 1,748,604 | 789,484 **(0.45x)** | 54,555 **(0.03x)** | 1,460,544 **(0.84x)** |

### Selective receive / fan-out / fan-in (msg/s)
*Evaluates complex orchestration and selection logic.*

| Pool | Go (Baseline) | libgoc | libgoc vmem | Clojure |
| :--- | :--- | :--- | :--- | :--- |
| **1** | 398,338 | 265,515 **(0.67x)** | 178,561 **(0.45x)** | 311,273 **(0.78x)** |
| **2** | 426,361 | 412,647 **(0.97x)** | 150,720 **(0.35x)** | 351,951 **(0.83x)** |
| **4** | 458,290 | 346,416 **(0.76x)** | 118,259 **(0.26x)** | 340,867 **(0.74x)** |
| **8** | 506,307 | 320,277 **(0.63x)** | 114,523 **(0.23x)** | 261,466 **(0.52x)** |

### Spawn idle tasks (tasks/s)
*Tests the efficiency of task creation and scheduling.*

| Pool | Go (Baseline) | libgoc | libgoc vmem | Clojure |
| :--- | :--- | :--- | :--- | :--- |
| **1** | 140,512 | 11,180 **(0.08x)** | 12,910 **(0.09x)** | 711,755 **(5.07x)** |
| **2** | 246,540 | 13,071 **(0.05x)** | 11,732 **(0.05x)** | 788,886 **(3.20x)** |
| **4** | 315,799 | 12,536 **(0.04x)** | 9,139 **(0.03x)** | 767,344 **(2.43x)** |
| **8** | 328,174 | 12,292 **(0.04x)** | 8,677 **(0.03x)** | 610,215 **(1.86x)** |

### Prime sieve (primes/s)
*High-concurrency filtering test.*

| Pool | Go (Baseline) | libgoc | libgoc vmem | Clojure |
| :--- | :--- | :--- | :--- | :--- |
| **1** | 1,340 | 2,029 **(1.51x)** | 297 **(0.22x)** | 1,881 **(1.40x)** |
| **2** | 2,591 | 1,857 **(0.72x)** | 284 **(0.11x)** | 1,864 **(0.72x)** |
| **4** | 5,041 | 1,825 **(0.36x)** | 202 **(0.04x)** | 1,709 **(0.34x)** |
| **8** | 9,539 | 1,906 **(0.20x)** | 197 **(0.02x)** | 397 **(0.04x)** |

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

This run reflects the current memory-derived bounded-admission policy.

**Ping-pong: near parity at higher pools.** Canary reaches 1.61 M round
trips/s at pool=2 (1.04× Go) and 1.66 M at pool=8 (1.00× Go), while pool=1 is
still below baseline.

**Ring: still substantially below Go.** Throughput sits around 0.44–0.45× Go
for pool=2/4/8, with no clear scaling win from additional threads.

**Fan-out/fan-in: below Go across all pools.** Canary remains in a 0.63–0.97×
Go band and no longer beats baseline in this benchmark.

**Spawn idle tasks: strongly bounded by admission.** Throughput now clusters
around 11k–13k tasks/s across all pool sizes, indicating the cap is effectively
controlling concurrent materialisation rather than letting burst creation scale
with worker count.

**Prime sieve: only pool=1 is a clear win.** Canary reaches 2,029 primes/s at
pool=1 (1.51× Go), then stays flat around ~1.8k–1.9k as Go continues scaling.

#### vmem

vmem remains far from canary performance in this run.

**Across all five benchmarks, vmem is substantially slower than both Go and
canary.** Ring remains around 35k–55k hops/s (~0.02–0.03× Go), fan-out/fan-in
is limited to ~115k–179k msg/s, and prime sieve stays in the 197–297 primes/s
range. Spawn-idle is now tightly bounded too (8.7k–12.9k tasks/s), and is
generally below canary except at pool=1.

At the moment, the vmem configuration should be considered a bounded-correctness
and stress configuration rather than a near-parity performance option.

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
