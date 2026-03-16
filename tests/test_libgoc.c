/*
 * tests/test_libgoc.c — Phase 1 & 2 integration tests for libgoc
 *
 * Build:  cmake -B build && cmake --build build
 * Run:    ctest --test-dir build --output-on-failure
 *         ./build/test_libgoc
 *
 * Compile requirements: -std=c11 -DGC_THREADS -D_GNU_SOURCE
 */

#define _GNU_SOURCE
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>

#include "goc.h"

/* =========================================================================
 * Minimal test harness
 * ====================================================================== */

static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_BEGIN(name)                                    \
    do {                                                    \
        g_tests_run++;                                      \
        printf("  %-50s ", (name));                         \
        fflush(stdout);                                     \
    } while (0)

#define ASSERT(cond)                                        \
    do {                                                    \
        if (!(cond)) {                                      \
            printf("FAIL\n    Assertion failed: %s\n"       \
                   "    %s:%d\n", #cond, __FILE__, __LINE__);\
            g_tests_failed++;                               \
            goto done;                                      \
        }                                                   \
    } while (0)

#define TEST_PASS()                                         \
    do {                                                    \
        printf("pass\n");                                   \
        g_tests_passed++;                                   \
        goto done;                                          \
    } while (0)

#define TEST_FAIL(msg)                                      \
    do {                                                    \
        printf("FAIL\n    %s\n    %s:%d\n",                 \
               (msg), __FILE__, __LINE__);                  \
        g_tests_failed++;                                   \
        goto done;                                          \
    } while (0)

/* =========================================================================
 * done_t — lightweight fiber-to-main synchronisation via POSIX semaphore
 * ====================================================================== */

typedef struct { sem_t sem; } done_t;

static void done_init(done_t* d)   { sem_init(&d->sem, 0, 0); }
static void done_signal(done_t* d) { sem_post(&d->sem); }
static void done_wait(done_t* d)   { sem_wait(&d->sem); }
static void done_destroy(done_t* d){ sem_destroy(&d->sem); }

/* =========================================================================
 * Phase 1 — Foundation
 * ====================================================================== */

static void test_p1_1(void) {
    TEST_BEGIN("P1.1  goc_scheduler() non-NULL after goc_init");
    uv_loop_t* loop = goc_scheduler();
    ASSERT(loop != NULL);
    TEST_PASS();
done:;
}

static void test_p1_2(void) {
    TEST_BEGIN("P1.2  goc_scheduler() pointer is stable across calls");
    uv_loop_t* a = goc_scheduler();
    uv_loop_t* b = goc_scheduler();
    ASSERT(a != NULL);
    ASSERT(a == b);
    TEST_PASS();
done:;
}

static void test_p1_3(void) {
    TEST_BEGIN("P1.3  goc_malloc returns non-NULL; memory is zero-initialised");
    const size_t SZ = 64;
    unsigned char* p = (unsigned char*)goc_malloc(SZ);
    ASSERT(p != NULL);
    for (size_t i = 0; i < SZ; i++) {
        ASSERT(p[i] == 0);
    }
    TEST_PASS();
done:;
}

static void test_p1_4(void) {
    TEST_BEGIN("P1.4  goc_in_fiber() returns false from main thread");
    ASSERT(goc_in_fiber() == false);
    TEST_PASS();
done:;
}

/* =========================================================================
 * Phase 2 — Channels and fiber launch
 * ====================================================================== */

static void test_p2_1(void) {
    TEST_BEGIN("P2.1  goc_chan_make(0) returns non-NULL (rendezvous)");
    goc_chan* ch = goc_chan_make(0);
    ASSERT(ch != NULL);
    goc_close(ch);
    TEST_PASS();
done:;
}

static void test_p2_2(void) {
    TEST_BEGIN("P2.2  goc_chan_make(16) returns non-NULL (buffered)");
    goc_chan* ch = goc_chan_make(16);
    ASSERT(ch != NULL);
    /* A buffered channel of capacity 16 should accept values without a taker.
     * Put 1 value synchronously from an OS thread to confirm buffering works. */
    goc_status_t st = goc_put_sync(ch, (void*)(uintptr_t)42);
    ASSERT(st == GOC_OK);
    goc_val_t v = goc_take_sync(ch);
    ASSERT(v.ok == GOC_OK);
    ASSERT((uintptr_t)v.val == 42);
    goc_close(ch);
    TEST_PASS();
done:;
}

/* --- P2.3: concurrent double-close ------------------------------------ */

typedef struct {
    goc_chan* ch;
    done_t*   ready; /* signals that closer thread is about to close */
    done_t*   go;    /* released when both threads should call goc_close */
} double_close_args_t;

static void* double_close_thread(void* arg) {
    double_close_args_t* a = (double_close_args_t*)arg;
    done_signal(a->ready);
    done_wait(a->go);
    goc_close(a->ch);
    return NULL;
}

static void test_p2_3(void) {
    TEST_BEGIN("P2.3  goc_close concurrent double-close is idempotent");
    goc_chan* ch = goc_chan_make(0);
    ASSERT(ch != NULL);

    done_t ready, go;
    done_init(&ready);
    done_init(&go);

    double_close_args_t args = { ch, &ready, &go };

    pthread_t tid;
    pthread_create(&tid, NULL, double_close_thread, &args);

    done_wait(&ready);
    /* Both the spawned thread and this thread will call goc_close. Release
     * the other thread and immediately call goc_close ourselves. */
    done_signal(&go);
    goc_close(ch);

    pthread_join(tid, NULL);

    done_destroy(&ready);
    done_destroy(&go);

    TEST_PASS();
done:;
}

/* --- P2.4 / P2.5 / P2.6 helpers -------------------------------------- */

typedef struct {
    void*   expected_arg;
    void*   received_arg;
    bool    in_fiber_flag;
    done_t* done;
} fiber_probe_t;

static void fiber_probe_fn(void* arg) {
    fiber_probe_t* p = (fiber_probe_t*)arg;
    p->received_arg   = arg;           /* note: arg IS the probe struct itself */
    p->in_fiber_flag  = goc_in_fiber();
    done_signal(p->done);
}

static void test_p2_4(void) {
    TEST_BEGIN("P2.4  goc_go launches fiber; join channel closed on return");
    done_t done;
    done_init(&done);

    fiber_probe_t probe = {
        .expected_arg  = NULL,
        .received_arg  = NULL,
        .in_fiber_flag = false,
        .done          = &done,
    };

    goc_chan* join = goc_go(fiber_probe_fn, &probe);
    ASSERT(join != NULL);

    done_wait(&done);

    /* Join channel must be closed (fiber has returned). */
    goc_val_t v = goc_take_sync(join);
    ASSERT(v.ok == GOC_CLOSED);

    done_destroy(&done);
    TEST_PASS();
done:;
}

static void test_p2_5(void) {
    TEST_BEGIN("P2.5  goc_go passes arg pointer through to fiber");
    done_t done;
    done_init(&done);

    fiber_probe_t probe = {
        .expected_arg  = NULL,
        .received_arg  = NULL,
        .in_fiber_flag = false,
        .done          = &done,
    };
    probe.expected_arg = &probe; /* the fiber receives a pointer to probe */

    goc_chan* join = goc_go(fiber_probe_fn, &probe);
    ASSERT(join != NULL);

    done_wait(&done);

    ASSERT(probe.received_arg == probe.expected_arg);

    goc_val_t v = goc_take_sync(join);
    ASSERT(v.ok == GOC_CLOSED);

    done_destroy(&done);
    TEST_PASS();
done:;
}

static void test_p2_6(void) {
    TEST_BEGIN("P2.6  goc_in_fiber() returns true from within a fiber");
    done_t done;
    done_init(&done);

    fiber_probe_t probe = {
        .expected_arg  = NULL,
        .received_arg  = NULL,
        .in_fiber_flag = false,
        .done          = &done,
    };

    goc_chan* join = goc_go(fiber_probe_fn, &probe);
    ASSERT(join != NULL);

    done_wait(&done);

    ASSERT(probe.in_fiber_flag == true);

    goc_val_t v = goc_take_sync(join);
    ASSERT(v.ok == GOC_CLOSED);

    done_destroy(&done);
    TEST_PASS();
done:;
}

/* --- P2.7: goc_take_sync blocks until join channel closes ------------- */

typedef struct {
    uint64_t  sleep_us;  /* how long the fiber sleeps (busy-spin free) */
    done_t*   done;
} slow_fiber_args_t;

static void slow_fiber_fn(void* arg) {
    slow_fiber_args_t* a = (slow_fiber_args_t*)arg;
    /* Simulate work with a short sleep on the fiber's OS thread. */
    struct timespec ts = {
        .tv_sec  = 0,
        .tv_nsec = (long)(a->sleep_us * 1000UL),
    };
    nanosleep(&ts, NULL);
    done_signal(a->done);
}

static void test_p2_7(void) {
    TEST_BEGIN("P2.7  join: goc_take_sync blocks until fiber returns");
    done_t done;
    done_init(&done);

    slow_fiber_args_t args = { .sleep_us = 20000 /* 20 ms */, .done = &done };

    goc_chan* join = goc_go(slow_fiber_fn, &args);
    ASSERT(join != NULL);

    /* Block the main thread until the join channel is closed. */
    goc_val_t v = goc_take_sync(join);
    ASSERT(v.ok == GOC_CLOSED);

    /* Fiber must have signalled before we could unblock. */
    /* (done_wait would return immediately if already signalled) */
    done_wait(&done);

    done_destroy(&done);
    TEST_PASS();
done:;
}

/* --- P2.8: goc_take from a second fiber blocks until target returns --- */

typedef struct {
    goc_chan* join;   /* join channel of the "target" fiber */
    done_t*   done;  /* signals the outer test when this fiber completes */
    bool      saw_closed;
} waiter_fiber_args_t;

static void waiter_fiber_fn(void* arg) {
    waiter_fiber_args_t* a = (waiter_fiber_args_t*)arg;
    goc_val_t v = goc_take(a->join);
    a->saw_closed = (v.ok == GOC_CLOSED);
    done_signal(a->done);
}

static void noop_fiber_fn(void* arg) {
    (void)arg;
}

static void test_p2_8(void) {
    TEST_BEGIN("P2.8  join: goc_take from second fiber suspends until target returns");
    done_t done;
    done_init(&done);

    /* Spawn the target fiber. */
    goc_chan* target_join = goc_go(noop_fiber_fn, NULL);
    ASSERT(target_join != NULL);

    /* Spawn a waiter fiber that calls goc_take on the target's join channel. */
    waiter_fiber_args_t wargs = {
        .join       = target_join,
        .done       = &done,
        .saw_closed = false,
    };
    goc_chan* waiter_join = goc_go(waiter_fiber_fn, &wargs);
    ASSERT(waiter_join != NULL);

    /* Wait for the waiter fiber to finish. */
    done_wait(&done);

    ASSERT(wargs.saw_closed == true);

    /* Clean up both join channels. */
    goc_val_t v;
    v = goc_take_sync(target_join);
    ASSERT(v.ok == GOC_CLOSED);
    v = goc_take_sync(waiter_join);
    ASSERT(v.ok == GOC_CLOSED);

    done_destroy(&done);
    TEST_PASS();
done:;
}

/* =========================================================================
 * main
 * ====================================================================== */

int main(void) {
    printf("libgoc test suite — Phase 1 & 2\n");
    printf("=================================\n\n");

    goc_init();

    /* --- Phase 1: Foundation --- */
    printf("Phase 1 — Foundation\n");
    test_p1_1();
    test_p1_2();
    test_p1_3();
    test_p1_4();
    printf("\n");

    /* --- Phase 2: Channels and fiber launch --- */
    printf("Phase 2 — Channels and fiber launch\n");
    test_p2_1();
    test_p2_2();
    test_p2_3();
    test_p2_4();
    test_p2_5();
    test_p2_6();
    test_p2_7();
    test_p2_8();
    printf("\n");

    goc_shutdown();

    printf("=================================\n");
    printf("Results: %d/%d passed", g_tests_passed, g_tests_run);
    if (g_tests_failed > 0) {
        printf(", %d FAILED", g_tests_failed);
    }
    printf("\n");

    return (g_tests_failed == 0) ? 0 : 1;
}
