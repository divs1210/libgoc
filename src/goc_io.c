/*
 * src/goc_io.c — Async I/O wrappers for libgoc
 *
 * Implements the channel-returning wrappers declared in include/goc_io.h.
 *
 * Thread-safety strategy
 * ----------------------
 * File-system (uv_fs_*) and DNS (uv_getaddrinfo, uv_getnameinfo) operations
 * are submitted directly from any thread because libuv routes them through
 * its internal worker-thread pool with proper locking.
 *
 * Stream and UDP handle operations (uv_read_start, uv_write, uv_tcp_connect,
 * uv_pipe_connect, uv_shutdown, uv_udp_send, uv_udp_recv_start and the
 * matching stop functions) touch libuv handle internals that are not
 * thread-safe.  These are dispatched to the event loop thread via a
 * one-shot uv_async_t, following the same pattern used by goc_timeout.
 *
 * Result delivery
 * ---------------
 * One-shot operation callbacks (write, connect, shutdown, FS, DNS) use
 * goc_put_sync() on a buffered channel (capacity 1).  Because the channel is
 * empty when the callback fires, goc_put_sync() completes immediately without
 * blocking the loop thread, then goc_close() is called to signal completion.
 *
 * Streaming callbacks (read, UDP recv) use goc_put_cb() (non-blocking) so
 * the loop thread is never stalled waiting for a fiber to consume data.
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <uv.h>
#include <gc.h>
#include "../include/goc_io.h"
#include "internal.h"

/* =========================================================================
 * Shared helpers
 * ====================================================================== */

/* Callback that frees a malloc-allocated libuv handle or context struct. */
static void free_io_handle(uv_handle_t* h)
{
    free(h);
}

/* =========================================================================
 * 3. File System Operations
 *
 * uv_fs_* functions are safe to call from any thread; no async bridge needed.
 * All FS context structs embed uv_fs_t as the first member so the context
 * pointer can be recovered from the req pointer inside callbacks.
 * ====================================================================== */

typedef struct {
    uv_fs_t  req;   /* MUST be first member */
    goc_chan* ch;
} goc_fs_ctx_t;

/* -------------------------------------------------------------------------
 * goc_fs_open
 * ---------------------------------------------------------------------- */

static void on_fs_open(uv_fs_t* req)
{
    goc_fs_ctx_t*  ctx = (goc_fs_ctx_t*)req;
    goc_fs_open_t* res = (goc_fs_open_t*)goc_malloc(sizeof(goc_fs_open_t));
    res->result = (uv_file)req->result;
    uv_fs_req_cleanup(req);
    goc_put_sync(ctx->ch, res);
    goc_close(ctx->ch);
    free(ctx);
}

goc_chan* goc_fs_open_ch(const char* path, int flags, int mode)
{
    goc_chan*     ch  = goc_chan_make(1);
    goc_fs_ctx_t* ctx = (goc_fs_ctx_t*)malloc(sizeof(goc_fs_ctx_t));
    assert(ctx);
    ctx->ch = ch;
    int rc = uv_fs_open(g_loop, &ctx->req, path, flags, mode, on_fs_open);
    if (rc < 0) {
        goc_fs_open_t* res = (goc_fs_open_t*)goc_malloc(sizeof(goc_fs_open_t));
        res->result = (uv_file)rc;
        goc_put_cb(ch, res, NULL, NULL);
        goc_close(ch);
        free(ctx);
    }
    return ch;
}

goc_fs_open_t* goc_fs_open(const char* path, int flags, int mode)
{
    goc_chan*  ch = goc_fs_open_ch(path, flags, mode);
    goc_val_t* v  = goc_take(ch);
    return (goc_fs_open_t*)v->val;
}

/* -------------------------------------------------------------------------
 * goc_fs_close
 * ---------------------------------------------------------------------- */

static void on_fs_close(uv_fs_t* req)
{
    goc_fs_ctx_t*   ctx = (goc_fs_ctx_t*)req;
    goc_fs_close_t* res = (goc_fs_close_t*)goc_malloc(sizeof(goc_fs_close_t));
    res->result = (int)req->result;
    uv_fs_req_cleanup(req);
    goc_put_sync(ctx->ch, res);
    goc_close(ctx->ch);
    free(ctx);
}

goc_chan* goc_fs_close_ch(uv_file file)
{
    goc_chan*     ch  = goc_chan_make(1);
    goc_fs_ctx_t* ctx = (goc_fs_ctx_t*)malloc(sizeof(goc_fs_ctx_t));
    assert(ctx);
    ctx->ch = ch;
    int rc = uv_fs_close(g_loop, &ctx->req, file, on_fs_close);
    if (rc < 0) {
        goc_fs_close_t* res = (goc_fs_close_t*)goc_malloc(sizeof(goc_fs_close_t));
        res->result = rc;
        goc_put_cb(ch, res, NULL, NULL);
        goc_close(ch);
        free(ctx);
    }
    return ch;
}

goc_fs_close_t* goc_fs_close(uv_file file)
{
    goc_chan*  ch = goc_fs_close_ch(file);
    goc_val_t* v  = goc_take(ch);
    return (goc_fs_close_t*)v->val;
}

/* -------------------------------------------------------------------------
 * goc_fs_read
 * ---------------------------------------------------------------------- */

static void on_fs_read(uv_fs_t* req)
{
    goc_fs_ctx_t*  ctx = (goc_fs_ctx_t*)req;
    goc_fs_read_t* res = (goc_fs_read_t*)goc_malloc(sizeof(goc_fs_read_t));
    res->result = (ssize_t)req->result;
    uv_fs_req_cleanup(req);
    goc_put_sync(ctx->ch, res);
    goc_close(ctx->ch);
    free(ctx);
}

goc_chan* goc_fs_read_ch(uv_file file,
                         const uv_buf_t bufs[], unsigned int nbufs,
                         int64_t offset)
{
    goc_chan*     ch  = goc_chan_make(1);
    goc_fs_ctx_t* ctx = (goc_fs_ctx_t*)malloc(sizeof(goc_fs_ctx_t));
    assert(ctx);
    ctx->ch = ch;
    int rc = uv_fs_read(g_loop, &ctx->req, file, bufs, nbufs, offset,
                        on_fs_read);
    if (rc < 0) {
        goc_fs_read_t* res = (goc_fs_read_t*)goc_malloc(sizeof(goc_fs_read_t));
        res->result = rc;
        goc_put_cb(ch, res, NULL, NULL);
        goc_close(ch);
        free(ctx);
    }
    return ch;
}

goc_fs_read_t* goc_fs_read(uv_file file,
                            const uv_buf_t bufs[], unsigned int nbufs,
                            int64_t offset)
{
    goc_chan*  ch = goc_fs_read_ch(file, bufs, nbufs, offset);
    goc_val_t* v  = goc_take(ch);
    return (goc_fs_read_t*)v->val;
}

/* -------------------------------------------------------------------------
 * goc_fs_write
 * ---------------------------------------------------------------------- */

static void on_fs_write(uv_fs_t* req)
{
    goc_fs_ctx_t*   ctx = (goc_fs_ctx_t*)req;
    goc_fs_write_t* res = (goc_fs_write_t*)goc_malloc(sizeof(goc_fs_write_t));
    res->result = (ssize_t)req->result;
    uv_fs_req_cleanup(req);
    goc_put_sync(ctx->ch, res);
    goc_close(ctx->ch);
    free(ctx);
}

goc_chan* goc_fs_write_ch(uv_file file,
                          const uv_buf_t bufs[], unsigned int nbufs,
                          int64_t offset)
{
    goc_chan*     ch  = goc_chan_make(1);
    goc_fs_ctx_t* ctx = (goc_fs_ctx_t*)malloc(sizeof(goc_fs_ctx_t));
    assert(ctx);
    ctx->ch = ch;
    int rc = uv_fs_write(g_loop, &ctx->req, file, bufs, nbufs, offset,
                         on_fs_write);
    if (rc < 0) {
        goc_fs_write_t* res = (goc_fs_write_t*)goc_malloc(sizeof(goc_fs_write_t));
        res->result = rc;
        goc_put_cb(ch, res, NULL, NULL);
        goc_close(ch);
        free(ctx);
    }
    return ch;
}

goc_fs_write_t* goc_fs_write(uv_file file,
                              const uv_buf_t bufs[], unsigned int nbufs,
                              int64_t offset)
{
    goc_chan*  ch = goc_fs_write_ch(file, bufs, nbufs, offset);
    goc_val_t* v  = goc_take(ch);
    return (goc_fs_write_t*)v->val;
}

/* -------------------------------------------------------------------------
 * goc_fs_unlink
 * ---------------------------------------------------------------------- */

static void on_fs_unlink(uv_fs_t* req)
{
    goc_fs_ctx_t*    ctx = (goc_fs_ctx_t*)req;
    goc_fs_unlink_t* res = (goc_fs_unlink_t*)goc_malloc(sizeof(goc_fs_unlink_t));
    res->result = (int)req->result;
    uv_fs_req_cleanup(req);
    goc_put_sync(ctx->ch, res);
    goc_close(ctx->ch);
    free(ctx);
}

goc_chan* goc_fs_unlink_ch(const char* path)
{
    goc_chan*     ch  = goc_chan_make(1);
    goc_fs_ctx_t* ctx = (goc_fs_ctx_t*)malloc(sizeof(goc_fs_ctx_t));
    assert(ctx);
    ctx->ch = ch;
    int rc = uv_fs_unlink(g_loop, &ctx->req, path, on_fs_unlink);
    if (rc < 0) {
        goc_fs_unlink_t* res = (goc_fs_unlink_t*)goc_malloc(sizeof(goc_fs_unlink_t));
        res->result = rc;
        goc_put_cb(ch, res, NULL, NULL);
        goc_close(ch);
        free(ctx);
    }
    return ch;
}

goc_fs_unlink_t* goc_fs_unlink(const char* path)
{
    goc_chan*  ch = goc_fs_unlink_ch(path);
    goc_val_t* v  = goc_take(ch);
    return (goc_fs_unlink_t*)v->val;
}

/* -------------------------------------------------------------------------
 * goc_fs_stat
 * ---------------------------------------------------------------------- */

static void on_fs_stat(uv_fs_t* req)
{
    goc_fs_ctx_t*  ctx = (goc_fs_ctx_t*)req;
    goc_fs_stat_t* res = (goc_fs_stat_t*)goc_malloc(sizeof(goc_fs_stat_t));
    res->result = (int)req->result;
    if (req->result == 0)
        res->statbuf = req->statbuf;
    uv_fs_req_cleanup(req);
    goc_put_sync(ctx->ch, res);
    goc_close(ctx->ch);
    free(ctx);
}

goc_chan* goc_fs_stat_ch(const char* path)
{
    goc_chan*     ch  = goc_chan_make(1);
    goc_fs_ctx_t* ctx = (goc_fs_ctx_t*)malloc(sizeof(goc_fs_ctx_t));
    assert(ctx);
    ctx->ch = ch;
    int rc = uv_fs_stat(g_loop, &ctx->req, path, on_fs_stat);
    if (rc < 0) {
        goc_fs_stat_t* res = (goc_fs_stat_t*)goc_malloc(sizeof(goc_fs_stat_t));
        res->result = rc;
        goc_put_cb(ch, res, NULL, NULL);
        goc_close(ch);
        free(ctx);
    }
    return ch;
}

goc_fs_stat_t* goc_fs_stat(const char* path)
{
    goc_chan*  ch = goc_fs_stat_ch(path);
    goc_val_t* v  = goc_take(ch);
    return (goc_fs_stat_t*)v->val;
}

/* -------------------------------------------------------------------------
 * goc_fs_rename
 * ---------------------------------------------------------------------- */

static void on_fs_rename(uv_fs_t* req)
{
    goc_fs_ctx_t*    ctx = (goc_fs_ctx_t*)req;
    goc_fs_rename_t* res = (goc_fs_rename_t*)goc_malloc(sizeof(goc_fs_rename_t));
    res->result = (int)req->result;
    uv_fs_req_cleanup(req);
    goc_put_sync(ctx->ch, res);
    goc_close(ctx->ch);
    free(ctx);
}

goc_chan* goc_fs_rename_ch(const char* path, const char* new_path)
{
    goc_chan*     ch  = goc_chan_make(1);
    goc_fs_ctx_t* ctx = (goc_fs_ctx_t*)malloc(sizeof(goc_fs_ctx_t));
    assert(ctx);
    ctx->ch = ch;
    int rc = uv_fs_rename(g_loop, &ctx->req, path, new_path, on_fs_rename);
    if (rc < 0) {
        goc_fs_rename_t* res = (goc_fs_rename_t*)goc_malloc(sizeof(goc_fs_rename_t));
        res->result = rc;
        goc_put_cb(ch, res, NULL, NULL);
        goc_close(ch);
        free(ctx);
    }
    return ch;
}

goc_fs_rename_t* goc_fs_rename(const char* path, const char* new_path)
{
    goc_chan*  ch = goc_fs_rename_ch(path, new_path);
    goc_val_t* v  = goc_take(ch);
    return (goc_fs_rename_t*)v->val;
}

/* -------------------------------------------------------------------------
 * goc_fs_sendfile
 * ---------------------------------------------------------------------- */

static void on_fs_sendfile(uv_fs_t* req)
{
    goc_fs_ctx_t*      ctx = (goc_fs_ctx_t*)req;
    goc_fs_sendfile_t* res = (goc_fs_sendfile_t*)goc_malloc(sizeof(goc_fs_sendfile_t));
    res->result = (ssize_t)req->result;
    uv_fs_req_cleanup(req);
    goc_put_sync(ctx->ch, res);
    goc_close(ctx->ch);
    free(ctx);
}

goc_chan* goc_fs_sendfile_ch(uv_file out_fd, uv_file in_fd,
                             int64_t in_offset, size_t length)
{
    goc_chan*     ch  = goc_chan_make(1);
    goc_fs_ctx_t* ctx = (goc_fs_ctx_t*)malloc(sizeof(goc_fs_ctx_t));
    assert(ctx);
    ctx->ch = ch;
    int rc = uv_fs_sendfile(g_loop, &ctx->req, out_fd, in_fd, in_offset,
                            length, on_fs_sendfile);
    if (rc < 0) {
        goc_fs_sendfile_t* res = (goc_fs_sendfile_t*)goc_malloc(sizeof(goc_fs_sendfile_t));
        res->result = rc;
        goc_put_cb(ch, res, NULL, NULL);
        goc_close(ch);
        free(ctx);
    }
    return ch;
}

goc_fs_sendfile_t* goc_fs_sendfile(uv_file out_fd, uv_file in_fd,
                                   int64_t in_offset, size_t length)
{
    goc_chan*  ch = goc_fs_sendfile_ch(out_fd, in_fd, in_offset, length);
    goc_val_t* v  = goc_take(ch);
    return (goc_fs_sendfile_t*)v->val;
}

/* =========================================================================
 * 4. DNS & Resolution
 *
 * uv_getaddrinfo and uv_getnameinfo are safe to call from any thread.
 * ====================================================================== */

/* -------------------------------------------------------------------------
 * goc_getaddrinfo
 * ---------------------------------------------------------------------- */

typedef struct {
    uv_getaddrinfo_t req;   /* MUST be first member */
    goc_chan*        ch;
} goc_getaddrinfo_ctx_t;

static void on_getaddrinfo(uv_getaddrinfo_t* req, int status,
                           struct addrinfo* res)
{
    goc_getaddrinfo_ctx_t* ctx = (goc_getaddrinfo_ctx_t*)req;
    goc_getaddrinfo_t*     r   = (goc_getaddrinfo_t*)goc_malloc(
                                     sizeof(goc_getaddrinfo_t));
    r->status = status;
    r->res    = res;   /* caller must call uv_freeaddrinfo(r->res) */
    goc_put_sync(ctx->ch, r);
    goc_close(ctx->ch);
    free(ctx);
}

goc_chan* goc_getaddrinfo_ch(const char* node, const char* service,
                             const struct addrinfo* hints)
{
    goc_chan*              ch  = goc_chan_make(1);
    goc_getaddrinfo_ctx_t* ctx = (goc_getaddrinfo_ctx_t*)malloc(
                                     sizeof(goc_getaddrinfo_ctx_t));
    assert(ctx);
    ctx->ch = ch;
    int rc = uv_getaddrinfo(g_loop, &ctx->req, on_getaddrinfo,
                            node, service, hints);
    if (rc < 0) {
        goc_getaddrinfo_t* r = (goc_getaddrinfo_t*)goc_malloc(
                                   sizeof(goc_getaddrinfo_t));
        r->status = rc;
        r->res    = NULL;
        goc_put_cb(ch, r, NULL, NULL);
        goc_close(ch);
        free(ctx);
    }
    return ch;
}

goc_getaddrinfo_t* goc_getaddrinfo(const char* node, const char* service,
                                   const struct addrinfo* hints)
{
    goc_chan*  ch = goc_getaddrinfo_ch(node, service, hints);
    goc_val_t* v  = goc_take(ch);
    return (goc_getaddrinfo_t*)v->val;
}

/* -------------------------------------------------------------------------
 * goc_getnameinfo
 * ---------------------------------------------------------------------- */

typedef struct {
    uv_getnameinfo_t req;   /* MUST be first member */
    goc_chan*        ch;
} goc_getnameinfo_ctx_t;

static void on_getnameinfo(uv_getnameinfo_t* req, int status,
                           const char* hostname, const char* service)
{
    goc_getnameinfo_ctx_t* ctx = (goc_getnameinfo_ctx_t*)req;
    goc_getnameinfo_t*     r   = (goc_getnameinfo_t*)goc_malloc(
                                     sizeof(goc_getnameinfo_t));
    r->status = status;
    if (hostname)
        strncpy(r->hostname, hostname, sizeof(r->hostname) - 1);
    else
        r->hostname[0] = '\0';
    if (service)
        strncpy(r->service, service, sizeof(r->service) - 1);
    else
        r->service[0] = '\0';
    r->hostname[sizeof(r->hostname) - 1] = '\0';
    r->service[sizeof(r->service)  - 1] = '\0';
    goc_put_sync(ctx->ch, r);
    goc_close(ctx->ch);
    free(ctx);
}

goc_chan* goc_getnameinfo_ch(const struct sockaddr* addr, int flags)
{
    goc_chan*              ch  = goc_chan_make(1);
    goc_getnameinfo_ctx_t* ctx = (goc_getnameinfo_ctx_t*)malloc(
                                     sizeof(goc_getnameinfo_ctx_t));
    assert(ctx);
    ctx->ch = ch;
    int rc = uv_getnameinfo(g_loop, &ctx->req, on_getnameinfo, addr, flags);
    if (rc < 0) {
        goc_getnameinfo_t* r = (goc_getnameinfo_t*)goc_malloc(
                                   sizeof(goc_getnameinfo_t));
        r->status    = rc;
        r->hostname[0] = '\0';
        r->service[0]  = '\0';
        goc_put_cb(ch, r, NULL, NULL);
        goc_close(ch);
        free(ctx);
    }
    return ch;
}

goc_getnameinfo_t* goc_getnameinfo(const struct sockaddr* addr, int flags)
{
    goc_chan*  ch = goc_getnameinfo_ch(addr, flags);
    goc_val_t* v  = goc_take(ch);
    return (goc_getnameinfo_t*)v->val;
}

/* =========================================================================
 * 1. Stream I/O  (TCP, Pipes, TTY)
 *
 * Stream handle operations are NOT thread-safe.  They are dispatched to the
 * event loop thread via a one-shot uv_async_t bridge.
 *
 * The streaming read and stop operations store a context pointer in
 * handle->data.  The caller must not use handle->data for other purposes
 * while a goc_read_start / goc_read_stop pair is active on that handle.
 * ====================================================================== */

/* -------------------------------------------------------------------------
 * Common alloc callback used by both stream read and UDP recv.
 * ---------------------------------------------------------------------- */

static void goc_alloc_cb(uv_handle_t* handle, size_t suggested_size,
                         uv_buf_t* buf)
{
    (void)handle;
    buf->base = (char*)malloc(suggested_size);
    buf->len  = buf->base ? suggested_size : 0;
}

/* -------------------------------------------------------------------------
 * goc_read_start
 * ---------------------------------------------------------------------- */

typedef struct {
    goc_chan* ch;
} goc_stream_ctx_t;

static void on_read_cb(uv_stream_t* stream, ssize_t nread,
                       const uv_buf_t* buf)
{
    goc_stream_ctx_t* ctx = (goc_stream_ctx_t*)stream->data;

    if (nread == 0) {
        /* EAGAIN / EWOULDBLOCK — no data right now; free the buffer. */
        free(buf->base);
        return;
    }

    if (nread < 0) {
        /* EOF or error: deliver a final result with the error code, then
         * close the channel and free the context. */
        free(buf->base);
        goc_read_t* res = (goc_read_t*)goc_malloc(sizeof(goc_read_t));
        res->nread    = nread;
        res->buf.base = NULL;
        res->buf.len  = 0;
        /* Use goc_put_cb so the loop thread is not blocked. */
        goc_put_cb(ctx->ch, res, NULL, NULL);
        goc_close(ctx->ch);
        free(ctx);
        stream->data = NULL;
        return;
    }

    /* Normal data: wrap and deliver.  buf.base ownership transfers to res. */
    goc_read_t* res = (goc_read_t*)goc_malloc(sizeof(goc_read_t));
    res->nread = nread;
    res->buf   = *buf;
    goc_put_cb(ctx->ch, res, NULL, NULL);
}

typedef struct {
    uv_async_t   async;   /* MUST be first member */
    uv_stream_t* stream;
    goc_chan*    ch;
} goc_read_start_dispatch_t;

static void on_read_start_dispatch(uv_async_t* h)
{
    goc_read_start_dispatch_t* d = (goc_read_start_dispatch_t*)h;

    goc_stream_ctx_t* ctx = (goc_stream_ctx_t*)malloc(sizeof(goc_stream_ctx_t));
    assert(ctx);
    ctx->ch        = d->ch;
    d->stream->data = ctx;

    int rc = uv_read_start(d->stream, goc_alloc_cb, on_read_cb);
    if (rc < 0) {
        /* Failed to start: deliver error and close channel. */
        goc_read_t* res = (goc_read_t*)goc_malloc(sizeof(goc_read_t));
        res->nread    = rc;
        res->buf.base = NULL;
        res->buf.len  = 0;
        goc_put_sync(d->ch, res);
        goc_close(d->ch);
        free(ctx);
        d->stream->data = NULL;
    }

    uv_close((uv_handle_t*)h, free_io_handle);
}

goc_chan* goc_read_start(uv_stream_t* stream)
{
    goc_chan*                   ch = goc_chan_make(16);
    goc_read_start_dispatch_t*  d  = (goc_read_start_dispatch_t*)malloc(
                                         sizeof(goc_read_start_dispatch_t));
    assert(d);
    d->stream = stream;
    d->ch     = ch;
    uv_async_init(g_loop, &d->async, on_read_start_dispatch);
    uv_async_send(&d->async);
    return ch;
}

/* -------------------------------------------------------------------------
 * goc_read_stop
 * ---------------------------------------------------------------------- */

typedef struct {
    uv_async_t   async;   /* MUST be first member */
    uv_stream_t* stream;
} goc_read_stop_dispatch_t;

static void on_read_stop_dispatch(uv_async_t* h)
{
    goc_read_stop_dispatch_t* d = (goc_read_stop_dispatch_t*)h;

    uv_read_stop(d->stream);

    if (d->stream->data) {
        goc_stream_ctx_t* ctx = (goc_stream_ctx_t*)d->stream->data;
        goc_close(ctx->ch);
        free(ctx);
        d->stream->data = NULL;
    }

    uv_close((uv_handle_t*)h, free_io_handle);
}

int goc_read_stop(uv_stream_t* stream)
{
    goc_read_stop_dispatch_t* d = (goc_read_stop_dispatch_t*)malloc(
                                      sizeof(goc_read_stop_dispatch_t));
    assert(d);
    d->stream = stream;
    uv_async_init(g_loop, &d->async, on_read_stop_dispatch);
    uv_async_send(&d->async);
    return 0;
}

/* -------------------------------------------------------------------------
 * goc_write / goc_write_ch
 * ---------------------------------------------------------------------- */

typedef struct {
    uv_write_t  req;    /* MUST be first member */
    goc_chan*   ch;
} goc_write_ctx_t;

static void on_write_cb(uv_write_t* req, int status)
{
    goc_write_ctx_t* ctx = (goc_write_ctx_t*)req;
    goc_write_t*     res = (goc_write_t*)goc_malloc(sizeof(goc_write_t));
    res->status = status;
    goc_put_sync(ctx->ch, res);
    goc_close(ctx->ch);
    free(ctx);
}

typedef struct {
    uv_async_t      async;    /* MUST be first member */
    uv_stream_t*    handle;
    const uv_buf_t* bufs;
    unsigned int    nbufs;
    goc_chan*       ch;
} goc_write_dispatch_t;

static void on_write_dispatch(uv_async_t* h)
{
    goc_write_dispatch_t* d   = (goc_write_dispatch_t*)h;
    goc_write_ctx_t*      ctx = (goc_write_ctx_t*)malloc(sizeof(goc_write_ctx_t));
    assert(ctx);
    ctx->ch = d->ch;
    int rc = uv_write(&ctx->req, d->handle, d->bufs, d->nbufs, on_write_cb);
    if (rc < 0) {
        goc_write_t* res = (goc_write_t*)goc_malloc(sizeof(goc_write_t));
        res->status = rc;
        goc_put_sync(ctx->ch, res);
        goc_close(ctx->ch);
        free(ctx);
    }
    uv_close((uv_handle_t*)h, free_io_handle);
}

goc_chan* goc_write_ch(uv_stream_t* handle,
                       const uv_buf_t bufs[], unsigned int nbufs)
{
    goc_chan*             ch = goc_chan_make(1);
    goc_write_dispatch_t* d  = (goc_write_dispatch_t*)malloc(
                                    sizeof(goc_write_dispatch_t));
    assert(d);
    d->handle = handle;
    d->bufs   = bufs;
    d->nbufs  = nbufs;
    d->ch     = ch;
    uv_async_init(g_loop, &d->async, on_write_dispatch);
    uv_async_send(&d->async);
    return ch;
}

goc_write_t* goc_write(uv_stream_t* handle,
                        const uv_buf_t bufs[], unsigned int nbufs)
{
    goc_chan*  ch = goc_write_ch(handle, bufs, nbufs);
    goc_val_t* v  = goc_take(ch);
    return (goc_write_t*)v->val;
}

/* -------------------------------------------------------------------------
 * goc_write2 / goc_write2_ch  (IPC streams)
 * ---------------------------------------------------------------------- */

typedef struct {
    uv_write_t   req;         /* MUST be first member */
    goc_chan*    ch;
} goc_write2_ctx_t;

/* Reuse on_write_cb for write2 (same signature, same semantics). */
static void on_write2_cb(uv_write_t* req, int status)
{
    goc_write2_ctx_t* ctx = (goc_write2_ctx_t*)req;
    goc_write_t*      res = (goc_write_t*)goc_malloc(sizeof(goc_write_t));
    res->status = status;
    goc_put_sync(ctx->ch, res);
    goc_close(ctx->ch);
    free(ctx);
}

typedef struct {
    uv_async_t      async;        /* MUST be first member */
    uv_stream_t*    handle;
    const uv_buf_t* bufs;
    unsigned int    nbufs;
    uv_stream_t*    send_handle;
    goc_chan*       ch;
} goc_write2_dispatch_t;

static void on_write2_dispatch(uv_async_t* h)
{
    goc_write2_dispatch_t* d   = (goc_write2_dispatch_t*)h;
    goc_write2_ctx_t*      ctx = (goc_write2_ctx_t*)malloc(
                                     sizeof(goc_write2_ctx_t));
    assert(ctx);
    ctx->ch = d->ch;
    int rc = uv_write2(&ctx->req, d->handle, d->bufs, d->nbufs,
                       d->send_handle, on_write2_cb);
    if (rc < 0) {
        goc_write_t* res = (goc_write_t*)goc_malloc(sizeof(goc_write_t));
        res->status = rc;
        goc_put_sync(ctx->ch, res);
        goc_close(ctx->ch);
        free(ctx);
    }
    uv_close((uv_handle_t*)h, free_io_handle);
}

goc_chan* goc_write2_ch(uv_stream_t* handle,
                        const uv_buf_t bufs[], unsigned int nbufs,
                        uv_stream_t* send_handle)
{
    goc_chan*              ch = goc_chan_make(1);
    goc_write2_dispatch_t* d  = (goc_write2_dispatch_t*)malloc(
                                     sizeof(goc_write2_dispatch_t));
    assert(d);
    d->handle      = handle;
    d->bufs        = bufs;
    d->nbufs       = nbufs;
    d->send_handle = send_handle;
    d->ch          = ch;
    uv_async_init(g_loop, &d->async, on_write2_dispatch);
    uv_async_send(&d->async);
    return ch;
}

goc_write_t* goc_write2(uv_stream_t* handle,
                         const uv_buf_t bufs[], unsigned int nbufs,
                         uv_stream_t* send_handle)
{
    goc_chan*  ch = goc_write2_ch(handle, bufs, nbufs, send_handle);
    goc_val_t* v  = goc_take(ch);
    return (goc_write_t*)v->val;
}

/* -------------------------------------------------------------------------
 * goc_shutdown_stream / goc_shutdown_stream_ch
 * ---------------------------------------------------------------------- */

typedef struct {
    uv_shutdown_t req;   /* MUST be first member */
    goc_chan*     ch;
} goc_shutdown_ctx_t;

static void on_shutdown_cb(uv_shutdown_t* req, int status)
{
    goc_shutdown_ctx_t* ctx = (goc_shutdown_ctx_t*)req;
    goc_shutdown_t*     res = (goc_shutdown_t*)goc_malloc(sizeof(goc_shutdown_t));
    res->status = status;
    goc_put_sync(ctx->ch, res);
    goc_close(ctx->ch);
    free(ctx);
}

typedef struct {
    uv_async_t   async;    /* MUST be first member */
    uv_stream_t* handle;
    goc_chan*    ch;
} goc_shutdown_dispatch_t;

static void on_shutdown_dispatch(uv_async_t* h)
{
    goc_shutdown_dispatch_t* d   = (goc_shutdown_dispatch_t*)h;
    goc_shutdown_ctx_t*      ctx = (goc_shutdown_ctx_t*)malloc(
                                       sizeof(goc_shutdown_ctx_t));
    assert(ctx);
    ctx->ch = d->ch;
    int rc = uv_shutdown(&ctx->req, d->handle, on_shutdown_cb);
    if (rc < 0) {
        goc_shutdown_t* res = (goc_shutdown_t*)goc_malloc(sizeof(goc_shutdown_t));
        res->status = rc;
        goc_put_sync(ctx->ch, res);
        goc_close(ctx->ch);
        free(ctx);
    }
    uv_close((uv_handle_t*)h, free_io_handle);
}

goc_chan* goc_shutdown_stream_ch(uv_stream_t* handle)
{
    goc_chan*                ch = goc_chan_make(1);
    goc_shutdown_dispatch_t* d  = (goc_shutdown_dispatch_t*)malloc(
                                      sizeof(goc_shutdown_dispatch_t));
    assert(d);
    d->handle = handle;
    d->ch     = ch;
    uv_async_init(g_loop, &d->async, on_shutdown_dispatch);
    uv_async_send(&d->async);
    return ch;
}

goc_shutdown_t* goc_shutdown_stream(uv_stream_t* handle)
{
    goc_chan*  ch = goc_shutdown_stream_ch(handle);
    goc_val_t* v  = goc_take(ch);
    return (goc_shutdown_t*)v->val;
}

/* -------------------------------------------------------------------------
 * goc_tcp_connect / goc_tcp_connect_ch
 * ---------------------------------------------------------------------- */

typedef struct {
    uv_connect_t req;   /* MUST be first member */
    goc_chan*    ch;
} goc_connect_ctx_t;

static void on_connect_cb(uv_connect_t* req, int status)
{
    goc_connect_ctx_t* ctx = (goc_connect_ctx_t*)req;
    goc_connect_t*     res = (goc_connect_t*)goc_malloc(sizeof(goc_connect_t));
    res->status = status;
    goc_put_sync(ctx->ch, res);
    goc_close(ctx->ch);
    free(ctx);
}

typedef struct {
    uv_async_t          async;    /* MUST be first member */
    uv_tcp_t*           handle;
    struct sockaddr_storage addr; /* copy of the target address */
    goc_chan*           ch;
} goc_tcp_connect_dispatch_t;

static void on_tcp_connect_dispatch(uv_async_t* h)
{
    goc_tcp_connect_dispatch_t* d   = (goc_tcp_connect_dispatch_t*)h;
    goc_connect_ctx_t*          ctx = (goc_connect_ctx_t*)malloc(
                                          sizeof(goc_connect_ctx_t));
    assert(ctx);
    ctx->ch = d->ch;
    int rc = uv_tcp_connect(&ctx->req, d->handle,
                            (const struct sockaddr*)&d->addr,
                            on_connect_cb);
    if (rc < 0) {
        goc_connect_t* res = (goc_connect_t*)goc_malloc(sizeof(goc_connect_t));
        res->status = rc;
        goc_put_sync(ctx->ch, res);
        goc_close(ctx->ch);
        free(ctx);
    }
    uv_close((uv_handle_t*)h, free_io_handle);
}

goc_chan* goc_tcp_connect_ch(uv_tcp_t* handle, const struct sockaddr* addr)
{
    goc_chan*                   ch = goc_chan_make(1);
    goc_tcp_connect_dispatch_t* d  = (goc_tcp_connect_dispatch_t*)malloc(
                                         sizeof(goc_tcp_connect_dispatch_t));
    assert(d);
    d->handle = handle;
    /* Copy the address to avoid dangling-pointer issues if the caller's
     * address lives on a stack frame that may be reused before the async
     * dispatch fires on the loop thread. */
    memcpy(&d->addr, addr,
           addr->sa_family == AF_INET6
               ? sizeof(struct sockaddr_in6)
               : sizeof(struct sockaddr_in));
    d->ch = ch;
    uv_async_init(g_loop, &d->async, on_tcp_connect_dispatch);
    uv_async_send(&d->async);
    return ch;
}

goc_connect_t* goc_tcp_connect(uv_tcp_t* handle, const struct sockaddr* addr)
{
    goc_chan*  ch = goc_tcp_connect_ch(handle, addr);
    goc_val_t* v  = goc_take(ch);
    return (goc_connect_t*)v->val;
}

/* -------------------------------------------------------------------------
 * goc_pipe_connect / goc_pipe_connect_ch
 * ---------------------------------------------------------------------- */

/* uv_pipe_connect has no return code; the callback always fires. */

typedef struct {
    uv_async_t   async;    /* MUST be first member */
    uv_pipe_t*   handle;
    char*        name;     /* malloc-copied pipe name */
    goc_chan*    ch;
} goc_pipe_connect_dispatch_t;

static void on_pipe_connect_dispatch(uv_async_t* h)
{
    goc_pipe_connect_dispatch_t* d   = (goc_pipe_connect_dispatch_t*)h;
    goc_connect_ctx_t*           ctx = (goc_connect_ctx_t*)malloc(
                                           sizeof(goc_connect_ctx_t));
    assert(ctx);
    ctx->ch = d->ch;
    uv_pipe_connect(&ctx->req, d->handle, d->name, on_connect_cb);
    free(d->name);
    uv_close((uv_handle_t*)h, free_io_handle);
}

goc_chan* goc_pipe_connect_ch(uv_pipe_t* handle, const char* name)
{
    goc_chan*                    ch = goc_chan_make(1);
    goc_pipe_connect_dispatch_t* d  = (goc_pipe_connect_dispatch_t*)malloc(
                                          sizeof(goc_pipe_connect_dispatch_t));
    assert(d);
    d->handle = handle;
    d->name   = strdup(name);   /* copied so the caller's string can be freed */
    assert(d->name);
    d->ch = ch;
    uv_async_init(g_loop, &d->async, on_pipe_connect_dispatch);
    uv_async_send(&d->async);
    return ch;
}

goc_connect_t* goc_pipe_connect(uv_pipe_t* handle, const char* name)
{
    goc_chan*  ch = goc_pipe_connect_ch(handle, name);
    goc_val_t* v  = goc_take(ch);
    return (goc_connect_t*)v->val;
}

/* =========================================================================
 * 2. UDP (Datagrams)
 * ====================================================================== */

/* -------------------------------------------------------------------------
 * goc_udp_send / goc_udp_send_ch
 * ---------------------------------------------------------------------- */

typedef struct {
    uv_udp_send_t req;   /* MUST be first member */
    goc_chan*     ch;
} goc_udp_send_ctx_t;

static void on_udp_send_cb(uv_udp_send_t* req, int status)
{
    goc_udp_send_ctx_t* ctx = (goc_udp_send_ctx_t*)req;
    goc_udp_send_t*     res = (goc_udp_send_t*)goc_malloc(sizeof(goc_udp_send_t));
    res->status = status;
    goc_put_sync(ctx->ch, res);
    goc_close(ctx->ch);
    free(ctx);
}

typedef struct {
    uv_async_t              async;    /* MUST be first member */
    uv_udp_t*               handle;
    const uv_buf_t*         bufs;
    unsigned int            nbufs;
    struct sockaddr_storage addr;     /* copy of destination address */
    goc_chan*               ch;
} goc_udp_send_dispatch_t;

static void on_udp_send_dispatch(uv_async_t* h)
{
    goc_udp_send_dispatch_t* d   = (goc_udp_send_dispatch_t*)h;
    goc_udp_send_ctx_t*      ctx = (goc_udp_send_ctx_t*)malloc(
                                       sizeof(goc_udp_send_ctx_t));
    assert(ctx);
    ctx->ch = d->ch;
    int rc = uv_udp_send(&ctx->req, d->handle, d->bufs, d->nbufs,
                         (const struct sockaddr*)&d->addr, on_udp_send_cb);
    if (rc < 0) {
        goc_udp_send_t* res = (goc_udp_send_t*)goc_malloc(sizeof(goc_udp_send_t));
        res->status = rc;
        goc_put_sync(ctx->ch, res);
        goc_close(ctx->ch);
        free(ctx);
    }
    uv_close((uv_handle_t*)h, free_io_handle);
}

goc_chan* goc_udp_send_ch(uv_udp_t* handle,
                          const uv_buf_t bufs[], unsigned int nbufs,
                          const struct sockaddr* addr)
{
    goc_chan*                ch = goc_chan_make(1);
    goc_udp_send_dispatch_t* d  = (goc_udp_send_dispatch_t*)malloc(
                                      sizeof(goc_udp_send_dispatch_t));
    assert(d);
    d->handle = handle;
    d->bufs   = bufs;
    d->nbufs  = nbufs;
    memcpy(&d->addr, addr,
           addr->sa_family == AF_INET6
               ? sizeof(struct sockaddr_in6)
               : sizeof(struct sockaddr_in));
    d->ch = ch;
    uv_async_init(g_loop, &d->async, on_udp_send_dispatch);
    uv_async_send(&d->async);
    return ch;
}

goc_udp_send_t* goc_udp_send(uv_udp_t* handle,
                              const uv_buf_t bufs[], unsigned int nbufs,
                              const struct sockaddr* addr)
{
    goc_chan*  ch = goc_udp_send_ch(handle, bufs, nbufs, addr);
    goc_val_t* v  = goc_take(ch);
    return (goc_udp_send_t*)v->val;
}

/* -------------------------------------------------------------------------
 * goc_udp_recv_start / goc_udp_recv_stop
 * ---------------------------------------------------------------------- */

typedef struct {
    goc_chan* ch;
} goc_udp_recv_ctx_t;

static void on_udp_recv_cb(uv_udp_t* handle, ssize_t nread,
                           const uv_buf_t* buf, const struct sockaddr* addr,
                           unsigned flags)
{
    goc_udp_recv_ctx_t* ctx = (goc_udp_recv_ctx_t*)handle->data;

    if (nread == 0 && addr == NULL) {
        /* libuv fires this when there is no more data; free unused buffer. */
        free(buf->base);
        return;
    }

    if (nread < 0) {
        /* Error: deliver final result with error code and close channel. */
        free(buf->base);
        goc_udp_recv_t* res = (goc_udp_recv_t*)goc_malloc(sizeof(goc_udp_recv_t));
        res->nread    = nread;
        res->buf.base = NULL;
        res->buf.len  = 0;
        memset(&res->addr, 0, sizeof(res->addr));
        res->flags    = 0;
        goc_put_cb(ctx->ch, res, NULL, NULL);
        goc_close(ctx->ch);
        free(ctx);
        handle->data = NULL;
        return;
    }

    /* Normal datagram: wrap and deliver. */
    goc_udp_recv_t* res = (goc_udp_recv_t*)goc_malloc(sizeof(goc_udp_recv_t));
    res->nread = nread;
    res->buf   = *buf;
    if (addr)
        memcpy(&res->addr, addr,
               addr->sa_family == AF_INET6
                   ? sizeof(struct sockaddr_in6)
                   : sizeof(struct sockaddr_in));
    else
        memset(&res->addr, 0, sizeof(res->addr));
    res->flags = flags;
    goc_put_cb(ctx->ch, res, NULL, NULL);
}

typedef struct {
    uv_async_t  async;    /* MUST be first member */
    uv_udp_t*   handle;
    goc_chan*   ch;
} goc_udp_recv_start_dispatch_t;

static void on_udp_recv_start_dispatch(uv_async_t* h)
{
    goc_udp_recv_start_dispatch_t* d = (goc_udp_recv_start_dispatch_t*)h;

    goc_udp_recv_ctx_t* ctx = (goc_udp_recv_ctx_t*)malloc(
                                  sizeof(goc_udp_recv_ctx_t));
    assert(ctx);
    ctx->ch        = d->ch;
    d->handle->data = ctx;

    int rc = uv_udp_recv_start(d->handle, goc_alloc_cb, on_udp_recv_cb);
    if (rc < 0) {
        goc_udp_recv_t* res = (goc_udp_recv_t*)goc_malloc(sizeof(goc_udp_recv_t));
        res->nread    = rc;
        res->buf.base = NULL;
        res->buf.len  = 0;
        memset(&res->addr, 0, sizeof(res->addr));
        res->flags    = 0;
        goc_put_sync(d->ch, res);
        goc_close(d->ch);
        free(ctx);
        d->handle->data = NULL;
    }

    uv_close((uv_handle_t*)h, free_io_handle);
}

goc_chan* goc_udp_recv_start(uv_udp_t* handle)
{
    goc_chan*                      ch = goc_chan_make(16);
    goc_udp_recv_start_dispatch_t* d  = (goc_udp_recv_start_dispatch_t*)malloc(
                                            sizeof(goc_udp_recv_start_dispatch_t));
    assert(d);
    d->handle = handle;
    d->ch     = ch;
    uv_async_init(g_loop, &d->async, on_udp_recv_start_dispatch);
    uv_async_send(&d->async);
    return ch;
}

typedef struct {
    uv_async_t  async;    /* MUST be first member */
    uv_udp_t*   handle;
} goc_udp_recv_stop_dispatch_t;

static void on_udp_recv_stop_dispatch(uv_async_t* h)
{
    goc_udp_recv_stop_dispatch_t* d = (goc_udp_recv_stop_dispatch_t*)h;

    uv_udp_recv_stop(d->handle);

    if (d->handle->data) {
        goc_udp_recv_ctx_t* ctx = (goc_udp_recv_ctx_t*)d->handle->data;
        goc_close(ctx->ch);
        free(ctx);
        d->handle->data = NULL;
    }

    uv_close((uv_handle_t*)h, free_io_handle);
}

int goc_udp_recv_stop(uv_udp_t* handle)
{
    goc_udp_recv_stop_dispatch_t* d = (goc_udp_recv_stop_dispatch_t*)malloc(
                                          sizeof(goc_udp_recv_stop_dispatch_t));
    assert(d);
    d->handle = handle;
    uv_async_init(g_loop, &d->async, on_udp_recv_stop_dispatch);
    uv_async_send(&d->async);
    return 0;
}
