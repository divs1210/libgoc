/*
 * goc_stats.h — Public API for libgoc telemetry
 *
 * Events are delivered synchronously via a registered callback.  The callback
 * is called on whichever thread emits the event (pool workers, main thread,
 * etc.) so it must be async-signal-safe with respect to the caller: keep it
 * short and non-blocking.  Use a mutex + buffer in the callback to hand off to
 * a consumer thread if heavier processing is needed.
 *
 * Typical usage:
 *   goc_stats_init();
 *   goc_stats_set_callback(my_cb, my_ud);
 *   // ... run workload ...
 *   goc_stats_shutdown();
 */

#ifndef GOC_STATS_H
#define GOC_STATS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Event types
 * ---------------------------------------------------------------------- */


enum goc_stats_event_type {
    GOC_STATS_EVENT_POOL_STATUS,
    GOC_STATS_EVENT_WORKER_STATUS,
    GOC_STATS_EVENT_FIBER_STATUS,
    GOC_STATS_EVENT_CHANNEL_STATUS,
};

enum goc_stats_pool_status {
    GOC_POOL_CREATED = 0,
    GOC_POOL_DESTROYED = 1,
};

enum goc_stats_worker_status {
    GOC_WORKER_CREATED = 0,
    GOC_WORKER_RUNNING = 1,
    GOC_WORKER_IDLE    = 2,
    GOC_WORKER_STOPPED = 3,
};

enum goc_stats_fiber_status {
    GOC_FIBER_CREATED   = 0,
    GOC_FIBER_COMPLETED = 1,
};

struct goc_stats_event {
    enum goc_stats_event_type type;
    uint64_t timestamp;
    union {
        struct { void* id; int status; int thread_count; } pool;
        struct { int id; void* pool_id; int status; int pending_jobs; } worker;
        struct { int id; int last_worker_id; int status; } fiber;
        struct { int id; int status; int buf_size; int item_count; } channel;
    } data;
};

/* -------------------------------------------------------------------------
 * Callback API
 *
 * goc_stats_set_callback — install (or clear) the event callback.
 *   cb  : function called for every emitted event; NULL to unregister.
 *   ud  : opaque pointer forwarded to cb unchanged.
 *
 * The callback is called with no internal locks held.  It may be replaced
 * or cleared at any time (thread-safe).  Setting NULL disables delivery.
 * ---------------------------------------------------------------------- */

typedef void (*goc_stats_callback)(const struct goc_stats_event* ev, void* ud);

void goc_stats_set_callback(goc_stats_callback cb, void* ud);

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

void goc_stats_init(void);
void goc_stats_shutdown(void);
bool goc_stats_is_enabled(void);

/* -------------------------------------------------------------------------
 * Internal emit functions (called by macros below; not for direct use)
 * ---------------------------------------------------------------------- */

#ifdef GOC_ENABLE_STATS
void goc_stats_submit_event_pool(void* id, int status, int thread_count);
void goc_stats_submit_event_worker(int id, void* pool_id, int status, int pending_jobs);
void goc_stats_submit_event_fiber(int id, int last_worker_id, int status);
void goc_stats_submit_event_channel(int id, int status, int buf_size, int item_count);
#endif

/* -------------------------------------------------------------------------
 * Emission macros (no-op unless GOC_ENABLE_STATS is defined)
 * ---------------------------------------------------------------------- */

#ifdef GOC_ENABLE_STATS
#  define GOC_STATS_POOL_STATUS(id, status, thread_count) \
    goc_stats_submit_event_pool((id), (status), (thread_count))
#  define GOC_STATS_WORKER_STATUS(id, pool_id, status, pending_jobs) \
    goc_stats_submit_event_worker((id), (pool_id), (status), (pending_jobs))
#  define GOC_STATS_FIBER_STATUS(id, last_worker_id, status) \
    goc_stats_submit_event_fiber((id), (last_worker_id), (status))
#  define GOC_STATS_CHANNEL_STATUS(id, status, buf_size, item_count) \
    goc_stats_submit_event_channel((id), (status), (buf_size), (item_count))
#else
#  define GOC_STATS_POOL_STATUS(id, status, thread_count)            ((void)0)
#  define GOC_STATS_WORKER_STATUS(id, pool_id, status, pending_jobs) ((void)0)
#  define GOC_STATS_FIBER_STATUS(id, last_worker_id, status)         ((void)0)
#  define GOC_STATS_CHANNEL_STATUS(id, status, buf_size, item_count) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* GOC_STATS_H */
