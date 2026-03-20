# Go Benchmarks

Standalone CSP benchmarks implemented in Go.  All five benchmarks are enabled
and mirror those in `bench/libgoc/bench.c` for a direct performance comparison.

## Running

```sh
# Single run (current GOMAXPROCS)
make run

# Multi-pool testing — runs with GOMAXPROCS = 1, 2, 4, 8
make run-all
```

## Benchmarks

| # | Name | Description |
|---|------|-------------|
| 1 | **Channel ping-pong** | Two goroutines exchange a token back and forth |
| 2 | **Ring** | A token is forwarded around a ring of N goroutines |
| 3 | **Selective receive / fan-out / fan-in** | Producer → N workers → `reflect.Select` collector |
| 4 | **Spawn idle tasks** | Spawn N goroutines that block immediately, then wake them |
| 5 | **Prime sieve** | Concurrent Eratosthenes pipeline |

## Output Format

All benchmarks produce a single line per run:

```
<description>: <count> <unit> in <ms>ms (<rate> <unit>/s)
```

## Multi-Pool Testing

`make run-all` tests performance at different `GOMAXPROCS` settings:

```
=== Pool Size: 1 ===
GOMAXPROCS=1
Channel ping-pong: 200000 round trips in 87ms (2280645 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 222ms (2243222 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 333ms (599056 msg/s)
Spawn idle tasks: 200000 goroutines in 1062ms (188282 tasks/s)
Prime sieve: 2262 primes up to 20000 in 1178ms (1919 primes/s)

=== Pool Size: 8 ===
GOMAXPROCS=8
Channel ping-pong: 200000 round trips in 88ms (2257564 round trips/s)
Ring benchmark: 500000 hops across 128 tasks in 222ms (2250942 hops/s)
Selective receive / fan-out / fan-in: 200000 messages with 8 workers in 304ms (657846 msg/s)
Spawn idle tasks: 200000 goroutines in 406ms (492388 tasks/s)
Prime sieve: 2262 primes up to 20000 in 160ms (14136 primes/s)
```

CPU-bound benchmarks (spawn, sieve) scale with GOMAXPROCS; communication-bound
ones (ping-pong, ring) remain roughly flat since the bottleneck is channel
round-trip latency, not parallelism.
