#include "dcbo_shim.h"

#include "d-balanced-queue.h"

#include <stdlib.h>

// Thread-local variables the d-CBO sources expect from the 2D framework's test driver (test.c)
__thread unsigned long* seeds;
__thread unsigned long my_put_cas_fail_count;
__thread unsigned long my_get_cas_fail_count;
__thread unsigned long my_null_count;
__thread unsigned long my_hop_count;
__thread unsigned long my_slide_count;
__thread int thread_id;

void* dcbo_create(uint32_t num_queues, uint32_t choices, int num_threads) {
    return create_queue_dcbo(num_queues, choices, num_threads);
}

void dcbo_thread_init(void* queue, int id) {
    thread_id = id;
    free(seeds);
    seeds = seed_rand();
    d_balanced_register((dbco_queue*)queue, id);
}

void dcbo_thread_free(void) {
    d_balanced_free();
    free(seeds);
    seeds = NULL;
}

int dcbo_enqueue(void* queue, uint64_t value) {
    return enqueue_dcbo((dbco_queue*)queue, value, value);
}

uint64_t dcbo_dequeue(void* queue) {
    return dequeue_dcbo((dbco_queue*)queue);
}
