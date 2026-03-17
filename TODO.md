# Todo

## First Release

### Safety

- [ ] **1.** `goc_alts` should assert `n_default_arms <= 1`

- [ ] **2.** `goc_pool_destroy` should not be callable from within the pool being destroyed

- [ ] **3.** `goc_init` and `goc_shutdown` should only be callable from the main thread
  - **3.1** `goc_init`
  - **3.2** `goc_shutdown`

- [ ] **4.** Sync variants should assert they are not running on a fiber
  - **4.1** `goc_take_sync`
  - **4.2** `goc_put_sync`
  - **4.3** `goc_alts_sync`

### Chores

- [ ] **5.** Resolve circular dependencies between source files; move shared declarations to a common header and simplify the build

- [ ] **6.** Build tagged artefacts for each supported platform and publish releases to GitHub

---

## Future Work

### Features

- [ ] **1.** Fiber Local Storage (see if we can piggyback on minicoro's)

- [ ] **2.** Telemetry — expose runtime metrics for all pools, worker threads, fibers, channels, and their metadata

- [ ] **3.** libuv I/O function wrappers that use channels rather than callbacks

- [ ] **4.** Go-like RW mutexes that park fibers and block threads

- [ ] **5.** Memory-managed, mutable dynamic array
  - **5.1** Amortized constant-time random access (read / write)
  - **5.2** Amortized constant-time push / pop from both head and tail
  - **5.3** Efficient concat (prepending / appending)
  - **5.4** Efficient slicing (creating shallow-copy subarrays)

### Chores

- [ ] **6.** Publish to package managers for each platform (Homebrew, apt/deb, vcpkg, etc.)

- [ ] **7.** Idiomatic C++ wrapper `libgocxx`
