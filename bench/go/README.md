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

`make run-all` tests performance at different `GOMAXPROCS` settings (GOMAXPROCS = 1, 2, 4, 8).

3 runs of this benchmark can be found in [bench/logs/go.log](../logs/go.log).
