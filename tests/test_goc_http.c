/*
 * tests/test_goc_http.c — goc_http server and client tests
 *
 * Verifies the goc_http HTTP library declared in goc_http.h.
 * Tests run a real HTTP server on loopback ports and make real HTTP client
 * requests against it.
 *
 * Build:  cmake -B build && cmake --build build
 * Run:    ctest --test-dir build --output-on-failure
 *         ./build/test_goc_http
 *
 * Test groupings:
 *
 *   H1.x  Server lifecycle
 *   H2.x  Routing
 *   H3.x  Request context helpers
 *   H4.x  Sending responses
 *   H5.x  Middleware
 *   H6.x  HTTP client
 *   H7.x  Security
 *   H8.x  Correctness
 *
 * Test coverage:
 *
 *   H1.1  Server lifecycle: make → listen → close (no routes)
 *   H2.1  Routing: exact path match
 *   H2.2  Routing: catch-all wildcard /*
 *   H2.3  Routing: unmatched request → 404
 *   H3.1  Request context: goc_http_server_header (present)
 *   H3.2  Request context: goc_http_server_header (absent)
 *   H3.3  Request context: goc_http_server_header (case-insensitive)
 *   H3.4  Request context: goc_http_server_body_str (with body)
 *   H3.5  Request context: goc_http_server_body_str (empty body)
 *   H4.1  Response: goc_http_server_respond (200)
 *   H4.2  Response: goc_http_server_respond_buf
 *   H4.3  Response: goc_http_server_respond_error
 *   H5.1  Middleware: chain runs in order; user_data propagates
 *   H5.2  Middleware: GOC_HTTP_ERR short-circuits with 500
 *   H6.1  HTTP client: goc_http_get
 *   H6.2  HTTP client: goc_http_post
 *   H6.3  HTTP client: parallel requests with goc_take_all
 *   H6.4  HTTP client: timeout fires correctly
 *   H7.1  Security: oversized request body rejected; server stays alive
 *   H7.2  Security: method mismatch (GET route hit with POST) → 404
 *   H7.3  Security: CRLF in header value blocked (header-injection)
 *   H8.1  Correctness: ctx->path and ctx->method populated correctly
 *   H8.2  Correctness: ctx->query parsed from URL query string
 *   H8.3  Correctness: goc_http_response_t->body_len matches respond_buf len
 *   H8.4  Correctness: custom request headers via opts->headers received
 *
 *   H9.x  Regression
 *   H9.1  Regression: ping-pong — two servers bounce a counter N times
 */

#if !defined(_WIN32) && !defined(__APPLE__)
#  define _GNU_SOURCE
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "test_harness.h"
#include "goc.h"
#include "goc_array.h"
#include "goc_http.h"

#define STRINGIFY_(x) #x
#define STRINGIFY(x)  STRINGIFY_(x)

/* =========================================================================
 * Helpers
 * ====================================================================== */

static int s_port = 18400;

static int next_port(void)
{
    return s_port++;
}

static char s_url_buf[256];
static const char* local_url(const char* path, int port)
{
    snprintf(s_url_buf, sizeof(s_url_buf), "http://127.0.0.1:%d%s", port, path);
    return s_url_buf;
}

/* Shared simple handler: replies 200 "pong". */
static void handler_ping(goc_http_ctx_t* ctx)
{
    goc_take(goc_http_server_respond(ctx, 200, "text/plain", "pong"));
}

/* Shared result structs used across multiple test groups. */
typedef struct {
    goc_chan* done;
    int       port;
    int       status;
    char      body[64];
} h_simple_t;

typedef struct { goc_chan* done; int port; int status; } h_status_t;

/* =========================================================================
 * H1.x — Server lifecycle
 * ====================================================================== */

typedef struct { goc_chan* done; int port; } h1_1_args_t;

static void fiber_h1_1(void* arg)
{
    h1_1_args_t* a = (h1_1_args_t*)arg;
    goc_http_server_t* srv = goc_http_server_make(goc_http_server_opts());
    int rc = (int)goc_unbox_int(
        goc_take(goc_http_server_listen(srv, "127.0.0.1", a->port))->val);
    int ok = (rc == 0);
    if (ok)
        goc_take(goc_http_server_close(srv));
    goc_put(a->done, goc_box_int(ok));
}

static void test_h1_1(void)
{
    TEST_BEGIN("H1.1  Server lifecycle: make → listen → close (no routes)");
    h1_1_args_t args = { goc_chan_make(1), next_port() };
    goc_go(fiber_h1_1, &args);
    ASSERT(goc_unbox_int(goc_take_sync(args.done)->val));
    TEST_PASS();
done:;
}

/* =========================================================================
 * H2.x — Routing
 * ====================================================================== */

static void fiber_h2_1(void* arg)
{
    h_simple_t* a = (h_simple_t*)arg;
    goc_http_server_t* srv = goc_http_server_make(goc_http_server_opts());
    goc_http_server_route(srv, "GET", "/ping", handler_ping);
    goc_take(goc_http_server_listen(srv, "127.0.0.1", a->port));

    goc_http_response_t* r =
        (goc_http_response_t*)goc_take(
            goc_http_get(local_url("/ping", a->port),
                         goc_http_request_opts()))->val;
    a->status = r ? r->status : -1;
    if (r && r->body)
        snprintf(a->body, sizeof(a->body), "%s", r->body);

    goc_take(goc_http_server_close(srv));
    goc_put(a->done, goc_box_int(1));
}

static void test_h2_1(void)
{
    TEST_BEGIN("H2.1  Routing: exact path GET /ping → 200 \"pong\"");
    h_simple_t args;
    memset(&args, 0, sizeof(args));
    args.done = goc_chan_make(1);
    args.port = next_port();
    goc_go(fiber_h2_1, &args);
    goc_take_sync(args.done);
    ASSERT(args.status == 200);
    ASSERT(strcmp(args.body, "pong") == 0);
    TEST_PASS();
done:;
}

static void handler_h2_2_catch_all(goc_http_ctx_t* ctx)
{
    goc_take(goc_http_server_respond(ctx, 200, "text/plain", "catch-all"));
}

static void fiber_h2_2(void* arg)
{
    h_status_t* a = (h_status_t*)arg;
    goc_http_server_t* srv = goc_http_server_make(goc_http_server_opts());
    goc_http_server_route(srv, "*", "/*", handler_h2_2_catch_all);
    goc_take(goc_http_server_listen(srv, "127.0.0.1", a->port));

    goc_http_response_t* r =
        (goc_http_response_t*)goc_take(
            goc_http_get(local_url("/anything/at/all", a->port),
                         goc_http_request_opts()))->val;
    a->status = r ? r->status : -1;

    goc_take(goc_http_server_close(srv));
    goc_put(a->done, goc_box_int(1));
}

static void test_h2_2(void)
{
    TEST_BEGIN("H2.2  Routing: wildcard /* catches any path → 200");
    h_status_t args = { goc_chan_make(1), next_port(), 0 };
    goc_go(fiber_h2_2, &args);
    goc_take_sync(args.done);
    ASSERT(args.status == 200);
    TEST_PASS();
done:;
}

static void fiber_h2_3(void* arg)
{
    h_status_t* a = (h_status_t*)arg;
    goc_http_server_t* srv = goc_http_server_make(goc_http_server_opts());
    goc_http_server_route(srv, "GET", "/exists", handler_ping);
    goc_take(goc_http_server_listen(srv, "127.0.0.1", a->port));

    goc_http_response_t* r =
        (goc_http_response_t*)goc_take(
            goc_http_get(local_url("/missing", a->port),
                         goc_http_request_opts()))->val;
    a->status = r ? r->status : -1;

    goc_take(goc_http_server_close(srv));
    goc_put(a->done, goc_box_int(1));
}

static void test_h2_3(void)
{
    TEST_BEGIN("H2.3  Routing: unmatched path → 404");
    h_status_t args = { goc_chan_make(1), next_port(), 0 };
    goc_go(fiber_h2_3, &args);
    goc_take_sync(args.done);
    ASSERT(args.status == 404);
    TEST_PASS();
done:;
}

/* =========================================================================
 * H3.x — Request context helpers
 * ====================================================================== */

typedef struct {
    goc_chan* done;
    int       port;
    int       has_ct;
    int       absent_ok;
    int       case_ok;
} h3_hdr_t;

static h3_hdr_t* g_h3_hdr;

static void handler_h3_headers(goc_http_ctx_t* ctx)
{
    const char* ct  = goc_http_server_header(ctx, "content-type");
    g_h3_hdr->has_ct    = (ct != NULL);
    const char* x   = goc_http_server_header(ctx, "x-no-such-header-h3");
    g_h3_hdr->absent_ok = (x == NULL);
    const char* ct2 = goc_http_server_header(ctx, "CONTENT-TYPE");
    g_h3_hdr->case_ok   = (ct2 != NULL);
    goc_take(goc_http_server_respond(ctx, 200, "text/plain", "ok"));
}

static void fiber_h3_hdrs(void* arg)
{
    h3_hdr_t* a = (h3_hdr_t*)arg;
    g_h3_hdr = a;
    goc_http_server_t* srv = goc_http_server_make(goc_http_server_opts());
    goc_http_server_route(srv, "POST", "/hdr", handler_h3_headers);
    goc_take(goc_http_server_listen(srv, "127.0.0.1", a->port));

    goc_take(goc_http_post(local_url("/hdr", a->port),
                           "application/json", "{}", goc_http_request_opts()));
    goc_take(goc_timeout(20));

    goc_take(goc_http_server_close(srv));
    goc_put(a->done, goc_box_int(1));
}

static void test_h3_1(void)
{
    TEST_BEGIN("H3.1  goc_http_server_header: present header found");
    h3_hdr_t args;
    memset(&args, 0, sizeof(args));
    args.done = goc_chan_make(1);
    args.port = next_port();
    goc_go(fiber_h3_hdrs, &args);
    goc_take_sync(args.done);
    ASSERT(args.has_ct);
    TEST_PASS();
done:;
}

static void test_h3_2(void)
{
    TEST_BEGIN("H3.2  goc_http_server_header: absent header returns NULL");
    h3_hdr_t args;
    memset(&args, 0, sizeof(args));
    args.done = goc_chan_make(1);
    args.port = next_port();
    goc_go(fiber_h3_hdrs, &args);
    goc_take_sync(args.done);
    ASSERT(args.absent_ok);
    TEST_PASS();
done:;
}

static void test_h3_3(void)
{
    TEST_BEGIN("H3.3  goc_http_server_header: case-insensitive lookup");
    h3_hdr_t args;
    memset(&args, 0, sizeof(args));
    args.done = goc_chan_make(1);
    args.port = next_port();
    goc_go(fiber_h3_hdrs, &args);
    goc_take_sync(args.done);
    ASSERT(args.case_ok);
    TEST_PASS();
done:;
}

typedef struct {
    goc_chan* done;
    int       port_body;
    int       port_empty;
    char      body_received[64];
    int       empty_ok;
} h3_body_t;

static h3_body_t* g_h3_body;

static void handler_h3_body_check(goc_http_ctx_t* ctx)
{
    const char* b = goc_http_server_body_str(ctx);
    if (b)
        snprintf(g_h3_body->body_received, sizeof(g_h3_body->body_received),
                 "%s", b);
    goc_take(goc_http_server_respond(ctx, 200, "text/plain", "ok"));
}

static void handler_h3_body_empty(goc_http_ctx_t* ctx)
{
    const char* b = goc_http_server_body_str(ctx);
    g_h3_body->empty_ok = (b != NULL && b[0] == '\0');
    goc_take(goc_http_server_respond(ctx, 200, "text/plain", "ok"));
}

static void fiber_h3_body(void* arg)
{
    h3_body_t* a = (h3_body_t*)arg;
    g_h3_body = a;

    goc_http_server_t* srv1 = goc_http_server_make(goc_http_server_opts());
    goc_http_server_route(srv1, "POST", "/body", handler_h3_body_check);
    goc_chan* ready1 = goc_http_server_listen(srv1, "127.0.0.1", a->port_body);

    goc_http_server_t* srv2 = goc_http_server_make(goc_http_server_opts());
    goc_http_server_route(srv2, "GET", "/empty", handler_h3_body_empty);
    goc_chan* ready2 = goc_http_server_listen(srv2, "127.0.0.1", a->port_empty);

    goc_take(ready1);
    goc_take(ready2);

    goc_take(goc_http_post(local_url("/body", a->port_body),
                           "text/plain", "hello-body",
                           goc_http_request_opts()));
    goc_take(goc_timeout(20));

    goc_take(goc_http_get(local_url("/empty", a->port_empty),
                          goc_http_request_opts()));
    goc_take(goc_timeout(20));

    goc_take(goc_http_server_close(srv1));
    goc_take(goc_http_server_close(srv2));
    goc_put(a->done, goc_box_int(1));
}

static void test_h3_4(void)
{
    TEST_BEGIN("H3.4  goc_http_server_body_str: POST body received correctly");
    h3_body_t args;
    memset(&args, 0, sizeof(args));
    args.done       = goc_chan_make(1);
    args.port_body  = next_port();
    args.port_empty = next_port();
    goc_go(fiber_h3_body, &args);
    goc_take_sync(args.done);
    ASSERT(strcmp(args.body_received, "hello-body") == 0);
    TEST_PASS();
done:;
}

static void test_h3_5(void)
{
    TEST_BEGIN("H3.5  goc_http_server_body_str: empty body returns \"\"");
    h3_body_t args;
    memset(&args, 0, sizeof(args));
    args.done       = goc_chan_make(1);
    args.port_body  = next_port();
    args.port_empty = next_port();
    goc_go(fiber_h3_body, &args);
    goc_take_sync(args.done);
    ASSERT(args.empty_ok);
    TEST_PASS();
done:;
}

/* =========================================================================
 * H4.x — Sending responses
 * ====================================================================== */

static void fiber_h4_1(void* arg)
{
    h_simple_t* a = (h_simple_t*)arg;
    goc_http_server_t* srv = goc_http_server_make(goc_http_server_opts());
    goc_http_server_route(srv, "GET", "/hello", handler_ping);
    goc_take(goc_http_server_listen(srv, "127.0.0.1", a->port));

    goc_http_response_t* r =
        (goc_http_response_t*)goc_take(
            goc_http_get(local_url("/hello", a->port),
                         goc_http_request_opts()))->val;
    a->status = r ? r->status : -1;
    if (r && r->body)
        snprintf(a->body, sizeof(a->body), "%s", r->body);

    goc_take(goc_http_server_close(srv));
    goc_put(a->done, goc_box_int(1));
}

static void test_h4_1(void)
{
    TEST_BEGIN("H4.1  goc_http_server_respond: status 200, body \"pong\"");
    h_simple_t args;
    memset(&args, 0, sizeof(args));
    args.done = goc_chan_make(1);
    args.port = next_port();
    goc_go(fiber_h4_1, &args);
    goc_take_sync(args.done);
    ASSERT(args.status == 200);
    ASSERT(strcmp(args.body, "pong") == 0);
    TEST_PASS();
done:;
}

static void handler_h4_2_buf(goc_http_ctx_t* ctx)
{
    static const char data[] = "Hello";
    goc_take(goc_http_server_respond_buf(ctx, 201, "application/octet-stream",
                                         data, 5));
}

static void fiber_h4_2(void* arg)
{
    h_simple_t* a = (h_simple_t*)arg;
    goc_http_server_t* srv = goc_http_server_make(goc_http_server_opts());
    goc_http_server_route(srv, "GET", "/buf", handler_h4_2_buf);
    goc_take(goc_http_server_listen(srv, "127.0.0.1", a->port));

    goc_http_response_t* r =
        (goc_http_response_t*)goc_take(
            goc_http_get(local_url("/buf", a->port),
                         goc_http_request_opts()))->val;
    a->status = r ? r->status : -1;
    if (r && r->body)
        snprintf(a->body, sizeof(a->body), "%s", r->body);

    goc_take(goc_http_server_close(srv));
    goc_put(a->done, goc_box_int(1));
}

static void test_h4_2(void)
{
    TEST_BEGIN("H4.2  goc_http_server_respond_buf: status 201, binary body");
    h_simple_t args;
    memset(&args, 0, sizeof(args));
    args.done = goc_chan_make(1);
    args.port = next_port();
    goc_go(fiber_h4_2, &args);
    goc_take_sync(args.done);
    ASSERT(args.status == 201);
    ASSERT(memcmp(args.body, "Hello", 5) == 0);
    TEST_PASS();
done:;
}

static void handler_h4_3_err(goc_http_ctx_t* ctx)
{
    goc_take(goc_http_server_respond_error(ctx, 400, "bad input"));
}

static void fiber_h4_3(void* arg)
{
    h_simple_t* a = (h_simple_t*)arg;
    goc_http_server_t* srv = goc_http_server_make(goc_http_server_opts());
    goc_http_server_route(srv, "GET", "/err", handler_h4_3_err);
    goc_take(goc_http_server_listen(srv, "127.0.0.1", a->port));

    goc_http_response_t* r =
        (goc_http_response_t*)goc_take(
            goc_http_get(local_url("/err", a->port),
                         goc_http_request_opts()))->val;
    a->status = r ? r->status : -1;
    if (r && r->body)
        snprintf(a->body, sizeof(a->body), "%s", r->body);

    goc_take(goc_http_server_close(srv));
    goc_put(a->done, goc_box_int(1));
}

static void test_h4_3(void)
{
    TEST_BEGIN("H4.3  goc_http_server_respond_error: status 400, error body");
    h_simple_t args;
    memset(&args, 0, sizeof(args));
    args.done = goc_chan_make(1);
    args.port = next_port();
    goc_go(fiber_h4_3, &args);
    goc_take_sync(args.done);
    ASSERT(args.status == 400);
    ASSERT(strstr(args.body, "bad input") != NULL);
    TEST_PASS();
done:;
}

/* =========================================================================
 * H5.x — Middleware
 * ====================================================================== */

typedef struct {
    int order[8];
    int n;
    const char* ud;
} mw_log_t;

static mw_log_t g_mw;

static goc_http_status_t mw1(goc_http_ctx_t* ctx)
{
    g_mw.order[g_mw.n++] = 1;
    ctx->user_data = (void*)"set-by-mw1";
    return GOC_HTTP_OK;
}

static goc_http_status_t mw2(goc_http_ctx_t* ctx)
{
    g_mw.order[g_mw.n++] = 2;
    (void)ctx;
    return GOC_HTTP_OK;
}

static void handler_h5_mw_ok(goc_http_ctx_t* ctx)
{
    g_mw.order[g_mw.n++] = 3;
    g_mw.ud = (const char*)ctx->user_data;
    goc_take(goc_http_server_respond(ctx, 200, "text/plain", "mw-ok"));
}

static goc_http_status_t mw_reject(goc_http_ctx_t* ctx)
{
    (void)ctx;
    g_mw.order[g_mw.n++] = 99;
    return GOC_HTTP_ERR;
}

static void handler_h5_mw_never(goc_http_ctx_t* ctx)
{
    g_mw.order[g_mw.n++] = 999;
    goc_take(goc_http_server_respond(ctx, 200, "text/plain", "unreachable"));
}

typedef struct {
    goc_chan* done;
    int       port_ok;
    int       port_rej;
    int       status_ok;
    int       status_rej;
} h5_mw_t;

static void fiber_h5_mw(void* arg)
{
    h5_mw_t* a = (h5_mw_t*)arg;
    memset(&g_mw, 0, sizeof(g_mw));

    goc_http_server_opts_t* opts_ok = goc_http_server_opts();
    opts_ok->middleware = goc_array_make(2);
    goc_array_push(opts_ok->middleware, (void*)(uintptr_t)mw1);
    goc_array_push(opts_ok->middleware, (void*)(uintptr_t)mw2);
    goc_http_server_t* srv_ok = goc_http_server_make(opts_ok);
    goc_http_server_route(srv_ok, "GET", "/mw", handler_h5_mw_ok);
    goc_chan* ready_ok = goc_http_server_listen(srv_ok, "127.0.0.1", a->port_ok);

    goc_http_server_opts_t* opts_rej = goc_http_server_opts();
    opts_rej->middleware = goc_array_make(1);
    goc_array_push(opts_rej->middleware, (void*)(uintptr_t)mw_reject);
    goc_http_server_t* srv_rej = goc_http_server_make(opts_rej);
    goc_http_server_route(srv_rej, "GET", "/mw", handler_h5_mw_never);
    goc_chan* ready_rej = goc_http_server_listen(srv_rej, "127.0.0.1", a->port_rej);

    goc_take(ready_ok);
    goc_take(ready_rej);

    goc_http_response_t* r_ok =
        (goc_http_response_t*)goc_take(
            goc_http_get(local_url("/mw", a->port_ok),
                         goc_http_request_opts()))->val;
    a->status_ok = r_ok ? r_ok->status : -1;
    goc_take(goc_timeout(20));

    goc_http_response_t* r_rej =
        (goc_http_response_t*)goc_take(
            goc_http_get(local_url("/mw", a->port_rej),
                         goc_http_request_opts()))->val;
    a->status_rej = r_rej ? r_rej->status : -1;
    goc_take(goc_timeout(20));

    goc_take(goc_http_server_close(srv_ok));
    goc_take(goc_http_server_close(srv_rej));
    goc_put(a->done, goc_box_int(1));
}

static void test_h5_1(void)
{
    TEST_BEGIN("H5.1  Middleware: chain runs in order; user_data propagates");
    h5_mw_t args;
    memset(&args, 0, sizeof(args));
    args.done     = goc_chan_make(1);
    args.port_ok  = next_port();
    args.port_rej = next_port();
    goc_go(fiber_h5_mw, &args);
    goc_take_sync(args.done);
    ASSERT(args.status_ok == 200);
    ASSERT(g_mw.n >= 3);
    ASSERT(g_mw.order[0] == 1 && g_mw.order[1] == 2 && g_mw.order[2] == 3);
    ASSERT(g_mw.ud && strcmp(g_mw.ud, "set-by-mw1") == 0);
    TEST_PASS();
done:;
}

static void test_h5_2(void)
{
    TEST_BEGIN("H5.2  Middleware: GOC_HTTP_ERR short-circuits with 500");
    h5_mw_t args;
    memset(&args, 0, sizeof(args));
    args.done     = goc_chan_make(1);
    args.port_ok  = next_port();
    args.port_rej = next_port();
    goc_go(fiber_h5_mw, &args);
    goc_take_sync(args.done);
    ASSERT(args.status_rej == 500);
    for (int i = 0; i < g_mw.n; i++)
        ASSERT(g_mw.order[i] != 999);
    TEST_PASS();
done:;
}

/* =========================================================================
 * H6.x — HTTP client
 * ====================================================================== */

static void fiber_h6_1(void* arg)
{
    h_simple_t* a = (h_simple_t*)arg;
    goc_http_server_t* srv = goc_http_server_make(goc_http_server_opts());
    goc_http_server_route(srv, "GET", "/hi", handler_ping);
    goc_take(goc_http_server_listen(srv, "127.0.0.1", a->port));

    goc_http_response_t* r =
        (goc_http_response_t*)goc_take(
            goc_http_get(local_url("/hi", a->port),
                         goc_http_request_opts()))->val;
    a->status = r ? r->status : -1;
    if (r && r->body)
        snprintf(a->body, sizeof(a->body), "%s", r->body);

    goc_take(goc_http_server_close(srv));
    goc_put(a->done, goc_box_int(1));
}

static void test_h6_1(void)
{
    TEST_BEGIN("H6.1  HTTP client: goc_http_get → 200 with body");
    h_simple_t args;
    memset(&args, 0, sizeof(args));
    args.done = goc_chan_make(1);
    args.port = next_port();
    goc_go(fiber_h6_1, &args);
    goc_take_sync(args.done);
    ASSERT(args.status == 200);
    ASSERT(strcmp(args.body, "pong") == 0);
    TEST_PASS();
done:;
}

static void handler_h6_2_echo(goc_http_ctx_t* ctx)
{
    const char* b = goc_http_server_body_str(ctx);
    goc_take(goc_http_server_respond(ctx, 200, "text/plain", b));
}

static void fiber_h6_2(void* arg)
{
    h_simple_t* a = (h_simple_t*)arg;
    goc_http_server_t* srv = goc_http_server_make(goc_http_server_opts());
    goc_http_server_route(srv, "POST", "/echo", handler_h6_2_echo);
    goc_take(goc_http_server_listen(srv, "127.0.0.1", a->port));

    goc_http_response_t* r =
        (goc_http_response_t*)goc_take(
            goc_http_post(local_url("/echo", a->port),
                          "text/plain", "echo-this",
                          goc_http_request_opts()))->val;
    a->status = r ? r->status : -1;
    if (r && r->body)
        snprintf(a->body, sizeof(a->body), "%s", r->body);

    goc_take(goc_http_server_close(srv));
    goc_put(a->done, goc_box_int(1));
}

static void test_h6_2(void)
{
    TEST_BEGIN("H6.2  HTTP client: goc_http_post → echoed body");
    h_simple_t args;
    memset(&args, 0, sizeof(args));
    args.done = goc_chan_make(1);
    args.port = next_port();
    goc_go(fiber_h6_2, &args);
    goc_take_sync(args.done);
    ASSERT(args.status == 200);
    ASSERT(strstr(args.body, "echo-this") != NULL);
    TEST_PASS();
done:;
}

static void handler_h6_3_a(goc_http_ctx_t* ctx)
{
    goc_take(goc_http_server_respond(ctx, 200, "text/plain", "a"));
}

static void handler_h6_3_b(goc_http_ctx_t* ctx)
{
    goc_take(goc_http_server_respond(ctx, 201, "text/plain", "b"));
}

typedef struct {
    goc_chan* done;
    int       port;
    int       status_a;
    int       status_b;
} h6_par_t;

static void fiber_h6_3(void* arg)
{
    h6_par_t* a = (h6_par_t*)arg;
    goc_http_server_t* srv = goc_http_server_make(goc_http_server_opts());
    goc_http_server_route(srv, "GET", "/a", handler_h6_3_a);
    goc_http_server_route(srv, "GET", "/b", handler_h6_3_b);
    goc_take(goc_http_server_listen(srv, "127.0.0.1", a->port));

    goc_chan* ca = goc_http_get(local_url("/a", a->port),
                                 goc_http_request_opts());
    goc_chan* cb = goc_http_get(local_url("/b", a->port),
                                 goc_http_request_opts());

    goc_chan* chs[2] = { ca, cb };
    goc_val_t** vals = goc_take_all(chs, 2);

    goc_http_response_t* ra = (goc_http_response_t*)vals[0]->val;
    goc_http_response_t* rb = (goc_http_response_t*)vals[1]->val;
    a->status_a = ra ? ra->status : -1;
    a->status_b = rb ? rb->status : -1;

    goc_take(goc_http_server_close(srv));
    goc_put(a->done, goc_box_int(1));
}

static void test_h6_3(void)
{
    TEST_BEGIN("H6.3  HTTP client: parallel requests with goc_take_all");
    h6_par_t args;
    memset(&args, 0, sizeof(args));
    args.done = goc_chan_make(1);
    args.port = next_port();
    goc_go(fiber_h6_3, &args);
    goc_take_sync(args.done);
    ASSERT(args.status_a == 200);
    ASSERT(args.status_b == 201);
    TEST_PASS();
done:;
}

static void handler_h6_4_slow(goc_http_ctx_t* ctx)
{
    /* Sleep 500 ms — longer than the 150 ms client timeout, but short
     * enough that the test can wait for it to finish before closing. */
    goc_take(goc_timeout(500));
    goc_take(goc_http_server_respond(ctx, 200, "text/plain", "too late"));
}

typedef struct {
    goc_chan* done;
    int       port;
    int       timed_out;
} h6_to_t;

static void fiber_h6_4(void* arg)
{
    h6_to_t* a = (h6_to_t*)arg;
    goc_http_server_t* srv = goc_http_server_make(goc_http_server_opts());
    goc_http_server_route(srv, "GET", "/slow", handler_h6_4_slow);
    goc_take(goc_http_server_listen(srv, "127.0.0.1", a->port));

    goc_http_request_opts_t* opts = goc_http_request_opts();
    opts->timeout_ms = 150;

    goc_http_response_t* r =
        (goc_http_response_t*)goc_take(
            goc_http_get(local_url("/slow", a->port), opts))->val;
    a->timed_out = (!r || r->status == 0);

    /* Wait for handler_h6_4_slow (500 ms) to finish before closing. */
    goc_take(goc_timeout(600));
    goc_take(goc_http_server_close(srv));
    goc_put(a->done, goc_box_int(1));
}

static void test_h6_4(void)
{
    TEST_BEGIN("H6.4  HTTP client: timeout fires → status 0 (timeout)");
    h6_to_t args = { goc_chan_make(1), next_port(), 0 };
    goc_go(fiber_h6_4, &args);
    goc_take_sync(args.done);
    ASSERT(args.timed_out);
    TEST_PASS();
done:;
}

/* =========================================================================
 * H7.x — Security
 * ====================================================================== */

/*
 * H7.1 — Oversized request body is cleanly rejected.
 *
 * GOC_SERVER_MAX_REQ_SIZE (8 MiB) is the internal cap.  A body exceeding
 * it must cause the server to close the connection without sending a
 * response; the client receives status == 0.  The server must remain
 * usable for subsequent requests.
 */
#define H7_1_BODY_BYTES  (8u * 1024u * 1024u + 1024u)  /* ~8 MiB + 1 KiB */

typedef struct {
    goc_chan* done;
    int       port;
    int       oversized_status;
    int       followup_status;
} h7_oversize_t;

static void fiber_h7_1(void* arg)
{
    h7_oversize_t* a = (h7_oversize_t*)arg;
    goc_http_server_t* srv = goc_http_server_make(goc_http_server_opts());
    goc_http_server_route(srv, "POST", "/ok", handler_ping);
    goc_take(goc_http_server_listen(srv, "127.0.0.1", a->port));

    char* big = (char*)calloc(H7_1_BODY_BYTES, 1);
    if (big) {
        goc_http_response_t* r1 = (goc_http_response_t*)goc_take(
            goc_http_post_buf(local_url("/ok", a->port),
                              "application/octet-stream",
                              big, H7_1_BODY_BYTES,
                              goc_http_request_opts()))->val;
        free(big);
        a->oversized_status = r1 ? r1->status : -1;
    }

    /* Follow-up: server must still accept normal requests after rejection. */
    goc_take(goc_timeout(20));
    goc_http_response_t* r2 = (goc_http_response_t*)goc_take(
        goc_http_post(local_url("/ok", a->port),
                      "text/plain", "hi",
                      goc_http_request_opts()))->val;
    a->followup_status = r2 ? r2->status : -1;

    goc_take(goc_http_server_close(srv));
    goc_put(a->done, goc_box_int(1));
}

static void test_h7_1(void)
{
    TEST_BEGIN("H7.1  Security: oversized request rejected; server stays alive");
    h7_oversize_t args;
    memset(&args, 0, sizeof(args));
    args.done = goc_chan_make(1);
    args.port = next_port();
    goc_go(fiber_h7_1, &args);
    goc_take_sync(args.done);
    ASSERT(args.oversized_status == 0);
    ASSERT(args.followup_status == 200);
    TEST_PASS();
done:;
}

typedef struct { goc_chan* done; int port; int status; } h7_mismatch_t;

static void fiber_h7_2(void* arg)
{
    h7_mismatch_t* a = (h7_mismatch_t*)arg;
    goc_http_server_t* srv = goc_http_server_make(goc_http_server_opts());
    goc_http_server_route(srv, "GET", "/secret", handler_ping);
    goc_take(goc_http_server_listen(srv, "127.0.0.1", a->port));

    goc_http_response_t* r = (goc_http_response_t*)goc_take(
        goc_http_post(local_url("/secret", a->port),
                      "text/plain", "",
                      goc_http_request_opts()))->val;
    a->status = r ? r->status : -1;

    goc_take(goc_http_server_close(srv));
    goc_put(a->done, goc_box_int(1));
}

static void test_h7_2(void)
{
    TEST_BEGIN("H7.2  Security: method mismatch (GET route hit with POST) → 404");
    h7_mismatch_t args = { goc_chan_make(1), next_port(), 0 };
    goc_go(fiber_h7_2, &args);
    goc_take_sync(args.done);
    ASSERT(args.status == 404);
    TEST_PASS();
done:;
}

/*
 * H7.3 — CRLF in a client request header value must not inject a second
 * header.  build_request() silently drops any header whose name or value
 * contains \r or \n.
 */
typedef struct { goc_chan* done; int port; int injected; } h7_inject_t;

static h7_inject_t* g_h7_inject;

static void handler_h7_3_injection(goc_http_ctx_t* ctx)
{
    /* If injection succeeded, the server sees X-Injected as a header. */
    const char* v = goc_http_server_header(ctx, "x-injected");
    g_h7_inject->injected = (v != NULL);
    goc_take(goc_http_server_respond(ctx, 200, "text/plain", "ok"));
}

static void fiber_h7_3(void* arg)
{
    h7_inject_t* a = (h7_inject_t*)arg;
    g_h7_inject = a;
    goc_http_server_t* srv = goc_http_server_make(goc_http_server_opts());
    goc_http_server_route(srv, "GET", "/check-inject", handler_h7_3_injection);
    goc_take(goc_http_server_listen(srv, "127.0.0.1", a->port));

    goc_http_header_t* hdr =
        (goc_http_header_t*)goc_malloc(sizeof(goc_http_header_t));
    hdr->name  = "x-legitimate";
    hdr->value = "foo\r\nX-Injected: evil";

    goc_http_request_opts_t* opts = goc_http_request_opts();
    opts->headers = goc_array_make(1);
    goc_array_push(opts->headers, hdr);

    goc_take(goc_http_get(local_url("/check-inject", a->port), opts));
    goc_take(goc_timeout(20));

    goc_take(goc_http_server_close(srv));
    goc_put(a->done, goc_box_int(1));
}

static void test_h7_3(void)
{
    TEST_BEGIN("H7.3  Security: CRLF in header value blocked (injection prevention)");
    h7_inject_t args = { goc_chan_make(1), next_port(), 0 };
    goc_go(fiber_h7_3, &args);
    goc_take_sync(args.done);
    ASSERT(!args.injected);
    TEST_PASS();
done:;
}

/* =========================================================================
 * H8.x — Correctness
 * ====================================================================== */

typedef struct {
    goc_chan* done;
    int       port;
    char      got_path[64];
    char      got_method[16];
} h8_ctx_t;

static h8_ctx_t* g_h8_ctx;

static void handler_h8_1_ctx(goc_http_ctx_t* ctx)
{
    snprintf(g_h8_ctx->got_path,   sizeof(g_h8_ctx->got_path),
             "%s", ctx->path   ? ctx->path   : "");
    snprintf(g_h8_ctx->got_method, sizeof(g_h8_ctx->got_method),
             "%s", ctx->method ? ctx->method : "");
    goc_take(goc_http_server_respond(ctx, 200, "text/plain", "ok"));
}

static void fiber_h8_1(void* arg)
{
    h8_ctx_t* a = (h8_ctx_t*)arg;
    g_h8_ctx = a;
    goc_http_server_t* srv = goc_http_server_make(goc_http_server_opts());
    goc_http_server_route(srv, "GET", "/check-ctx", handler_h8_1_ctx);
    goc_take(goc_http_server_listen(srv, "127.0.0.1", a->port));

    goc_take(goc_http_get(local_url("/check-ctx", a->port),
                          goc_http_request_opts()));
    goc_take(goc_timeout(20));

    goc_take(goc_http_server_close(srv));
    goc_put(a->done, goc_box_int(1));
}

static void test_h8_1(void)
{
    TEST_BEGIN("H8.1  Correctness: ctx->path and ctx->method populated correctly");
    h8_ctx_t args;
    memset(&args, 0, sizeof(args));
    args.done = goc_chan_make(1);
    args.port = next_port();
    goc_go(fiber_h8_1, &args);
    goc_take_sync(args.done);
    ASSERT(strcmp(args.got_path,   "/check-ctx") == 0);
    ASSERT(strcmp(args.got_method, "GET")        == 0);
    TEST_PASS();
done:;
}

typedef struct {
    goc_chan* done;
    int       port;
    char      got_path[64];
    char      got_query[64];
} h8_query_t;

static h8_query_t* g_h8_query;

static void handler_h8_2_query(goc_http_ctx_t* ctx)
{
    snprintf(g_h8_query->got_path,  sizeof(g_h8_query->got_path),
             "%s", ctx->path  ? ctx->path  : "");
    snprintf(g_h8_query->got_query, sizeof(g_h8_query->got_query),
             "%s", ctx->query ? ctx->query : "");
    goc_take(goc_http_server_respond(ctx, 200, "text/plain", "ok"));
}

static void fiber_h8_2(void* arg)
{
    h8_query_t* a = (h8_query_t*)arg;
    g_h8_query = a;
    goc_http_server_t* srv = goc_http_server_make(goc_http_server_opts());
    goc_http_server_route(srv, "GET", "/search", handler_h8_2_query);
    goc_take(goc_http_server_listen(srv, "127.0.0.1", a->port));

    goc_take(goc_http_get(local_url("/search?q=hello&limit=10", a->port),
                          goc_http_request_opts()));
    goc_take(goc_timeout(20));

    goc_take(goc_http_server_close(srv));
    goc_put(a->done, goc_box_int(1));
}

static void test_h8_2(void)
{
    TEST_BEGIN("H8.2  Correctness: ctx->query parsed from URL query string");
    h8_query_t args;
    memset(&args, 0, sizeof(args));
    args.done = goc_chan_make(1);
    args.port = next_port();
    goc_go(fiber_h8_2, &args);
    goc_take_sync(args.done);
    ASSERT(strcmp(args.got_path,  "/search")          == 0);
    ASSERT(strcmp(args.got_query, "q=hello&limit=10") == 0);
    TEST_PASS();
done:;
}

#define H8_3_DATA  "ABCDE"
#define H8_3_LEN   5

static void handler_h8_3_binbuf(goc_http_ctx_t* ctx)
{
    goc_take(goc_http_server_respond_buf(ctx, 200, "application/octet-stream",
                                         H8_3_DATA, H8_3_LEN));
}

typedef struct { goc_chan* done; int port; size_t body_len; } h8_bodylen_t;

static void fiber_h8_3(void* arg)
{
    h8_bodylen_t* a = (h8_bodylen_t*)arg;
    goc_http_server_t* srv = goc_http_server_make(goc_http_server_opts());
    goc_http_server_route(srv, "GET", "/bin", handler_h8_3_binbuf);
    goc_take(goc_http_server_listen(srv, "127.0.0.1", a->port));

    goc_http_response_t* r = (goc_http_response_t*)goc_take(
        goc_http_get(local_url("/bin", a->port),
                     goc_http_request_opts()))->val;
    a->body_len = r ? r->body_len : (size_t)-1;

    goc_take(goc_http_server_close(srv));
    goc_put(a->done, goc_box_int(1));
}

static void test_h8_3(void)
{
    TEST_BEGIN("H8.3  Correctness: goc_http_response_t->body_len matches respond_buf length");
    h8_bodylen_t args;
    memset(&args, 0, sizeof(args));
    args.done = goc_chan_make(1);
    args.port = next_port();
    goc_go(fiber_h8_3, &args);
    goc_take_sync(args.done);
    ASSERT(args.body_len == (size_t)H8_3_LEN);
    TEST_PASS();
done:;
}

typedef struct {
    goc_chan* done;
    int       port;
    char      custom_value[64];
} h8_custom_hdr_t;

static h8_custom_hdr_t* g_h8_custom_hdr;

static void handler_h8_4_custom_hdr(goc_http_ctx_t* ctx)
{
    const char* v = goc_http_server_header(ctx, "x-custom-header");
    snprintf(g_h8_custom_hdr->custom_value, sizeof(g_h8_custom_hdr->custom_value),
             "%s", v ? v : "");
    goc_take(goc_http_server_respond(ctx, 200, "text/plain", "ok"));
}

static void fiber_h8_4(void* arg)
{
    h8_custom_hdr_t* a = (h8_custom_hdr_t*)arg;
    g_h8_custom_hdr = a;
    goc_http_server_t* srv = goc_http_server_make(goc_http_server_opts());
    goc_http_server_route(srv, "GET", "/echo-hdr", handler_h8_4_custom_hdr);
    goc_take(goc_http_server_listen(srv, "127.0.0.1", a->port));

    goc_http_header_t* hdr =
        (goc_http_header_t*)goc_malloc(sizeof(goc_http_header_t));
    hdr->name  = "x-custom-header";
    hdr->value = "test-value-h8-4";

    goc_http_request_opts_t* opts = goc_http_request_opts();
    opts->headers = goc_array_make(1);
    goc_array_push(opts->headers, hdr);

    goc_take(goc_http_get(local_url("/echo-hdr", a->port), opts));
    goc_take(goc_timeout(20));

    goc_take(goc_http_server_close(srv));
    goc_put(a->done, goc_box_int(1));
}

static void test_h8_4(void)
{
    TEST_BEGIN("H8.4  Correctness: custom request headers received by server");
    h8_custom_hdr_t args;
    memset(&args, 0, sizeof(args));
    args.done = goc_chan_make(1);
    args.port = next_port();
    goc_go(fiber_h8_4, &args);
    goc_take_sync(args.done);
    ASSERT(strcmp(args.custom_value, "test-value-h8-4") == 0);
    TEST_PASS();
done:;
}

/* =========================================================================
 * H9.x — Regression
 * ====================================================================== */

/*
 * H9.1 — Ping-pong: two HTTP servers bounce a counter back and forth.
 *
 * Mirrors the Ping-Pong Example from HTTP.md exactly.
 * Server A (port_a): POST /ping → read counter, respond, forward counter+1
 *   to server B.  When counter >= ROUNDS, signal the done channel.
 * Server B (port_b): POST /ping → read counter, respond, forward counter+1
 *   back to server A.
 * The test fires the first request and waits on done.
 */
#define H9_1_ROUNDS 1000
/* 1 s/round + 30 s keeps the regression deterministic across
 * slower CI runners without turning real deadlocks into indefinite waits. */
#define H9_1_TIMEOUT_MS \
    (((H9_1_ROUNDS) * 1000) + 30000)

typedef struct {
    goc_chan* done;      /* closed by handler_h9_1_a when rounds complete */
    goc_chan* result_ch; /* fiber signals 1=ok / 0=timed-out back to test */
    int       port_a;
    int       port_b;
    int       completed;  /* set to 1 if all rounds finish before timeout */
} h9_1_args_t;

static h9_1_args_t* g_h9_1;

static char h9_1_url_a[64];
static char h9_1_url_b[64];

static void handler_h9_1_a(goc_http_ctx_t* ctx)
{
    int n = atoi(goc_http_server_body_str(ctx));
    goc_take(goc_http_server_respond(ctx, 200, "text/plain", "ok"));

    if (n >= H9_1_ROUNDS) {
        g_h9_1->completed = 1;
        goc_close(g_h9_1->done);
        return;
    }

    char* msg = goc_sprintf("%d", n + 1);
    goc_http_post(h9_1_url_b, "text/plain", msg, goc_http_request_opts());
}

static void handler_h9_1_b(goc_http_ctx_t* ctx)
{
    int n = atoi(goc_http_server_body_str(ctx));
    goc_take(goc_http_server_respond(ctx, 200, "text/plain", "ok"));

    char* msg = goc_sprintf("%d", n + 1);
    goc_http_post(h9_1_url_a, "text/plain", msg, goc_http_request_opts());
}

static void fiber_h9_1(void* arg)
{
    h9_1_args_t* a = (h9_1_args_t*)arg;
    g_h9_1 = a;

    snprintf(h9_1_url_a, sizeof(h9_1_url_a),
             "http://127.0.0.1:%d/ping", a->port_a);
    snprintf(h9_1_url_b, sizeof(h9_1_url_b),
             "http://127.0.0.1:%d/ping", a->port_b);

    goc_http_server_t* srv_a = goc_http_server_make(goc_http_server_opts());
    goc_http_server_t* srv_b = goc_http_server_make(goc_http_server_opts());

    goc_http_server_route(srv_a, "POST", "/ping", handler_h9_1_a);
    goc_http_server_route(srv_b, "POST", "/ping", handler_h9_1_b);

    goc_chan* ready_a = goc_http_server_listen(srv_a, "127.0.0.1", a->port_a);
    goc_chan* ready_b = goc_http_server_listen(srv_b, "127.0.0.1", a->port_b);

    goc_take(ready_a);
    goc_take(ready_b);

    goc_http_post(h9_1_url_a, "text/plain", "0", goc_http_request_opts());

    /* Wait for all rounds to complete with a rounds-scaled timeout so this
     * remains a correctness regression test rather than a machine-speed test. */
    goc_chan* timeout_ch = goc_timeout(H9_1_TIMEOUT_MS);
    goc_alt_op_t ops[2] = {
        { a->done,   GOC_ALT_TAKE, NULL },
        { timeout_ch, GOC_ALT_TAKE, NULL },
    };
    goc_alts_result_t* res = goc_alts(ops, 2);
    int timed_out = (res->ch == timeout_ch);

    goc_take(goc_http_server_close(srv_a));
    goc_take(goc_http_server_close(srv_b));
    /* Signal the result: 1 = all rounds completed, 0 = timed out. */
    goc_put(a->result_ch, goc_box_int(timed_out ? 0 : 1));
}

static void test_h9_1(void)
{
    TEST_BEGIN("H9.1  Regression: ping-pong " STRINGIFY(H9_1_ROUNDS) " round trips complete");
    h9_1_args_t args;
    memset(&args, 0, sizeof(args));
    args.done      = goc_chan_make(0);
    args.result_ch = goc_chan_make(1);
    args.port_a    = next_port();
    args.port_b    = next_port();
    goc_go(fiber_h9_1, &args);
    int ok = (int)goc_unbox_int(goc_take_sync(args.result_ch)->val);
    ASSERT(ok == 1);
    TEST_PASS();
done:;
}

/* =========================================================================
 * main
 * ====================================================================== */

int main(void)
{
    install_crash_handler();
    goc_test_arm_watchdog(480);
    goc_init();

    printf("goc_http — HTTP server and client\n");

    test_h1_1();

    test_h2_1();
    test_h2_2();
    test_h2_3();

    test_h3_1();
    test_h3_2();
    test_h3_3();
    test_h3_4();
    test_h3_5();

    test_h4_1();
    test_h4_2();
    test_h4_3();

    test_h5_1();
    test_h5_2();

    test_h6_1();
    test_h6_2();
    test_h6_3();
    test_h6_4();

    test_h7_1();
    test_h7_2();
    test_h7_3();

    test_h8_1();
    test_h8_2();
    test_h8_3();
    test_h8_4();

    test_h9_1();

    printf("\n%d/%d tests passed", g_tests_passed, g_tests_run);
    if (g_tests_failed)
        printf(", %d FAILED", g_tests_failed);
    printf("\n");

    goc_shutdown();
    return g_tests_failed ? 1 : 0;
}
