#include <assert.h>
#include <dpu.h>
#include <stdlib.h>
#include <math.h>

#define MAX_DATA_VAL 127
#define NUM_TASKLETS 16

struct dpu_arguments_t {
  int ts_length;
  int query_length;
  int query_mean;
  int query_std;
  int slice_per_dpu;
  int exclusion_zone;
  int kernel;
};

struct dpu_result_t {
  int minValue;
  int minIndex;
  int maxValue;
  int maxIndex;
};

int *create_test_file(int *tSeries, int *query, int ts_elements,
                      int query_elements) {
  for (int i = 0; i < ts_elements; i++)
    tSeries[i] = i % MAX_DATA_VAL;
  for (int i = 0; i < query_elements; i++)
    query[i] = i % MAX_DATA_VAL;
  return tSeries;
}

void streamp(int *tSeries, int *AMean, int *ASigma, int *minHost,
             int *minHostIdx, int ProfileLength, int *query, int queryLength,
             int queryMean, int queryStdDeviation) {
  int distance;
  int dotprod;
  *minHost = 2147483647;
  *minHostIdx = 0;

  for (int subseq = 0; subseq < ProfileLength; subseq++) {
    dotprod = 0;
    for (int j = 0; j < queryLength; j++)
      dotprod += tSeries[j + subseq] * query[j];
    distance =
        2 * (queryLength - (dotprod - queryLength * AMean[subseq] * queryMean) /
                               (ASigma[subseq] * queryStdDeviation));
    if (distance < *minHost) {
      *minHost = distance;
      *minHostIdx = subseq;
    }
  }
}

void tsHost(int *tSeries, int *aMean, int *aSigma, int timeSeriesLen,
            int profileLen, int queryLen) {
  int *AScan = malloc(sizeof(int) * timeSeriesLen);
  AScan[0] = tSeries[0];
  for (int i = 1; i < timeSeriesLen; i++)
    AScan[i] = tSeries[i] + AScan[i - 1];
  int *ASqrScan = malloc(sizeof(int) * timeSeriesLen);
  ASqrScan[0] = tSeries[0] * tSeries[0];
  for (int i = 1; i < timeSeriesLen; i++)
    ASqrScan[i] = tSeries[i] * tSeries[i] + ASqrScan[i - 1];

  int *ASum = malloc(sizeof(int) * profileLen);
  ASum[0] = AScan[queryLen - 1];
  for (int i = 0; i < timeSeriesLen - queryLen; i++)
    ASum[i + 1] = AScan[queryLen + i] - AScan[i];
  int *ASumSq = malloc(sizeof(int) * profileLen);
  ASumSq[0] = ASqrScan[queryLen - 1];
  for (int i = 0; i < timeSeriesLen - queryLen; i++)
    ASumSq[i + 1] = ASqrScan[queryLen + i] - ASqrScan[i];

  int *AMean_tmp = malloc(sizeof(int) * profileLen);
  for (int i = 0; i < profileLen; i++)
    AMean_tmp[i] = ASum[i] / queryLen;
  int *ASigmaSq = malloc(sizeof(int) * profileLen);
  for (int i = 0; i < profileLen; i++)
    ASigmaSq[i] = ASumSq[i] / queryLen - aMean[i] * aMean[i];
  for (int i = 0; i < profileLen; i++) {
    aSigma[i] = sqrt(ASigmaSq[i]);
    aMean[i] = AMean_tmp[i];
  }

  free(AScan);
  free(ASqrScan);
  free(ASum);
  free(ASumSq);
  free(ASigmaSq);
  free(AMean_tmp);
}

int main(int argc, char **argv) {
  struct dpu_set_t set, eachDpu;
  int nrDpu = atoi(argv[2]);
  DPU_ASSERT(dpu_alloc(nrDpu, NULL, &set));
  DPU_ASSERT(dpu_load(set, argv[3], NULL));

  int tsLen = atoi(argv[1]), queryLength = 64;
  if (tsLen % (nrDpu * NUM_TASKLETS * queryLength)) {
    tsLen = tsLen + (nrDpu * NUM_TASKLETS * queryLength -
                         tsLen % (nrDpu * NUM_TASKLETS * queryLength));
  }
  int *tSeries = malloc(tsLen * sizeof(int));
  int *query = malloc(queryLength * sizeof(int));
  create_test_file(tSeries, query, tsLen, queryLength);
  int *const AMean = malloc(tsLen * sizeof(int));
  int *const ASigma = malloc(tsLen * sizeof(int));
  int *const minHost = malloc(sizeof(int));
  int *const minHostIdx = malloc(sizeof(int));
  tsHost(tSeries, AMean, ASigma, tsLen, tsLen - queryLength, queryLength);

  int query_mean;
  int queryMean = 0;
  for (int i = 0; i < queryLength; i++) {
    queryMean += query[i];
  }
  queryMean /= queryLength;
  query_mean = queryMean;

  int query_std;
  int queryStdDeviation;
  int queryVariance = 0;
  for (int i = 0; i < queryLength; i++) {
    queryVariance += (query[i] - queryMean) * (query[i] - queryMean);
  }
  queryVariance /= queryLength;
  queryStdDeviation = sqrt(queryVariance);
  query_std = queryStdDeviation;

  int slice_per_dpu = tsLen / nrDpu;
  struct dpu_arguments_t input_arguments;
  input_arguments.ts_length = tsLen;
  input_arguments.query_length = queryLength;
  input_arguments.query_mean = query_mean;
  input_arguments.query_std = query_std;
  input_arguments.slice_per_dpu = slice_per_dpu;
  input_arguments.exclusion_zone = 0;
  input_arguments.kernel = 0;

  struct dpu_result_t result;
  result.minValue = 2147483647;
  result.minIndex = 0;
  result.maxValue = 0;
  result.maxIndex = 0;

  size_t i;
  DPU_FOREACH(set, eachDpu, i) {
    input_arguments.exclusion_zone = 0;
    dpu_copy_to(eachDpu, "DPU_INPUT_ARGUMENTS", 0, &input_arguments,
                sizeof(struct dpu_arguments_t));
  }

  size_t mem_offset = 0;
  DPU_FOREACH(set, eachDpu, i) { dpu_prepare_xfer(eachDpu, tSeries); }
  dpu_push_xfer(set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0,
                queryLength * sizeof(int), DPU_XFER_DEFAULT);
  mem_offset += queryLength * sizeof(int);

  DPU_FOREACH(set, eachDpu, i) {
    dpu_prepare_xfer(eachDpu, &tSeries[slice_per_dpu * i]);
  }
  dpu_push_xfer(set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME,
                mem_offset, (slice_per_dpu + queryLength) * sizeof(int),
                DPU_XFER_DEFAULT);
  mem_offset += ((slice_per_dpu + queryLength) * sizeof(int));

  DPU_FOREACH(set, eachDpu, i) {
    dpu_prepare_xfer(eachDpu, &AMean[slice_per_dpu * i]);
  }
  dpu_push_xfer(set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME,
                mem_offset, (slice_per_dpu + queryLength) * sizeof(int),
                DPU_XFER_DEFAULT);
  mem_offset += ((slice_per_dpu + queryLength) * sizeof(int));

  DPU_FOREACH(set, eachDpu, i) {
    dpu_prepare_xfer(eachDpu, &ASigma[slice_per_dpu * i]);
  }
  dpu_push_xfer(set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME,
                mem_offset, (slice_per_dpu + queryLength) * sizeof(int),
                DPU_XFER_DEFAULT);

  dpu_launch(set, DPU_SYNCHRONOUS);
  struct dpu_result_t *results_retrieve =
      malloc(nrDpu * NUM_TASKLETS * sizeof(struct dpu_result_t));
  DPU_FOREACH(set, eachDpu, i) {
    dpu_prepare_xfer(eachDpu, &results_retrieve[i * NUM_TASKLETS]);
  }
  dpu_push_xfer(set, DPU_XFER_FROM_DPU, "DPU_RESULTS", 0,
                NUM_TASKLETS * sizeof(struct dpu_result_t), DPU_XFER_DEFAULT);

  DPU_FOREACH(set, eachDpu, i) {
    for (size_t eachTlet = 0; eachTlet < NUM_TASKLETS; eachTlet++) {
      if (results_retrieve[i * NUM_TASKLETS + eachTlet].minValue <
              result.minValue &&
          results_retrieve[i * NUM_TASKLETS + eachTlet].minValue > 0) {
        result.minValue =
            results_retrieve[i * NUM_TASKLETS + eachTlet].minValue;
        result.minIndex =
            results_retrieve[i * NUM_TASKLETS + eachTlet].minIndex +
            (i * slice_per_dpu);
      }
    }
  }

  streamp(tSeries, AMean, ASigma, minHost, minHostIdx,
          tsLen - queryLength - 1, query, queryLength, query_mean,
          query_std);

  int status = (*minHost == result.minValue);
  assert(status);
  // dpu_log_read(dpu_set, stdout);
  dpu_free(set);
  return 0;
}
