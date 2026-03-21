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
    mco_desc desc   = mco_desc_init(fiber_trampoline, LIBGOC_STACK_SIZE); /* 0 = use minicoro's default */
    desc.user_data  = entry;

    /* 2. Create the coroutine (allocates the fiber stack). */
    mco_result rc = mco_create(&entry->coro, &desc);
    if (rc != MCO_SUCCESS) {
        fprintf(stderr, "libgoc: mco_create failed (%d)\n", rc);
        abort();
    }

    /* 3. Install the fiber's stack bounds into the pre-registered GC root slot
     *    (the slot was claimed by goc_go_on with NULL stack range to keep the
     *    entry alive; now fill in the actual stack so future GC cycles scan it).
     *    Unregistered in pool.c before mco_destroy when the fiber reaches MCO_DEAD. */
    void* fiber_stack_top = (char*)entry->coro->stack_base + entry->coro->stack_size;
    goc_fiber_root_set_stack(entry->fiber_root_handle, entry->coro, fiber_stack_top);

    /* 4. Record the canary pointer (lowest word of the fiber stack). */
    goc_stack_canary_init(entry);

    /* 5. Write the canary value so pool_worker_fn can detect stack overflow. */
    goc_stack_canary_set(entry->stack_canary_ptr);
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
