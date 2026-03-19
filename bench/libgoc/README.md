# libgoc Benchmarks

Standalone CSP benchmarks implemented in C using libgoc for performance comparison with Go.

These benchmarks are built separately from the main libgoc library build.

## Prerequisites

- C11 compiler
- CMake  
- pkg-config
- libuv
- Threaded Boehm GC (bdw-gc-threaded)

## Running

```sh
# Single run (uses current GOC_POOL_THREADS setting)
make run

# Multi-pool testing (runs with pool sizes 1, 2, 4, 8)
make run-all
```

## Benchmarks Included

Currently enabled (working):
1. **Channel ping-pong** — Two fibers pass a message back and forth
2. **Ring benchmark** — Token passing around a ring of fibers

Additional benchmarks (selective receive, spawn idle, prime sieve) are available but currently disabled due to implementation issues.

## Output Format

- **Time**: Integer milliseconds (e.g., `234ms`)  
- **Rates**: Operations per second (e.g., `1234567 ops/s`)

## Multi-Pool Testing

The `make run-all` command tests performance with different `GOC_POOL_THREADS` settings:

```
=== Pool Size: 1 ===
GOC_POOL_THREADS=1
Channel ping-pong: 200000 round trips in 89ms (2244633 round trips/s)
...

=== Pool Size: 2 ===
GOC_POOL_THREADS=2
Channel ping-pong: 200000 round trips in 253ms (788650 round trips/s)
...
```

This helps analyze how libgoc's fiber scheduler performs with different thread pool sizes.
