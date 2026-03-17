# Todo

## 1. Features

- [ ] **1.1** Fiber Local Storage (see if we can piggyback on minicoro's)

- [ ] **1.2** Telemetry — expose runtime metrics for all pools, worker threads, fibers, channels, and their metadata

- [ ] **1.3** libuv I/O function wrappers that use channels rather than callbacks

- [ ] **1.4** Go-like RW mutexes that park fibers and block threads

- [ ] **1.5** Memory-managed, mutable dynamic array
  - **1.5.1** Amortized constant-time random access (read / write)
  - **1.5.2** Amortized constant-time push / pop from both head and tail
  - **1.5.3** Efficient concat (prepending / appending)
  - **1.5.4** Efficient slicing (creating shallow-copy subarrays)

## 2. Safety Enhancements

- [ ] **2.1** `goc_alts` should assert `n_default_arms <= 1`

- [ ] **2.2** `goc_pool_destroy` should not be callable from within the pool being destroyed

- [ ] **2.3** `goc_init` and `goc_shutdown` should only be callable from the main thread
  - **2.3.1** `goc_init`
  - **2.3.2** `goc_shutdown`

- [ ] **2.4** Sync variants should assert they are not running on a fiber
  - **2.4.1** `goc_take_sync`
  - **2.4.2** `goc_put_sync`
  - **2.4.3** `goc_alts_sync`

## 3. Chores

- [ ] **3.1** Resolve circular dependencies between source files; move shared declarations to a common header and simplify the build

- [ ] **3.2** Build tagged artefacts for each supported platform and publish releases to GitHub

- [ ] **3.3** Publish to package managers for each platform (Homebrew, apt/deb, vcpkg, etc.)

- [ ] **3.4** Idiomatic C++ wrapper `libgocxx`
