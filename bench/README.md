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
Channel ping-pong: 200000 round trips in 81ms (2450068 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 208ms (2396744 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 301ms (663626 msg/s)
Spawn idle tasks: 200000 goroutines in 1238ms (161433 tasks/s)
Prime sieve: 2262 primes up to 20000 in 1249ms (1810 primes/s)

=== Pool Size: 2 ===
GOMAXPROCS=2
Channel ping-pong: 200000 round trips in 87ms (2292035 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 217ms (2298805 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 280ms (712778 msg/s)
Spawn idle tasks: 200000 goroutines in 475ms (420510 tasks/s)
Prime sieve: 2262 primes up to 20000 in 549ms (4114 primes/s)

=== Pool Size: 4 ===
GOMAXPROCS=4
Channel ping-pong: 200000 round trips in 87ms (2277465 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 222ms (2250022 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 269ms (741545 msg/s)
Spawn idle tasks: 200000 goroutines in 358ms (557693 tasks/s)
Prime sieve: 2262 primes up to 20000 in 293ms (7694 primes/s)

=== Pool Size: 8 ===
GOMAXPROCS=8
Channel ping-pong: 200000 round trips in 87ms (2277539 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 221ms (2254470 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 267ms (747653 msg/s)
Spawn idle tasks: 200000 goroutines in 345ms (579313 tasks/s)
Prime sieve: 2262 primes up to 20000 in 159ms (14206 primes/s)
```

### libgoc canary — (default) — (`make -C libgoc run-all`)

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

### libgoc vmem (`make -C libgoc LIBGOC_VMEM=ON BUILD_DIR=../../build-bench-vmem build run-all`)

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

### Clojure core.async (`make -C clojure run-all`)

```
=== Pool Size: 1 ===
CLOJURE_POOL_THREADS=1
Channel ping-pong: 200000 round trips in 143ms (1393573 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 163ms (3060197 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 596ms (335299 msg/s)
Spawn idle tasks: 200000 go-blocks in 245ms (816057 tasks/s)
Prime sieve: 2262 primes up to 20000 in 1043ms (2167 primes/s)

=== Pool Size: 2 ===
CLOJURE_POOL_THREADS=2
Channel ping-pong: 200000 round trips in 220ms (908386 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 253ms (1974783 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 537ms (372276 msg/s)
Spawn idle tasks: 200000 go-blocks in 216ms (923576 tasks/s)
Prime sieve: 2262 primes up to 20000 in 973ms (2324 primes/s)

=== Pool Size: 4 ===
CLOJURE_POOL_THREADS=4
Channel ping-pong: 200000 round trips in 248ms (803577 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 239ms (2090092 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 542ms (368716 msg/s)
Spawn idle tasks: 200000 go-blocks in 226ms (882022 tasks/s)
Prime sieve: 2262 primes up to 20000 in 1303ms (1735 primes/s)

=== Pool Size: 8 ===
CLOJURE_POOL_THREADS=8
Channel ping-pong: 200000 round trips in 262ms (761833 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 228ms (2189096 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 551ms (362718 msg/s)
Spawn idle tasks: 200000 go-blocks in 239ms (835404 tasks/s)
Prime sieve: 2262 primes up to 20000 in 1417ms (1596 primes/s)
```

## Report: libgoc vs. Go Baseline (+ Clojure)

This report evaluates the performance of **libgoc canary**, **libgoc vmem**, and **Clojure core.async** relative to the **Go** runtime. All figures represent operations per second; the multiplier in parentheses indicates performance relative to the Go baseline (e.g., **1.10x** means 10% faster, **0.50x** means half the speed).

---

### Channel ping-pong (round trips/s)
*Measures overhead of basic synchronization and context switching.*

| Pool | Go (Baseline) | libgoc | libgoc vmem | Clojure |
| :--- | :--- | :--- | :--- | :--- |
| **1** | 2,450,068 | 1,600,265 **(0.65x)** | 561,421 **(0.23x)** | 1,393,573 **(0.57x)** |
| **2** | 2,292,035 | 1,588,830 **(0.69x)** | 524,909 **(0.23x)** | 908,386 **(0.40x)** |
| **4** | 2,277,465 | 1,796,811 **(0.79x)** | 682,715 **(0.30x)** | 803,577 **(0.35x)** |
| **8** | 2,277,539 | 2,051,353 **(0.90x)** | 868,409 **(0.38x)** | 761,833 **(0.33x)** |

### Ring (hops/s)
*Measures message passing latency across a circular topology.*

| Pool | Go (Baseline) | libgoc | libgoc vmem | Clojure |
| :--- | :--- | :--- | :--- | :--- |
| **1** | 2,396,744 | 909,431 **(0.38x)** | 34,316 **(0.01x)** | 3,060,197 **(1.28x)** |
| **2** | 2,298,805 | 732,366 **(0.32x)** | 33,991 **(0.01x)** | 1,974,783 **(0.86x)** |
| **4** | 2,250,022 | 732,873 **(0.33x)** | 33,259 **(0.01x)** | 2,090,092 **(0.93x)** |
| **8** | 2,254,470 | 1,001,727 **(0.44x)** | 48,632 **(0.02x)** | 2,189,096 **(0.97x)** |

### Selective receive / fan-out / fan-in (msg/s)
*Evaluates complex orchestration and selection logic.*

| Pool | Go (Baseline) | libgoc | libgoc vmem | Clojure |
| :--- | :--- | :--- | :--- | :--- |
| **1** | 663,626 | 213,288 **(0.32x)** | 218,791 **(0.33x)** | 335,299 **(0.51x)** |
| **2** | 712,778 | 198,079 **(0.28x)** | 205,537 **(0.29x)** | 372,276 **(0.52x)** |
| **4** | 741,545 | 188,743 **(0.25x)** | 179,819 **(0.24x)** | 368,716 **(0.50x)** |
| **8** | 747,653 | 213,821 **(0.29x)** | 219,742 **(0.29x)** | 362,718 **(0.49x)** |

### Spawn idle tasks (tasks/s)
*Tests the efficiency of task creation and scheduling.*

| Pool | Go (Baseline) | libgoc | libgoc vmem | Clojure |
| :--- | :--- | :--- | :--- | :--- |
| **1** | 161,433 | 136,217 **(0.84x)** | 65,136 **(0.40x)** | 816,057 **(5.06x)** |
| **2** | 420,510 | 156,738 **(0.37x)** | 64,083 **(0.15x)** | 923,576 **(2.20x)** |
| **4** | 557,693 | 152,888 **(0.27x)** | 64,262 **(0.12x)** | 882,022 **(1.58x)** |
| **8** | 579,313 | 120,005 **(0.21x)** | 65,128 **(0.11x)** | 835,404 **(1.44x)** |

### Prime sieve (primes/s)
*High-concurrency filtering test.*

| Pool | Go (Baseline) | libgoc | libgoc vmem | Clojure |
| :--- | :--- | :--- | :--- | :--- |
| **1** | 1,810 | 2,788 **(1.54x)** | 675 **(0.37x)** | 2,167 **(1.20x)** |
| **2** | 4,114 | 2,528 **(0.61x)** | 665 **(0.16x)** | 2,324 **(0.56x)** |
| **4** | 7,694 | 2,473 **(0.32x)** | 809 **(0.11x)** | 1,735 **(0.23x)** |
| **8** | 14,206 | 2,785 **(0.20x)** | 729 **(0.05x)** | 1,596 **(0.11x)** |

---

## Summary

Geometric mean of the ×Go multipliers across pool sizes 1, 2, 4, 8.

| Benchmark | libgoc (canary) | libgoc (vmem) | Clojure |
| :--- | :---: | :---: | :---: |
| Ping-pong | 0.75× | 0.28× | 0.41× |
| Ring | 0.36× | 0.02× | 1.01× |
| Fan-out/Fan-in | 0.28× | 0.29× | 0.50× |
| Spawn idle | 0.37× | 0.17× | 2.18× |
| Prime sieve | 0.49× | 0.13× | 0.36× |

**Takeaways:**
- libgoc canary's ping-pong performance now reaches 0.90× Go at pool=8, with a geometric mean of 0.75×—a clear improvement, though still trailing Go at lower pool sizes.
- Ring throughput for canary crosses 1M hops/s at pool=8 (0.44× Go), with a geometric mean of 0.36×. vmem remains extremely slow on this test (0.02×).
- Fan-out/fan-in for both canary (0.28×) and vmem (0.29×) are now nearly identical, both well below Clojure (0.50×) and Go.
- Spawn idle tasks: canary geomean is 0.37× Go, vmem 0.17×. vmem improved significantly from 0.13× after fixing a wakeup-drop bug that caused a hang at pool=8. Clojure's advantage (2.18×) comes from heap-allocated, stack-free go-blocks.
- Prime sieve: canary (0.49×) outpaces Clojure (0.36×) and notably beats Go at pool=1 (1.54×). vmem lags (0.13×).
- Overall, canary shows improved scaling and stability. vmem spawn idle is now healthy across all pool sizes. Clojure continues to excel at task creation and ring topologies.
