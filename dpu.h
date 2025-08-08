#ifndef __DOWNMEM__
#define __DOWNMEM__
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "dpu_error.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef void* DmmMap;
typedef struct DmmDpu DmmDpu;

/**
 * @brief The different synchronization methods for launching DPUs.
 * polling method to check whether the execution is complete or not.
 * NOT IMPLEMENTED YET!!!!
 */
typedef enum _dpu_launch_policy_t {
  /**
   * Do not wait for the DPUs to complete their execution.
   * The application is given back the control once the DPUs are booted and
   * shall use a synchronization
   */
  DPU_ASYNCHRONOUS,
  /** Suspend the application execution until the booted DPUs complete their
     work. */
  DPU_SYNCHRONOUS,
} dpu_launch_policy_t;

/**
 * @brief Direction for a DPU memory transfer.
 */
typedef enum _dpu_xfer_t {
    /** Transfer from Host to DPU. */
    DPU_XFER_TO_DPU,
    /** Transfer from DPU to Host. */
    DPU_XFER_FROM_DPU,
} dpu_xfer_t;

/**
 * @brief Options for a DPU memory transfer.
 * All equivalent to DPU_XFER_DEFAULT --- NOT IMPLEMENTED YET!!!!
 */
typedef enum _dpu_xfer_flags_t {
  /** Memory transfer is executed and transfer buffer pointers are cleared. */
  DPU_XFER_DEFAULT = 0,
  /** Memory transfer is executed and transfer buffer pointers are not cleared */
  DPU_XFER_NO_RESET = 1 << 0,
  /**
   * Memory transfer is done asynchronously. The application is given back the
   * control once the transfer is enqueue in the asynchronous job list of the
   * rank(s).
   */
  DPU_XFER_ASYNC = 1 << 1,
  /**
   * The memory transfer is scheduled in parallel of the DPU execution, it
   * does not need to wait for the DPU to finish. This is supported for WRAM
   *transfer only.
   * @warning there is no synchronization between the host and the DPU for this
   * transfer. The user needs to take care of it.
   **/
  DPU_XFER_PARALLEL = 1 << 2,
} dpu_xfer_flags_t;

/**
 * @brief Allow user to allocate the number of dpus available on a real UPMEM
 * machine with function `dpu_alloc`
 * @hideinitializer */
#define DPU_ALLOCATE_ALL 2560

struct dpu_set_t {
  DmmDpu *dmm_dpu; // internal
  DmmMap *symbols;
  // For keeping track of which DPU(s) it is referring to
  // High 32b of `end` denotes the host thread `dmm_dpu[0]` binds to
  uint64_t begin, end;
  // Array to host buffer addresses (void*) that will be transferred to or from
  // the corresponding DPU. A null address means no transfer to that DPU.
  void **xfer_addr;
};

/**
 * @brief Allocate a number of ranks that will have the specified number of
 * DPUs. The simulator allows allocating arbitrary amounts of DPUs but a
 * multiple of 6, 8 or 10 is recommended (see implementation for why).
 *
 * @param nr_dpus number of DPUs to allocate. Use `DPU_ALLOCATE_ALL` to allocate
 * 2560 = 2 * 20 * 64 DPUs similar to a real UPMEM machine
 * @param profile NOT IMPLEMENTED!!!!!
 * @param dpu_set storage for the DPU set
 * @return Always DPU_OK for simulator
 */
dpu_error_t
dpu_alloc(uint32_t nr_dpus, const char *noImpl, struct dpu_set_t *dpu_set);

/**
 * @brief Allocate the specified number of DPU ranks. Each ranks contains a
 * number of DPUs specified by CANE_NumBanksPerRank environment variable, or 64
 * if not set.
 *
 * @param nr_ranks number of DPU ranks to allocate. Use `DPU_ALLOCATE_ALL` to
 * allocate 40 ranks similar to a real UPMEM machine.
 * @param profile NOT IMPLEMENTED!!!!
 * @param dpu_set storage for the DPU set
 * @return Always DPU_OK for simulator
 */
dpu_error_t
dpu_alloc_ranks(uint32_t nr_ranks, const char *noImpl, struct dpu_set_t *dpu_set);

/**
 * @brief Free all the DPUs of a DPU set.
 *
 * Note that this function will **cause undefined behavior** if called with a
 * DPU set not provided by `dpu_alloc`.
 *
 * @param dpu_set the identifier of the freed DPU set
 * @return Always DPU_OK for simulator
 */
dpu_error_t dpu_free(struct dpu_set_t dpu_set);

// No NULL checking 'cause UPMEM does not either
static inline dpu_error_t
dpu_get_nr_ranks(struct dpu_set_t dpu_set, uint32_t *nr_ranks) {
  return DPU_ERR_DRIVER;
}
// No NULL checking 'cause UPMEM does not either
static inline dpu_error_t
dpu_get_nr_dpus(struct dpu_set_t dpu_set, uint32_t *nr_ranks) {
  *nr_ranks = (uint32_t)dpu_set.end - dpu_set.begin;
  return DPU_OK;
}

// Rank Interface NOT IMPLEMENTED!!!!

// Magical marcos
#define _CONCAT_X(x, y) x##y
#define _CONCAT(x, y) _CONCAT_X(x, y)
#define _XSTR(x) #x
#define _STR(x) _XSTR(x)
#define _DPU_FOREACH_VARIANT(...) _DPU_FOREACH_VARIANT_(__VA_ARGS__, _DPU_FOREACH_VARIANT_SEQ())
#define _DPU_FOREACH_VARIANT_(a, ...) _DPU_FOREACH_VARIANT_N(__VA_ARGS__)
#define _DPU_FOREACH_VARIANT_N(_1, _2, N, ...) N
#define _DPU_FOREACH_VARIANT_SEQ() I, X, WRONG_NR_OF_ARGUMENTS

/**
 * @brief Iterator over all DPUs of a DPU set.
 * @param set the targeted DPU set
 * @param ... a pointer to a `struct dpu_set_t`, which will store the dpu
 * context for the current iteration, and an optional pointer to an integer that
 * will store the dpu index for the current iteration
 * @hideinitializer */
#define DPU_FOREACH(set, ...)                                                  \
  _CONCAT(_DPU_FOREACH_, _DPU_FOREACH_VARIANT(set, ##__VA_ARGS__))             \
  (set, ##__VA_ARGS__)
#define _DPU_FOREACH_X(set, one)                                               \
  for (one = set; one.begin < set.end; ++one.begin)
#define _DPU_FOREACH_I(set, one, i)                                            \
  for (one = set, i = 0; one.begin < one.end; ++one.begin, ++i)

/**
 * @brief Load a program in all the DPUs of a DPU set.
 *
 * @param dpu_set the targeted DPU set
 * @param binary_path the path generated by `dmm-simprep`
 * @return Whether the operation was successful.
 */
dpu_error_t dpu_load(struct dpu_set_t dpu_set, const char *objdmp_path,
                     void **noImpl);

/**
 * @brief Request the boot of all the DPUs in a DPU set.
 * @param dpu_set the identifier of the DPU set we want to boot
 * @param policy NOT IMPLEMENTED!!!! Always SYNCHRONOUS for now
 * @return Whether the operation was successful.
 */
dpu_error_t dpu_launch(struct dpu_set_t dpu_set, dpu_launch_policy_t noImpl);

// NOT IMPLEMENTED! DPU sets are always done, always return success.
static inline dpu_error_t
dpu_status(struct dpu_set_t dpu_set, bool *done, bool *fault) {
  if (done != NULL) *done = true;
  if (fault != NULL) *fault = false;
  return DPU_OK;
}
// NOT IMPLEMENTED! No-op for now
static inline dpu_error_t dpu_sync(struct dpu_set_t dpu_set) { return DPU_OK; }

#define DPU_MRAM_HEAP_POINTER_NAME "__sys_used_mram_end"
/**
 * @brief Copy data from the Host memory buffer to **one of** the DPU memories.
 * @param dpu_set the identifier of the DPU set
 * @param symbol_name the name of the DPU symbol where to copy the data
 * @param symbol_offset the byte offset from the base DPU symbol address where
 * to copy the data
 * @param src the host buffer containing the data to copy
 * @param length the number of bytes to copy
 * @return Whether the operation was successful.
 */
dpu_error_t dpu_copy_to(struct dpu_set_t dpu_set, const char *symbol_name,
                        uint32_t symbol_offset, const void *src, size_t length);

/**
 * @brief Copy data from one of the DPU memories to the Host memory buffer.
 * @param dpu_set the identifier of the DPU set
 * @param symbol_name the name of the DPU symbol from where to copy the data
 * @param symbol_offset the byte offset from the base DPU symbol address from
 * where to copy the data
 * @param dst the host buffer where the data is copied
 * @param length the number of bytes to copy
 * @return Whether the operation was successful.
 */
dpu_error_t dpu_copy_from(struct dpu_set_t dpu_set, const char *symbol_name,
                          uint32_t symbol_offset, void *dst, size_t length);

/**
 * @brief Set the Host buffer of **a single DPU** for the next memory transfer.
 * @warn UPMEM hostlib allows multiple DPUs to be set, but in DMM each set
 * refers to exactly 1 DPU. For general use case where `dpu_prepare_xfer` is
 * called in DPU_FOREACH body there is no difference.
 *
 * `NULL` can be used to clear the buffer pointer.
 * An error will be reported if any buffer was already set; the buffer pointer
 * will be overridden.
 *
 * @param dpu_set the identifier of the DPU set
 * @param host_addr pointer to the host buffer
 * @return Whether the operation was successful.
 */
dpu_error_t dpu_prepare_xfer(struct dpu_set_t dpu_set, void *host_addr);

dpu_error_t dpu_broadcast_to(struct dpu_set_t dpu_set, const char *symbol_name,
                             uint32_t symbol_offset, const void *src,
                             size_t length, dpu_xfer_flags_t flags);

/**
 * @brief Execute the memory transfer on the DPU set
 *
 * Use the host buffers previously defined by `dpu_prepare_xfer`.
 * When reading memory from the DPUs, if a host buffer is used for multiple
 * DPUs, no error will be reported, and the buffer contents are undefined.
 *
 * @param dpu_set the identifier of the DPU set
 * @param xfer direction of the transfer
 * @param symbol_name the name of the DPU symbol where the transfer starts
 * @param symbol_offset the byte offset from the base DPU symbol address where
 * the transfer starts
 * @param length the number of bytes to copy
 * @param flags NOT IMPLEMENTED!!!!
 * @return always succeeds in cane (or program panics)
 */
dpu_error_t dpu_push_xfer(struct dpu_set_t dpu_set, dpu_xfer_t xfer,
                          const char *symbol_name, uint32_t symbol_offset,
                          size_t length, dpu_xfer_flags_t noImpl);

/**
 * @brief reads and displays the contents of the log of a DPU. Despite having
 * the same name, the written "logs" in cane are completely different from those
 * in UPMEM. In cane logs contain execution cycles, memory cycles, etc. which can
 * be used to estimate wall clock time.
 * @param set the DPU set from which to extract the log
 * @param stream output stream where messages should be sent
 * @return always succeeds in cane
 */
// dpu_error_t dpu_log_read(struct dpu_set_t set, FILE *stream);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif
