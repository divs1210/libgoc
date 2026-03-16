/*
 * tests/test_p8_safety.c — Phase 8: Safety and crash behaviour tests for libgoc
 *
 * Verifies that the runtime detects and terminates on undefined or unsafe
 * usage patterns before they can cause silent corruption.  Every test in this
 * phase expects the child process to exit with SIGABRT; each test is run in a
 * forked child so that an abort in the child never terminates the parent test
 * harness.
 *
 * Build:  cmake -B build && cmake --build build
 * Run:    ctest --test-dir build --output-on-failure
 *         ./build/test_p8_safety
 *
 * Compile requirements: -std=c11 -DGC_THREADS -D_GNU_SOURCE
 *
 * Dependencies:
 *   - libgoc (goc.h)  — runtime under test
 *   - Boehm GC        — must be the threaded variant (bdw-gc-threaded);
 *                        initialised internally by goc_init()
 *   - libuv           — event loop; drives fiber scheduling
 *   - POSIX fork / waitpid — used to isolate each abort()-inducing test in a
 *                        child process; the parent waits for the child and
 *                        verifies it was killed by SIGABRT
 *
 * Test isolation via fork:
 *   Each test that is expected to call abort() spawns a child with fork().
 *   The child runs goc_init(), performs the unsafe operation, and should never
 *   return — the runtime calls abort() before that is possible.  The parent
 *   waits with waitpid() and checks WIFSIGNALED(status) && WTERMSIG(status)
 *   == SIGABRT.  If the child exits normally (no signal) the test fails.
 *
 *   The crash handler installed by install_crash_handler() is intentionally
 *   NOT called in the child: the child must exit via the runtime's own abort()
 *   call, not via a SIGSEGV handler.  The parent's crash handler is installed
 *   before fork() so that any unexpected crash in the parent itself is still
 *   reported with a backtrace.
 *
 *   Note: goc_init() must be called in every child process because goc state
 *   is process-local.  The child never calls goc_shutdown() — it is expected
 *   to abort() before reaching that point.
 *
 * Test coverage (Phase 8 — Safety and crash behaviour):
 *
 *   P8.1   Stack overflow: a fiber that exhausts its 64 KB stack overwrites
 *          the canary → pool worker calls abort() before the next mco_resume;
 *          verified via fork + waitpid asserting SIGABRT
 *   P8.2   goc_take() called from a bare OS thread (not a fiber) → abort();
 *          verified via fork + waitpid asserting SIGABRT
 *   P8.3   goc_put() called from a bare OS thread (not a fiber) → abort();
 *          verified via fork + waitpid asserting SIGABRT
 *
 * Notes:
 *   - goc_init() is called once in the parent main() before forking, but
 *     each child also calls goc_init() independently because the forked
 *     address space inherits the parent's (partially-initialised) GC state
 *     in an inconsistent way.  Each child must re-initialise from scratch.
 *   - goc_shutdown() is called once in the parent main() after all tests
 *     complete.
 *   - The goto-based cleanup pattern from the harness (ASSERT → done:) is
 *     used in the parent-side test wrappers only; child-side code has no
 *     cleanup label.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>

#include "test_harness.h"
#include "goc.h"

/* =========================================================================
 * fork_expect_sigabrt — helper that forks and asserts the child dies with
 *                        SIGABRT.
 *
 * Forks a child process.  The child calls child_fn(arg) and should never
 * return — the runtime is expected to call abort() from within child_fn.
 *
 * The parent blocks in waitpid().  Returns true if the child was killed by
 * SIGABRT, false otherwise (e.g. exited normally or died with another signal).
 * ====================================================================== */

typedef void (*child_fn_t)(void* arg);

static bool fork_expect_sigabrt(child_fn_t child_fn, void* arg) {
    pid_t pid = fork();
    if (pid < 0) {
        /* fork failed — treat as a test failure */
        return false;
    }

    if (pid == 0) {
        /*
         * Child process.
         *
         * Re-initialise the runtime from scratch.  The forked address space
         * inherits the parent's memory image but libuv handles, mutexes, and
         * the GC's internal thread table are all in an inconsistent state
         * because the background threads were not forked.  A fresh goc_init()
         * in the child is required before any goc_* call.
         *
         * The crash handler from the parent is NOT installed here — the child
         * must exit via abort() called by the runtime.
         */
        goc_init();
        child_fn(arg);
        /* Should never reach here — if we do, exit with a distinctive code
         * (2) so the parent can tell the child completed without aborting. */
        _exit(2);
    }

    /* Parent: wait for the child and inspect its exit status. */
    int status = 0;
    if (waitpid(pid, &status, 0) != pid) {
        return false;
    }

    return WIFSIGNALED(status) && (WTERMSIG(status) == SIGABRT);
}

/* =========================================================================
 * Phase 8 — Safety and crash behaviour
 * ====================================================================== */

/* --- P8.1: Stack overflow overwrites canary → abort() ------------------- */

/*
 * Recursive overflow function.
 *
 * Allocates a large on-stack buffer at each recursion level to exhaust the
 * fiber's 64 KB stack as quickly as possible without the compiler optimising
 * the recursion away.  The volatile keyword prevents dead-store elimination.
 *
 * Each call frame consumes ~512 bytes; ~130 levels overflow a 64 KB stack.
 * We cap at 512 levels to ensure we always overflow regardless of frame size
 * variance across platforms and optimisation levels.
 */
#define P8_1_OVERFLOW_DEPTH 512
#define P8_1_FRAME_BYTES    512

static void overflow_recursive(int depth) {
    volatile uint8_t buf[P8_1_FRAME_BYTES];
    buf[0] = (uint8_t)depth;   /* prevent dead-store elimination */
    if (depth > 0) {
        overflow_recursive(depth - 1);
    }
    /* buf[0] read back to ensure the compiler keeps the array live */
    (void)buf[0];
}

/*
 * Fiber entry point for P8.1.
 *
 * Triggers a stack overflow by recursing until the 64 KB fiber stack is
 * exhausted.  After overflow, the canary at the bottom of the stack is
 * overwritten.  The pool worker detects this before the next mco_resume and
 * calls abort().
 *
 * The fiber must suspend at least once (via goc_put) so that the pool worker
 * gets a chance to check the canary on the resumption path.  The overflow
 * happens during the recursive phase; the subsequent goc_put parks the fiber,
 * and on the next resume attempt the canary check fires.
 *
 * arg — pointer to an unbuffered goc_chan used to synchronise with the parent.
 */
static void p8_1_overflow_fiber(void* arg) {
    goc_chan* ch = (goc_chan*)arg;

    /*
     * Signal the parent that the fiber is running, then yield.  When the
     * parent puts a value on ch the fiber resumes — but by then the overflow
     * has already corrupted the canary, so the worker calls abort() before
     * calling mco_resume.
     */
    goc_put(ch, (void*)1);          /* park: waits for parent take — yields to worker */

    /* This point is never reached because abort fires on the resume path. */
    overflow_recursive(P8_1_OVERFLOW_DEPTH);
}

/*
 * Alternative design: overflow in the first quantum, then put.
 *
 * The fiber overflows its stack during the first run slice (before any yield).
 * The stack grows downward from the top of the 64 KB region; the recursive
 * calls walk past the canary at the base.  When the fiber finally yields via
 * goc_put, the worker re-enqueues it.  On the next iteration of the worker
 * loop, the canary check fires before mco_resume.
 *
 * This two-phase approach (overflow, then yield) is necessary because the
 * canary is checked only on the resume path, not during the initial run.
 */
/* Forward declaration — defined at file scope below p8_1_child_fn. */
static void overflow_then_yield(void* c);

static void p8_1_child_fn(void* arg) {
    (void)arg;

    /*
     * Use an unbuffered rendezvous channel.  The fiber runs overflow_recursive
     * to corrupt the stack, then attempts goc_put to suspend.  The pool worker
     * picks it up, checks the canary, and abort()s.
     *
     * We use goc_take_sync in the child's main thread to give the fiber a
     * chance to run and overflow before we signal it.
     */
    goc_chan* ch = goc_chan_make(0);

    /* Fiber: overflow stack, then park on goc_put(ch, ...). */
    goc_go(overflow_then_yield, ch);

    /*
     * Wait for the fiber's first put (proof it reached the yield point after
     * the overflow).  The pool worker then re-schedules the fiber.  On the
     * next resume, the canary check fires → abort().
     *
     * If abort() does not fire within a reasonable time, the child exits
     * normally (the parent catches this as a test failure).
     */
    goc_take_sync(ch);

    /* Unreachable if abort() fires correctly. */
}

/*
 * Fiber function (file-scope so that we can forward-declare above).
 *
 * Overflows the stack first, then parks on a put.  The worker resumes it
 * after the main thread takes from the channel; the canary check in the
 * worker fires on that resume attempt.
 */
static void overflow_then_yield(void* c) {
    goc_chan* ch = (goc_chan*)c;

    /* Corrupt the canary by exhausting the stack. */
    overflow_recursive(P8_1_OVERFLOW_DEPTH);

    /*
     * Attempt to suspend.  The fiber yields to the pool worker here.  The
     * worker will find the corrupted canary and abort() before the next
     * mco_resume.
     *
     * If somehow abort() does not fire (e.g. under unusual toolchain or
     * sanitizer conditions), the test will not hang: the child exits with
     * status 2 (the _exit(2) after child_fn returns) which the parent
     * recognises as a failure.
     */
    goc_put(ch, (void*)1);
}

/*
 * P8.1 — Stack overflow: canary overwrite detected, runtime calls abort()
 *
 * Forks a child that:
 *   1. Spawns a fiber that overflows its 64 KB stack via deep recursion.
 *   2. The fiber then yields to the pool worker via goc_put.
 *   3. The pool worker checks the canary before the next mco_resume and finds
 *      it corrupted → calls abort().
 *
 * The parent verifies the child was killed by SIGABRT.
 */
static void test_p8_1(void) {
    TEST_BEGIN("P8.1   stack overflow: canary overwrite → abort()");
    bool got_sigabrt = fork_expect_sigabrt(p8_1_child_fn, NULL);
    ASSERT(got_sigabrt);
    TEST_PASS();
done:;
}

/* --- P8.2: goc_take() from a bare OS thread → abort() ------------------- */

/*
 * Child function for P8.2.
 *
 * Calls goc_take() directly from the main (OS) thread, which is not a fiber.
 * goc_take() checks goc_in_fiber() and abort()s if the caller is not in a
 * fiber context.
 *
 * A dummy rendezvous channel is created so that goc_take() has a valid target;
 * the check fires before any channel operation takes place.
 */
static void p8_2_child_fn(void* arg) {
    (void)arg;
    goc_chan* ch = goc_chan_make(0);
    /* Call goc_take from a bare OS thread — must abort(). */
    goc_take(ch);
    /* Unreachable. */
}

/*
 * P8.2 — goc_take() from a bare OS thread → abort()
 *
 * Verifies that calling goc_take() outside a fiber causes the runtime to
 * abort() immediately.  Uses fork + waitpid to isolate the expected crash.
 */
static void test_p8_2(void) {
    TEST_BEGIN("P8.2   goc_take() from OS thread → abort()");
    bool got_sigabrt = fork_expect_sigabrt(p8_2_child_fn, NULL);
    ASSERT(got_sigabrt);
    TEST_PASS();
done:;
}

/* --- P8.3: goc_put() from a bare OS thread → abort() -------------------- */

/*
 * Child function for P8.3.
 *
 * Calls goc_put() directly from the main (OS) thread.  As with goc_take(),
 * the fiber-context assertion fires and the runtime calls abort().
 *
 * A buffered channel (capacity 1) is used so the put would ordinarily succeed
 * without a rendezvous partner; the abort must fire before the channel is
 * touched.
 */
static void p8_3_child_fn(void* arg) {
    (void)arg;
    goc_chan* ch = goc_chan_make(1);
    /* Call goc_put from a bare OS thread — must abort(). */
    goc_put(ch, (void*)(uintptr_t)0xCAFE);
    /* Unreachable. */
}

/*
 * P8.3 — goc_put() from a bare OS thread → abort()
 *
 * Verifies that calling goc_put() outside a fiber causes the runtime to
 * abort() immediately.  Uses fork + waitpid to isolate the expected crash.
 */
static void test_p8_3(void) {
    TEST_BEGIN("P8.3   goc_put() from OS thread → abort()");
    bool got_sigabrt = fork_expect_sigabrt(p8_3_child_fn, NULL);
    ASSERT(got_sigabrt);
    TEST_PASS();
done:;
}

/* =========================================================================
 * main
 *
 * Initialises the runtime once in the parent, runs all Phase 8 tests in
 * order, shuts down the runtime, then prints a summary and exits with 0 on
 * success or 1 if any test failed.
 *
 * The crash handler is installed before goc_init() so that any unexpected
 * crash in the parent process (as opposed to a deliberately aborted child)
 * is reported with a backtrace.
 * ====================================================================== */

int main(void) {
    install_crash_handler();

    printf("libgoc test suite — Phase 8: Safety and crash behaviour\n");
    printf("=========================================================\n\n");

    goc_init();

    printf("Phase 8 — Safety and crash behaviour\n");
    test_p8_1();
    test_p8_2();
    test_p8_3();
    printf("\n");

    goc_shutdown();

    printf("=========================================================\n");
    printf("Results: %d/%d passed", g_tests_passed, g_tests_run);
    if (g_tests_failed > 0) {
        printf(", %d FAILED", g_tests_failed);
    }
    printf("\n");

    return (g_tests_failed == 0) ? 0 : 1;
}
