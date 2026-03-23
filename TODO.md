# Todo

### Features

- **F1** libuv I/O function wrappers that use channels rather than callbacks

- **F2** dynamic fiber stacks, like Go

- **F3** Memory-managed, mutable dynamic array
  - **F3.1** Amortized constant-time random access (read / write)
  - **F3.2** Amortized constant-time push / pop from both head and tail
  - **F3.3** Efficient concat (prepending / appending)
  - **F3.4** Efficient slicing (creating shallow-copy subarrays)

### Chores

- **F4** Publish to package managers for each platform (Homebrew, apt/deb, vcpkg, etc.)

- **F5** Idiomatic C++ wrapper `libgocxx`
