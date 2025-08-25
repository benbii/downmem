#include <defs.h>
#ifndef ALL_THREADS_BARRIER_INIT
#define ALL_THREADS_BARRIER_INIT() BARRIER_INIT(bar_allthrd, NR_TASKLETS)
#define all_threads_barrier_wait() barrier_wait(&bar_allthrd)
#endif