#include <assert.h>
#include <dpu.h>
#include <stdlib.h>

#define REGS 128
#define NUM_TASKLETS 16

struct dpu_arguments_t {
  int size;
  int kernel;
  long t_count;
};

struct dpu_results_t {
  long t_count;
};

void read_input(long *A, int nr_elements, int nr_elements_round) {
  for (int i = 0; i < nr_elements; i++) {
    A[i] = i % 100;
  }
  for (int i = nr_elements; i < nr_elements_round; i++) {
    A[i] = 0;
  }
}

void scan_host(long *C, long *A, int nr_elements) {
  C[0] = A[0];
  for (int i = 1; i < nr_elements; i++) {
    C[i] = C[i - 1] + A[i];
  }
}

int roundup(int n, int m) { return ((n / m) * m + m); }

int divceil(int n, int m) { return ((n - 1) / m + 1); }

int main(int argc, char **argv) {
  int *A;
  long *C;
  long *C2;

  struct dpu_set_t dpu_set;
  struct dpu_set_t dpu;
  int NUM_DPUS = atoi(argv[2]);

  dpu_alloc(NUM_DPUS, NULL, &dpu_set);
  dpu_load(dpu_set, argv[3], NULL);

  long accum = 0;

  int input_size = atoi(argv[1]);
  int input_size_dpu_ = divceil(input_size, NUM_DPUS);
  int input_size_dpu_round =
      (input_size_dpu_ % (NUM_TASKLETS * REGS) != 0)
          ? roundup(input_size_dpu_, (NUM_TASKLETS * REGS))
          : input_size_dpu_;

  A = malloc(input_size_dpu_round * NUM_DPUS * sizeof(long));
  C = malloc(input_size_dpu_round * NUM_DPUS * sizeof(long));
  C2 = malloc(input_size_dpu_round * NUM_DPUS * sizeof(long));
  long *bufferA = (long*)A;
  long *bufferC = C2;

  read_input(bufferA, input_size, input_size_dpu_round * NUM_DPUS);

  scan_host(C, bufferA, input_size);

  int input_size_dpu = input_size_dpu_round;
  int kernel = 0;
  struct dpu_arguments_t input_arguments;
  input_arguments.size = input_size_dpu * sizeof(long);
  input_arguments.kernel = kernel;
  input_arguments.t_count = 0;

  size_t i;
  DPU_FOREACH(dpu_set, dpu, i) { dpu_prepare_xfer(dpu, &input_arguments); }
  dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, "DPU_INPUT_ARGUMENTS", 0,
                sizeof(struct dpu_arguments_t), DPU_XFER_DEFAULT);

  DPU_FOREACH(dpu_set, dpu, i) {
    dpu_prepare_xfer(dpu, &bufferA[input_size_dpu * i]);
  }
  dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0,
                input_size_dpu * sizeof(long), DPU_XFER_DEFAULT);

  dpu_launch(dpu_set, DPU_SYNCHRONOUS);

  struct dpu_results_t *results =
      malloc(NUM_DPUS * sizeof(struct dpu_results_t));
  long *results_scan = malloc(NUM_DPUS * sizeof(long));

  accum = 0;

  struct dpu_results_t *results_retrieve =
      malloc(NUM_DPUS * NUM_TASKLETS * sizeof(struct dpu_results_t));

  DPU_FOREACH(dpu_set, dpu, i) {
    dpu_prepare_xfer(dpu, &results_retrieve[i * NUM_TASKLETS]);
  }
  dpu_push_xfer(dpu_set, DPU_XFER_FROM_DPU, "DPU_RESULTS", 0,
                NUM_TASKLETS * sizeof(struct dpu_results_t), DPU_XFER_DEFAULT);

  DPU_FOREACH(dpu_set, dpu, i) {
    for (int each_tasklet = 0; each_tasklet < NUM_TASKLETS; each_tasklet++) {
      if (each_tasklet == 0) {
        results[i].t_count =
            results_retrieve[i * NUM_TASKLETS + each_tasklet].t_count;
      }
    }

    long temp = results[i].t_count;
    results_scan[i] = accum;
    accum += temp;
  }

  kernel = 1;
  struct dpu_arguments_t *input_arguments_2 =
      malloc(NUM_DPUS * sizeof(struct dpu_arguments_t));
  for (int i = 0; i < NUM_DPUS; i++) {
    input_arguments_2[i].size = input_size_dpu * sizeof(long);
    input_arguments_2[i].kernel = kernel;
    input_arguments_2[i].t_count = results_scan[i];
  }
  DPU_FOREACH(dpu_set, dpu, i) { dpu_prepare_xfer(dpu, &input_arguments_2[i]); }
  dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, "DPU_INPUT_ARGUMENTS", 0,
                sizeof(struct dpu_arguments_t), DPU_XFER_DEFAULT);

  dpu_launch(dpu_set, DPU_SYNCHRONOUS);

  DPU_FOREACH(dpu_set, dpu, i) {
    dpu_prepare_xfer(dpu, &bufferC[input_size_dpu * i]);
  }
  dpu_push_xfer(dpu_set, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME,
                input_size_dpu * sizeof(long), input_size_dpu * sizeof(long),
                DPU_XFER_DEFAULT);

  int status = 1;
  for (int i = 0; i < input_size; i++) {
    if (C[i] != bufferC[i]) {
      status = 0;
      break;
    }
  }

  assert(status);

  free(A);
  free(C);
  free(C2);
  // dpu_log_read(dpu_set, stdout);
  dpu_free(dpu_set);
  return status ? 0 : -1;
}
