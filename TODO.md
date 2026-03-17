# Todo

## Features

- Memory-managed, mutable dynamic array with:
  - amortized constant time random access (read / write)
  - efficient concat (prepending / appending)
  - amortized constant time push / pop from both head and tail
  - efficient slicing (creating shallow copy subarrays)

- Go-like rw mutexes that park fibers and block threads

- libuv I/O function wrappers that use channels rather than callbacks

- Fiber Local Storage (see if we can piggyback on minicoro's)

- Telemetry — expose runtime metrics for all pools, worker threads, fibers, channels and their metadata

## Safety Enhancements

- `goc_init` and `goc_shutdown` should only be callable from the main thread
- `goc_pool_destroy` should not be callable from within the pool being destroyed
- Sync variants of `goc_take_sync`, `goc_put_sync`, `goc_alts_sync` should assert they are not running on a fiber
- `goc_alts` should assert `n_default_arms <= 1`

## Chores

- Resolve circular dependencies between source files; move shared declarations to a common header and simplify the build
- Build tagged artefacts for each supported platform and publish releases to GitHub
- Publish to package managers for each platform (Homebrew, apt/deb, vcpkg, etc.)
- Idiomatic C++ wrapper `libgocxx`
