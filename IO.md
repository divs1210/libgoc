# libgoc Async I/O (`goc_io`)

> Async I/O wrappers for libgoc — channel-based libuv I/O across four categories: Stream I/O, UDP datagrams, File System, and DNS resolution.

**Header:** `#include "goc_io.h"`

`goc_io.h` is a separate header from `goc.h`; include both when using async I/O:

```c
#include "goc.h"
#include "goc_io.h"
```

---

## Table of Contents

- [Design](#design)
- [Thread Safety](#thread-safety)
- [Buffer Lifetime](#buffer-lifetime)
- [Result Types](#result-types)
  - [Stream I/O result types](#stream-io-result-types)
  - [UDP result types](#udp-result-types)
  - [File system result types](#file-system-result-types)
  - [DNS result types](#dns-result-types)
- [1. Stream I/O (TCP, Pipes, TTY)](#1-stream-io-tcp-pipes-tty)
- [2. UDP (Datagrams)](#2-udp-datagrams)
- [3. File System Operations](#3-file-system-operations)
- [4. DNS & Resolution](#4-dns--resolution)

---

## Design

Each libuv operation is exposed in two forms:

**`goc_io_XXX_ch(...) → goc_chan*`**

Initiates the async I/O and returns a channel that delivers a single
`goc_io_XXX_t*` result when the operation completes. The channel is closed
immediately after the result is delivered. This form is safe to call from both
fiber context and OS thread context, and is compatible with `goc_alts()` for
select-style multiplexing across I/O operations.

**`goc_io_XXX(...) → goc_io_XXX_t*`**

Convenience wrapper: calls `goc_io_XXX_ch()` then `goc_take()` and returns
the unwrapped result. Must only be called from fiber context
(i.e. `goc_in_fiber() == true`).

Streaming operations (`uv_read_start`, `uv_udp_recv_start`) are inherently
multi-shot and expose only the channel form. A matching stop function is
provided to close the channel and halt I/O.

---

## Thread Safety

File-system (`uv_fs_*`) and DNS (`uv_getaddrinfo`, `uv_getnameinfo`) operations
are safe to initiate from any thread; libuv routes them through its internal
worker-thread pool and fires the callback on the event loop.

Stream and UDP operations (`uv_write`, `uv_read_start`, etc.) require the
libuv handle to be used from the event loop thread. The `*_ch` wrappers use a
`uv_async_t` bridge so they can be called safely from fiber or OS-thread
context. The caller is responsible for initialising and configuring the handle
(`uv_tcp_init`, `uv_tcp_bind`, etc.) on the event loop thread before passing it
to these wrappers.

---

## Buffer Lifetime

For **write / send** operations the caller must keep the `uv_buf_t` array and
all `buf.base` pointers valid until the result channel delivers its value (i.e.
the operation has completed). This matches the libuv contract.

For **read / recv** operations each delivered `goc_io_read_t` or
`goc_io_udp_recv_t` carries a `malloc`-allocated `buf.base` that is owned by
the caller; call `free(result->buf.base)` after consuming the data.

---

## Result Types

### Stream I/O result types

```c
/* Result of one read callback fired by goc_io_read_start.
 * nread >= 0: bytes read.  nread < 0: libuv error code (channel closed).
 * buf.base is malloc-allocated; caller must free(buf.base) after use. */
typedef struct { ssize_t nread; uv_buf_t buf; } goc_io_read_t;

/* Result of goc_io_write / goc_io_write2.
 * status: 0 on success, negative libuv error code on failure. */
typedef struct { int status; } goc_io_write_t;

/* Result of goc_io_shutdown_stream.
 * status: 0 on success, negative libuv error code on failure. */
typedef struct { int status; } goc_io_shutdown_t;

/* Result of goc_io_tcp_connect / goc_io_pipe_connect.
 * status: 0 on success, negative libuv error code on failure. */
typedef struct { int status; } goc_io_connect_t;
```

### UDP result types

```c
/* Result of goc_io_udp_send.
 * status: 0 on success, negative libuv error code on failure. */
typedef struct { int status; } goc_io_udp_send_t;

/* Result of one receive callback fired by goc_io_udp_recv_start.
 * nread >= 0: bytes received.  nread < 0: libuv error code (channel closed).
 * buf.base is malloc-allocated; caller must free(buf.base) after use.
 * addr: source address of the datagram (always valid). */
typedef struct {
    ssize_t                 nread;
    uv_buf_t                buf;
    struct sockaddr_storage addr;
    unsigned                flags;
} goc_io_udp_recv_t;
```

### File system result types

```c
/* result >= 0: file descriptor on success; negative libuv error on failure. */
typedef struct { uv_file result; } goc_io_fs_open_t;

/* result == 0 on success; negative libuv error on failure. */
typedef struct { int result; } goc_io_fs_close_t;

/* result >= 0: bytes read on success; negative libuv error on failure. */
typedef struct { ssize_t result; } goc_io_fs_read_t;

/* result >= 0: bytes written on success; negative libuv error on failure. */
typedef struct { ssize_t result; } goc_io_fs_write_t;

/* result == 0 on success; negative libuv error on failure. */
typedef struct { int result; } goc_io_fs_unlink_t;

/* result == 0 on success; negative libuv error on failure.
 * statbuf is populated when result == 0. */
typedef struct { int result; uv_stat_t statbuf; } goc_io_fs_stat_t;

/* result == 0 on success; negative libuv error on failure. */
typedef struct { int result; } goc_io_fs_rename_t;

/* result >= 0: bytes transferred on success; negative libuv error on failure. */
typedef struct { ssize_t result; } goc_io_fs_sendfile_t;
```

### DNS result types

```c
/* status == 0 on success, negative libuv / EAI error on failure.
 * res: linked list of addrinfo allocated by libuv; release with
 * uv_freeaddrinfo(result->res) when done.  NULL when status != 0. */
typedef struct { int status; struct addrinfo* res; } goc_io_getaddrinfo_t;

/* status == 0 on success, negative libuv / EAI error on failure.
 * hostname and service are NUL-terminated strings.  Empty when status != 0. */
typedef struct {
    int  status;
    char hostname[NI_MAXHOST];
    char service[NI_MAXSERV];
} goc_io_getnameinfo_t;
```

---

## 1. Stream I/O (TCP, Pipes, TTY)

### Read

| Function | Signature | Description |
|---|---|---|
| `goc_io_read_start` | `goc_chan* goc_io_read_start(uv_stream_t* stream)` | Begin receiving data from a stream. Returns a channel that delivers `goc_io_read_t*` values, one per read callback. The channel is closed on EOF or unrecoverable error; the last delivered value carries the error code in `nread`. Call `goc_io_read_stop()` to stop before EOF. Safe from any context. |
| `goc_io_read_stop` | `int goc_io_read_stop(uv_stream_t* stream)` | Dispatch `uv_read_stop()` to the event loop thread and close the read channel. Returns 0; the stop takes effect asynchronously. Safe from any context. |

```c
goc_chan* rch = goc_io_read_start(stream);
goc_val_t* v;
while ((v = goc_take(rch))->ok == GOC_OK) {
    goc_io_read_t* rd = (goc_io_read_t*)v->val;
    if (rd->nread < 0) break;          /* error */
    fwrite(rd->buf.base, 1, (size_t)rd->nread, stdout);
    free(rd->buf.base);
}
```

### Write

| Function | Signature | Description |
|---|---|---|
| `goc_io_write_ch` | `goc_chan* goc_io_write_ch(uv_stream_t* handle, const uv_buf_t bufs[], unsigned int nbufs)` | Initiate an async stream write; return result channel delivering `goc_io_write_t*`. Safe from any context. |
| `goc_io_write` | `goc_io_write_t* goc_io_write(uv_stream_t* handle, const uv_buf_t bufs[], unsigned int nbufs)` | Async stream write; block until complete. Fiber context only. |
| `goc_io_write2_ch` | `goc_chan* goc_io_write2_ch(uv_stream_t* handle, const uv_buf_t bufs[], unsigned int nbufs, uv_stream_t* send_handle)` | Initiate an async write with handle passing (IPC); return result channel. |
| `goc_io_write2` | `goc_io_write_t* goc_io_write2(uv_stream_t* handle, const uv_buf_t bufs[], unsigned int nbufs, uv_stream_t* send_handle)` | Async write with handle passing. Fiber context only. |

### Shutdown / Connect

| Function | Signature | Description |
|---|---|---|
| `goc_io_shutdown_stream_ch` | `goc_chan* goc_io_shutdown_stream_ch(uv_stream_t* handle)` | Initiate a stream half-close (write side); return result channel delivering `goc_io_shutdown_t*`. |
| `goc_io_shutdown_stream` | `goc_io_shutdown_t* goc_io_shutdown_stream(uv_stream_t* handle)` | Shutdown a stream; block until complete. Fiber context only. |
| `goc_io_tcp_connect_ch` | `goc_chan* goc_io_tcp_connect_ch(uv_tcp_t* handle, const struct sockaddr* addr)` | Initiate a TCP connection; return result channel delivering `goc_io_connect_t*`. |
| `goc_io_tcp_connect` | `goc_io_connect_t* goc_io_tcp_connect(uv_tcp_t* handle, const struct sockaddr* addr)` | TCP connect; block until complete. Fiber context only. |
| `goc_io_pipe_connect_ch` | `goc_chan* goc_io_pipe_connect_ch(uv_pipe_t* handle, const char* name)` | Initiate a pipe (named pipe / Unix socket) connect; return result channel. |
| `goc_io_pipe_connect` | `goc_io_connect_t* goc_io_pipe_connect(uv_pipe_t* handle, const char* name)` | Pipe connect; block until complete. Fiber context only. |

---

## 2. UDP (Datagrams)

| Function | Signature | Description |
|---|---|---|
| `goc_io_udp_send_ch` | `goc_chan* goc_io_udp_send_ch(uv_udp_t* handle, const uv_buf_t bufs[], unsigned int nbufs, const struct sockaddr* addr)` | Initiate an async UDP send; return result channel delivering `goc_io_udp_send_t*`. |
| `goc_io_udp_send` | `goc_io_udp_send_t* goc_io_udp_send(uv_udp_t* handle, const uv_buf_t bufs[], unsigned int nbufs, const struct sockaddr* addr)` | Async UDP send; block until complete. Fiber context only. |
| `goc_io_udp_recv_start` | `goc_chan* goc_io_udp_recv_start(uv_udp_t* handle)` | Begin receiving UDP datagrams. Returns a channel delivering `goc_io_udp_recv_t*` values, one per datagram. Channel is closed on unrecoverable error. Call `goc_io_udp_recv_stop()` to stop. |
| `goc_io_udp_recv_stop` | `int goc_io_udp_recv_stop(uv_udp_t* handle)` | Stop receiving UDP datagrams and close the receive channel. Returns 0; takes effect asynchronously. |

---

## 3. File System Operations

All file-system functions are safe to call from any context (fiber or OS thread).

| Function | Signature | Description |
|---|---|---|
| `goc_io_fs_open_ch` | `goc_chan* goc_io_fs_open_ch(const char* path, int flags, int mode)` | Async file open; channel delivers `goc_io_fs_open_t*`. |
| `goc_io_fs_open` | `goc_io_fs_open_t* goc_io_fs_open(const char* path, int flags, int mode)` | Open a file; block until complete. Fiber context only. |
| `goc_io_fs_close_ch` | `goc_chan* goc_io_fs_close_ch(uv_file file)` | Async file close; channel delivers `goc_io_fs_close_t*`. |
| `goc_io_fs_close` | `goc_io_fs_close_t* goc_io_fs_close(uv_file file)` | Close a file; block until complete. Fiber context only. |
| `goc_io_fs_read_ch` | `goc_chan* goc_io_fs_read_ch(uv_file file, const uv_buf_t bufs[], unsigned int nbufs, int64_t offset)` | Async file read; channel delivers `goc_io_fs_read_t*`. Pass `offset == -1` to use the current file position. |
| `goc_io_fs_read` | `goc_io_fs_read_t* goc_io_fs_read(uv_file file, const uv_buf_t bufs[], unsigned int nbufs, int64_t offset)` | Read from a file; block until complete. Fiber context only. |
| `goc_io_fs_write_ch` | `goc_chan* goc_io_fs_write_ch(uv_file file, const uv_buf_t bufs[], unsigned int nbufs, int64_t offset)` | Async file write; channel delivers `goc_io_fs_write_t*`. |
| `goc_io_fs_write` | `goc_io_fs_write_t* goc_io_fs_write(uv_file file, const uv_buf_t bufs[], unsigned int nbufs, int64_t offset)` | Write to a file; block until complete. Fiber context only. |
| `goc_io_fs_unlink_ch` | `goc_chan* goc_io_fs_unlink_ch(const char* path)` | Async file deletion; channel delivers `goc_io_fs_unlink_t*`. |
| `goc_io_fs_unlink` | `goc_io_fs_unlink_t* goc_io_fs_unlink(const char* path)` | Delete a file; block until complete. Fiber context only. |
| `goc_io_fs_stat_ch` | `goc_chan* goc_io_fs_stat_ch(const char* path)` | Async file stat; channel delivers `goc_io_fs_stat_t*`. |
| `goc_io_fs_stat` | `goc_io_fs_stat_t* goc_io_fs_stat(const char* path)` | Stat a file; block until complete. Fiber context only. |
| `goc_io_fs_rename_ch` | `goc_chan* goc_io_fs_rename_ch(const char* path, const char* new_path)` | Async file rename; channel delivers `goc_io_fs_rename_t*`. |
| `goc_io_fs_rename` | `goc_io_fs_rename_t* goc_io_fs_rename(const char* path, const char* new_path)` | Rename a file; block until complete. Fiber context only. |
| `goc_io_fs_sendfile_ch` | `goc_chan* goc_io_fs_sendfile_ch(uv_file out_fd, uv_file in_fd, int64_t in_offset, size_t length)` | Async zero-copy file transfer; channel delivers `goc_io_fs_sendfile_t*`. |
| `goc_io_fs_sendfile` | `goc_io_fs_sendfile_t* goc_io_fs_sendfile(uv_file out_fd, uv_file in_fd, int64_t in_offset, size_t length)` | Sendfile; block until complete. Fiber context only. |

**Example — open, write, read, close (fiber context)**

```c
#include "goc.h"
#include "goc_io.h"
#include <fcntl.h>

static void file_fiber(void* _) {
    /* Open */
    goc_io_fs_open_t* opened = goc_io_fs_open("/tmp/hello.txt",
                                               O_WRONLY | O_CREAT | O_TRUNC, 0644);
    uv_file fd = (uv_file)opened->result;

    /* Write */
    char data[] = "hello, libgoc\n";
    uv_buf_t buf = uv_buf_init(data, sizeof(data) - 1);
    goc_io_fs_write_t* wr = goc_io_fs_write(fd, &buf, 1, 0);
    (void)wr;

    /* Close */
    goc_io_fs_close(fd);
}

int main(void) {
    goc_init();
    goc_chan* done = goc_go(file_fiber, NULL);
    goc_take_sync(done);
    goc_shutdown();
    return 0;
}
```

---

## 4. DNS & Resolution

Both functions are safe to call from any context.

| Function | Signature | Description |
|---|---|---|
| `goc_io_getaddrinfo_ch` | `goc_chan* goc_io_getaddrinfo_ch(const char* node, const char* service, const struct addrinfo* hints)` | Async `getaddrinfo`; channel delivers `goc_io_getaddrinfo_t*`. On success `result->res` is a libuv-allocated linked list; release with `uv_freeaddrinfo(result->res)`. |
| `goc_io_getaddrinfo` | `goc_io_getaddrinfo_t* goc_io_getaddrinfo(const char* node, const char* service, const struct addrinfo* hints)` | Async getaddrinfo; block until complete. Fiber context only. |
| `goc_io_getnameinfo_ch` | `goc_chan* goc_io_getnameinfo_ch(const struct sockaddr* addr, int flags)` | Async `getnameinfo`; channel delivers `goc_io_getnameinfo_t*` with `hostname` and `service` strings. |
| `goc_io_getnameinfo` | `goc_io_getnameinfo_t* goc_io_getnameinfo(const struct sockaddr* addr, int flags)` | Async getnameinfo; block until complete. Fiber context only. |

**Example — resolve a hostname (fiber context)**

```c
#include "goc.h"
#include "goc_io.h"
#include <stdio.h>
#include <netdb.h>

static void dns_fiber(void* _) {
    goc_io_getaddrinfo_t* result =
        goc_io_getaddrinfo("example.com", "https", NULL);
    if (result->status == 0) {
        char host[256];
        getnameinfo(result->res->ai_addr, result->res->ai_addrlen,
                    host, sizeof(host), NULL, 0, NI_NUMERICHOST);
        printf("example.com → %s\n", host);
        uv_freeaddrinfo(result->res);
    } else {
        fprintf(stderr, "DNS error: %s\n", uv_strerror(result->status));
    }
}

int main(void) {
    goc_init();
    goc_chan* done = goc_go(dns_fiber, NULL);
    goc_take_sync(done);
    goc_shutdown();
    return 0;
}
```

---

**Select across multiple I/O operations (`goc_alts`)**

The `_ch` variants return plain `goc_chan*` values and are first-class arms in
`goc_alts()`, enabling deadline-aware I/O:

```c
#include "goc.h"
#include "goc_io.h"

static void io_fiber(void* _) {
    goc_chan* open_ch    = goc_io_fs_open_ch("/tmp/data.txt", O_RDONLY, 0);
    goc_chan* timeout_ch = goc_timeout(500);   /* from goc.h */

    goc_alt_op ops[] = {
        { .ch = open_ch,    .op_kind = GOC_ALT_TAKE },
        { .ch = timeout_ch, .op_kind = GOC_ALT_TAKE },
    };
    goc_alts_result* r = goc_alts(ops, 2);

    if (r->ch == timeout_ch) {
        printf("open timed out\n");
    } else {
        goc_io_fs_open_t* res = (goc_io_fs_open_t*)r->value.val;
        if (res->result >= 0)
            printf("opened fd=%d\n", res->result);
    }
}
```
