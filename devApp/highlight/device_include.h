#pragma once
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

struct ImidpuMems {
  uint8_t *wram;
  uint8_t *mram;
  atomic_size_t wramUsed;
  uint8_t *ownGlobals; // a copy of globals owned by this DPU
  // Imitated intra-DPU inter-tasklet handshakes
  pthread_mutex_t shakeMuts[24];
  pthread_cond_t shakeConds[24];
  int shakeIsWaiting[24];
};

typedef unsigned int sysname_t;
#define DPU_MRAM_HEAP_POINTER (void*)_Imidpu->mram
#define __dma_aligned __aligned(8)
#define __mram_ptr
#define __host

// Common UPMEM functions
#define main UpmemInitTid(size_t id) {_Imidpu_Tid = id; return 0;} \
  int UpmemRoutine
#define me() _Imidpu_Tid
#define mem_alloc(size) (_Imidpu->wram + \
  atomic_fetch_add_explicit(&_Imidpu->wramUsed, size, memory_order_relaxed))
#define mem_reset() _Imidpu->wramUsed = 0

// ldma and sdma wrapper
static inline void mram_read(const void *from, void *to, size_t size) {
  // ldma instruction do these automatically
  from = (void*)((uintptr_t)from & ~7);
  to = (void*)((uintptr_t)to & ~3);
  assert((size & 7) == 0 && size <= 2048);
  memcpy(to, from, size);
}
static inline void mram_write(const void *from, void *to, size_t size) {
  // sdma instruction do these automatically
  from = (void*)((uintptr_t)from & ~3);
  to = (void*)((uintptr_t)to & ~7);
  assert((size & 7) == 0 && size <= 2048);
  memcpy(to, from, size);
}

// mutexed
#define MUTEX_INIT(name) pthread_mutex_t name = PTHREAD_MUTEX_INITIALIZER
#define mutex_lock(a) pthread_mutex_lock(&a)
#define mutex_trylock(a) pthread_mutex_trylock(&a)
#define mutex_unlock(a) pthread_mutex_unlock(&a)
typedef pthread_mutex_t mutex_id_t;
#define VMUTEX_INIT MUTEX_INIT
#define vmutex_lock MUTEX_LOCK
#define vmutex_trylock MUTEX_TRYLOCK
#define vmutex_unlock MUTEX_UNLOCK

typedef struct {
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  size_t count;
} sem_t;
#define SEM_INIT(name, cnt)                                                    \
  sem_t name = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, cnt}
static inline void sem_take(sem_t *sem) {
  pthread_mutex_lock(&sem->mutex);
  while (sem->count == 0)
    pthread_cond_wait(&sem->cond, &sem->mutex);
  sem->count--;
  pthread_mutex_unlock(&sem->mutex);
}
static inline void sem_give(sem_t *sem) {
  pthread_mutex_lock(&sem->mutex);
  sem->count++;
  pthread_cond_signal(&sem->cond);
  pthread_mutex_unlock(&sem->mutex);
}

// pthread_barrier gives SIGFPE when used with our tricks.
// BUT mutex and cond variables do not :D
typedef struct {
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  size_t count;
  size_t thresh;
} barrier_t;
#define BARRIER_INIT(name, cnt)                                                \
  barrier_t name = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, cnt}
static inline void barrier_wait(barrier_t *barrier) {
  pthread_mutex_lock(&barrier->mutex);
  if (++barrier->count == barrier->thresh) {
    barrier->count = 0; // Reset for reuse
    pthread_cond_broadcast(&barrier->cond);
  } else
    pthread_cond_wait(&barrier->cond, &barrier->mutex);
  pthread_mutex_unlock(&barrier->mutex);
}

// Does not support direct control of UPMEM atomic bits yet
#define ATOMIC_BIT_INIT(name) atomic_uint_fast8_t name

static inline void Imidpu_handshakeNotify(struct ImidpuMems *dpu, sysname_t tid) {
  pthread_mutex_t* m = &dpu->shakeMuts[tid];
  pthread_mutex_lock(m);
  int waitState = dpu->shakeIsWaiting[tid];
  if (waitState == 1) {
    pthread_cond_signal(&dpu->shakeConds[tid]);
    dpu->shakeIsWaiting[tid] = 0;
  } else /* if (waitState == 0) */ {
    dpu->shakeIsWaiting[tid] = -1;
    pthread_cond_wait(&dpu->shakeConds[tid], m);
  }
  pthread_mutex_unlock(m);
}

static inline int Imidpu_handshakeWaitFor(struct ImidpuMems *dpu, sysname_t tid) {
  pthread_mutex_t* m = &dpu->shakeMuts[tid];
  pthread_mutex_lock(m);
  int ret = 0, waitState = dpu->shakeIsWaiting[tid];
  if (waitState == 1) {
    ret = 1;
  } else if (waitState == 0) {
    dpu->shakeIsWaiting[tid] = 1;
    pthread_cond_wait(&dpu->shakeConds[tid], m);
  } else /* if (waitState == -1) */ {
    pthread_cond_signal(&dpu->shakeConds[tid]);
    dpu->shakeIsWaiting[tid] = 0;
  }
  pthread_mutex_unlock(m);
  return ret;
}
#define handshake_notify() Imidpu_handshakeNotify(_Imidpu, _Imidpu_Tid)
#define handshake_wait_for(id) Imidpu_handshakeWaitFor(_Imidpu, id)

extern struct ImidpuMems* _Imidpu;
extern __thread sysname_t _Imidpu_Tid;
