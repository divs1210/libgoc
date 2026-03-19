# Go Benchmarks

Standalone CSP benchmarks implemented in Go for performance comparison with libgoc.

## Running

```sh
# Single run (uses current GOMAXPROCS setting)
make run

# Multi-pool testing (runs with pool sizes 1, 2, 4, 8)
make run-all
```

## Benchmarks Included

1. **Channel ping-pong** — Two goroutines pass a message back and forth
2. **Ring benchmark** — Token passing around a ring of goroutines
3. **Selective receive / fan-out / fan-in** — Producer-consumer with select
4. **Spawn idle tasks** — Goroutine creation and joining overhead
5. **Prime sieve** — Concurrent prime number generation pipeline

## Output Format

- **Time**: Integer milliseconds (e.g., `234ms`)
- **Rates**: Operations per second (e.g., `1234567 ops/s`)

## Multi-Pool Testing

The `make run-all` command tests performance with different `GOMAXPROCS` settings:

```
=== Pool Size: 1 ===
GOMAXPROCS=1
Channel ping-pong: 200000 round trips in 87ms (2280645 round trips/s)
...

=== Pool Size: 2 ===
GOMAXPROCS=2
Channel ping-pong: 200000 round trips in 89ms (2224597 round trips/s)
...
```

This helps analyze how Go's scheduler performs with different levels of parallelism.
