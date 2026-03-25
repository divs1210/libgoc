#include <stdatomic.h>
#include <string.h>
#include <time.h>
#include <uv.h>
#include "../include/goc_stats.h"
#include <stdio.h>

#ifdef GOC_ENABLE_STATS

static _Atomic int        stats_enabled  = 0;
static _Atomic int        mutex_inited   = 0;
static uv_mutex_t         g_cb_mutex;
static goc_stats_callback g_cb  = NULL;
static void*              g_cb_ud = NULL;

static uint64_t goc_stats_now(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void goc_stats_dispatch(const struct goc_stats_event* ev) {
    if (!atomic_load_explicit(&stats_enabled, memory_order_acquire)) return;

    /* Read callback under lock, then call without holding it. */
    uv_mutex_lock(&g_cb_mutex);
    goc_stats_callback cb = g_cb;
    void* ud               = g_cb_ud;
    uv_mutex_unlock(&g_cb_mutex);

    if (cb) cb(ev, ud);
}

void goc_stats_submit_event_pool(void* id, int status, int thread_count) {
    struct goc_stats_event ev;
    ev.type = GOC_STATS_EVENT_POOL_STATUS;
    ev.timestamp = goc_stats_now();
    ev.data.pool.id = id;
    ev.data.pool.status = status;
    ev.data.pool.thread_count = thread_count;
    goc_stats_dispatch(&ev);
}

void goc_stats_submit_event_worker(int id, int status, int pending_jobs) {
    struct goc_stats_event ev;
    ev.type                  = GOC_STATS_EVENT_WORKER_STATUS;
    ev.timestamp             = goc_stats_now();
    ev.data.worker.id        = id;
    ev.data.worker.status    = status;
    ev.data.worker.pending_jobs = pending_jobs;
    goc_stats_dispatch(&ev);
}

void goc_stats_submit_event_fiber(int id, int last_worker_id, int status) {
    struct goc_stats_event ev;
    ev.type                       = GOC_STATS_EVENT_FIBER_STATUS;
    ev.timestamp                  = goc_stats_now();
    ev.data.fiber.id              = id;
    ev.data.fiber.last_worker_id  = last_worker_id;
    ev.data.fiber.status          = status;
    goc_stats_dispatch(&ev);
}

void goc_stats_submit_event_channel(int id, int status, int buf_size, int item_count) {
    struct goc_stats_event ev;
    ev.type                    = GOC_STATS_EVENT_CHANNEL_STATUS;
    ev.timestamp               = goc_stats_now();
    ev.data.channel.id         = id;
    ev.data.channel.status     = status;
    ev.data.channel.buf_size   = buf_size;
    ev.data.channel.item_count = item_count;
    goc_stats_dispatch(&ev);
}

// Default callback: prints event to stdout in a readable format
static void goc_stats_default_callback(const struct goc_stats_event* ev, void* ud) {
    (void)ud;
    const char* type = "?";
    switch (ev->type) {
        case GOC_STATS_EVENT_POOL_STATUS:    type = "POOL"; break;
        case GOC_STATS_EVENT_WORKER_STATUS:  type = "WORKER"; break;
        case GOC_STATS_EVENT_FIBER_STATUS:   type = "FIBER"; break;
        case GOC_STATS_EVENT_CHANNEL_STATUS: type = "CHANNEL"; break;
    }
    printf("[goc_stats] %s @ %llu: ", type, (unsigned long long)ev->timestamp);
    switch (ev->type) {
        case GOC_STATS_EVENT_POOL_STATUS:
            printf("pool=%p status=%d threads=%d\n", ev->data.pool.id, ev->data.pool.status, ev->data.pool.thread_count);
            break;
        case GOC_STATS_EVENT_WORKER_STATUS:
            printf("id=%d status=%d pending=%d\n", ev->data.worker.id, ev->data.worker.status, ev->data.worker.pending_jobs);
            break;
        case GOC_STATS_EVENT_FIBER_STATUS:
            printf("id=%d last_worker=%d status=%d\n", ev->data.fiber.id, ev->data.fiber.last_worker_id, ev->data.fiber.status);
            break;
        case GOC_STATS_EVENT_CHANNEL_STATUS:
            printf("id=%d status=%d buf_size=%d item_count=%d\n", ev->data.channel.id, ev->data.channel.status, ev->data.channel.buf_size, ev->data.channel.item_count);
            break;
        default:
            printf("(unknown event)\n");
    }
}

void goc_stats_set_callback(goc_stats_callback cb, void* ud) {
    uv_mutex_lock(&g_cb_mutex);
    if (cb) {
        g_cb = cb;
        g_cb_ud = ud;
    } else {
        g_cb = NULL;
        g_cb_ud = NULL;
    }
    uv_mutex_unlock(&g_cb_mutex);
}

void goc_stats_init(void) {
    if (atomic_load_explicit(&stats_enabled, memory_order_acquire)) return;
    /* Initialize the mutex exactly once across init/shutdown cycles. */
    int expected = 0;
    if (atomic_compare_exchange_strong_explicit(
            &mutex_inited, &expected, 1,
            memory_order_acq_rel, memory_order_acquire)) {
        uv_mutex_init(&g_cb_mutex);
    }
    uv_mutex_lock(&g_cb_mutex);
    g_cb = goc_stats_default_callback;
    g_cb_ud = NULL;
    uv_mutex_unlock(&g_cb_mutex);
    atomic_store_explicit(&stats_enabled, 1, memory_order_release);
}

void goc_stats_shutdown(void) {
    /* Disable delivery first so new callers short-circuit before locking. */
    atomic_store_explicit(&stats_enabled, 0, memory_order_release);
    /* Clear the callback under the lock so any thread that slipped past the
     * stats_enabled check above and is waiting on the mutex will read NULL
     * and call nothing after acquiring it. */
    uv_mutex_lock(&g_cb_mutex);
    g_cb    = NULL;
    g_cb_ud = NULL;
    uv_mutex_unlock(&g_cb_mutex);
    /* Do NOT destroy the mutex here. A thread that passed the stats_enabled
     * check before shutdown ran may still be waiting to acquire it.
     * Destroying the mutex while another thread is about to lock it causes
     * libuv to abort. The mutex is a process-global and is reclaimed on exit. */
}

bool goc_stats_is_enabled(void) {
    return atomic_load_explicit(&stats_enabled, memory_order_acquire) != 0;
}

#else /* !GOC_ENABLE_STATS */

void goc_stats_set_callback(goc_stats_callback cb, void* ud) { (void)cb; (void)ud; }
void goc_stats_init(void)     {}
void goc_stats_shutdown(void) {}
bool goc_stats_is_enabled(void) { return false; }

#endif /* GOC_ENABLE_STATS */
