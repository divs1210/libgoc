/* src/loop.c
 *
 * Owns the libuv event loop (g_loop) and its thread.
 * Owns the cross-thread wakeup handle (g_wakeup) and shutdown handle
 * (g_shutdown_async).
 * Owns the MPSC callback queue (g_cb_queue) and its drain logic (on_wakeup).
 * Exposes: goc_scheduler(), loop_init(), loop_shutdown(), post_callback().
 */

#include <stdlib.h>
#include <assert.h>
#include <sched.h>
#include <uv.h>
#include <gc.h>
#include "../include/goc.h"
#include "internal.h"
#include "channel_internal.h"

/* --------------------------------------------------------------------------
 * MPSC queue (Vyukov-style intrusive lock-free queue)
 * Nodes are GC-allocated so the GC can trace the goc_entry* inside.
 * -------------------------------------------------------------------------- */

typedef struct mpsc_node {
    _Atomic(struct mpsc_node *) next;
    goc_entry                  *entry;
} mpsc_node;

typedef struct {
    _Atomic(mpsc_node *) head;  /* producers exchange here */
    mpsc_node           *tail;  /* consumer-only */
} mpsc_queue_t;

/* --------------------------------------------------------------------------
 * Global state (exported via extern in internal.h)
 * -------------------------------------------------------------------------- */

uv_loop_t            *g_loop           = NULL;
_Atomic(uv_async_t *) g_wakeup;                 /* exported */

static uv_async_t    *g_wakeup_raw     = NULL;  /* raw pointer behind g_wakeup */
static uv_async_t    *g_shutdown_async = NULL;
static uv_thread_t    g_loop_thread;
static mpsc_queue_t   g_cb_queue;
static uv_mutex_t     g_loop_submit_lock;

typedef enum {
    GOC_LOOP_STATE_STOPPED = 0,
    GOC_LOOP_STATE_RUNNING = 1,
    GOC_LOOP_STATE_SHUTTING_DOWN = 2,
} goc_loop_state_t;

static _Atomic int g_loop_state = GOC_LOOP_STATE_STOPPED;
static _Atomic int g_shutdown_requested = 0;

/* Loop-thread drain reentrancy guard.
 * on_wakeup may trigger callbacks that enqueue more work; avoid recursively
 * re-entering drain_cb_queue on the same thread frame. */
static _Thread_local int g_in_cb_drain = 0;
_Thread_local int goc_uv_callback_depth = 0;

/* Callback queue depth tracking — relaxed atomics; zero hot-path overhead */
static _Atomic size_t g_cb_queue_depth = 0;
static _Atomic size_t g_cb_queue_hwm   = 0;

/* Forward declarations used by post_callback fast-path. */
static void drain_cb_queue(void);

int goc_on_loop_thread(void)
{
    uv_thread_t self = uv_thread_self();
    return uv_thread_equal(&self, &g_loop_thread);
}

static void drain_cb_queue_guarded(void)
{
    if (g_in_cb_drain) {
        GOC_DBG("drain_cb_queue_guarded: reentrant skip\n");
        return;
    }
    GOC_DBG("drain_cb_queue_guarded: enter depth=%zu state=%d g_loop=%p\n",
            atomic_load_explicit(&g_cb_queue_depth, memory_order_relaxed),
            atomic_load_explicit(&g_loop_state, memory_order_relaxed),
            (void*)g_loop);
    g_in_cb_drain = 1;
    drain_cb_queue();
    g_in_cb_drain = 0;
    GOC_DBG("drain_cb_queue_guarded: exit depth=%zu state=%d g_loop=%p\n",
            atomic_load_explicit(&g_cb_queue_depth, memory_order_relaxed),
            atomic_load_explicit(&g_loop_state, memory_order_relaxed),
            (void*)g_loop);
}

/* Bound callback processing per wakeup to avoid starving libuv I/O/timers
 * when callback producers are very hot (e.g. debug-heavy HTTP stress). */
#define GOC_CB_DRAIN_BUDGET 512

/* --------------------------------------------------------------------------
 * MPSC queue implementation
 * -------------------------------------------------------------------------- */

static void cb_queue_init(void)
{
    mpsc_node *sentinel = (mpsc_node *)malloc(sizeof(mpsc_node));
    atomic_store_explicit(&sentinel->next, NULL, memory_order_relaxed);
    sentinel->entry = NULL;
    atomic_store_explicit(&g_cb_queue.head, sentinel, memory_order_relaxed);
    g_cb_queue.tail = sentinel;
}

/* Enqueue an entry onto g_cb_queue (producer — any thread).
 * Returns the queue depth *before* this push (0 means queue was empty). */
static size_t cb_queue_push(mpsc_node *node)
{
    atomic_store_explicit(&node->next, NULL, memory_order_relaxed);
    /* Vyukov: exchange head and link previous head's next to this node. */
    mpsc_node *prev = atomic_exchange_explicit(&g_cb_queue.head, node,
                                               memory_order_acq_rel);
    atomic_store_explicit(&prev->next, node, memory_order_release);

    /* Update depth and high-water mark (relaxed — approximate is fine). */
    size_t prev_depth = atomic_fetch_add_explicit(&g_cb_queue_depth, 1, memory_order_relaxed);
    size_t depth = prev_depth + 1;
    size_t hwm   = atomic_load_explicit(&g_cb_queue_hwm, memory_order_relaxed);
    while (depth > hwm) {
        if (atomic_compare_exchange_weak_explicit(&g_cb_queue_hwm, &hwm, depth,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed))
            break;
    }
    if (prev_depth == 0) {
        GOC_DBG("cb_queue_push: empty->non-empty transition depth=%zu hwm=%zu g_loop_state=%d g_wakeup=%p\n",
                depth, hwm,
                atomic_load_explicit(&g_loop_state, memory_order_relaxed),
                (void*)atomic_load_explicit(&g_wakeup, memory_order_acquire));
    } else {
        GOC_DBG("cb_queue_push: depth=%zu hwm=%zu g_loop_state=%d g_wakeup=%p\n",
                depth, hwm,
                atomic_load_explicit(&g_loop_state, memory_order_relaxed),
                (void*)atomic_load_explicit(&g_wakeup, memory_order_acquire));
    }
    return prev_depth;
}

/* Dequeue from g_cb_queue (consumer — loop thread only).
 * Returns the entry or NULL if the queue is empty. */
static goc_entry *cb_queue_pop(void)
{
    mpsc_node *tail = g_cb_queue.tail;
    mpsc_node *next = atomic_load_explicit(&tail->next, memory_order_acquire);
    if (!next)
        return NULL;
    g_cb_queue.tail = next;         /* advance tail past sentinel */
    goc_entry *e = next->entry;
    free(tail);                     /* old sentinel; malloc'd, not GC-tracked */
    atomic_fetch_sub_explicit(&g_cb_queue_depth, 1, memory_order_relaxed);
    return e;
}

/* Accessor — safe to call from any thread at any time. */
size_t goc_cb_queue_get_hwm(void)
{
    return atomic_load_explicit(&g_cb_queue_hwm, memory_order_relaxed);
}

/* --------------------------------------------------------------------------
 * post_callback — called from any thread (pool workers, loop thread)
 * -------------------------------------------------------------------------- */

void post_callback(goc_entry *entry, void *value)
{
    /* For take callbacks: store the delivered value. */
    entry->cb_result = value;

    mpsc_node *node = (mpsc_node *)malloc(sizeof(mpsc_node));
    node->entry = entry;
    size_t prev_depth = cb_queue_push(node);

    uv_async_t *w = atomic_load_explicit(&g_wakeup, memory_order_acquire);
    GOC_DBG("post_callback: entry=%p ch=%p is_put=%d put_cb=%p wakeup=%p prev_depth=%zu\n",
            (void*)entry, (void*)entry->ch, (int)entry->is_put,
            (void*)(uintptr_t)entry->put_cb, (void*)w, prev_depth);

    /* Coalescing: skip uv_async_send when the queue was already non-empty.
     * A prior send is already in flight; on_wakeup will drain everything
     * enqueued up to that point, including this entry. */
    if (w && prev_depth == 0) {
        int w_active = uv_is_active((uv_handle_t*)w);
        int w_closing = uv_is_closing((uv_handle_t*)w);
        GOC_DBG("post_callback: uv_async_send about to send wakeup=%p active=%d closing=%d prev_depth=%zu g_loop_state=%d\n",
                (void*)w, w_active, w_closing, prev_depth,
                atomic_load_explicit(&g_loop_state, memory_order_relaxed));
        if (w_closing) {
            GOC_DBG("post_callback: wakeup closing, skipping uv_async_send wakeup=%p active=%d closing=%d prev_depth=%zu\n",
                    (void*)w, w_active, w_closing, prev_depth);
            if (goc_on_loop_thread()) {
                GOC_DBG("post_callback: loop thread detected closing wakeup, deferring drain to loop teardown prev_depth=%zu\n",
                        prev_depth);
                return;
            }
        } else {
            int rc = uv_async_send(w);
            GOC_DBG("post_callback: SENT rc=%d wakeup=%p active=%d closing=%d prev_depth=%zu\n",
                    rc, (void*)w, w_active, w_closing, prev_depth);
            if (rc < 0) {
                GOC_DBG("post_callback: uv_async_send FAILED wakeup=%p rc=%d active=%d closing=%d prev_depth=%zu\n",
                        (void*)w, rc, w_active, w_closing, prev_depth);
            }
        }
    } else if (!w && goc_on_loop_thread()) {
        GOC_DBG("post_callback: wakeup missing on loop thread, deferring drain to loop teardown depth=%zu state=%d g_wakeup=%p\n",
                atomic_load_explicit(&g_cb_queue_depth, memory_order_relaxed),
                atomic_load_explicit(&g_loop_state, memory_order_relaxed),
                (void*)atomic_load_explicit(&g_wakeup, memory_order_acquire));
        /* Shutdown/teardown edge: wakeup handle may already be gone. Defer
         * remaining work to the loop shutdown path so callbacks are never
         * executed inline while the runtime is mutating internal state. */
        return;
    }
}

/* --------------------------------------------------------------------------
 * post_on_loop — dispatch a function to run on the loop thread
 * -------------------------------------------------------------------------- */

typedef struct {
    void (*fn)(void*);
    void* arg;
    goc_entry e;   /* embedded entry — freed together with task */
} goc_loop_task_t;

static void on_loop_task_cb(goc_chan* _ch, void* _val, goc_status_t _ok, void* ud)
{
    goc_loop_task_t* task = (goc_loop_task_t*)ud;
    task->fn(task->arg);
    free(task);
}

void post_on_loop(void (*fn)(void*), void* arg)
{
    GOC_DBG("post_on_loop: entry fn=%p arg=%p\n", (void*)fn, arg);
    goc_loop_task_t* task = (goc_loop_task_t*)malloc(sizeof(goc_loop_task_t));
    task->fn = fn;
    task->arg = arg;
    task->e = (goc_entry){ 0 };
    task->e.kind = GOC_CALLBACK;
    task->e.cb   = on_loop_task_cb;
    task->e.ud   = task;
    post_callback(&task->e, NULL);
    GOC_DBG("post_on_loop: queued fn=%p arg=%p\n", (void*)fn, arg);
}

int post_on_loop_checked(void (*fn)(void*), void* arg)
{
    int rc = 0;

    uv_mutex_lock(&g_loop_submit_lock);
    if (atomic_load_explicit(&g_loop_state, memory_order_acquire) !=
        GOC_LOOP_STATE_RUNNING) {
        rc = UV_ECANCELED;
    } else {
        goc_loop_task_t* task =
            (goc_loop_task_t*)malloc(sizeof(goc_loop_task_t));
        task->fn  = fn;
        task->arg = arg;
        task->e   = (goc_entry){ 0 };
        task->e.kind = GOC_CALLBACK;
        task->e.cb   = on_loop_task_cb;
        task->e.ud   = task;
        post_callback(&task->e, NULL);
    }
    uv_mutex_unlock(&g_loop_submit_lock);

    if (rc < 0) {
        GOC_DBG("post_on_loop_checked: rejected fn=%p arg=%p state=%d g_loop=%p rc=%d\n",
                (void*)fn, arg,
                atomic_load_explicit(&g_loop_state, memory_order_relaxed),
                (void*)g_loop,
                rc);
    } else {
        GOC_DBG("post_on_loop_checked: queued fn=%p arg=%p state=%d g_loop=%p rc=%d\n",
                (void*)fn, arg,
                atomic_load_explicit(&g_loop_state, memory_order_relaxed),
                (void*)g_loop,
                rc);
    }
    return rc;
}

int goc_loop_is_shutting_down(void)
{
    return atomic_load_explicit(&g_loop_state, memory_order_acquire) !=
           GOC_LOOP_STATE_RUNNING;
}

static void free_handle_cb(uv_handle_t *h);  /* defined in loop-thread callbacks section below */

static void loop_walk_handle_cb(uv_handle_t* handle, void* arg)
{
    size_t* count = (size_t*)arg;
    (*count)++;
    GOC_DBG("loop_walk_handle_cb: handle=%p type=%s active=%d has_ref=%d closing=%d loop=%p data=%p\n",
            (void*)handle,
            uv_handle_type_name(uv_handle_get_type(handle)),
            uv_is_active(handle),
            uv_has_ref(handle),
            uv_is_closing(handle),
            (void*)handle->loop,
            handle->data);
}

typedef struct {
    size_t async_wakeup;
    size_t async_shutdown;
    size_t timer_mgr;
    size_t other;
} handle_summary_t;

static uv_timer_t*          g_timer_mgr      = NULL;  /* malloc'd; one per loop */
static timer_heap_entry_t* g_timer_heap     = NULL;  /* goc_malloc'd */
static size_t               g_timer_heap_len = 0;
static size_t               g_timer_heap_cap = 0;

static void loop_handle_summary_cb(uv_handle_t* handle, void* arg)
{
    handle_summary_t* summary = (handle_summary_t*)arg;
    switch (uv_handle_get_type(handle)) {
    case UV_ASYNC:
        if (handle == (uv_handle_t*)g_wakeup_raw)
            summary->async_wakeup++;
        else if (handle == (uv_handle_t*)g_shutdown_async)
            summary->async_shutdown++;
        else
            summary->other++;
        break;
    case UV_TIMER:
        if (handle == (uv_handle_t*)g_timer_mgr)
            summary->timer_mgr++;
        else
            summary->other++;
        break;
    default:
        summary->other++;
        break;
    }
}

static void dump_loop_internal_state(const char *phase, long iter)
{
    size_t loop_count = 0;
    handle_summary_t summary = {0, 0, 0, 0};
    uv_walk(g_loop, loop_walk_handle_cb, &loop_count);
    uv_walk(g_loop, loop_handle_summary_cb, &summary);
    GOC_DBG("loop_internal_state: %s iter=%ld g_loop=%p alive=%d handle_count=%zu cb_depth=%zu hwm=%zu g_loop_state=%d g_wakeup=%p g_wakeup_raw=%p g_shutdown_async=%p g_timer_mgr=%p timer_heap_len=%zu async_wakeup=%zu async_shutdown=%zu timer_mgr=%zu other_handles=%zu\n",
            phase, iter, (void*)g_loop, uv_loop_alive(g_loop), loop_count,
            atomic_load_explicit(&g_cb_queue_depth, memory_order_relaxed),
            atomic_load_explicit(&g_cb_queue_hwm, memory_order_relaxed),
            atomic_load_explicit(&g_loop_state, memory_order_relaxed),
            (void*)atomic_load_explicit(&g_wakeup, memory_order_acquire),
            (void*)g_wakeup_raw,
            (void*)g_shutdown_async,
            (void*)g_timer_mgr,
            g_timer_heap_len,
            summary.async_wakeup,
            summary.async_shutdown,
            summary.timer_mgr,
            summary.other);
}

/* --------------------------------------------------------------------------
 * Central timer manager
 *
 * Replaces per-timeout uv_timer_t allocation with a single long-lived
 * uv_timer_t driven by a GC-allocated min-heap of pending timeouts.
 *
 * The heap array is goc_malloc'd so the GC can trace goc_timeout_timer_ctx*
 * pointers inside and keep ctx objects alive while they are pending — no
 * per-timeout gc_handle_register call is needed.
 *
 * All operations run on the loop thread (no extra lock required).
 * -------------------------------------------------------------------------- */

struct timer_heap_entry {
    uint64_t               deadline_ns;
    goc_timeout_timer_ctx* ctx;
};

#define TIMER_HEAP_INIT_CAP 64

static void on_timer_mgr_fire(uv_timer_t* h);  /* forward declaration */

static void timer_heap_swap(size_t a, size_t b)
{
    timer_heap_entry_t tmp = g_timer_heap[a];
    g_timer_heap[a]        = g_timer_heap[b];
    g_timer_heap[b]        = tmp;
    g_timer_heap[a].ctx->heap_idx = a;
    g_timer_heap[b].ctx->heap_idx = b;
}

static void timer_heap_sift_up(size_t i)
{
    while (i > 0) {
        size_t parent = (i - 1) / 2;
        if (g_timer_heap[parent].deadline_ns <= g_timer_heap[i].deadline_ns)
            break;
        timer_heap_swap(parent, i);
        i = parent;
    }
}

static void timer_heap_sift_down(size_t i)
{
    for (;;) {
        size_t smallest = i;
        size_t left  = 2 * i + 1;
        size_t right = 2 * i + 2;
        if (left  < g_timer_heap_len &&
            g_timer_heap[left].deadline_ns  < g_timer_heap[smallest].deadline_ns)
            smallest = left;
        if (right < g_timer_heap_len &&
            g_timer_heap[right].deadline_ns < g_timer_heap[smallest].deadline_ns)
            smallest = right;
        if (smallest == i)
            break;
        timer_heap_swap(i, smallest);
        i = smallest;
    }
}

/* Re-arm the backing uv_timer_t to fire at the earliest pending deadline,
 * or stop it if the heap is empty. */
static void timer_mgr_rearm(void)
{
    if (g_timer_heap_len == 0) {
        GOC_DBG("timer_mgr_rearm: heap empty, stopping timer_mgr=%p g_loop=%p\n",
                (void*)g_timer_mgr, (void*)g_loop);
        uv_timer_stop(g_timer_mgr);
        return;
    }
    uint64_t now_ns   = uv_hrtime();
    uint64_t next_ns  = g_timer_heap[0].deadline_ns;
    uint64_t delay_ms = (next_ns > now_ns)
                      ? ((next_ns - now_ns + 999999ULL) / 1000000ULL)
                      : 0;
    GOC_DBG("timer_mgr_rearm: timer_mgr=%p g_loop=%p heap_len=%zu now_ns=%llu next_ns=%llu delay_ms=%llu active=%d closing=%d\n",
            (void*)g_timer_mgr, (void*)g_loop, g_timer_heap_len,
            (unsigned long long)now_ns,
            (unsigned long long)next_ns,
            (unsigned long long)delay_ms,
            g_timer_mgr ? uv_is_active((uv_handle_t*)g_timer_mgr) : -1,
            g_timer_mgr ? uv_is_closing((uv_handle_t*)g_timer_mgr) : -1);
    uv_timer_start(g_timer_mgr, on_timer_mgr_fire, delay_ms, 0);
}

/* Loop-thread callback: fires all expired entries and re-arms. */
static void on_timer_mgr_fire(uv_timer_t* h)
{
    (void)h;
    goc_uv_callback_enter("on_timer_mgr_fire");
    uint64_t now_ns = uv_hrtime();
    GOC_DBG("on_timer_mgr_fire: now_ns=%llu heap_len=%zu g_timer_mgr=%p active=%d closing=%d g_loop=%p loop_alive=%d g_wakeup=%p shutdown_async=%p\n",
            (unsigned long long)now_ns, g_timer_heap_len,
            (void*)g_timer_mgr,
            g_timer_mgr ? uv_is_active((uv_handle_t*)g_timer_mgr) : -1,
            g_timer_mgr ? uv_is_closing((uv_handle_t*)g_timer_mgr) : -1,
            (void*)g_loop,
            g_loop ? uv_loop_alive(g_loop) : -1,
            (void*)atomic_load_explicit(&g_wakeup, memory_order_acquire),
            (void*)g_shutdown_async);

    while (g_timer_heap_len > 0 && g_timer_heap[0].deadline_ns <= now_ns) {
        goc_timeout_timer_ctx* ctx = g_timer_heap[0].ctx;
        GOC_DBG("on_timer_mgr_fire: expiring tctx=%p ch=%p deadline=%llu\n",
                (void*)ctx, (void*)ctx->ch,
                (unsigned long long)g_timer_heap[0].deadline_ns);

        /* Remove root from heap (swap with last element, then sift down). */
        ctx->heap_idx = SIZE_MAX;
        size_t last = --g_timer_heap_len;
        if (last > 0) {
            g_timer_heap[0] = g_timer_heap[last];
            g_timer_heap[0].ctx->heap_idx = 0;
            timer_heap_sift_down(0);
        }

        /* Mark closed before firing so on_cancel_timer (if posted) is a no-op. */
        atomic_store_explicit(&ctx->start_state, 2, memory_order_release);
        goc_timeout_ctx_expire(ctx);
    }

    timer_mgr_rearm();
}

/* Public: insert ctx into the heap; re-arm the backing timer if needed.
 * Called from on_start_timer (loop thread only). */
goc_timer_manager_t* goc_global_timer_mgr(void)
{
    static goc_timer_manager_t g_mgr;
    return &g_mgr;
}

void goc_timer_manager_insert(goc_timer_manager_t* mgr,
                              goc_timeout_timer_ctx* ctx,
                              uint64_t deadline_ns)
{
    /* Grow the GC-managed heap array if full. */
    (void)mgr;
    GOC_DBG("goc_timer_manager_insert: ctx=%p ch=%p deadline=%llu heap_len=%zu cap=%zu g_loop=%p g_timer_mgr=%p active=%d closing=%d\n",
            (void*)ctx, (void*)ctx->ch,
            (unsigned long long)deadline_ns,
            g_timer_heap_len, g_timer_heap_cap,
            (void*)g_loop, (void*)g_timer_mgr,
            g_timer_mgr ? uv_is_active((uv_handle_t*)g_timer_mgr) : -1,
            g_timer_mgr ? uv_is_closing((uv_handle_t*)g_timer_mgr) : -1);
    if (g_timer_heap_len == g_timer_heap_cap) {
        size_t new_cap = g_timer_heap_cap ? g_timer_heap_cap * 2 : TIMER_HEAP_INIT_CAP;
        g_timer_heap = (timer_heap_entry_t*)goc_realloc(
                            g_timer_heap, new_cap * sizeof(timer_heap_entry_t));
        g_timer_heap_cap = new_cap;
    }

    size_t i = g_timer_heap_len++;
    g_timer_heap[i].deadline_ns = deadline_ns;
    g_timer_heap[i].ctx         = ctx;
    ctx->heap_idx               = i;
    timer_heap_sift_up(i);

    /* Re-arm only when the new entry became the new earliest deadline. */
    if (ctx->heap_idx == 0)
        timer_mgr_rearm();
}

/* Public: remove ctx from the heap (cancel path).
 * Called from on_cancel_timer (loop thread only). */
void goc_timer_manager_remove(goc_timer_manager_t* mgr,
                              goc_timeout_timer_ctx* ctx)
{
    (void)mgr;
    GOC_DBG("goc_timer_manager_remove: ctx=%p ch=%p heap_idx=%zu heap_len=%zu g_loop=%p g_timer_mgr=%p active=%d closing=%d\n",
            (void*)ctx, (void*)ctx->ch,
            ctx->heap_idx, g_timer_heap_len,
            (void*)g_loop, (void*)g_timer_mgr,
            g_timer_mgr ? uv_is_active((uv_handle_t*)g_timer_mgr) : -1,
            g_timer_mgr ? uv_is_closing((uv_handle_t*)g_timer_mgr) : -1);
    size_t i = ctx->heap_idx;
    if (i == SIZE_MAX || i >= g_timer_heap_len)
        return;

    ctx->heap_idx = SIZE_MAX;

    size_t last = --g_timer_heap_len;
    if (i == last) {
        /* Was the last element; just shrink. Re-arm (may stop if heap now empty). */
        timer_mgr_rearm();
        return;
    }

    /* Replace hole with the last element and restore heap property. */
    goc_timeout_timer_ctx* moved = g_timer_heap[last].ctx;
    g_timer_heap[i] = g_timer_heap[last];
    moved->heap_idx = i;

    timer_heap_sift_up(i);
    /* After sift_up, moved may have risen; find its current position via heap_idx. */
    timer_heap_sift_down(moved->heap_idx);

    /* The root may have changed; re-arm unconditionally. */
    timer_mgr_rearm();
}

/* Initialise the timer manager; called from loop_init after uv_loop_init. */
static void goc_timer_manager_init(void)
{
    g_timer_heap     = (timer_heap_entry_t*)goc_malloc(
                            TIMER_HEAP_INIT_CAP * sizeof(timer_heap_entry_t));
    g_timer_heap_cap = TIMER_HEAP_INIT_CAP;
    g_timer_heap_len = 0;

    g_timer_mgr = (uv_timer_t*)malloc(sizeof(uv_timer_t));
    assert(g_timer_mgr != NULL);
    int rc = uv_timer_init(g_loop, g_timer_mgr);
    goc_uv_init_log("uv_timer_init timer_mgr", rc, g_loop, (uv_handle_t*)g_timer_mgr);
    (void)rc;
    /* Do not start the timer yet; it will be armed on the first insert. */
}

/* Drain and close the timer manager; called from on_shutdown_signal (loop thread).
 * Closes all pending timeout channels, then closes the backing uv_timer_t. */
static void goc_timer_manager_shutdown(void)
{
    GOC_DBG("goc_timer_manager_shutdown: draining %zu pending timeouts g_timer_mgr=%p g_loop=%p active=%d closing=%d\n",
            g_timer_heap_len, (void*)g_timer_mgr, (void*)g_loop,
            g_timer_mgr ? uv_is_active((uv_handle_t*)g_timer_mgr) : -1,
            g_timer_mgr ? uv_is_closing((uv_handle_t*)g_timer_mgr) : -1);

    if (g_timer_mgr) {
        GOC_DBG("goc_timer_manager_shutdown: uv_timer_stop g_timer_mgr=%p active=%d closing=%d\n",
                (void*)g_timer_mgr,
                uv_is_active((uv_handle_t*)g_timer_mgr),
                uv_is_closing((uv_handle_t*)g_timer_mgr));
        uv_timer_stop(g_timer_mgr);
    }

    for (size_t i = 0; i < g_timer_heap_len; i++) {
        goc_timeout_timer_ctx* ctx = g_timer_heap[i].ctx;
        int state = atomic_load_explicit(&ctx->start_state, memory_order_acquire);
        int cancel = atomic_load_explicit(&ctx->cancel_requested, memory_order_acquire);
        GOC_DBG("goc_timer_manager_shutdown: closing tctx=%p ch=%p start_state=%d cancel_requested=%d\n",
                (void*)ctx, (void*)ctx->ch, state, cancel);
        /* Set state=2 so any in-flight on_cancel_timer callbacks are no-ops. */
        atomic_store_explicit(&ctx->start_state, 2, memory_order_release);
        ctx->heap_idx = SIZE_MAX;
        goc_close(ctx->ch);
    }
    g_timer_heap_len = 0;

    if (g_timer_mgr) {
        goc_uv_close_log("goc_timer_manager_shutdown", (uv_handle_t*)g_timer_mgr);
        goc_uv_close_internal((uv_handle_t*)g_timer_mgr, free_handle_cb);
        g_timer_mgr = NULL;
    } else {
        GOC_DBG("goc_timer_manager_shutdown: no g_timer_mgr to close\n");
    }
    GOC_DBG("goc_timer_manager_shutdown: done\n");
}

/* --------------------------------------------------------------------------
 * goc_scheduler — public
 * -------------------------------------------------------------------------- */

uv_loop_t *goc_scheduler(void)
{
    return g_loop;
}

/* --------------------------------------------------------------------------
 * Loop thread callbacks
 * -------------------------------------------------------------------------- */

static void free_handle_cb(uv_handle_t *h)
{
    goc_uv_callback_enter("free_handle_cb");
    goc_uv_handle_log("free_handle_cb", h);
    free(h);
    goc_uv_callback_exit("free_handle_cb");
}

static void unregister_gc_handle_cb(uv_handle_t *h)
{
    goc_uv_callback_enter("unregister_gc_handle_cb");
    goc_uv_handle_log("unregister_gc_handle_cb", h);
    gc_handle_unregister(h);
    goc_uv_callback_exit("unregister_gc_handle_cb");
}

static void on_shutdown_signal(uv_async_t *h);

static void close_remaining_handle_cb(uv_handle_t *h, void *arg)
{
    (void)arg;
    if (uv_is_closing(h)) {
        GOC_DBG("shutdown uv_walk skip closing handle=%p type=%s active=%d has_ref=%d\n",
                (void*)h,
                uv_handle_type_name(uv_handle_get_type(h)),
                uv_is_active(h),
                uv_has_ref(h));
        return;
    }

    if (h == (uv_handle_t *)g_wakeup_raw ||
        h == (uv_handle_t *)g_shutdown_async) {
        GOC_DBG("shutdown uv_walk skip internal handle=%p type=%s active=%d has_ref=%d\n",
                (void*)h,
                uv_handle_type_name(uv_handle_get_type(h)),
                uv_is_active(h),
                uv_has_ref(h));
        return;
    }

    GOC_DBG("shutdown uv_walk close: handle=%p type=%s active=%d has_ref=%d closing=%d loop=%p data=%p\n",
            (void*)h,
            uv_handle_type_name(uv_handle_get_type(h)),
            uv_is_active(h),
            uv_has_ref(h),
            uv_is_closing(h),
            (void*)h->loop,
            h->data);
    goc_uv_close_log("close_remaining_handle_cb", h);
    goc_uv_close_internal(h, unregister_gc_handle_cb);
}

static void on_wakeup_closed(uv_handle_t *h)
{
    goc_uv_callback_enter("on_wakeup_closed");
    goc_uv_close_log("on_wakeup_closed", h);
    goc_uv_handle_log("on_wakeup_closed", h);
    /* Set g_wakeup to NULL only once libuv guarantees no further callbacks
     * will fire for this handle, preventing a race with post_callback. */
    atomic_store_explicit(&g_wakeup, NULL, memory_order_release);
    free(h);
    goc_uv_callback_exit("on_wakeup_closed");
}

/* --------------------------------------------------------------------------
 * loop_process_pending_put / loop_process_pending_take
 *
 * Called on the loop thread to perform the channel operation for a
 * goc_put_cb / goc_take_cb entry that was posted without holding ch->lock.
 * Acquires ch->lock, attempts fast delivery, and parks the entry if no
 * match is available yet.  Fires the optional callback immediately when
 * the operation resolves; otherwise the entry stays parked and wake() will
 * call post_callback() later to fire it.
 * -------------------------------------------------------------------------- */

static void loop_process_pending_put(goc_entry *e)
{
    goc_chan *ch = e->ch;
    e->ch = NULL;   /* clear so a future drain_cb_queue pass skips this entry */

    goc_entry *fe_taker = NULL;

    uv_mutex_lock(ch->lock);

    if (ch->dead_count >= GOC_DEAD_COUNT_THRESHOLD)
        compact_dead_entries(ch);

    /* Walk taker list to verify each entry is accessible before claim. */
    {
        goc_entry *_t = ch->takers;
        int _i = 0;
        while (_t) {
            _t = _t->next;
            _i++;
        }
    }

    /* Closed: always fail (matches goc_put ordering). */
    if (ch->closed) {
        e->ok = GOC_CLOSED;
        uv_mutex_unlock(ch->lock);
        if (e->put_cb) e->put_cb(ch, e->put_val, GOC_CLOSED, e->ud);
        if (e->free_on_drain) free(e);
        return;
    }

    /* Parked taker available: deliver directly. */
    if (chan_put_to_taker_claim(ch, e->put_val, &fe_taker)) {
        e->ok = GOC_OK;
        uv_mutex_unlock(ch->lock);
        if (fe_taker != NULL) {
            long spin = 0;
            while (atomic_load_explicit(&fe_taker->parked, memory_order_acquire) == 0) {
                sched_yield();
            }
            post_to_run_queue(fe_taker->pool, fe_taker);
        }
        if (e->put_cb) e->put_cb(ch, e->put_val, GOC_OK, e->ud);
        if (e->free_on_drain) free(e);
        return;
    }

    /* Buffer space available. */
    if (chan_put_to_buffer(ch, e->put_val)) {
        e->ok = GOC_OK;
        uv_mutex_unlock(ch->lock);
        if (e->put_cb) e->put_cb(ch, e->put_val, GOC_OK, e->ud);
        if (e->free_on_drain) free(e);
        return;
    }

    /* No match yet: park.  wake() → post_callback() will fire put_cb later. */
    chan_list_append(&ch->putters, &ch->putters_tail, e);
    uv_mutex_unlock(ch->lock);
}

static void loop_process_pending_take(goc_entry *e)
{
    goc_chan *ch = e->ch;
    e->ch = NULL;   /* clear so a future drain_cb_queue pass skips this entry */

    void *val = NULL;
    goc_entry *fe_putter = NULL;

    uv_mutex_lock(ch->lock);

    if (ch->dead_count >= GOC_DEAD_COUNT_THRESHOLD)
        compact_dead_entries(ch);

    /* Value available from buffer. */
    if (chan_take_from_buffer(ch, &val)) {
        e->cb_result = val;
        e->ok = GOC_OK;
        uv_mutex_unlock(ch->lock);
        if (e->cb) e->cb(ch, val, GOC_OK, e->ud);
        if (e->free_on_drain) free(e);
        return;
    }

    /* Value available from parked putter. */
    if (chan_take_from_putter_claim(ch, &val, &fe_putter)) {
        e->cb_result = val;
        e->ok = GOC_OK;
        uv_mutex_unlock(ch->lock);
        if (fe_putter != NULL) {
            long spin = 0;
            while (atomic_load_explicit(&fe_putter->parked, memory_order_acquire) == 0) {
                sched_yield();
                if (++spin == 10000000L) {
                    GOC_DBG("loop_process_pending_take: SPIN STALL >10M ch=%p fe_putter=%p\n",
                            (void*)ch, (void*)fe_putter);
                }
            }
            post_to_run_queue(fe_putter->pool, fe_putter);
        }
        if (e->cb) e->cb(ch, val, GOC_OK, e->ud);
        if (e->free_on_drain) free(e);
        return;
    }

    /* Closed and empty. */
    if (ch->closed) {
        e->ok = GOC_CLOSED;
        uv_mutex_unlock(ch->lock);
        if (e->cb) e->cb(ch, NULL, GOC_CLOSED, e->ud);
        if (e->free_on_drain) free(e);
        return;
    }

    /* No match yet: park.  wake() → post_callback() will fire cb later. */
    chan_list_append(&ch->takers, &ch->takers_tail, e);
    uv_mutex_unlock(ch->lock);
}

/* Drain the callback queue and fire pending callbacks (loop thread). */
static void drain_cb_queue(void)
{
    goc_entry *e;
    int iter = 0;
    size_t budget = GOC_CB_DRAIN_BUDGET;
    while (budget-- > 0 && (e = cb_queue_pop()) != NULL) {
        ++iter;
        if (e->kind != GOC_CALLBACK)
            continue;

        /* Pending channel op posted by goc_put_cb / goc_take_cb:
         * e->ch is non-NULL and the entry has not yet been claimed by wake().
         * Process the channel operation here on the loop thread. */
        if (e->ch != NULL &&
            !atomic_load_explicit(&e->woken, memory_order_acquire)) {
            if (e->is_put) {
                loop_process_pending_put(e);
            } else {
                loop_process_pending_take(e);
            }
            continue;
        }

        /* Already claimed by wake(): fire the callback. */
        bool fod = e->free_on_drain;
        if (e->cb)
            e->cb(e->ch, e->cb_result, e->ok, e->ud);
        else if (e->put_cb)
            e->put_cb(e->ch, e->put_val, e->ok, e->ud);
        if (fod) free(e);
    }

    /* More callbacks queued? Yield back to libuv and request another wakeup
     * pass instead of monopolizing the loop thread in one giant drain. */
    if (atomic_load_explicit(&g_cb_queue_depth, memory_order_relaxed) > 0) {
        uv_async_t *w = atomic_load_explicit(&g_wakeup, memory_order_acquire);
        if (w)
            uv_async_send(w);
    }
}

/* on_wakeup — loop thread; drains g_cb_queue and fires callbacks. */
static void on_wakeup(uv_async_t *h)
{
    (void)h;
    goc_uv_callback_enter("on_wakeup");
    GOC_DBG("on_wakeup: entered g_loop=%p g_wakeup_raw=%p active=%d closing=%d loop_alive=%d depth=%zu\n",
            (void*)g_loop,
            (void*)g_wakeup_raw,
            g_wakeup_raw ? uv_is_active((uv_handle_t*)g_wakeup_raw) : -1,
            g_wakeup_raw ? uv_is_closing((uv_handle_t*)g_wakeup_raw) : -1,
            g_loop ? uv_loop_alive(g_loop) : -1,
            atomic_load_explicit(&g_cb_queue_depth, memory_order_relaxed));
    drain_cb_queue_guarded();
    GOC_DBG("on_wakeup: exit g_loop=%p depth=%zu\n",
            (void*)g_loop,
            atomic_load_explicit(&g_cb_queue_depth, memory_order_relaxed));
    goc_uv_callback_exit("on_wakeup");
}

/* on_shutdown_signal — loop thread; fires remaining callbacks, then stops
 * the loop.  Closing all handles and stopping the loop allows uv_run to exit
 * cleanly before the loop is closed on the join side. */
static void on_shutdown_signal(uv_async_t *h)
{
    (void)h;
    goc_uv_callback_enter("on_shutdown_signal");
    GOC_DBG("on_shutdown_signal: entered g_loop=%p g_shutdown_async=%p g_wakeup_raw=%p g_wakeup=%p alive=%d state=%d depth=%zu hwm=%zu\n",
            (void*)g_loop, (void*)g_shutdown_async, (void*)g_wakeup_raw,
            (void*)atomic_load_explicit(&g_wakeup, memory_order_relaxed),
            g_loop ? uv_loop_alive(g_loop) : -1,
            atomic_load_explicit(&g_loop_state, memory_order_relaxed),
            atomic_load_explicit(&g_cb_queue_depth, memory_order_relaxed),
            atomic_load_explicit(&g_cb_queue_hwm, memory_order_relaxed));

    size_t before_count = 0;
    handle_summary_t before_summary = {0, 0, 0, 0};
    uv_walk(g_loop, loop_walk_handle_cb, &before_count);
    uv_walk(g_loop, loop_handle_summary_cb, &before_summary);
    GOC_DBG("on_shutdown_signal: loop handle summary before drain count=%zu async_wakeup=%zu async_shutdown=%zu timer_mgr=%zu other=%zu\n",
            before_count, before_summary.async_wakeup,
            before_summary.async_shutdown,
            before_summary.timer_mgr,
            before_summary.other);

    uv_mutex_lock(&g_loop_submit_lock);
    atomic_store_explicit(&g_loop_state, GOC_LOOP_STATE_SHUTTING_DOWN,
                          memory_order_release);
    uv_mutex_unlock(&g_loop_submit_lock);

    /* Fire any remaining pending callbacks before closing. */
    GOC_DBG("on_shutdown_signal: before drain_cb_queue\n");
    drain_cb_queue_guarded();
    GOC_DBG("on_shutdown_signal: after drain_cb_queue depth=%zu\n",
            atomic_load_explicit(&g_cb_queue_depth, memory_order_relaxed));

    /* Shut down the central timer manager: close remaining timeout channels
     * and close the backing uv_timer_t before uv_walk sweeps up other handles. */
    GOC_DBG("on_shutdown_signal: timer manager shutdown\n");
    goc_timer_manager_shutdown();

    size_t after_count = 0;
    handle_summary_t after_summary = {0, 0, 0, 0};
    uv_walk(g_loop, loop_walk_handle_cb, &after_count);
    uv_walk(g_loop, loop_handle_summary_cb, &after_summary);
    GOC_DBG("on_shutdown_signal: loop handle summary after timer shutdown count=%zu async_wakeup=%zu async_shutdown=%zu timer_mgr=%zu other=%zu\n",
            after_count, after_summary.async_wakeup,
            after_summary.async_shutdown,
            after_summary.timer_mgr,
            after_summary.other);

    GOC_DBG("on_shutdown_signal: shutdown stop requested\n");
    uv_stop(g_loop);
    GOC_DBG("on_shutdown_signal: after uv_stop g_loop=%p alive=%d g_wakeup_raw=%p active=%d closing=%d g_shutdown_async=%p active=%d closing=%d\n",
            (void*)g_loop,
            g_loop ? uv_loop_alive(g_loop) : -1,
            (void*)g_wakeup_raw,
            g_wakeup_raw ? uv_is_active((uv_handle_t*)g_wakeup_raw) : -1,
            g_wakeup_raw ? uv_is_closing((uv_handle_t*)g_wakeup_raw) : -1,
            (void*)g_shutdown_async,
            g_shutdown_async ? uv_is_active((uv_handle_t*)g_shutdown_async) : -1,
            g_shutdown_async ? uv_is_closing((uv_handle_t*)g_shutdown_async) : -1);

    if (g_shutdown_async) {
        GOC_DBG("on_shutdown_signal: shutdown_async remains active until close shutdown_async=%p\n",
                (void*)g_shutdown_async);
    }

    GOC_DBG("on_shutdown_signal: exit state=%d depth=%zu hwm=%zu\n",
            atomic_load_explicit(&g_loop_state, memory_order_relaxed),
            atomic_load_explicit(&g_cb_queue_depth, memory_order_relaxed),
            atomic_load_explicit(&g_cb_queue_hwm, memory_order_relaxed));
    goc_uv_callback_exit("on_shutdown_signal");
}

/* --------------------------------------------------------------------------
 * Loop thread entry point
 * -------------------------------------------------------------------------- */

static void loop_thread_fn(void *arg)
{
    (void)arg;
    long iter = 0;
    static _Atomic int last_loop_iter = 0;
    while (1) {
        ++iter;
        if (iter % 500 == 0) {
            GOC_DBG("loop_thread_fn: before UV_RUN iter=%ld alive=%d state=%d\n",
                    iter, uv_loop_alive(g_loop),
                    atomic_load_explicit(&g_loop_state, memory_order_relaxed));
        }
        GOC_DBG("loop_thread_fn: uv_run(ONCE) about to enter iter=%ld g_loop=%p state=%d\n",
                iter, (void*)g_loop,
                atomic_load_explicit(&g_loop_state, memory_order_relaxed));
        size_t pre_loop_count = 0;
        handle_summary_t pre_summary = {0, 0, 0, 0};
        uv_walk(g_loop, loop_walk_handle_cb, &pre_loop_count);
        uv_walk(g_loop, loop_handle_summary_cb, &pre_summary);
        bool pre_internal_only = pre_loop_count <= 3 &&
            pre_summary.async_wakeup == 1 &&
            pre_summary.async_shutdown == 1 &&
            pre_summary.timer_mgr == 1 &&
            pre_summary.other == 0;
        if (iter >= 10 && iter <= 15 || pre_loop_count <= 4 ||
            (pre_summary.async_wakeup + pre_summary.async_shutdown + pre_summary.timer_mgr) <= 3) {
            dump_loop_internal_state("loop_thread_fn: pre-uv_run(ONCE)", iter);
        }
        goc_uv_walk_log(g_loop, "loop_thread_fn before uv_run");
        if (iter % 100 == 0) {
            goc_uv_walk_log(g_loop, "loop_thread_fn periodic");
        }
        if (pre_internal_only) {
            dump_loop_internal_state("loop_thread_fn: fatal-window pre-uv_run(ONCE)", iter);
        }
        int ret = uv_run(g_loop, UV_RUN_ONCE);
        size_t post_loop_count = 0;
        handle_summary_t post_summary = {0, 0, 0, 0};
        uv_walk(g_loop, loop_walk_handle_cb, &post_loop_count);
        uv_walk(g_loop, loop_handle_summary_cb, &post_summary);
        bool post_internal_only = post_loop_count <= 3 &&
            post_summary.async_wakeup == 1 &&
            post_summary.async_shutdown == 1 &&
            post_summary.timer_mgr == 1 &&
            post_summary.other == 0;
        if (iter >= 10 && iter <= 15 || post_loop_count <= 4 ||
            (post_summary.async_wakeup + post_summary.async_shutdown + post_summary.timer_mgr) <= 3) {
            dump_loop_internal_state("loop_thread_fn: post-uv_run(ONCE)", iter);
        }
        if (post_internal_only) {
            dump_loop_internal_state("loop_thread_fn: fatal-window post-uv_run(ONCE)", iter);
        }
        GOC_DBG("loop_thread_fn: uv_run(ONCE) returned ret=%d iter=%ld g_loop=%p alive=%d state=%d depth=%zu\n",
                ret, iter, (void*)g_loop, uv_loop_alive(g_loop),
                atomic_load_explicit(&g_loop_state, memory_order_relaxed),
                atomic_load_explicit(&g_cb_queue_depth, memory_order_relaxed));
        if (iter % 500 == 0) {
            GOC_DBG("loop_thread_fn: after UV_RUN iter=%ld ret=%d depth=%zu\n",
                    iter, ret, atomic_load_explicit(&g_cb_queue_depth, memory_order_relaxed));
        }
        goc_deferred_handle_unreg_flush();
        goc_deferred_close_flush();
        /* Safe point: event pass complete, no libuv callbacks running, no
         * ch->lock held.  Drive incremental GC collection. */
        GC_collect_a_little();
        atomic_store_explicit(&last_loop_iter, iter, memory_order_relaxed);
        if (atomic_load_explicit(&g_loop_state, memory_order_acquire) == GOC_LOOP_STATE_SHUTTING_DOWN) {
            GOC_DBG("loop_thread_fn: shutdown requested after uv_run(ONCE) ret=%d iter=%ld g_loop=%p alive=%d state=%d\n",
                    ret, iter, (void*)g_loop, uv_loop_alive(g_loop),
                    atomic_load_explicit(&g_loop_state, memory_order_relaxed));
            break;
        }
        if (!ret) break;
    }

    if (atomic_load_explicit(&g_loop_state, memory_order_acquire) == GOC_LOOP_STATE_SHUTTING_DOWN) {
            GOC_DBG("loop_thread_fn: shutdown cleanup begin loop=%p g_wakeup_raw=%p g_shutdown_async=%p state=%d\n",
                    (void*)g_loop, (void*)g_wakeup_raw, (void*)g_shutdown_async,
                    atomic_load_explicit(&g_loop_state, memory_order_relaxed));
            size_t pre_close_count = 0;
            uv_walk(g_loop, loop_walk_handle_cb, &pre_close_count);
            GOC_DBG("loop_thread_fn: pre-close handle count=%zu g_loop=%p alive=%d state=%d\n",
                    pre_close_count, (void*)g_loop, uv_loop_alive(g_loop),
                    atomic_load_explicit(&g_loop_state, memory_order_relaxed));

            if (g_wakeup_raw && !uv_is_closing((uv_handle_t*)g_wakeup_raw)) {
                atomic_store_explicit(&g_wakeup, NULL, memory_order_release);
                GOC_DBG("loop_thread_fn: cleared g_wakeup before closing g_wakeup_raw=%p\n",
                        (void*)g_wakeup_raw);
                goc_uv_close_log("loop_thread_fn close g_wakeup_raw", (uv_handle_t*)g_wakeup_raw);
                GOC_DBG("loop_thread_fn: closing g_wakeup_raw=%p active=%d closing=%d loop_alive=%d\n",
                        (void*)g_wakeup_raw,
                        uv_is_active((uv_handle_t*)g_wakeup_raw),
                        uv_is_closing((uv_handle_t*)g_wakeup_raw),
                        g_loop ? uv_loop_alive(g_loop) : -1);
                goc_uv_close_internal((uv_handle_t*)g_wakeup_raw, on_wakeup_closed);
            }
            if (g_shutdown_async && !uv_is_closing((uv_handle_t*)g_shutdown_async)) {
                goc_uv_close_log("loop_thread_fn close g_shutdown_async", (uv_handle_t*)g_shutdown_async);
                GOC_DBG("loop_thread_fn: closing g_shutdown_async=%p active=%d closing=%d loop_alive=%d\n",
                        (void*)g_shutdown_async,
                        uv_is_active((uv_handle_t*)g_shutdown_async),
                        uv_is_closing((uv_handle_t*)g_shutdown_async),
                        g_loop ? uv_loop_alive(g_loop) : -1);
                goc_uv_close_internal((uv_handle_t*)g_shutdown_async, free_handle_cb);
            }

            GOC_DBG("loop_thread_fn: before close_remaining_handle_cb walk g_loop=%p alive=%d\n",
                    (void*)g_loop, uv_loop_alive(g_loop));
            uv_walk(g_loop, close_remaining_handle_cb, NULL);
            GOC_DBG("loop_thread_fn: after close_remaining_handle_cb walk g_loop=%p alive=%d\n",
                    (void*)g_loop, uv_loop_alive(g_loop));

            while (uv_loop_alive(g_loop)) {
                size_t loop_count = 0;
                uv_walk(g_loop, loop_walk_handle_cb, &loop_count);
                GOC_DBG("loop_thread_fn: before shutdown uv_run DEFAULT loop=%p alive=%d handle_count=%zu state=%d\n",
                        (void*)g_loop, uv_loop_alive(g_loop), loop_count,
                        atomic_load_explicit(&g_loop_state, memory_order_relaxed));
                int run_rc = uv_run(g_loop, UV_RUN_DEFAULT);
                GOC_DBG("loop_thread_fn: after shutdown uv_run DEFAULT rc=%d loop=%p alive=%d state=%d\n",
                        run_rc, (void*)g_loop, uv_loop_alive(g_loop),
                        atomic_load_explicit(&g_loop_state, memory_order_relaxed));
                if (run_rc <= 0 && uv_loop_alive(g_loop)) {
                    size_t still_alive_count = 0;
                    uv_walk(g_loop, loop_walk_handle_cb, &still_alive_count);
                    GOC_DBG("loop_thread_fn: uv_run DEFAULT returned %d but loop still alive loop=%p alive=%d handle_count=%zu\n",
                            run_rc, (void*)g_loop, uv_loop_alive(g_loop), still_alive_count);
                }
            }

            size_t post_close_count = 0;
            uv_walk(g_loop, loop_walk_handle_cb, &post_close_count);
            GOC_DBG("loop_thread_fn: shutdown cleanup complete loop=%p alive=%d post_close_count=%zu\n",
                    (void*)g_loop, uv_loop_alive(g_loop), post_close_count);
        }
    GOC_DBG("loop_thread_fn: loop EXITED iter=%ld\n", iter);
}

/* --------------------------------------------------------------------------
 * loop_init / loop_shutdown — internal (declared in internal.h)
 * -------------------------------------------------------------------------- */

void loop_init(void)
{
    uv_mutex_init(&g_loop_submit_lock);
    atomic_store_explicit(&g_loop_state, GOC_LOOP_STATE_STOPPED,
                          memory_order_relaxed);

    /* 1. Allocate and initialise the event loop. */
    g_loop = (uv_loop_t *)malloc(sizeof(uv_loop_t));
    assert(g_loop);
    int rc = uv_loop_init(g_loop);
    GOC_DBG("[uv_init] uv_loop_init rc=%d loop=%p thread=%llu\n",
            rc, (void*)g_loop, goc_uv_thread_id());

    /* 2. Wakeup async handle. */
    g_wakeup_raw = (uv_async_t *)malloc(sizeof(uv_async_t));
    assert(g_wakeup_raw);
    rc = uv_async_init(g_loop, g_wakeup_raw, on_wakeup);
    goc_uv_init_log("uv_async_init wakeup", rc, g_loop, (uv_handle_t*)g_wakeup_raw);

    /* 3. Publish wakeup pointer atomically. */
    atomic_store_explicit(&g_wakeup, g_wakeup_raw, memory_order_release);
    GOC_DBG("loop_init: published g_wakeup=%p\n", (void*)g_wakeup_raw);

    /* 4. Shutdown async handle. */
    g_shutdown_async = (uv_async_t *)malloc(sizeof(uv_async_t));
    assert(g_shutdown_async);
    rc = uv_async_init(g_loop, g_shutdown_async, on_shutdown_signal);
    goc_uv_init_log("uv_async_init shutdown", rc, g_loop, (uv_handle_t*)g_shutdown_async);

    /* 5. Initialise the MPSC callback queue. */
    cb_queue_init();

    /* 6. Initialise the central timer manager (one uv_timer_t for all timeouts). */
    goc_timer_manager_init();

    atomic_store_explicit(&g_loop_state, GOC_LOOP_STATE_RUNNING,
                          memory_order_release);
    GOC_DBG("loop_init: g_loop_state transitioned to RUNNING g_loop=%p\n",
            (void*)g_loop);

    /* 7. Spawn the loop thread (GC-registered on all platforms via
     *    goc_thread_create — see internal.h). */
    goc_thread_create(&g_loop_thread, loop_thread_fn, NULL);
}

void loop_shutdown(void)
{
    GOC_DBG("loop_shutdown: begin g_loop=%p shutdown_async=%p depth=%zu hwm=%zu state=%d\n",
            (void*)g_loop, (void*)g_shutdown_async,
            atomic_load_explicit(&g_cb_queue_depth, memory_order_relaxed),
            atomic_load_explicit(&g_cb_queue_hwm, memory_order_relaxed),
            atomic_load_explicit(&g_loop_state, memory_order_relaxed));
    GOC_DBG("loop_shutdown: sending shutdown async\n");

    int state_before;
    uv_mutex_lock(&g_loop_submit_lock);
    state_before = atomic_load_explicit(&g_loop_state, memory_order_acquire);
    if (state_before == GOC_LOOP_STATE_RUNNING) {
        atomic_store_explicit(&g_loop_state, GOC_LOOP_STATE_SHUTTING_DOWN,
                              memory_order_release);
    } else {
        GOC_DBG("loop_shutdown: already shutting down or stopped g_loop=%p state=%d\n",
                (void*)g_loop, state_before);
    }
    uv_mutex_unlock(&g_loop_submit_lock);

    bool shutdown_sent = false;
    if (atomic_exchange_explicit(&g_shutdown_requested, 1, memory_order_acq_rel) == 0) {
        shutdown_sent = true;
    } else {
        GOC_DBG("loop_shutdown: shutdown already requested, skipping uv_async_send shutdown_async=%p\n",
                (void*)g_shutdown_async);
    }
    GOC_DBG("loop_shutdown: g_loop_state set to SHUTTING_DOWN g_loop=%p state=%d g_shutdown_async=%p active=%d closing=%d shutdown_sent=%d\n",
            (void*)g_loop,
            atomic_load_explicit(&g_loop_state, memory_order_relaxed),
            (void*)g_shutdown_async,
            g_shutdown_async ? uv_is_active((uv_handle_t*)g_shutdown_async) : -1,
            g_shutdown_async ? uv_is_closing((uv_handle_t*)g_shutdown_async) : -1,
            shutdown_sent);

    /* Signal the loop thread to drain, close handles, and exit. */
    GOC_DBG("loop_shutdown: before uv_async_send shutdown_async=%p active=%d closing=%d timer_heap_len=%zu\n",
            (void*)g_shutdown_async,
            g_shutdown_async ? uv_is_active((uv_handle_t*)g_shutdown_async) : -1,
            g_shutdown_async ? uv_is_closing((uv_handle_t*)g_shutdown_async) : -1,
            g_timer_heap_len);
    int rcs = UV_EINVAL;
    if (shutdown_sent) {
        if (g_shutdown_async && !uv_is_closing((uv_handle_t*)g_shutdown_async)) {
            rcs = uv_async_send(g_shutdown_async);
        } else {
            GOC_DBG("loop_shutdown: shutdown_async closing or missing, skipping uv_async_send shutdown_async=%p active=%d closing=%d\n",
                    (void*)g_shutdown_async,
                    g_shutdown_async ? uv_is_active((uv_handle_t*)g_shutdown_async) : -1,
                    g_shutdown_async ? uv_is_closing((uv_handle_t*)g_shutdown_async) : -1);
        }
    } else {
        GOC_DBG("loop_shutdown: shutdown send suppressed because request already pending shutdown_async=%p\n",
                (void*)g_shutdown_async);
    }
    GOC_DBG("loop_shutdown: shutdown send rc=%d g_shutdown_async=%p active=%d closing=%d timer_heap_len=%zu\n", rcs,
            (void*)g_shutdown_async,
            g_shutdown_async ? uv_is_active((uv_handle_t*)g_shutdown_async) : -1,
            g_shutdown_async ? uv_is_closing((uv_handle_t*)g_shutdown_async) : -1,
            g_timer_heap_len);

    GOC_DBG("loop_shutdown: joining loop thread\n");
    /* Wait for the loop thread to finish. */
    goc_thread_join(&g_loop_thread);
    GOC_DBG("loop_shutdown: joined g_loop=%p shutdown_async=%p depth=%zu hwm=%zu state=%d\n",
            (void*)g_loop, (void*)g_shutdown_async,
            atomic_load_explicit(&g_cb_queue_depth, memory_order_relaxed),
            atomic_load_explicit(&g_cb_queue_hwm, memory_order_relaxed),
            atomic_load_explicit(&g_loop_state, memory_order_relaxed));
    GOC_DBG("loop_shutdown: done\n");

    /* All handles are closed; the loop should be idle. */
    size_t walk_handles = 0;
    uv_walk(g_loop, close_remaining_handle_cb, &walk_handles);
    GOC_DBG("loop_shutdown: after uv_walk g_loop=%p alive=%d walk_handles=%zu state=%d\n",
            (void*)g_loop,
            uv_loop_alive(g_loop),
            walk_handles,
            atomic_load_explicit(&g_loop_state, memory_order_relaxed));
    size_t loop_shutdown_count = 0;
    uv_walk(g_loop, loop_walk_handle_cb, &loop_shutdown_count);
    GOC_DBG("loop_shutdown: pre-final uv_run handle count=%zu g_loop=%p alive=%d\n",
            loop_shutdown_count, (void*)g_loop, uv_loop_alive(g_loop));
    GOC_DBG("loop_shutdown: final uv_run(NOWAIT) before uv_loop_close g_loop=%p\n",
            (void*)g_loop);
    g_loop->stop_flag = 1;
    uv_run(g_loop, UV_RUN_NOWAIT);

    int abandon_loop_close = 0;
    while (true) {
        size_t close_handle_count = 0;
        uv_walk(g_loop, loop_walk_handle_cb, &close_handle_count);
        int pre_close_alive = uv_loop_alive(g_loop);
        GOC_DBG("loop_shutdown: before uv_loop_close g_loop=%p alive=%d handle_count=%zu\n",
                (void*)g_loop, pre_close_alive, close_handle_count);
        if (!pre_close_alive && close_handle_count == 0) {
            GOC_DBG("loop_shutdown: loop is already dead with no handles before uv_loop_close, skipping close g_loop=%p\n",
                    (void*)g_loop);
            break;
        }
        int loop_close_rc = uv_loop_close(g_loop);
        int loop_alive = uv_loop_alive(g_loop);
        GOC_DBG("loop_shutdown: uv_loop_close g_loop=%p rc=%d alive=%d depth=%zu hwm=%zu state=%d\n",
                (void*)g_loop,
                loop_close_rc,
                loop_alive,
                atomic_load_explicit(&g_cb_queue_depth, memory_order_relaxed),
                atomic_load_explicit(&g_cb_queue_hwm, memory_order_relaxed),
                atomic_load_explicit(&g_loop_state, memory_order_relaxed));

        if (loop_close_rc == 0 && !loop_alive)
            break;

        if (loop_close_rc == 0 && loop_alive) {
            GOC_DBG("loop_shutdown: uv_loop_close returned 0 but loop still alive, abandoning close and preserving g_loop=%p alive=%d\n",
                    (void*)g_loop, loop_alive);
            abandon_loop_close = true;
            break;
        }

        if (loop_close_rc == UV_EBUSY) {
            size_t busy_handle_count = 0;
            GOC_DBG("loop_shutdown: uv_loop_close busy, dumping remaining handles g_loop=%p\n",
                    (void*)g_loop);
            uv_walk(g_loop, loop_walk_handle_cb, &busy_handle_count);
            GOC_DBG("loop_shutdown: uv_loop_close busy handle_count=%zu g_loop=%p\n",
                    busy_handle_count, (void*)g_loop);
        }

        if (loop_close_rc != 0 && loop_close_rc != UV_EBUSY) {
            GOC_DBG("loop_shutdown: uv_loop_close unexpected rc=%d g_loop=%p alive=%d\n",
                    loop_close_rc, (void*)g_loop, uv_loop_alive(g_loop));
            break;
        }

        uv_walk(g_loop, close_remaining_handle_cb, NULL);
        while (uv_loop_alive(g_loop)) {
            size_t loop_count = 0;
            uv_walk(g_loop, loop_walk_handle_cb, &loop_count);
            GOC_DBG("loop_shutdown: retry uv_run DEFAULT g_loop=%p alive=%d handle_count=%zu\n",
                    (void*)g_loop, uv_loop_alive(g_loop), loop_count);
            int run_rc = uv_run(g_loop, UV_RUN_DEFAULT);
            GOC_DBG("loop_shutdown: retry uv_run DEFAULT rc=%d g_loop=%p alive=%d\n",
                    run_rc, (void*)g_loop, uv_loop_alive(g_loop));
            uv_walk(g_loop, close_remaining_handle_cb, NULL);
        }
    }

    uv_mutex_destroy(&g_loop_submit_lock);
    atomic_store_explicit(&g_loop_state, GOC_LOOP_STATE_STOPPED,
                          memory_order_release);
    if (!abandon_loop_close) {
        free(g_loop);
        g_loop = NULL;
    } else {
        GOC_DBG("loop_shutdown: preserved g_loop=%p after abandoning uv_loop_close\n",
                (void*)g_loop);
    }
}
