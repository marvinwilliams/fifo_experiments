#pragma once

// C interface to the d-CBO of the 2D framework (contenders/2D in the BlockFIFO repository). The original headers
// define many generic macros, so C++ code only includes this header.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// d-CBO with `num_queues` LCRQs, choosing the best of `choices` random queues per operation
void* dcbo_create(uint32_t num_queues, uint32_t choices, int num_threads);

// Initializes the thread-local state (allocator, random seeds) of the calling thread
void dcbo_thread_init(void* queue, int thread_id);
void dcbo_thread_free(void);

// Values must not be 0 (empty) or UINT64_MAX (empty LCRQ cell)
int dcbo_enqueue(void* queue, uint64_t value);
// Returns 0 if empty
uint64_t dcbo_dequeue(void* queue);

#ifdef __cplusplus
}
#endif
