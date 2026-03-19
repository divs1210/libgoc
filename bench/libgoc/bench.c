#include "goc.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <uv.h>

// Helpers
// =======



// 1. Ping Pong Benchmark
// ======================
static void bench_ping_pong(size_t ping_rounds) {
}


// 2. Ring Benchmark
// =================


// 3. Selective Receive / Fan-out / Fan-in Benchmark
// =================================================



// 4. Spawn / Join Benchmark
// =========================



// 5. Prime Sieve Benchmark
// ========================


// Main
// ====
int main(void) {
    goc_init();

    const size_t ping_rounds = 200000;
    const size_t ring_nodes = 128;
    const size_t ring_hops = 500000;
    const size_t select_workers = 8;
    const size_t select_tasks = 200000;
    const size_t spawn_tasks = 200000;
    const size_t prime_max = 20000;

    bench_ping_pong(ping_rounds);
    // bench_ring(ring_nodes, ring_hops);
    // bench_select_fan(select_workers, select_tasks);
    // bench_spawn(spawn_tasks);
    // bench_primes(prime_max);

    goc_shutdown();
    return 0;
}
