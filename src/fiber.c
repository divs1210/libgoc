/*
 * src/fiber.c — Fiber launch and lifecycle
 *
 * Implements goc_in_fiber, goc_go, and goc_go_on.
 * Owns g_default_pool (set by goc_init via gc.c).
 * Defines fiber_trampoline (the minicoro entry point).
 */

#include <stdlib.h>
#include <stdio.h>
#include <uv.h>
#include <gc.h>
#include "minicoro.h"
#include "../include/goc.h"
#include "internal.h"

/* ---------------------------------------------------------------------------
 * Global state
 * --------------------------------------------------------------------------- */

goc_pool* g_default_pool = NULL;   /* set by goc_init; exported via extern in internal.h */

/* ---------------------------------------------------------------------------
 * Thread-local coroutine free-list (canary mode only)
 *
 * Pooling mco_coro flat allocations avoids the malloc/mmap cost and the
 * TLB/page-fault penalty on the bulk of the stack pages.  vmem mode is
 * excluded: vmem stacks grow on demand with variable committed size, so
 * pooling would balloon RSS.
 *
 * Both helpers are static (TU-private).  pool.c accesses the pool only via
 * the exported wrappers goc_coro_pool_return / goc_coro_pool_drain, which
 * must be called from a pool worker thread.
 * --------------------------------------------------------------------------- */

#ifndef LIBGOC_VMEM_ENABLED
static _Thread_local mco_coro* tl_coro_pool[GOC_CORO_POOL_MAX];
static _Thread_local size_t    tl_coro_pool_len = 0;

/* Pop a dead coroutine from the thread-local pool.  Returns NULL if empty. */
static mco_coro* coro_pool_pop(void) {
    if (tl_coro_pool_len == 0) return NULL;
    return tl_coro_pool[--tl_coro_pool_len];
}

/* Return a dead coroutine to the pool.
 * If the pool is full, destroy it immediately (frees the flat allocation). */
static void coro_pool_push(mco_coro* coro) {
    if (tl_coro_pool_len < GOC_CORO_POOL_MAX) {
        tl_coro_pool[tl_coro_pool_len++] = coro;
    } else {
        mco_destroy(coro);
    }
}
#endif

/* ---------------------------------------------------------------------------
 * goc_in_fiber
 * --------------------------------------------------------------------------- */

bool goc_in_fiber(void) {
    return mco_running() != NULL;
}

/* ---------------------------------------------------------------------------
 * fiber_trampoline
 *
 * minicoro entry point for every fiber.  Retrieves the goc_entry that was
 * stored as user data on the coroutine, invokes the user function, then
 * closes the join channel so that any waiter on goc_take(join_ch) unblocks.
 * --------------------------------------------------------------------------- */

static void fiber_trampoline(mco_coro* co) {
    goc_entry* entry = (goc_entry*)mco_get_user_data(co);
    entry->fn(entry->fn_arg);
    goc_close(entry->join_ch);
}

/* ---------------------------------------------------------------------------
 * goc_fiber_materialize — deferred coroutine creation (called from pool.c)
 *
 * Invoked by pool_worker_fn the first time it picks up an entry whose coro
 * field is NULL (i.e. a fiber whose stack has not been allocated yet).
 * Separating this from goc_go_on makes spawn very cheap: the caller records
 * the closure and posts it to the run queue without touching any stack memory,
 * deferring the expensive mco_create + GC-root registration to the worker
 * that will actually run the fiber.
 * --------------------------------------------------------------------------- */

void goc_fiber_materialize(goc_entry* entry) {
    /* 1. Initialise the minicoro descriptor with the trampoline and stack size. */
    mco_desc desc   = mco_desc_init(fiber_trampoline, LIBGOC_STACK_SIZE);
    desc.user_data  = entry;

#ifndef LIBGOC_VMEM_ENABLED
    /* 2a. Try to reuse a pooled flat allocation.
     *     mco_init memsets the mco_coro header, recreates _mco_context, and
     *     writes a small initial frame (~2–3 words) at the TOP of the stack via
     *     _mco_makectx.  The rest of the stack (bottom 55+ KB) is untouched —
     *     those pages remain warm in the TLB from the previous fiber. */
    mco_coro* recycled = coro_pool_pop();
    if (recycled != NULL) {
        mco_result rc = mco_init(recycled, &desc);
        if (rc != MCO_SUCCESS) {
            fprintf(stderr, "libgoc: mco_init failed (%d)\n", rc);
            abort();
        }
        entry->coro = recycled;
    } else {
#endif
    /* 2b. No pooled coro available — allocate a fresh one. */
    mco_result rc = mco_create(&entry->coro, &desc);
    if (rc != MCO_SUCCESS) {
        fprintf(stderr, "libgoc: mco_create failed (%d)\n", rc);
        abort();
    }
#ifndef LIBGOC_VMEM_ENABLED
    }
#endif

    /* 3. Install the fiber's stack bounds into the pre-registered GC root slot
     *    (the slot was claimed by goc_go_on with NULL stack range to keep the
     *    entry alive; now fill in the actual stack so future GC cycles scan it).
     *    Unregistered in pool.c before goc_coro_pool_return when the fiber
     *    reaches MCO_DEAD. */
    void* fiber_stack_top = (char*)entry->coro->stack_base + entry->coro->stack_size;
    goc_fiber_root_set_stack(entry->fiber_root_handle, entry->coro, fiber_stack_top);

    /* 4. Record the canary pointer (lowest word of the fiber stack). */
    goc_stack_canary_init(entry);

    /* 5. Write the canary value so pool_worker_fn can detect stack overflow. */
    goc_stack_canary_set(entry->stack_canary_ptr);
}

/* ---------------------------------------------------------------------------
 * goc_coro_pool_return — called from pool.c (worker thread only)
 *
 * In canary mode: returns the dead coroutine to the thread-local pool
 * (destroying it immediately if the pool is full).
 * In vmem mode: falls through to mco_destroy.
 * --------------------------------------------------------------------------- */

void goc_coro_pool_return(mco_coro* coro) {
#ifndef LIBGOC_VMEM_ENABLED
    coro_pool_push(coro);
#else
    mco_destroy(coro);
#endif
}

/* ---------------------------------------------------------------------------
 * goc_coro_pool_drain — called from pool.c on worker exit (worker thread only)
 *
 * Destroys all pooled coroutine allocations on the current thread.
 * Must run after the worker's dispatch loop exits so no new fibers arrive.
 * --------------------------------------------------------------------------- */

void goc_coro_pool_drain(void) {
#ifndef LIBGOC_VMEM_ENABLED
    while (tl_coro_pool_len > 0)
        mco_destroy(tl_coro_pool[--tl_coro_pool_len]);
#endif
}

/* ---------------------------------------------------------------------------
 * goc_go_on — launch a fiber on a specific pool
 * --------------------------------------------------------------------------- */

goc_chan* goc_go_on(goc_pool* pool, void (*fn)(void*), void* arg) {
    /* 1. Create the join channel (rendezvous; closed when fiber returns). */
    goc_chan* join_ch = goc_chan_make(0);

    /* 2. Allocate the entry on the GC heap (zero-initialised by GC_malloc).
     *    coro == NULL signals that the coroutine has not been materialised yet;
     *    pool_worker_fn calls goc_fiber_materialize() on first dispatch. */
    goc_entry* entry = (goc_entry*)goc_malloc(sizeof(goc_entry));

    /* 3. Populate fiber launch fields. */
    entry->kind     = GOC_FIBER;
    entry->fn       = fn;
    entry->fn_arg   = arg;
    entry->join_ch  = join_ch;
    entry->pool     = pool;
    /* entry->coro is NULL (GC_malloc zeroes memory) — deferred until first dispatch */

    /* 4. Pre-register the entry as a GC root (no stack yet; stack_top and
     *    scan_from will be NULL until goc_fiber_materialize runs on the worker).
     *    This ensures the GC keeps entry alive between post_to_run_queue and
     *    the first dispatch even though the run queue is malloc'd (not GC heap). */
    entry->fiber_root_handle = goc_fiber_root_register(NULL, NULL, entry);

    /* 5. Record this fiber's birth in live_count (exactly once per fiber),
     *    then hand the entry to the pool run queue.
     *    pool_fiber_born must come before post_to_run_queue so that live_count
     *    is non-zero before the worker could potentially decrement it. */
    pool_fiber_born(pool);
    post_to_run_queue(pool, entry);

    /* 6. Return the join channel to the caller. */
    return join_ch;
}

/* ---------------------------------------------------------------------------
 * goc_go — launch a fiber on the default pool
 * --------------------------------------------------------------------------- */

goc_chan* goc_go(void (*fn)(void*), void* arg) {
    return goc_go_on(g_default_pool, fn, arg);
}

/* ---------------------------------------------------------------------------
 * goc_default_pool — return the default pool created by goc_init
 * --------------------------------------------------------------------------- */

goc_pool* goc_default_pool(void) {
    return g_default_pool;
}
