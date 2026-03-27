#include <assert.h>
#include <dpu.h>
#include <stdlib.h>

#define NUM_TASKLETS 16
struct dpu_arguments_t {
  int m;
  int n;
  int M_;
  int kernel;
};

void trns_host(int64_t *input, size_t A, size_t B) {
  int64_t *output = malloc(sizeof(int64_t) * A * B);
  assert(output);
  for (size_t i = 0; i < A * B; i++) {
    size_t next = (i * A) - (A * B - 1) * (i / B);
    output[next] = input[i];
  }
  for (size_t k = 0; k < A * B; k++)
    input[k] = output[k];
  free(output);
}

int main(int argc, char **argv) {
  struct dpu_set_t dpu_set, dpu;
  const size_t M_ = atol(argv[1]), m = 4;
  const size_t N_ = M_ > 512 ? M_ / 8 : 64, n = 8;
  const size_t total_size = M_ * m * N_ * n;
  int64_t *const A_host = malloc(total_size * sizeof(int64_t));
  int64_t *const A_backup = malloc(total_size * sizeof(int64_t));
  int64_t *const A_result = malloc(total_size * sizeof(int64_t));
  size_t nrDpu = atoi(argv[2]);
  char *done_host = malloc(M_ * n * sizeof(char));

  for (size_t i = 0; i < M_ * n; i++) done_host[i] = 0;
  for (size_t i = 0; i < M_ * m * N_ * n; i++)
    A_host[i] = i % 100;
  for (size_t i = 0; i < M_ * m * N_ * n; i++) A_backup[i] = A_host[i];
  trns_host(A_host, M_ * m, N_ * n);

  size_t curr_dpu = 0, active_dpus, active_dpus_before = 0;
  _Bool first_round = 1;

  while (curr_dpu < N_) {
    if ((N_ - curr_dpu) > nrDpu) {
      active_dpus = nrDpu;
    } else {
      active_dpus = (N_ - curr_dpu);
    }

    if ((active_dpus_before != active_dpus) && (!(first_round))) {
      dpu_free(dpu_set);
      dpu_alloc(active_dpus, NULL, &dpu_set);
      dpu_load(dpu_set, argv[3], NULL);
      nrDpu = active_dpus;
    } else if (first_round) {
      dpu_alloc(active_dpus, NULL, &dpu_set);
      dpu_load(dpu_set, argv[3], NULL);
      nrDpu = active_dpus;
    }

    size_t i;
    for (size_t j = 0; j < M_ * m; j++) {
      DPU_FOREACH(dpu_set, dpu, i) {
        dpu_prepare_xfer(dpu, &A_backup[j * N_ * n + n * (i + curr_dpu)]);
      }
      dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME,
                    sizeof(long) * j * n, sizeof(long) * n, DPU_XFER_DEFAULT);
    }

    DPU_FOREACH(dpu_set, dpu, i) { dpu_prepare_xfer(dpu, done_host); }
    dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME,
                  M_ * m * n * sizeof(long), (M_ * n) / 8 == 0 ? 8 : M_ * n,
                  DPU_XFER_DEFAULT);

    int kernel = 0;
    struct dpu_arguments_t input_arguments;
    input_arguments.m = m;
    input_arguments.n = n;
    input_arguments.M_ = M_;
    input_arguments.kernel = kernel;

    DPU_FOREACH(dpu_set, dpu, i) { dpu_prepare_xfer(dpu, &input_arguments); }
    dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, "DPU_INPUT_ARGUMENTS", 0,
                  sizeof(struct dpu_arguments_t), DPU_XFER_DEFAULT);

    dpu_launch(dpu_set, DPU_SYNCHRONOUS);

    kernel = 1;
    struct dpu_arguments_t input_arguments2;
    input_arguments2.m = m;
    input_arguments2.n = n;
    input_arguments2.M_ = M_;
    input_arguments2.kernel = kernel;

    DPU_FOREACH(dpu_set, dpu, i) { dpu_prepare_xfer(dpu, &input_arguments2); }
    dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, "DPU_INPUT_ARGUMENTS", 0,
                  sizeof(struct dpu_arguments_t), DPU_XFER_DEFAULT);

    dpu_launch(dpu_set, DPU_SYNCHRONOUS);

    DPU_FOREACH(dpu_set, dpu, i) {
      dpu_prepare_xfer(dpu, &A_result[curr_dpu * m * n * M_]);
      curr_dpu++;
    }
    dpu_push_xfer(dpu_set, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0,
                  sizeof(long) * m * n * M_, DPU_XFER_DEFAULT);

    if (first_round) {
      first_round = 0;
    }
  }

  dpu_free(dpu_set);

  int status = 1;
  for (int i = 0; i < M_ * m * N_ * n; i++) {
    if (A_host[i] != A_result[i]) {
      status = 0;
      break;
    }
  }

  assert(status);
  // dpu_log_read(dpu_set, stdout);
  free(A_host);
  free(A_backup);
  free(A_result);
  free(done_host);
  return status ? 0 : -1;
}
