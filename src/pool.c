/*
 * src/pool.c
 *
 * Thread pool workers, per-worker run queues, work stealing, pool registry,
 * and drain logic.  Defines goc_pool and all pool operations.
 *
 * Work-stealing design
 * --------------------
 * Each worker thread owns a local run queue.  New work is routed to the
 * calling worker's own queue when posted from a worker thread (local
 * affinity), or distributed round-robin when posted from an OS thread.
 * When a worker's queue is empty after waking on the shared semaphore, it
 * attempts to steal tasks from every other worker in round-robin order
 * before going back to sleep.  This eliminates the single shared-queue
 * bottleneck: instead of all threads contending on one mutex pair, each
 * push targets one worker's queue and cross-queue contention only arises
 * during stealing.
 *
 * Internal symbols exposed via internal.h:
 *   pool_registry_init()
 *   pool_registry_destroy_all()
 *   post_to_run_queue()
 *
 * Public API implemented here:
 *   goc_pool_make()
 *   goc_pool_destroy()
 *   goc_pool_destroy_timeout()
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <uv.h>
#include <gc.h>
#include "../include/goc.h"
#include "minicoro.h"
#include "internal.h"

/* -------------------------------------------------------------------------
 * Run-queue node (plain malloc — must not be GC-heap because goc_pool is
 * malloc'd and BDWGC won't scan its fields for GC pointers)
 * ---------------------------------------------------------------------- */

typedef struct goc_runq_node {
    goc_entry*             entry;
    struct goc_runq_node*  next;
} goc_runq_node;

/* -------------------------------------------------------------------------
 * Two-lock MPMC run queue (Michael & Scott style)
 *
 * One instance per worker.  The owner pushes to the tail; thieves and the
 * owner both pop from the head.  Head and tail locks are independent so
 * owner-push and thief-pop do not block each other.
 * ---------------------------------------------------------------------- */

typedef struct {
    goc_runq_node* head;
    goc_runq_node* tail;
    uv_mutex_t     head_lock;
    uv_mutex_t     tail_lock;
} goc_runq;

/* -------------------------------------------------------------------------
 * goc_worker — per-thread state owned by a pool
 * ---------------------------------------------------------------------- */

typedef struct goc_worker {
    goc_runq         runq;   /* per-worker run queue; others steal from head */
    pthread_t        thread; /* OS thread handle */
    struct goc_pool* pool;   /* back-pointer to owning pool */
    size_t           id;     /* index in pool->workers[] */
} goc_worker;

/* -------------------------------------------------------------------------
 * goc_pool — full definition (opaque outside pool.c)
 * ---------------------------------------------------------------------- */

struct goc_pool {
    goc_worker*     workers;       /* per-worker state (thread_count entries) */
    size_t          thread_count;
    uv_sem_t        work_sem;      /* global semaphore: one post per queued task */
    _Atomic int     shutdown;
    pthread_mutex_t drain_mutex;
    pthread_cond_t  drain_cond;
    size_t          active_count;  /* fibers currently queued or executing; decremented
                                      unconditionally after every mco_resume (yield or exit).
                                      Used only for internal scheduling accounting. */
    size_t          live_count;    /* fibers still alive on this pool; incremented exactly
                                      once per fiber at birth (pool_fiber_born), decremented
                                      only when mco_status == MCO_DEAD.  This is the correct
                                      drain signal: a parked fiber still has live_count > 0
                                      even though active_count has already been decremented. */
    _Atomic size_t  post_counter;  /* round-robin index for non-worker postings */
};

/* -------------------------------------------------------------------------
 * Thread-local pointer to the current worker (NULL outside worker threads).
 * Used by post_to_run_queue to prefer posting to the calling worker's own
 * queue, keeping work local and reducing cross-queue contention.
 * ---------------------------------------------------------------------- */

static _Thread_local goc_worker* tls_worker = NULL;

/* -------------------------------------------------------------------------
 * Pool registry (file-scope; owned entirely by pool.c)
 * ---------------------------------------------------------------------- */

static goc_pool**  g_pool_registry     = NULL;
static size_t      g_pool_registry_len = 0;
static size_t      g_pool_registry_cap = 0;
static uv_mutex_t  g_pool_registry_mutex;

/* -------------------------------------------------------------------------
 * pool_registry_init — allocates registry + mutex; called from gc.c:goc_init
 * ---------------------------------------------------------------------- */

void pool_registry_init(void) {
    g_pool_registry_cap = 8;
    g_pool_registry     = malloc(g_pool_registry_cap * sizeof(goc_pool*));
    g_pool_registry_len = 0;
    uv_mutex_init(&g_pool_registry_mutex);
}

/* -------------------------------------------------------------------------
 * pool_registry_destroy_all — called from gc.c:goc_shutdown (B.1)
 * ---------------------------------------------------------------------- */

void pool_registry_destroy_all(void) {
    uv_mutex_lock(&g_pool_registry_mutex);
    /* Snapshot the list before destroying, since goc_pool_destroy will
     * attempt to unregister itself and take the same lock.  We clear
     * the registry now so that unregister finds nothing to do. */
    size_t    len   = g_pool_registry_len;
    goc_pool** snap = malloc(len * sizeof(goc_pool*));
    memcpy(snap, g_pool_registry, len * sizeof(goc_pool*));
    g_pool_registry_len = 0;
    uv_mutex_unlock(&g_pool_registry_mutex);

    for (size_t i = 0; i < len; i++) {
        goc_pool_destroy(snap[i]);
    }
    free(snap);

    uv_mutex_destroy(&g_pool_registry_mutex);
    free(g_pool_registry);
    g_pool_registry = NULL;
}

/* -------------------------------------------------------------------------
 * registry_add / registry_remove (static helpers)
 * ---------------------------------------------------------------------- */

static void registry_add(goc_pool* pool) {
    uv_mutex_lock(&g_pool_registry_mutex);
    if (g_pool_registry_len == g_pool_registry_cap) {
        g_pool_registry_cap *= 2;
        g_pool_registry = realloc(g_pool_registry,
                                  g_pool_registry_cap * sizeof(goc_pool*));
    }
    g_pool_registry[g_pool_registry_len++] = pool;
    uv_mutex_unlock(&g_pool_registry_mutex);
}

static void registry_remove(goc_pool* pool) {
    uv_mutex_lock(&g_pool_registry_mutex);
    for (size_t i = 0; i < g_pool_registry_len; i++) {
        if (g_pool_registry[i] == pool) {
            g_pool_registry[i] = g_pool_registry[--g_pool_registry_len];
            break;
        }
    }
    uv_mutex_unlock(&g_pool_registry_mutex);
}

/* -------------------------------------------------------------------------
 * runq_init / runq_destroy — initialise and tear down a per-worker queue
 * ---------------------------------------------------------------------- */

static void runq_init(goc_runq* q) {
    goc_runq_node* sentinel = malloc(sizeof(goc_runq_node));
    sentinel->entry = NULL;
    sentinel->next  = NULL;
    q->head = sentinel;
    q->tail = sentinel;
    uv_mutex_init(&q->head_lock);
    uv_mutex_init(&q->tail_lock);
}

static void runq_destroy(goc_runq* q) {
    uv_mutex_destroy(&q->head_lock);
    uv_mutex_destroy(&q->tail_lock);
    goc_runq_node* n = q->head;
    while (n != NULL) {
        goc_runq_node* next = n->next;
        free(n);
        n = next;
    }
}

/* -------------------------------------------------------------------------
 * runq_push / runq_pop — two-lock MPMC operations
 * ---------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * runq_push — Add entry to the tail of the FIFO run queue
 *
 * Thread-safe via two-lock queue (separate head and tail locks).
 * Called by multiple threads to enqueue work.
 * ---------------------------------------------------------------------- */

static void runq_push(goc_runq* q, goc_entry* entry) {
    goc_runq_node* node = malloc(sizeof(goc_runq_node));
    node->entry = entry;
    node->next  = NULL;

    uv_mutex_lock(&q->tail_lock);
    q->tail->next = node;
    q->tail       = node;
    uv_mutex_unlock(&q->tail_lock);
}

/* -------------------------------------------------------------------------
 * runq_pop — Remove entry from the head of the FIFO run queue  
 *
 * Thread-safe via two-lock queue design. Returns NULL if queue is empty.
 * Called by worker threads to dequeue work.
 * ---------------------------------------------------------------------- */

static goc_entry* runq_pop(goc_runq* q) {
    uv_mutex_lock(&q->head_lock);
    goc_runq_node* sentinel = q->head;
    goc_runq_node* next     = sentinel->next;
    if (next == NULL) {
        uv_mutex_unlock(&q->head_lock);
        return NULL;
    }
    goc_entry* entry = next->entry;
    q->head          = next;
    uv_mutex_unlock(&q->head_lock);
    free(sentinel);  /* malloc'd in runq_push; must be freed explicitly */
    return entry;
}

/* -------------------------------------------------------------------------
 * pool_worker_fn — thread entry point
 * ---------------------------------------------------------------------- */

static void* pool_worker_fn(void* arg) {
    goc_worker* worker = (goc_worker*)arg;
    goc_pool*   pool   = worker->pool;
    size_t      id     = worker->id;

    /* Publish this thread as the current worker so post_to_run_queue can
     * target our own queue for local affinity. */
    tls_worker = worker;

    while (!atomic_load_explicit(&pool->shutdown, memory_order_acquire)) {
        uv_sem_wait(&pool->work_sem);

        /* Try own queue first (likely warm in cache). */
        goc_entry* entry = runq_pop(&worker->runq);

        /* Own queue empty — steal from other workers in round-robin order. */
        if (entry == NULL) {
            for (size_t i = 1; i < pool->thread_count; i++) {
                size_t victim = (id + i) % pool->thread_count;
                entry = runq_pop(&pool->workers[victim].runq);
                if (entry != NULL)
                    break;
            }
        }

        if (entry == NULL) {
            /* Spurious wake (work already stolen by an active worker, or
             * shutdown signal); re-check loop condition. */
            continue;
        }

        /* Canary check — abort on stack overflow before corrupting anything. */
        goc_stack_canary_check(entry->stack_canary_ptr);

        /* Save the coroutine handle before resuming.  If this is a parking
         * entry (stack-allocated inside goc_take on the fiber's own stack),
         * the fiber may run to completion during mco_resume — clobbering the
         * memory where `entry` lives before control returns here.  The
         * mco_coro object itself is minicoro heap-allocated and remains valid
         * until mco_destroy, so `coro` is safe to dereference after the
         * resume regardless of what happened to `entry`. */
        mco_coro* coro = entry->coro;

        /* Redirect the GC's stack scan for this thread to the fiber's stack.
         *
         * When mco_resume switches the stack pointer into the fiber's stack,
         * the GC stop-the-world handler sees a thread RSP that is far below
         * the worker thread's registered stack bottom.  The resulting scan
         * range [RSP-in-fiber-stack, OS-thread-stack-top] spans a huge region
         * of virtual address space containing unmapped pages, causing SIGSEGV
         * inside the GC marker.
         *
         * Fix: tell the GC the new "stack bottom" (high end of the fiber
         * stack) before resuming, so it scans only [RSP, fiber_stack_top].
         * Restore the original bottom after mco_resume returns. */
        struct GC_stack_base orig_sb;
        GC_get_my_stackbottom(&orig_sb);
        struct GC_stack_base fiber_sb;
        fiber_sb.mem_base = (char*)coro->stack_base + coro->stack_size;
        GC_set_stackbottom(NULL, &fiber_sb);

        mco_resume(coro);

        GC_set_stackbottom(NULL, &orig_sb);

        /* If the fiber just parked (called goc_take, goc_put, or goc_alts slow
         * path and set fiber_entry->parked = 0 before yielding), set it back
         * to 1 now that mco_resume has returned and the coroutine is truly
         * MCO_SUSPENDED.  This unblocks any wake() call spinning on parked==0. */
        goc_entry* fe = (goc_entry*)mco_get_user_data(coro);
        if (fe != NULL)
            atomic_store_explicit(&fe->parked, 1, memory_order_release);

        /* Update the cached fiber SP so the next GC cycle scans only the
         * used portion of the stack instead of the full vmem allocation. */
        if (mco_status(coro) == MCO_SUSPENDED && fe != NULL)
            goc_fiber_root_update_sp(fe->fiber_root_handle, coro);

        /* Correctness invariant: every fiber that yields (MCO_SUSPENDED) must
         * be re-posted to a run queue via post_to_run_queue(), which increments
         * active_count before the next resume.  If a fiber yields without being
         * re-queued (a bug in the channel / alts layer), active_count will reach
         * zero prematurely and goc_pool_destroy will return with live coroutines.
         * The canary check above and the assert(winner != NULL) in alts.c are the
         * primary guards against this happening silently.
         * Note: live_count is NOT touched here — it is managed by pool_fiber_born
         * (increment) and the MCO_DEAD branch below (decrement). */
        pthread_mutex_lock(&pool->drain_mutex);
        pool->active_count--;
        pthread_mutex_unlock(&pool->drain_mutex);

        /* Decrement live_count and broadcast only when the fiber has actually
         * exited.  A parked fiber (MCO_SUSPENDED) still counts as live — it will
         * be re-enqueued by wake() when a channel operation resumes it.
         * Broadcasting on every yield would let goc_pool_destroy_timeout see
         * live_count == 0 while fibers are merely parked, causing a premature
         * GOC_DRAIN_OK return and pool teardown. */
        if (mco_status(coro) == MCO_DEAD) {
            /* Unregister the fiber stack registered at birth (fiber.c).
             * fe is still valid here — mco_get_user_data returns the entry
             * set at creation, and mco_destroy has not yet been called. */
            if (fe != NULL)
                goc_fiber_root_unregister(fe->fiber_root_handle);
            mco_destroy(coro);

            pthread_mutex_lock(&pool->drain_mutex);
            pool->live_count--;
            pthread_cond_broadcast(&pool->drain_cond);
            pthread_mutex_unlock(&pool->drain_mutex);
        }
    }

    return NULL;
}

/* -------------------------------------------------------------------------
 * post_to_run_queue — internal; called from fiber.c and channel.c
 *
 * When called from a worker thread belonging to this pool, the entry is
 * pushed to that worker's own queue (local affinity — keeps work warm in
 * cache and avoids cross-queue contention).  Otherwise a round-robin worker
 * is selected so that idle threads are not starved.
 * ---------------------------------------------------------------------- */

void post_to_run_queue(goc_pool* pool, goc_entry* entry) {
    pthread_mutex_lock(&pool->drain_mutex);
    pool->active_count++;
    /* live_count is NOT incremented here.  It is incremented exactly once per
     * fiber, at birth, by pool_fiber_born() (called from goc_go_on in fiber.c).
     * Re-queuing a parked fiber via wake() must not inflate live_count, because
     * live_count is the drain signal: it reaches zero only when every fiber has
     * run to MCO_DEAD.  Incrementing it on every re-queue caused it to grow
     * unboundedly and goc_pool_destroy to block forever. */
    pthread_mutex_unlock(&pool->drain_mutex);

    size_t idx;
    goc_worker* self = tls_worker;
    if (self != NULL && self->pool == pool) {
        /* Posted from within a worker of this pool: use local queue. */
        idx = self->id;
    } else {
        /* Posted from an OS thread: distribute round-robin. */
        idx = atomic_fetch_add_explicit(&pool->post_counter, 1, memory_order_relaxed)
              % pool->thread_count;
    }

    runq_push(&pool->workers[idx].runq, entry);
    uv_sem_post(&pool->work_sem);
}

/* -------------------------------------------------------------------------
 * pool_fiber_born — increment live_count exactly once per new fiber
 *
 * Called from goc_go_on (fiber.c) before post_to_run_queue, so that
 * live_count tracks the number of fibers alive on the pool rather than
 * the number of scheduler events.  This is the only correct drain signal:
 * a parked fiber must still count as live even while active_count is zero.
 * ---------------------------------------------------------------------- */

void pool_fiber_born(goc_pool* pool) {
    pthread_mutex_lock(&pool->drain_mutex);
    pool->live_count++;
    pthread_mutex_unlock(&pool->drain_mutex);
}

/* -------------------------------------------------------------------------
 * pool_abort_if_called_from_worker
 *
 * Destroying a pool from one of its own worker threads is invalid: the
 * destroy path waits on drain/join and would attempt to join the caller
 * thread itself. Detect this explicitly and abort with a diagnostic.
 * ---------------------------------------------------------------------- */

static void pool_abort_if_called_from_worker(goc_pool* pool, const char* api_name) {
    pthread_t self = pthread_self();
    for (size_t i = 0; i < pool->thread_count; i++) {
        if (pthread_equal(self, pool->workers[i].thread)) {
            fprintf(stderr,
                    "libgoc: %s called from within target pool worker thread; "
                    "this is unsupported and would deadlock\n",
                    api_name);
            abort();
        }
    }
}

/* -------------------------------------------------------------------------
 * goc_pool_make
 * ---------------------------------------------------------------------- */

goc_pool* goc_pool_make(size_t threads) {
    goc_pool* pool = malloc(sizeof(goc_pool));
    memset(pool, 0, sizeof(goc_pool));

    pool->thread_count = threads;
    pool->workers = malloc(threads * sizeof(goc_worker));

    uv_sem_init(&pool->work_sem, 0);

    pthread_mutex_init(&pool->drain_mutex, NULL);
    pthread_cond_init(&pool->drain_cond, NULL);

    pool->active_count = 0;
    pool->live_count   = 0;
    atomic_store(&pool->shutdown, 0);
    atomic_store(&pool->post_counter, 0);

    for (size_t i = 0; i < threads; i++) {
        goc_worker* w = &pool->workers[i];
        w->pool = pool;
        w->id   = i;
        runq_init(&w->runq);
        gc_pthread_create(&w->thread, NULL, pool_worker_fn, w);
    }

    registry_add(pool);

    return pool;
}

/* -------------------------------------------------------------------------
 * goc_pool_destroy
 * ---------------------------------------------------------------------- */

void goc_pool_destroy(goc_pool* pool) {
    pool_abort_if_called_from_worker(pool, "goc_pool_destroy");

    /* 1. Wait for all live fibers to exit (live_count reaches zero). */
    pthread_mutex_lock(&pool->drain_mutex);
    while (pool->live_count > 0) {
        pthread_cond_wait(&pool->drain_cond, &pool->drain_mutex);
    }
    pthread_mutex_unlock(&pool->drain_mutex);

    /* 2. Signal workers to exit. */
    atomic_store_explicit(&pool->shutdown, 1, memory_order_release);

    /* 3. Unblock all waiting workers (one post per thread). */
    for (size_t i = 0; i < pool->thread_count; i++) {
        uv_sem_post(&pool->work_sem);
    }

    /* 4. Reap worker threads. */
    for (size_t i = 0; i < pool->thread_count; i++) {
        gc_pthread_join(pool->workers[i].thread, NULL);
    }

    /* 5. Destroy synchronisation primitives. */
    uv_sem_destroy(&pool->work_sem);
    pthread_mutex_destroy(&pool->drain_mutex);
    pthread_cond_destroy(&pool->drain_cond);

    /* 6. Drain and release per-worker run queues. */
    for (size_t i = 0; i < pool->thread_count; i++) {
        runq_destroy(&pool->workers[i].runq);
    }

    /* 7. Remove from registry (no-op if already removed by destroy_all). */
    registry_remove(pool);

    /* 8. Free pool itself. */
    free(pool->workers);
    free(pool);
}

/* -------------------------------------------------------------------------
 * goc_pool_destroy_timeout
 * ---------------------------------------------------------------------- */

goc_drain_result_t goc_pool_destroy_timeout(goc_pool* pool, uint64_t ms) {
    pool_abort_if_called_from_worker(pool, "goc_pool_destroy_timeout");

    /* Build an absolute deadline. */
    struct timespec deadline;
    timespec_get(&deadline, TIME_UTC);
    deadline.tv_sec  += (time_t)(ms / 1000);
    deadline.tv_nsec += (long)((ms % 1000) * 1000000L);
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec  += 1;
        deadline.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&pool->drain_mutex);
    int timed_out = 0;
    while (pool->live_count > 0 && !timed_out) {
        int rc = pthread_cond_timedwait(&pool->drain_cond,
                                        &pool->drain_mutex,
                                        &deadline);
        if (rc == ETIMEDOUT) {
            timed_out = 1;
        }
    }
    pthread_mutex_unlock(&pool->drain_mutex);

    if (timed_out && pool->live_count > 0) {
        /* Pool stays valid and running — do not tear it down. */
        return GOC_DRAIN_TIMEOUT;
    }

    /* Drain completed within the deadline; perform full shutdown. */
    atomic_store_explicit(&pool->shutdown, 1, memory_order_release);

    for (size_t i = 0; i < pool->thread_count; i++) {
        uv_sem_post(&pool->work_sem);
    }

    for (size_t i = 0; i < pool->thread_count; i++) {
#ifndef _WIN32
        GC_pthread_join(pool->workers[i].thread, NULL);
#else
        pthread_join(pool->workers[i].thread, NULL);
#endif
    }

    uv_sem_destroy(&pool->work_sem);
    pthread_mutex_destroy(&pool->drain_mutex);
    pthread_cond_destroy(&pool->drain_cond);

    for (size_t i = 0; i < pool->thread_count; i++) {
        runq_destroy(&pool->workers[i].runq);
    }

    registry_remove(pool);

    free(pool->workers);
    free(pool);

    return GOC_DRAIN_OK;
}
