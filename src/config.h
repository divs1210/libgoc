#ifndef GOC_CONFIG_H
#define GOC_CONFIG_H

#define GOC_DEAD_COUNT_THRESHOLD 8
#define GOC_ALTS_STACK_THRESHOLD 8

#ifndef LIBGOC_VMEM_ENABLED
/* Maximum number of idle mco_coro* objects retained per worker thread.
 * At 56 KB per slot, each worker retains at most 64 × 56 KB = 3.5 MB.
 * With 8 workers the process-wide cap is ~28 MB. */
#  define GOC_CORO_POOL_MAX  64
#endif

#endif /* GOC_CONFIG_H */
