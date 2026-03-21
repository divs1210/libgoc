/*
 * include/goc_io.h — Async I/O wrappers for libgoc
 *
 * Provides channel-returning wrappers for libuv I/O functions across four
 * categories: Stream I/O, UDP datagrams, File System, and DNS resolution.
 *
 * Design
 * ------
 * Each libuv operation is exposed in two forms:
 *
 *   goc_XXX_ch(...)  → goc_chan*
 *       Initiates the async I/O and returns a channel that delivers a single
 *       goc_XXX_t* result when the operation completes.  The channel is closed
 *       immediately after the result is delivered.  This form is safe to call
 *       from both fiber context and OS thread context, and is compatible with
 *       goc_alts() for select-style multiplexing across I/O operations.
 *
 *   goc_XXX(...)  → goc_XXX_t*
 *       Convenience wrapper: calls goc_XXX_ch() then goc_take() and returns
 *       the unwrapped result.  Must only be called from fiber context
 *       (i.e. goc_in_fiber() == true).
 *
 * Streaming operations (uv_read_start, uv_udp_recv_start) are inherently
 * multi-shot and return only the channel form; a matching stop function is
 * provided to close the channel and halt I/O.
 *
 * Thread safety
 * -------------
 * File-system (uv_fs_*) and DNS (uv_getaddrinfo, uv_getnameinfo) operations
 * are safe to initiate from any thread; libuv routes them through its internal
 * worker-thread pool and fires the callback on the event loop.
 *
 * Stream and UDP operations (uv_write, uv_read_start, etc.) require the
 * libuv handle to be used from the event loop thread.  The *_ch wrappers use
 * a uv_async_t bridge so they can be called safely from fiber or OS-thread
 * context.  The caller is responsible for initialising and configuring the
 * handle (uv_tcp_init, uv_tcp_bind, etc.) on the event loop thread before
 * passing it to these wrappers.
 *
 * Buffer lifetime
 * ---------------
 * For write / send operations the caller must keep the uv_buf_t array and
 * all buf.base pointers valid until the result channel delivers its value
 * (i.e. the operation has completed).  This matches the libuv contract.
 *
 * For read / recv operations each delivered goc_read_t or goc_udp_recv_t
 * carries a malloc-allocated buf.base that is owned by the caller; the
 * caller must free(result->buf.base) after consuming the data.
 *
 * Compile requirements: -std=c11
 *   goc_io.h is included automatically via goc.h.
 */

#ifndef GOC_IO_H
#define GOC_IO_H

#include <stddef.h>
#include <stdint.h>
#include <netdb.h>
#include <uv.h>
#include "goc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Result types
 * ====================================================================== */

/* -------------------------------------------------------------------------
 * Stream I/O
 * ---------------------------------------------------------------------- */

/**
 * goc_read_t — result of one read callback fired by goc_read_start.
 *
 * nread : bytes read (>= 0).  On error this is a negative libuv error code.
 * buf   : buffer containing the data.  buf.base is malloc-allocated and
 *         owned by the caller; call free(buf.base) after use.
 *
 * When nread < 0 the channel is already closed; this is the last value.
 */
typedef struct {
    ssize_t  nread;
    uv_buf_t buf;
} goc_read_t;

/**
 * goc_write_t — result of goc_write / goc_write2.
 *
 * status : 0 on success, a negative libuv error code on failure.
 */
typedef struct {
    int status;
} goc_write_t;

/**
 * goc_shutdown_t — result of goc_shutdown_stream.
 *
 * status : 0 on success, a negative libuv error code on failure.
 */
typedef struct {
    int status;
} goc_shutdown_t;

/**
 * goc_connect_t — result of goc_tcp_connect / goc_pipe_connect.
 *
 * status : 0 on success, a negative libuv error code on failure.
 */
typedef struct {
    int status;
} goc_connect_t;

/* -------------------------------------------------------------------------
 * UDP
 * ---------------------------------------------------------------------- */

/**
 * goc_udp_send_t — result of goc_udp_send.
 *
 * status : 0 on success, a negative libuv error code on failure.
 */
typedef struct {
    int status;
} goc_udp_send_t;

/**
 * goc_udp_recv_t — result of one receive callback fired by goc_udp_recv_start.
 *
 * nread : bytes received (>= 0).  On error this is a negative libuv error code.
 * buf   : buffer containing the datagram data.  buf.base is malloc-allocated
 *         and owned by the caller; call free(buf.base) after use.
 * addr  : source address of the datagram (copied; always valid).
 * flags : libuv-defined receive flags.
 *
 * When nread < 0 the channel is already closed; this is the last value.
 */
typedef struct {
    ssize_t                nread;
    uv_buf_t               buf;
    struct sockaddr_storage addr;
    unsigned               flags;
} goc_udp_recv_t;

/* -------------------------------------------------------------------------
 * File system
 * ---------------------------------------------------------------------- */

/**
 * goc_fs_open_t — result of goc_fs_open.
 *
 * result : file descriptor (>= 0) on success, a negative libuv error code
 *          on failure.
 */
typedef struct {
    uv_file result;
} goc_fs_open_t;

/**
 * goc_fs_close_t — result of goc_fs_close.
 *
 * result : 0 on success, a negative libuv error code on failure.
 */
typedef struct {
    int result;
} goc_fs_close_t;

/**
 * goc_fs_read_t — result of goc_fs_read.
 *
 * result : bytes read (>= 0) on success, a negative libuv error code on
 *          failure.
 */
typedef struct {
    ssize_t result;
} goc_fs_read_t;

/**
 * goc_fs_write_t — result of goc_fs_write.
 *
 * result : bytes written (>= 0) on success, a negative libuv error code on
 *          failure.
 */
typedef struct {
    ssize_t result;
} goc_fs_write_t;

/**
 * goc_fs_unlink_t — result of goc_fs_unlink.
 *
 * result : 0 on success, a negative libuv error code on failure.
 */
typedef struct {
    int result;
} goc_fs_unlink_t;

/**
 * goc_fs_stat_t — result of goc_fs_stat.
 *
 * result  : 0 on success, a negative libuv error code on failure.
 * statbuf : populated when result == 0.
 */
typedef struct {
    int       result;
    uv_stat_t statbuf;
} goc_fs_stat_t;

/**
 * goc_fs_rename_t — result of goc_fs_rename.
 *
 * result : 0 on success, a negative libuv error code on failure.
 */
typedef struct {
    int result;
} goc_fs_rename_t;

/**
 * goc_fs_sendfile_t — result of goc_fs_sendfile.
 *
 * result : bytes sent (>= 0) on success, a negative libuv error code on
 *          failure.
 */
typedef struct {
    ssize_t result;
} goc_fs_sendfile_t;

/* -------------------------------------------------------------------------
 * DNS & resolution
 * ---------------------------------------------------------------------- */

/**
 * goc_getaddrinfo_t — result of goc_getaddrinfo.
 *
 * status : 0 on success, a negative libuv / EAI error code on failure.
 * res    : linked list of addrinfo results allocated by libuv.  The caller
 *          must release it with uv_freeaddrinfo(res) when done.
 *          NULL when status != 0.
 */
typedef struct {
    int              status;
    struct addrinfo* res;
} goc_getaddrinfo_t;

/**
 * goc_getnameinfo_t — result of goc_getnameinfo.
 *
 * status   : 0 on success, a negative libuv / EAI error code on failure.
 * hostname : resolved host name (NUL-terminated).  Empty when status != 0.
 * service  : resolved service name (NUL-terminated).  Empty when status != 0.
 */
typedef struct {
    int  status;
    char hostname[NI_MAXHOST];
    char service[NI_MAXSERV];
} goc_getnameinfo_t;

/* =========================================================================
 * 1. Stream I/O (TCP, Pipes, TTY)
 * ====================================================================== */

/**
 * goc_read_start() — Begin receiving data from a stream.
 *
 * stream : an initialised and connected libuv stream handle.
 *
 * Returns a channel that delivers goc_read_t* values, one per read callback
 * fired by libuv.  The channel is closed when EOF or an unrecoverable error
 * is encountered; the last delivered value carries the error code in nread.
 * Call goc_read_stop() to stop reading before EOF and close the channel.
 *
 * This function is safe to call from any context (fiber or OS thread).
 */
goc_chan* goc_read_start(uv_stream_t* stream);

/**
 * goc_read_stop() — Stop reading from a stream previously started with
 * goc_read_start().
 *
 * Dispatches uv_read_stop() to the event loop thread and closes the
 * associated read channel.  Returns 0; the actual stop happens
 * asynchronously on the event loop thread.
 *
 * stream : the same handle passed to goc_read_start().
 *
 * Safe to call from any context.
 */
int goc_read_stop(uv_stream_t* stream);

/**
 * goc_write_ch() — Initiate an async stream write; return result channel.
 *
 * handle : target stream handle.
 * bufs   : array of nbufs buffers to write.
 * nbufs  : number of buffers.
 *
 * Returns a channel that delivers a single goc_write_t* when the write
 * completes.  The channel is closed after delivery.
 *
 * Safe to call from any context.
 */
goc_chan* goc_write_ch(uv_stream_t* handle,
                       const uv_buf_t bufs[], unsigned int nbufs);

/**
 * goc_write() — Async stream write; block until complete (fiber context).
 *
 * Calls goc_write_ch() then goc_take().  Must only be called from fiber
 * context.
 *
 * Returns a GC-managed goc_write_t*.
 */
goc_write_t* goc_write(uv_stream_t* handle,
                        const uv_buf_t bufs[], unsigned int nbufs);

/**
 * goc_write2_ch() — Initiate an async write with handle passing (IPC).
 *
 * Same as goc_write_ch() with an additional send_handle for IPC streams.
 */
goc_chan* goc_write2_ch(uv_stream_t* handle,
                        const uv_buf_t bufs[], unsigned int nbufs,
                        uv_stream_t* send_handle);

/**
 * goc_write2() — Async write with handle passing (fiber context).
 */
goc_write_t* goc_write2(uv_stream_t* handle,
                         const uv_buf_t bufs[], unsigned int nbufs,
                         uv_stream_t* send_handle);

/**
 * goc_shutdown_stream_ch() — Initiate a stream shutdown; return result channel.
 *
 * handle : stream to shut down (half-close the write side).
 *
 * Returns a channel delivering goc_shutdown_t* on completion.
 */
goc_chan* goc_shutdown_stream_ch(uv_stream_t* handle);

/**
 * goc_shutdown_stream() — Shutdown a stream; block until complete (fiber).
 */
goc_shutdown_t* goc_shutdown_stream(uv_stream_t* handle);

/**
 * goc_tcp_connect_ch() — Initiate a TCP connection; return result channel.
 *
 * handle : an initialised and bound (optional) uv_tcp_t handle.
 * addr   : target address.
 *
 * Returns a channel delivering goc_connect_t* on connection completion.
 */
goc_chan* goc_tcp_connect_ch(uv_tcp_t* handle, const struct sockaddr* addr);

/**
 * goc_tcp_connect() — TCP connect; block until complete (fiber context).
 */
goc_connect_t* goc_tcp_connect(uv_tcp_t* handle, const struct sockaddr* addr);

/**
 * goc_pipe_connect_ch() — Initiate a pipe (named pipe / Unix socket) connect.
 *
 * handle : an initialised uv_pipe_t handle.
 * name   : path to the named pipe / Unix domain socket.
 *
 * Returns a channel delivering goc_connect_t* on completion.
 */
goc_chan* goc_pipe_connect_ch(uv_pipe_t* handle, const char* name);

/**
 * goc_pipe_connect() — Pipe connect; block until complete (fiber context).
 */
goc_connect_t* goc_pipe_connect(uv_pipe_t* handle, const char* name);

/* =========================================================================
 * 2. UDP (Datagrams)
 * ====================================================================== */

/**
 * goc_udp_send_ch() — Initiate an async UDP send; return result channel.
 *
 * handle : an initialised uv_udp_t handle.
 * bufs   : array of nbufs buffers to send.
 * nbufs  : number of buffers.
 * addr   : destination address.
 *
 * Returns a channel delivering a single goc_udp_send_t* when the datagram
 * has been sent (or failed).
 */
goc_chan* goc_udp_send_ch(uv_udp_t* handle,
                          const uv_buf_t bufs[], unsigned int nbufs,
                          const struct sockaddr* addr);

/**
 * goc_udp_send() — Async UDP send; block until complete (fiber context).
 */
goc_udp_send_t* goc_udp_send(uv_udp_t* handle,
                              const uv_buf_t bufs[], unsigned int nbufs,
                              const struct sockaddr* addr);

/**
 * goc_udp_recv_start() — Begin receiving UDP datagrams.
 *
 * handle : an initialised and bound uv_udp_t handle.
 *
 * Returns a channel that delivers goc_udp_recv_t* values, one per datagram
 * received.  The channel is closed on unrecoverable error; the last delivered
 * value carries the error code in nread.
 * Call goc_udp_recv_stop() to stop receiving and close the channel.
 */
goc_chan* goc_udp_recv_start(uv_udp_t* handle);

/**
 * goc_udp_recv_stop() — Stop receiving UDP datagrams.
 *
 * Dispatches uv_udp_recv_stop() to the event loop thread and closes the
 * associated receive channel.  Returns 0; the stop takes effect
 * asynchronously.
 */
int goc_udp_recv_stop(uv_udp_t* handle);

/* =========================================================================
 * 3. File System Operations
 * ====================================================================== */

/**
 * goc_fs_open_ch() — Initiate an async file open; return result channel.
 *
 * path  : file path.
 * flags : open flags (O_RDONLY, O_WRONLY | O_CREAT, ...).
 * mode  : permission bits used when creating a new file.
 *
 * Returns a channel delivering goc_fs_open_t*.
 * Safe to call from any context.
 */
goc_chan* goc_fs_open_ch(const char* path, int flags, int mode);

/** goc_fs_open() — Open a file; block until complete (fiber context). */
goc_fs_open_t* goc_fs_open(const char* path, int flags, int mode);

/**
 * goc_fs_close_ch() — Initiate an async file close; return result channel.
 *
 * file : file descriptor returned by goc_fs_open.
 */
goc_chan* goc_fs_close_ch(uv_file file);

/** goc_fs_close() — Close a file; block until complete (fiber context). */
goc_fs_close_t* goc_fs_close(uv_file file);

/**
 * goc_fs_read_ch() — Initiate an async file read; return result channel.
 *
 * file   : open file descriptor.
 * bufs   : array of nbufs buffers to read into.
 * nbufs  : number of buffers.
 * offset : file offset (-1 to use current position).
 */
goc_chan* goc_fs_read_ch(uv_file file,
                         const uv_buf_t bufs[], unsigned int nbufs,
                         int64_t offset);

/** goc_fs_read() — Read from a file; block until complete (fiber context). */
goc_fs_read_t* goc_fs_read(uv_file file,
                            const uv_buf_t bufs[], unsigned int nbufs,
                            int64_t offset);

/**
 * goc_fs_write_ch() — Initiate an async file write; return result channel.
 *
 * file   : open file descriptor.
 * bufs   : array of nbufs buffers to write.
 * nbufs  : number of buffers.
 * offset : file offset (-1 to use current position).
 */
goc_chan* goc_fs_write_ch(uv_file file,
                          const uv_buf_t bufs[], unsigned int nbufs,
                          int64_t offset);

/** goc_fs_write() — Write to a file; block until complete (fiber context). */
goc_fs_write_t* goc_fs_write(uv_file file,
                              const uv_buf_t bufs[], unsigned int nbufs,
                              int64_t offset);

/**
 * goc_fs_unlink_ch() — Initiate an async file deletion; return result channel.
 *
 * path : path of the file to delete.
 */
goc_chan* goc_fs_unlink_ch(const char* path);

/** goc_fs_unlink() — Delete a file; block until complete (fiber context). */
goc_fs_unlink_t* goc_fs_unlink(const char* path);

/**
 * goc_fs_stat_ch() — Initiate an async file stat; return result channel.
 *
 * path : path of the file to stat.
 */
goc_chan* goc_fs_stat_ch(const char* path);

/** goc_fs_stat() — Stat a file; block until complete (fiber context). */
goc_fs_stat_t* goc_fs_stat(const char* path);

/**
 * goc_fs_rename_ch() — Initiate an async file rename; return result channel.
 *
 * path     : current file path.
 * new_path : new file path.
 */
goc_chan* goc_fs_rename_ch(const char* path, const char* new_path);

/** goc_fs_rename() — Rename a file; block until complete (fiber context). */
goc_fs_rename_t* goc_fs_rename(const char* path, const char* new_path);

/**
 * goc_fs_sendfile_ch() — Initiate an async sendfile; return result channel.
 *
 * out_fd    : destination file descriptor.
 * in_fd     : source file descriptor.
 * in_offset : offset within the source file to start reading from.
 * length    : number of bytes to transfer.
 */
goc_chan* goc_fs_sendfile_ch(uv_file out_fd, uv_file in_fd,
                             int64_t in_offset, size_t length);

/** goc_fs_sendfile() — Sendfile; block until complete (fiber context). */
goc_fs_sendfile_t* goc_fs_sendfile(uv_file out_fd, uv_file in_fd,
                                   int64_t in_offset, size_t length);

/* =========================================================================
 * 4. DNS & Resolution
 * ====================================================================== */

/**
 * goc_getaddrinfo_ch() — Initiate an async getaddrinfo; return result channel.
 *
 * node    : host name or numeric address string (may be NULL).
 * service : service name or port number string (may be NULL).
 * hints   : address hints (may be NULL).
 *
 * Returns a channel delivering goc_getaddrinfo_t*.  On success the caller
 * must release result->res with uv_freeaddrinfo().
 *
 * Safe to call from any context.
 */
goc_chan* goc_getaddrinfo_ch(const char* node, const char* service,
                             const struct addrinfo* hints);

/**
 * goc_getaddrinfo() — Async getaddrinfo; block until complete (fiber context).
 */
goc_getaddrinfo_t* goc_getaddrinfo(const char* node, const char* service,
                                   const struct addrinfo* hints);

/**
 * goc_getnameinfo_ch() — Initiate an async getnameinfo; return result channel.
 *
 * addr  : address to resolve.
 * flags : NI_* flags.
 *
 * Returns a channel delivering goc_getnameinfo_t*.
 *
 * Safe to call from any context.
 */
goc_chan* goc_getnameinfo_ch(const struct sockaddr* addr, int flags);

/**
 * goc_getnameinfo() — Async getnameinfo; block until complete (fiber context).
 */
goc_getnameinfo_t* goc_getnameinfo(const struct sockaddr* addr, int flags);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GOC_IO_H */
