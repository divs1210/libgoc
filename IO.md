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
- [Status Codes](#status-codes)
- [Result Types](#result-types)
  - [Stream I/O result types](#stream-io-result-types)
  - [UDP result types](#udp-result-types)
  - [File system result types](#file-system-result-types)
  - [DNS result types](#dns-result-types)
- [0. Handle Initialisation](#0-handle-initialisation)
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

Stream and UDP operations (`uv_write`, `uv_read_start`, etc.) use a
`uv_async_t` bridge and are safe to call from fiber or OS-thread context.
Use `goc_io_tcp_init_ch()`, `goc_io_pipe_init_ch()`, and `goc_io_udp_init_ch()`
to initialise handles on the event loop thread before use.

For **write / send** operations the caller must keep the `uv_buf_t` array and
all `buf.base` pointers valid until the result channel delivers its value (i.e.
the operation has completed). This matches the libuv contract.


---

## Status Codes

Composite result types that can succeed or fail carry a `goc_io_status_t ok` field:

```c
typedef enum {
    GOC_IO_ERR =  0,  /* I/O operation failed                 */
    GOC_IO_OK  =  1,  /* I/O operation completed successfully  */
} goc_io_status_t;
```

Scalar-returning operations (write, connect, open, read, etc.) use the raw
libuv convention: 0 or a non-negative value on success, a negative
`UV_E*` error code on failure.

---

## Result Types

### Result types for composite operations

Single-result operations (write, connect, open, read, etc.) return scalars directly:

| Operation | `_ch` channel delivers | `_()` blocking returns |
|---|---|---|
| `goc_io_tcp_init`, `goc_io_pipe_init`, `goc_io_udp_init`, `goc_io_write*`, `goc_io_shutdown_stream`, `goc_io_tcp_connect`, `goc_io_pipe_connect`, `goc_io_udp_send`, `goc_io_fs_close`, `goc_io_fs_unlink`, `goc_io_fs_rename` | `(void*)(intptr_t)status` — 0 on success, negative libuv error | `int` |
| `goc_io_fs_open` | `(void*)(intptr_t)fd` — fd >= 0 on success, negative libuv error | `uv_file` |
| `goc_io_fs_read`, `goc_io_fs_write`, `goc_io_fs_sendfile` | `(void*)(intptr_t)result` — bytes >= 0 on success, negative libuv error | `ssize_t` |

Composite result types (channel delivers a GC-managed pointer):

```c
/* goc_io_read_t — result of one read callback fired by goc_io_read_start_ch.
 * nread > 0: bytes read; nread < 0: libuv error (channel closed after this).
 * buf: GC-managed buffer. Valid when nread > 0; NULL otherwise.
 * libgoc does not free buf. */
typedef struct {
    ssize_t   nread;
    uv_buf_t* buf;
} goc_io_read_t;

/* goc_io_udp_recv_t — result of one receive callback fired by
 * goc_io_udp_recv_start.
 * nread > 0: bytes received; nread < 0: libuv error (channel closed).
 * buf: GC-managed buffer. addr: GC-managed source address.
 * libgoc does not free buf or addr. */
typedef struct {
    ssize_t          nread;
    uv_buf_t*        buf;
    struct sockaddr* addr;
    unsigned         flags;
} goc_io_udp_recv_t;

/* goc_io_fs_stat_t — result of goc_io_fs_stat.
 * ok == GOC_IO_OK: success (statbuf populated).  ok == GOC_IO_ERR: failure. */
typedef struct {
    goc_io_status_t ok;
    uv_stat_t    statbuf;
} goc_io_fs_stat_t;

/* goc_io_getaddrinfo_t — result of goc_io_getaddrinfo.
 * ok == GOC_IO_OK: success (res populated).  ok == GOC_IO_ERR: failure.
 * Release res with uv_freeaddrinfo() when done. */
typedef struct {
    goc_status_t     ok;
    struct addrinfo* res;
} goc_io_getaddrinfo_t;

/* goc_io_getnameinfo_t — result of goc_io_getnameinfo.
 * ok == GOC_IO_OK: success (hostname/service populated). */
typedef struct {
    goc_io_status_t ok;
    char         hostname[NI_MAXHOST];
    char         service[NI_MAXSERV];
} goc_io_getnameinfo_t;
```

---

## 0. Handle Initialisation

These wrappers initialise a libuv handle on the event loop thread.  They are
safe to call from any context.

| Function | Signature | Description |
|---|---|---|
| `goc_io_tcp_init_ch` | `goc_chan* goc_io_tcp_init_ch(uv_tcp_t* handle)` | Initialise a `uv_tcp_t` handle on the event loop. Returns a channel delivering `(void*)(intptr_t)status`. |
| `goc_io_tcp_init` | `int goc_io_tcp_init(uv_tcp_t* handle)` | Blocking form (fiber context). |
| `goc_io_pipe_init_ch` | `goc_chan* goc_io_pipe_init_ch(uv_pipe_t* handle, int ipc)` | Initialise a `uv_pipe_t` handle on the event loop. `ipc` enables IPC mode. |
| `goc_io_pipe_init` | `int goc_io_pipe_init(uv_pipe_t* handle, int ipc)` | Blocking form (fiber context). |
| `goc_io_udp_init_ch` | `goc_chan* goc_io_udp_init_ch(uv_udp_t* handle)` | Initialise a `uv_udp_t` handle on the event loop. |
| `goc_io_udp_init` | `int goc_io_udp_init(uv_udp_t* handle)` | Blocking form (fiber context). |

**Example — create and connect a TCP socket:**

```c
uv_tcp_t* tcp = goc_malloc(sizeof(uv_tcp_t));
int rc = goc_io_tcp_init(tcp);      // initialise on event loop
if (rc < 0) { /* handle error */ }

struct sockaddr_in addr;
uv_ip4_addr("127.0.0.1", 7000, &addr);
rc = goc_io_tcp_connect(tcp, (struct sockaddr*)&addr);
```

---

## 1. Stream I/O (TCP, Pipes, TTY)

### Read

| Function | Signature | Description |
|---|---|---|
| `goc_io_read_start_ch` | `goc_chan* goc_io_read_start_ch(uv_stream_t* stream)` | Begin receiving data from a stream. Returns a channel that delivers `goc_io_read_t*` values, one per read callback. The channel is closed on EOF or unrecoverable error; the last delivered value carries the error code in `nread`. Call `goc_io_read_stop()` to stop before EOF. Safe from any context. |
| `goc_io_read_stop` | `int goc_io_read_stop(uv_stream_t* stream)` | Dispatch `uv_read_stop()` to the event loop thread and close the read channel. Returns 0; the stop takes effect asynchronously. Safe from any context. |

```c
goc_chan* rch = goc_io_read_start_ch(stream);
goc_val_t* v;
while ((v = goc_take(rch))->ok == GOC_IO_OK) {
    goc_io_read_t* rd = (goc_io_read_t*)v->val;
    if (rd->nread < 0) break;          /* error */
    fwrite(rd->buf->base, 1, (size_t)rd->nread, stdout);
    /* buf is GC-managed; no free() needed */
}
```

### Write

| Function | Signature | Description |
|---|---|---|
| `goc_io_write_ch` | `goc_chan* goc_io_write_ch(uv_stream_t* handle, const uv_buf_t bufs[], unsigned int nbufs)` | Initiate an async stream write; return result channel delivering `goc_io_write_t*`. Safe from any context. |
| `goc_io_write` | `int goc_io_write(uv_stream_t* handle, const uv_buf_t bufs[], unsigned int nbufs)` | Async stream write; block until complete. Returns 0 on success. Fiber context only. |
| `goc_io_write2_ch` | `goc_chan* goc_io_write2_ch(uv_stream_t* handle, const uv_buf_t bufs[], unsigned int nbufs, uv_stream_t* send_handle)` | Initiate an async write with handle passing (IPC); return result channel. |
| `goc_io_write2` | `int goc_io_write2(uv_stream_t* handle, const uv_buf_t bufs[], unsigned int nbufs, uv_stream_t* send_handle)` | Async write with handle passing. Returns 0 on success. Fiber context only. |

### Shutdown / Connect

| Function | Signature | Description |
|---|---|---|
| `goc_io_shutdown_stream_ch` | `goc_chan* goc_io_shutdown_stream_ch(uv_stream_t* handle)` | Initiate a stream half-close (write side); return result channel delivering `goc_io_shutdown_t*`. |
| `goc_io_shutdown_stream` | `int goc_io_shutdown_stream(uv_stream_t* handle)` | Shutdown a stream; block until complete. Returns 0 on success. Fiber context only. |
| `goc_io_tcp_connect_ch` | `goc_chan* goc_io_tcp_connect_ch(uv_tcp_t* handle, const struct sockaddr* addr)` | Initiate a TCP connection; return result channel delivering `goc_io_connect_t*`. |
| `goc_io_tcp_connect` | `int goc_io_tcp_connect(uv_tcp_t* handle, const struct sockaddr* addr)` | TCP connect; block until complete. Returns 0 on success. Fiber context only. |
| `goc_io_pipe_connect_ch` | `goc_chan* goc_io_pipe_connect_ch(uv_pipe_t* handle, const char* name)` | Initiate a pipe (named pipe / Unix socket) connect; return result channel. |
| `goc_io_pipe_connect` | `int goc_io_pipe_connect(uv_pipe_t* handle, const char* name)` | Pipe connect; block until complete. Returns 0 on success. Fiber context only. |

---

## 2. UDP (Datagrams)

| Function | Signature | Description |
|---|---|---|
| `goc_io_udp_send_ch` | `goc_chan* goc_io_udp_send_ch(uv_udp_t* handle, const uv_buf_t bufs[], unsigned int nbufs, const struct sockaddr* addr)` | Initiate an async UDP send; return result channel delivering `goc_io_udp_send_t*`. |
| `goc_io_udp_send` | `int goc_io_udp_send(uv_udp_t* handle, const uv_buf_t bufs[], unsigned int nbufs, const struct sockaddr* addr)` | Async UDP send; block until complete. Returns 0 on success. Fiber context only. |
| `goc_io_udp_recv_start_ch` | `goc_chan* goc_io_udp_recv_start_ch(uv_udp_t* handle)` | Begin receiving UDP datagrams. Returns a channel delivering `goc_io_udp_recv_t*` values, one per datagram. Channel is closed on unrecoverable error. Call `goc_io_udp_recv_stop()` to stop. |
| `goc_io_udp_recv_stop` | `int goc_io_udp_recv_stop(uv_udp_t* handle)` | Stop receiving UDP datagrams and close the receive channel. Returns 0; takes effect asynchronously. |

---

## 3. File System Operations

All file-system functions are safe to call from any context (fiber or OS thread).

| Function | Signature | Description |
|---|---|---|
| `goc_io_fs_open_ch` | `goc_chan* goc_io_fs_open_ch(const char* path, int flags, int mode)` | Async file open; channel delivers `goc_io_fs_open_t*`. |
| `goc_io_fs_open` | `uv_file goc_io_fs_open(const char* path, int flags, int mode)` | Open a file; block until complete. Returns fd >= 0 on success. Fiber context only. |
| `goc_io_fs_close_ch` | `goc_chan* goc_io_fs_close_ch(uv_file file)` | Async file close; channel delivers `goc_io_fs_close_t*`. |
| `goc_io_fs_close` | `int goc_io_fs_close(uv_file file)` | Close a file; block until complete. Returns 0 on success. Fiber context only. |
| `goc_io_fs_read_ch` | `goc_chan* goc_io_fs_read_ch(uv_file file, const uv_buf_t bufs[], unsigned int nbufs, int64_t offset)` | Async file read; channel delivers `goc_io_fs_read_t*`. Pass `offset == -1` to use the current file position. |
| `goc_io_fs_read` | `ssize_t goc_io_fs_read(uv_file file, const uv_buf_t bufs[], unsigned int nbufs, int64_t offset)` | Read from a file; block until complete. Returns bytes read. Fiber context only. |
| `goc_io_fs_write_ch` | `goc_chan* goc_io_fs_write_ch(uv_file file, const uv_buf_t bufs[], unsigned int nbufs, int64_t offset)` | Async file write; channel delivers `goc_io_fs_write_t*`. |
| `goc_io_fs_write` | `ssize_t goc_io_fs_write(uv_file file, const uv_buf_t bufs[], unsigned int nbufs, int64_t offset)` | Write to a file; block until complete. Returns bytes written. Fiber context only. |
| `goc_io_fs_unlink_ch` | `goc_chan* goc_io_fs_unlink_ch(const char* path)` | Async file deletion; channel delivers `goc_io_fs_unlink_t*`. |
| `goc_io_fs_unlink` | `int goc_io_fs_unlink(const char* path)` | Delete a file; block until complete. Returns 0 on success. Fiber context only. |
| `goc_io_fs_stat_ch` | `goc_chan* goc_io_fs_stat_ch(const char* path)` | Async file stat; channel delivers `goc_io_fs_stat_t*`. |
| `goc_io_fs_stat` | `goc_io_fs_stat_t* goc_io_fs_stat(const char* path)` | Stat a file; block until complete. Returns GC-managed struct. Fiber context only. |
| `goc_io_fs_rename_ch` | `goc_chan* goc_io_fs_rename_ch(const char* path, const char* new_path)` | Async file rename; channel delivers `goc_io_fs_rename_t*`. |
| `goc_io_fs_rename` | `int goc_io_fs_rename(const char* path, const char* new_path)` | Rename a file; block until complete. Returns 0 on success. Fiber context only. |
| `goc_io_fs_sendfile_ch` | `goc_chan* goc_io_fs_sendfile_ch(uv_file out_fd, uv_file in_fd, int64_t in_offset, size_t length)` | Async zero-copy file transfer; channel delivers `goc_io_fs_sendfile_t*`. |
| `goc_io_fs_sendfile` | `ssize_t goc_io_fs_sendfile(uv_file out_fd, uv_file in_fd, int64_t in_offset, size_t length)` | Sendfile; block until complete. Returns bytes transferred. Fiber context only. |

**Example — open, write, read, close (fiber context)**

```c
#include "goc.h"
#include "goc_io.h"
#include <fcntl.h>

static void file_fiber(void* _) {
    /* Open */
    uv_file fd = goc_io_fs_open("/tmp/hello.txt",
                                O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;

    /* Write */
    char data[] = "hello, libgoc\n";
    uv_buf_t buf = uv_buf_init(data, sizeof(data) - 1);
    ssize_t written = goc_io_fs_write(fd, &buf, 1, 0);
    (void)written;

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
| `goc_io_getaddrinfo` | `goc_io_getaddrinfo_t* goc_io_getaddrinfo(const char* node, const char* service, const struct addrinfo* hints)` | Async getaddrinfo; block until complete. Returns GC-managed struct. Fiber context only. |
| `goc_io_getnameinfo_ch` | `goc_chan* goc_io_getnameinfo_ch(const struct sockaddr* addr, int flags)` | Async `getnameinfo`; channel delivers `goc_io_getnameinfo_t*` with `hostname` and `service` strings. |
| `goc_io_getnameinfo` | `goc_io_getnameinfo_t* goc_io_getnameinfo(const struct sockaddr* addr, int flags)` | Async getnameinfo; block until complete. Returns GC-managed struct. Fiber context only. |

**Example — resolve a hostname (fiber context)**

```c
#include "goc.h"
#include "goc_io.h"
#include <stdio.h>
#include <netdb.h>

static void dns_fiber(void* _) {
    goc_io_getaddrinfo_t* result =
        goc_io_getaddrinfo("example.com", "https", NULL);
    if (result->ok == GOC_IO_OK) {
        char host[256];
        getnameinfo(result->res->ai_addr, result->res->ai_addrlen,
                    host, sizeof(host), NULL, 0, NI_NUMERICHOST);
        printf("example.com → %s\n", host);
        uv_freeaddrinfo(result->res);
    } else {
        fprintf(stderr, "DNS error (getaddrinfo failed)\n");
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
        uv_file fd = (uv_file)(intptr_t)r->value.val;
        if (fd >= 0)
            printf("opened fd=%d\n", (int)fd);
    }
}
```
