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

3 runs of each benchmark (Go, libgoc canary, libgoc vmem, Clojure) can be found in the [bench/logs/](logs/) directory. All numbers in the report below are the best of those 3 runs for each pool size.

## Report: libgoc vs. Go Baseline (+ Clojure)

This report evaluates the performance of **libgoc canary**, **libgoc vmem**, and **Clojure core.async** relative to the **Go** runtime. All figures represent operations per second; the multiplier in parentheses indicates performance relative to the Go baseline (e.g., **1.10x** means 10% faster, **0.50x** means half the speed).

---

### Channel ping-pong (round trips/s)
*Measures overhead of basic synchronization and context switching.*

| Pool | Go (Baseline) | libgoc | libgoc vmem | Clojure |
| :--- | :--- | :--- | :--- | :--- |
| **1** | 2,462,723 | 1,635,016 **(0.66x)** | 567,312 **(0.23x)** | 1,475,056 **(0.60x)** |
| **2** | 2,299,174 | 1,555,626 **(0.68x)** | 573,282 **(0.25x)** | 917,285 **(0.40x)** |
| **4** | 2,316,612 | 1,757,418 **(0.76x)** | 696,689 **(0.30x)** | 927,094 **(0.40x)** |
| **8** | 2,305,221 | 1,988,937 **(0.86x)** | 894,177 **(0.39x)** | 863,775 **(0.37x)** |

### Ring (hops/s)
*Measures message passing latency across a circular topology.*

| Pool | Go (Baseline) | libgoc | libgoc vmem | Clojure |
| :--- | :--- | :--- | :--- | :--- |
| **1** | 2,394,205 | 919,141 **(0.38x)** | 48,999 **(0.02x)** | 3,151,742 **(1.32x)** |
| **2** | 2,278,856 | 786,703 **(0.35x)** | 34,749 **(0.02x)** | 2,018,366 **(0.89x)** |
| **4** | 2,259,653 | 760,960 **(0.34x)** | 33,716 **(0.01x)** | 2,269,710 **(1.00x)** |
| **8** | 2,263,918 | 1,042,933 **(0.46x)** | 49,230 **(0.02x)** | 2,075,803 **(0.92x)** |

### Selective receive / fan-out / fan-in (msg/s)
*Evaluates complex orchestration and selection logic.*

| Pool | Go (Baseline) | libgoc | libgoc vmem | Clojure |
| :--- | :--- | :--- | :--- | :--- |
| **1** | 640,172 | 220,347 **(0.34x)** | 223,037 **(0.35x)** | 344,863 **(0.54x)** |
| **2** | 707,669 | 217,599 **(0.31x)** | 222,069 **(0.31x)** | 376,259 **(0.53x)** |
| **4** | 755,743 | 198,436 **(0.26x)** | 223,638 **(0.30x)** | 378,694 **(0.50x)** |
| **8** | 764,424 | 217,015 **(0.28x)** | 242,620 **(0.32x)** | 369,592 **(0.48x)** |

### Spawn idle tasks (tasks/s)
*Tests the efficiency of task creation and scheduling.*

| Pool | Go (Baseline) | libgoc | libgoc vmem | Clojure |
| :--- | :--- | :--- | :--- | :--- |
| **1** | 237,190 | 157,846 **(0.67x)** | 65,844 **(0.28x)** | 815,298 **(3.44x)** |
| **2** | 419,821 | 71,358 **(0.17x)** | 57,712 **(0.14x)** | 917,459 **(2.19x)** |
| **4** | 563,616 | 73,200 **(0.13x)** | 57,051 **(0.10x)** | 842,494 **(1.49x)** |
| **8** | 583,660 | 74,095 **(0.13x)** | 17,572 **(0.03x)** | 905,371 **(1.55x)** |

### Prime sieve (primes/s)
*High-concurrency filtering test.*

| Pool | Go (Baseline) | libgoc | libgoc vmem | Clojure |
| :--- | :--- | :--- | :--- | :--- |
| **1** | 2,024 | 2,976 **(1.47x)** | 869 **(0.43x)** | 2,099 **(1.04x)** |
| **2** | 4,088 | 2,962 **(0.72x)** | 830 **(0.20x)** | 2,314 **(0.57x)** |
| **4** | 7,747 | 2,790 **(0.36x)** | 828 **(0.11x)** | 1,853 **(0.24x)** |
| **8** | 14,297 | 2,933 **(0.21x)** | 842 **(0.06x)** | 1,805 **(0.13x)** |

---

## Summary

Geometric mean of the ×Go multipliers across pool sizes 1, 2, 4, 8.

| Benchmark | libgoc (canary) | libgoc (vmem) | Clojure |
| :--- | :---: | :---: | :---: |
| Ping-pong | 0.74× | 0.29× | 0.43× |
| Ring | 0.38× | 0.02× | 1.02× |
| Fan-out/Fan-in | 0.30× | 0.32× | 0.51× |
| Spawn idle | 0.21× | 0.10× | 2.04× |
| Prime sieve | 0.53× | 0.15× | 0.37× |

**Takeaways:**
- libgoc canary's ping-pong performance reaches 0.86× Go at pool=8, with a geometric mean of 0.74×—solid scaling, though still trailing Go at lower pool sizes.
- Ring throughput for canary crosses 1M hops/s at pool=8 (0.46× Go), with a geometric mean of 0.38×. vmem remains extremely slow on this test (0.02×) due to TLB/page-fault pressure.
- Fan-out/fan-in: canary (0.30×) and vmem (0.32×) are nearly identical, both well below Clojure (0.51×) and Go.
- Spawn idle tasks: canary geomean drops to 0.21× Go due to steal thrashing at pool≥2 — work stealing causes excessive wakeup contention for tasks that immediately park. vmem (0.10×) is also impacted. Clojure's advantage (2.04×) comes from heap-allocated, stack-free go-blocks.
- Prime sieve: canary (0.53×) outpaces Clojure (0.37×) and beats Go at pool=1 (1.47×). vmem lags (0.15×).
- Overall, canary ring and ping-pong scale well with thread count. The spawn idle regression at pool≥2 is the primary open issue for the current optimization phase.
