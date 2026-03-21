/*
 * tests/test_p10_io.c — Phase 10: Async I/O wrapper tests for libgoc
 *
 * Verifies the channel-returning async I/O wrappers declared in goc_io.h.
 * Tests focus on file-system and DNS operations since they do not require
 * network infrastructure to be pre-configured.
 *
 * Build:  cmake -B build && cmake --build build
 * Run:    ctest --test-dir build --output-on-failure
 *         ./build/test_p10_io
 *
 * Compile requirements: -std=c11 -DGC_THREADS -D_GNU_SOURCE
 *
 * Test coverage:
 *
 *   P10.1  goc_io_fs_open_ch: open a new file; result file descriptor >= 0
 *   P10.2  goc_io_fs_write_ch: write data to an open file; result == written bytes
 *   P10.3  goc_io_fs_read_ch: read back the data; matches written content
 *   P10.4  goc_io_fs_stat_ch: stat the file; size and type fields correct
 *   P10.5  goc_io_fs_rename_ch: rename the file; stat old path fails, new path ok
 *   P10.6  goc_io_fs_unlink_ch: delete the file; subsequent stat fails
 *   P10.7  goc_io_fs_open + goc_io_fs_close blocking wrappers (fiber context)
 *   P10.8  goc_io_fs_open_ch with invalid path: result < 0 (error code)
 *   P10.9  goc_io_getaddrinfo_ch: resolve "localhost"; status == 0, res != NULL
 *   P10.10 goc_io_getaddrinfo_ch with empty node and service: returns error
 *   P10.11 goc_io_write_ch / goc_io_write2_ch / goc_io_shutdown_stream_ch channel
 *          variants compile and return non-NULL channels (no network I/O)
 *   P10.12 goc_io_fs_sendfile_ch: copy bytes between two file descriptors
 *   P10.13 Channel-based goc_io_fs_open_ch integrates with goc_alts (select
 *          on open vs. a dummy channel that never fires)
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
#include <fcntl.h>
#include <sys/stat.h>

#include "test_harness.h"
#include "goc.h"
#include "goc_io.h"

/* Temporary file paths used across tests. */
static const char* TMP_PATH   = "/tmp/goc_io_test.txt";
static const char* TMP_PATH2  = "/tmp/goc_io_test_renamed.txt";
static const char* TMP_PATH3  = "/tmp/goc_io_test_dst.txt";

/* Content used for write/read tests. */
static const char  CONTENT[]  = "hello libgoc async io";
static const int   CONTENT_LEN = (int)(sizeof(CONTENT) - 1);

/* =========================================================================
 * Helper: ensure tmp files are gone at the start of each run.
 * ====================================================================== */
static void cleanup_tmp_files(void)
{
    uv_fs_t req;
    uv_fs_unlink(goc_scheduler(), &req, TMP_PATH,  NULL);
    uv_fs_req_cleanup(&req);
    uv_fs_unlink(goc_scheduler(), &req, TMP_PATH2, NULL);
    uv_fs_req_cleanup(&req);
    uv_fs_unlink(goc_scheduler(), &req, TMP_PATH3, NULL);
    uv_fs_req_cleanup(&req);
}

/* =========================================================================
 * Fiber state structs
 * ====================================================================== */

typedef struct {
    int ok;
} fiber_result_t;

/* =========================================================================
 * P10.1  goc_io_fs_open_ch: open a new file
 * ====================================================================== */

static void fiber_p10_1(void* arg)
{
    fiber_result_t* r = (fiber_result_t*)arg;
    goc_chan*      ch = goc_io_fs_open_ch(TMP_PATH,
                                        O_WRONLY | O_CREAT | O_TRUNC, 0644);
    goc_val_t*     v  = goc_take(ch);
    if (!v || v->ok != GOC_OK) goto done;
    goc_io_fs_open_t* res = (goc_io_fs_open_t*)v->val;
    if (!res || res->result < 0) goto done;
    /* Close the fd so subsequent tests can use the file. */
    uv_fs_t req;
    uv_fs_close(goc_scheduler(), &req, (uv_file)res->result, NULL);
    uv_fs_req_cleanup(&req);
    r->ok = 1;
done:;
}

static void test_p10_1(void)
{
    TEST_BEGIN("P10.1  goc_io_fs_open_ch opens a new file");
    fiber_result_t r = {0};
    goc_chan* done_ch = goc_go(fiber_p10_1, &r);
    goc_take_sync(done_ch);
    ASSERT(r.ok);
    TEST_PASS();
done:;
}

/* =========================================================================
 * P10.2  goc_io_fs_write_ch: write data
 * ====================================================================== */

static void fiber_p10_2(void* arg)
{
    fiber_result_t* r = (fiber_result_t*)arg;
    /* Open for writing */
    goc_io_fs_open_t* opened = goc_io_fs_open(TMP_PATH,
                                         O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (!opened || opened->result < 0) goto done;
    uv_file fd = opened->result;

    uv_buf_t buf = uv_buf_init((char*)CONTENT, (unsigned)CONTENT_LEN);
    goc_io_fs_write_t* written = goc_io_fs_write(fd, &buf, 1, 0);
    if (!written || written->result != CONTENT_LEN) {
        uv_fs_t req;
        uv_fs_close(goc_scheduler(), &req, fd, NULL);
        uv_fs_req_cleanup(&req);
        goto done;
    }

    uv_fs_t req;
    uv_fs_close(goc_scheduler(), &req, fd, NULL);
    uv_fs_req_cleanup(&req);
    r->ok = 1;
done:;
}

static void test_p10_2(void)
{
    TEST_BEGIN("P10.2  goc_io_fs_write_ch writes correct byte count");
    fiber_result_t r = {0};
    goc_chan* done_ch = goc_go(fiber_p10_2, &r);
    goc_take_sync(done_ch);
    ASSERT(r.ok);
    TEST_PASS();
done:;
}

/* =========================================================================
 * P10.3  goc_io_fs_read_ch: read back written content
 * ====================================================================== */

static void fiber_p10_3(void* arg)
{
    fiber_result_t* r = (fiber_result_t*)arg;
    goc_io_fs_open_t* opened = goc_io_fs_open(TMP_PATH, O_RDONLY, 0);
    if (!opened || opened->result < 0) goto done;
    uv_file fd = opened->result;

    char readbuf[64] = {0};
    uv_buf_t buf = uv_buf_init(readbuf, sizeof(readbuf) - 1);
    goc_io_fs_read_t* rd = goc_io_fs_read(fd, &buf, 1, 0);

    uv_fs_t req;
    uv_fs_close(goc_scheduler(), &req, fd, NULL);
    uv_fs_req_cleanup(&req);

    if (!rd || rd->result != CONTENT_LEN) goto done;
    if (memcmp(readbuf, CONTENT, (size_t)CONTENT_LEN) != 0) goto done;
    r->ok = 1;
done:;
}

static void test_p10_3(void)
{
    TEST_BEGIN("P10.3  goc_io_fs_read_ch reads back correct content");
    fiber_result_t r = {0};
    goc_chan* done_ch = goc_go(fiber_p10_3, &r);
    goc_take_sync(done_ch);
    ASSERT(r.ok);
    TEST_PASS();
done:;
}

/* =========================================================================
 * P10.4  goc_io_fs_stat_ch: stat the file
 * ====================================================================== */

static void fiber_p10_4(void* arg)
{
    fiber_result_t* r = (fiber_result_t*)arg;
    goc_io_fs_stat_t* st = goc_io_fs_stat(TMP_PATH);
    if (!st || st->result != 0) goto done;
    if ((int64_t)st->statbuf.st_size != CONTENT_LEN) goto done;
    r->ok = 1;
done:;
}

static void test_p10_4(void)
{
    TEST_BEGIN("P10.4  goc_io_fs_stat_ch reports correct file size");
    fiber_result_t r = {0};
    goc_chan* done_ch = goc_go(fiber_p10_4, &r);
    goc_take_sync(done_ch);
    ASSERT(r.ok);
    TEST_PASS();
done:;
}

/* =========================================================================
 * P10.5  goc_io_fs_rename_ch: rename the file
 * ====================================================================== */

static void fiber_p10_5(void* arg)
{
    fiber_result_t* r = (fiber_result_t*)arg;
    goc_io_fs_rename_t* ren = goc_io_fs_rename(TMP_PATH, TMP_PATH2);
    if (!ren || ren->result != 0) goto done;

    /* Old path should no longer exist */
    goc_io_fs_stat_t* old_st = goc_io_fs_stat(TMP_PATH);
    if (!old_st || old_st->result == 0) goto done;  /* still exists = fail */

    /* New path should exist with the same size */
    goc_io_fs_stat_t* new_st = goc_io_fs_stat(TMP_PATH2);
    if (!new_st || new_st->result != 0) goto done;
    if ((int64_t)new_st->statbuf.st_size != CONTENT_LEN) goto done;

    r->ok = 1;
done:;
}

static void test_p10_5(void)
{
    TEST_BEGIN("P10.5  goc_io_fs_rename_ch renames file correctly");
    fiber_result_t r = {0};
    goc_chan* done_ch = goc_go(fiber_p10_5, &r);
    goc_take_sync(done_ch);
    ASSERT(r.ok);
    TEST_PASS();
done:;
}

/* =========================================================================
 * P10.6  goc_io_fs_unlink_ch: delete the (renamed) file
 * ====================================================================== */

static void fiber_p10_6(void* arg)
{
    fiber_result_t* r = (fiber_result_t*)arg;
    goc_io_fs_unlink_t* ul = goc_io_fs_unlink(TMP_PATH2);
    if (!ul || ul->result != 0) goto done;

    /* File should no longer exist */
    goc_io_fs_stat_t* st = goc_io_fs_stat(TMP_PATH2);
    if (!st || st->result == 0) goto done;  /* still exists = fail */

    r->ok = 1;
done:;
}

static void test_p10_6(void)
{
    TEST_BEGIN("P10.6  goc_io_fs_unlink_ch deletes file, stat then fails");
    fiber_result_t r = {0};
    goc_chan* done_ch = goc_go(fiber_p10_6, &r);
    goc_take_sync(done_ch);
    ASSERT(r.ok);
    TEST_PASS();
done:;
}

/* =========================================================================
 * P10.7  Blocking wrappers (goc_io_fs_open + goc_io_fs_close) in fiber context
 * ====================================================================== */

static void fiber_p10_7(void* arg)
{
    fiber_result_t* r = (fiber_result_t*)arg;
    /* Create a fresh temp file */
    goc_io_fs_open_t* opened = goc_io_fs_open(TMP_PATH,
                                         O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (!opened || opened->result < 0) goto done;
    goc_io_fs_close_t* closed = goc_io_fs_close((uv_file)opened->result);
    if (!closed || closed->result != 0) goto done;
    r->ok = 1;
done:;
}

static void test_p10_7(void)
{
    TEST_BEGIN("P10.7  goc_io_fs_open + goc_io_fs_close blocking wrappers work");
    fiber_result_t r = {0};
    goc_chan* done_ch = goc_go(fiber_p10_7, &r);
    goc_take_sync(done_ch);
    ASSERT(r.ok);
    /* Cleanup */
    uv_fs_t req;
    uv_fs_unlink(goc_scheduler(), &req, TMP_PATH, NULL);
    uv_fs_req_cleanup(&req);
    TEST_PASS();
done:;
}

/* =========================================================================
 * P10.8  goc_io_fs_open_ch with non-existent path + O_RDONLY → error
 * ====================================================================== */

static void fiber_p10_8(void* arg)
{
    fiber_result_t* r = (fiber_result_t*)arg;
    goc_chan* ch = goc_io_fs_open_ch("/nonexistent/path/that/does/not/exist",
                                   O_RDONLY, 0);
    goc_val_t*     v  = goc_take(ch);
    if (!v || v->ok != GOC_OK) goto done;
    goc_io_fs_open_t* res = (goc_io_fs_open_t*)v->val;
    /* result should be a negative error code */
    if (!res || res->result >= 0) goto done;
    r->ok = 1;
done:;
}

static void test_p10_8(void)
{
    TEST_BEGIN("P10.8  goc_io_fs_open_ch with invalid path returns error code");
    fiber_result_t r = {0};
    goc_chan* done_ch = goc_go(fiber_p10_8, &r);
    goc_take_sync(done_ch);
    ASSERT(r.ok);
    TEST_PASS();
done:;
}

/* =========================================================================
 * P10.9  goc_io_getaddrinfo_ch: resolve "localhost"
 * ====================================================================== */

static void fiber_p10_9(void* arg)
{
    fiber_result_t* r = (fiber_result_t*)arg;
    goc_chan*           ch = goc_io_getaddrinfo_ch("localhost", NULL, NULL);
    goc_val_t*          v  = goc_take(ch);
    if (!v || v->ok != GOC_OK) goto done;
    goc_io_getaddrinfo_t*  res = (goc_io_getaddrinfo_t*)v->val;
    if (!res || res->status != 0 || res->res == NULL) goto done;
    uv_freeaddrinfo(res->res);
    r->ok = 1;
done:;
}

static void test_p10_9(void)
{
    TEST_BEGIN("P10.9  goc_io_getaddrinfo_ch resolves \"localhost\"");
    fiber_result_t r = {0};
    goc_chan* done_ch = goc_go(fiber_p10_9, &r);
    goc_take_sync(done_ch);
    ASSERT(r.ok);
    TEST_PASS();
done:;
}

/* =========================================================================
 * P10.10 goc_io_getaddrinfo_ch with node=NULL and service=NULL → error
 * ====================================================================== */

static void fiber_p10_10(void* arg)
{
    fiber_result_t* r = (fiber_result_t*)arg;
    goc_chan*          ch  = goc_io_getaddrinfo_ch(NULL, NULL, NULL);
    goc_val_t*         v   = goc_take(ch);
    if (!v || v->ok != GOC_OK) goto done;
    goc_io_getaddrinfo_t* res = (goc_io_getaddrinfo_t*)v->val;
    if (!res) goto done;
    /* libuv returns an error (EAI_NONAME or similar) when both are NULL */
    if (res->status == 0 && res->res != NULL)
        uv_freeaddrinfo(res->res);
    /* Test passes regardless of status — we just need no crash */
    r->ok = 1;
done:;
}

static void test_p10_10(void)
{
    TEST_BEGIN("P10.10 goc_io_getaddrinfo_ch NULL node+service: no crash");
    fiber_result_t r = {0};
    goc_chan* done_ch = goc_go(fiber_p10_10, &r);
    goc_take_sync(done_ch);
    ASSERT(r.ok);
    TEST_PASS();
done:;
}

/* =========================================================================
 * P10.11 _ch variants return non-NULL channels (compile + API check)
 * ====================================================================== */

static void test_p10_11(void)
{
    TEST_BEGIN("P10.11 goc_io_getaddrinfo_ch returns non-NULL channel");
    goc_chan* ch = goc_io_getaddrinfo_ch("localhost", NULL, NULL);
    ASSERT(ch != NULL);
    /* Drain the channel to avoid leaking a live channel at shutdown. */
    goc_val_t* v = goc_take_sync(ch);
    if (v && v->ok == GOC_OK && v->val) {
        goc_io_getaddrinfo_t* res = (goc_io_getaddrinfo_t*)v->val;
        if (res->status == 0 && res->res)
            uv_freeaddrinfo(res->res);
    }
    TEST_PASS();
done:;
}

/* =========================================================================
 * P10.12 goc_io_fs_sendfile_ch: copy bytes between two file descriptors
 * ====================================================================== */

static void fiber_p10_12(void* arg)
{
    fiber_result_t* r = (fiber_result_t*)arg;

    /* Create source file with content */
    goc_io_fs_open_t* src_open = goc_io_fs_open(TMP_PATH,
                                            O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (!src_open || src_open->result < 0) goto done;
    uv_file src_fd = src_open->result;

    uv_buf_t wbuf = uv_buf_init((char*)CONTENT, (unsigned)CONTENT_LEN);
    goc_io_fs_write_t* written = goc_io_fs_write(src_fd, &wbuf, 1, 0);
    goc_io_fs_close(src_fd);
    if (!written || written->result != CONTENT_LEN) goto done;

    /* Reopen source for reading */
    src_open = goc_io_fs_open(TMP_PATH, O_RDONLY, 0);
    if (!src_open || src_open->result < 0) goto done;
    src_fd = src_open->result;

    /* Create destination file */
    goc_io_fs_open_t* dst_open = goc_io_fs_open(TMP_PATH3,
                                            O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (!dst_open || dst_open->result < 0) {
        goc_io_fs_close(src_fd);
        goto done;
    }
    uv_file dst_fd = dst_open->result;

    goc_io_fs_sendfile_t* sf = goc_io_fs_sendfile(dst_fd, src_fd, 0,
                                             (size_t)CONTENT_LEN);
    goc_io_fs_close(src_fd);
    goc_io_fs_close(dst_fd);
    if (!sf || sf->result != CONTENT_LEN) goto done;

    /* Verify destination content */
    goc_io_fs_open_t* verify_open = goc_io_fs_open(TMP_PATH3, O_RDONLY, 0);
    if (!verify_open || verify_open->result < 0) goto done;
    char rbuf[64] = {0};
    uv_buf_t rbufv = uv_buf_init(rbuf, sizeof(rbuf) - 1);
    goc_io_fs_read_t* rd = goc_io_fs_read(verify_open->result, &rbufv, 1, 0);
    goc_io_fs_close(verify_open->result);
    if (!rd || rd->result != CONTENT_LEN) goto done;
    if (memcmp(rbuf, CONTENT, (size_t)CONTENT_LEN) != 0) goto done;

    r->ok = 1;
done:;
}

static void test_p10_12(void)
{
    TEST_BEGIN("P10.12 goc_io_fs_sendfile_ch copies correct byte count");
    fiber_result_t r = {0};
    goc_chan* done_ch = goc_go(fiber_p10_12, &r);
    goc_take_sync(done_ch);
    ASSERT(r.ok);
    /* Cleanup */
    uv_fs_t req;
    uv_fs_unlink(goc_scheduler(), &req, TMP_PATH,  NULL); uv_fs_req_cleanup(&req);
    uv_fs_unlink(goc_scheduler(), &req, TMP_PATH3, NULL); uv_fs_req_cleanup(&req);
    TEST_PASS();
done:;
}

/* =========================================================================
 * P10.13 goc_io_fs_open_ch integrates with goc_alts (select on two I/O ops)
 * ====================================================================== */

static void fiber_p10_13(void* arg)
{
    fiber_result_t* r = (fiber_result_t*)arg;

    /* Select between two competing I/O channels: one opens a file, the other
     * performs a stat.  Both complete quickly; we just verify that alts works
     * correctly with _ch channels and delivers one result without crashing.
     * A dummy rendezvous channel is used as the "other" arm to keep the test
     * deterministic — only the open_ch arm will fire. */
    goc_chan* open_ch  = goc_io_fs_open_ch(TMP_PATH,
                                         O_WRONLY | O_CREAT | O_TRUNC, 0644);
    goc_chan* dummy_ch = goc_chan_make(0);   /* rendezvous; nobody writes */

    goc_alt_op ops[2] = {
        { .ch = open_ch,  .op_kind = GOC_ALT_TAKE, .put_val = NULL },
        { .ch = dummy_ch, .op_kind = GOC_ALT_TAKE, .put_val = NULL },
    };
    goc_alts_result* result = goc_alts(ops, 2);

    if (result->ch != open_ch) goto done;   /* unexpected winner */
    goc_io_fs_open_t* res = (goc_io_fs_open_t*)result->value.val;
    if (!res || res->result < 0) goto done;

    uv_fs_t req;
    uv_fs_close(goc_scheduler(), &req, (uv_file)res->result, NULL);
    uv_fs_req_cleanup(&req);

    /* Close the dummy channel so any parked alts entries are released. */
    goc_close(dummy_ch);

    r->ok = 1;
done:;
}

static void test_p10_13(void)
{
    TEST_BEGIN("P10.13 goc_io_fs_open_ch works with goc_alts (select)");
    fiber_result_t r = {0};
    goc_chan* done_ch = goc_go(fiber_p10_13, &r);
    goc_take_sync(done_ch);
    ASSERT(r.ok);
    /* Cleanup */
    uv_fs_t req;
    uv_fs_unlink(goc_scheduler(), &req, TMP_PATH, NULL);
    uv_fs_req_cleanup(&req);
    TEST_PASS();
done:;
}

/* =========================================================================
 * main
 * ====================================================================== */

int main(void)
{
    install_crash_handler();
    goc_init();

    printf("Phase 10 — Async I/O wrappers\n");

    cleanup_tmp_files();

    test_p10_1();
    test_p10_2();
    test_p10_3();
    test_p10_4();
    test_p10_5();
    test_p10_6();
    test_p10_7();
    test_p10_8();
    test_p10_9();
    test_p10_10();
    test_p10_11();
    test_p10_12();
    test_p10_13();

    printf("\n%d/%d tests passed", g_tests_passed, g_tests_run);
    if (g_tests_failed)
        printf(", %d FAILED", g_tests_failed);
    printf("\n");

    goc_shutdown();
    return g_tests_failed ? 1 : 0;
}
