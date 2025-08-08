/* Select with multiple tasklets */
#include <alloc.h>
#include <barrier.h>
#include <defs.h>
#include <handshake.h>
#include <mram.h>

#define BLOCK_SIZE_LOG2 10
#define BLOCK_SIZE (1 << BLOCK_SIZE_LOG2)
#define T uint64_t
#define REGS (BLOCK_SIZE >> 3) // 64 bits
typedef struct {
  uint32_t size;
  enum kernels {
    kernel1 = 0,
    nr_kernels = 1,
  } kernel;
} dpu_arguments_t;
typedef struct {
  uint32_t t_count;
} dpu_results_t;
__host dpu_arguments_t DPU_INPUT_ARGUMENTS;
__host dpu_results_t DPU_RESULTS[NR_TASKLETS];

// Array for communication between adjacent tasklets
uint32_t message[NR_TASKLETS];
uint32_t message_partial_count;

static inline _Bool pred(const T x) { return (x % 2) == 0; }

// SEL in each tasklet
unsigned int __attribute__((noinline)) primSel(T *output, T *input) {
  unsigned int pos = 0;
#pragma unroll
  for (unsigned int j = 0; j < REGS; j++) {
    if (!pred(input[j])) {
      output[pos] = input[j];
      pos++;
    }
  }
  return pos;
}

// Handshake with adjacent tasklets
unsigned int __attribute__((noinline))
handshake_sync(unsigned int l_count, unsigned int tasklet_id) {
  unsigned int p_count;
  // Wait and read message
  if (tasklet_id != 0) {
    handshake_wait_for(tasklet_id - 1);
    p_count = message[tasklet_id];
  } else
    p_count = 0;
  // Write message and notify
  if (tasklet_id < NR_TASKLETS - 1) {
    message[tasklet_id + 1] = p_count + l_count;
    handshake_notify();
  }
  return p_count;
}

// Barrier
BARRIER_INIT(my_barrier, NR_TASKLETS);

extern int main_kernel1(void);

int (*kernels[nr_kernels])(void) = {main_kernel1};

int main(void) {
  // Kernel
  return kernels[DPU_INPUT_ARGUMENTS.kernel]();
}

// main_kernel1
int main_kernel1() {
  unsigned int tasklet_id = me();
#if PRINT
  printf("tasklet_id = %u\n", tasklet_id);
#endif
  if (tasklet_id == 0) { // Initialize once the cycle counter
    mem_reset();         // Reset the heap
  }
  // Barrier
  barrier_wait(&my_barrier);

  dpu_results_t *result = &DPU_RESULTS[tasklet_id];

  uint32_t input_size_dpu_bytes = DPU_INPUT_ARGUMENTS.size;

  // Address of the current processing block in MRAM
  uint32_t base_tasklet = tasklet_id << BLOCK_SIZE_LOG2;
  uintptr_t mram_base_addr_A = (uintptr_t)DPU_MRAM_HEAP_POINTER;
  uintptr_t mram_base_addr_B =
      (uintptr_t)(DPU_MRAM_HEAP_POINTER + input_size_dpu_bytes);

  // Initialize a local cache to store the MRAM block
  T *cache_A = (T *)mem_alloc(BLOCK_SIZE);
  T *cache_B = (T *)mem_alloc(BLOCK_SIZE);

  // Initialize shared variable
  if (tasklet_id == NR_TASKLETS - 1)
    message_partial_count = 0;
  // Barrier
  barrier_wait(&my_barrier);

  for (unsigned int byte_index = base_tasklet;
       byte_index < input_size_dpu_bytes;
       byte_index += BLOCK_SIZE * NR_TASKLETS) {

    // Load cache with current MRAM block
    mram_read((__mram_ptr void const *)(mram_base_addr_A + byte_index), cache_A,
              BLOCK_SIZE);

    // SELECT in each tasklet
    uint32_t l_count = primSel(cache_B, cache_A); // In-place or out-of-place?
    // Sync with adjacent tasklets
    uint32_t p_count = handshake_sync(l_count, tasklet_id);
    // Write cache to current MRAM block
    mram_write(
        cache_B,
        (__mram_ptr void *)(mram_base_addr_B +
                            (message_partial_count + p_count) * sizeof(T)),
        l_count * sizeof(T));

    barrier_wait(&my_barrier);
    // Total count in this DPU
    if (tasklet_id == NR_TASKLETS - 1) {
      result->t_count = message_partial_count + p_count + l_count;
      message_partial_count = result->t_count;
    }
    barrier_wait(&my_barrier);
  }

  return 0;
}
