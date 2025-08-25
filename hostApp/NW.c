#include <assert.h>
#include <dpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define MATCH_SCORE 2
#define MISMATCH_SCORE -1
#define GAP_PENALTY -1
#define TILESZ 64

// Tile dependency data
typedef struct {
  int corner_value;             // NW corner value
  int north_row[TILESZ]; // Bottom row from north tile
  int west_col[TILESZ];  // Right column from west tile
} tile_deps_t;

// Tile output data
typedef struct {
  int corner_value;             // SE corner value
  int south_row[TILESZ]; // Bottom row for south neighbor
  int east_col[TILESZ];  // Right column for east neighbor
} tile_output_t;

// Tile information
typedef struct {
  uint16_t tile_row;
  uint16_t tile_col;
  uint16_t tile_height; // May be smaller for edge tiles
  uint16_t tile_width;  // May be smaller for edge tiles
} tile_info_t;

// Sequential implementation for verification
void sequential_nw(char *seq1, char *seq2, int len1, int len2, int **dp) {
  // Initialize first row and column
  for (int i = 0; i <= len1; i++)
    dp[i][0] = i * GAP_PENALTY;
  for (int j = 0; j <= len2; j++)
    dp[0][j] = j * GAP_PENALTY;
  // Fill DP table
  for (int i = 1; i <= len1; i++) {
    for (int j = 1; j <= len2; j++) {
      int match_score =
          (seq1[i - 1] == seq2[j - 1]) ? MATCH_SCORE : MISMATCH_SCORE;
      int diagonal = dp[i - 1][j - 1] + match_score;
      int from_left = dp[i][j - 1] + GAP_PENALTY;
      int from_top = dp[i - 1][j] + GAP_PENALTY;
      dp[i][j] = diagonal;
      if (from_left > dp[i][j])
        dp[i][j] = from_left;
      if (from_top > dp[i][j])
        dp[i][j] = from_top;
    }
  }
}

int main(int argc, char **argv) {
  int seq1_len = atoi(argv[1]);
  int seq2_len = atoi(argv[2]);
  uint32_t num_dpus = atoi(argv[3]);
  const char *dpu_binary = argv[4];
  // Generate random sequences
  srand(time(NULL));
  char *seq1 = malloc(seq1_len + 1);
  char *seq2 = malloc(seq2_len + 1);
  assert(seq1 != NULL && seq2 != NULL);
  const char alphabet[] = "ACGT";
  for (int i = 0; i < seq1_len; i++)
    seq1[i] = alphabet[rand() % 4];
  for (int i = 0; i < seq2_len; i++)
    seq2[i] = alphabet[rand() % 4];
  seq1[seq1_len] = '\0';
  seq2[seq2_len] = '\0';
  // Compute reference result
  int **ref_dp = malloc((seq1_len + 1) * sizeof(int *));
  for (int i = 0; i <= seq1_len; i++)
    ref_dp[i] = calloc(seq2_len + 1, sizeof(int));
  sequential_nw(seq1, seq2, seq1_len, seq2_len, ref_dp);

  // Initialize DPU system
  struct dpu_set_t set, dpu;
  uint32_t each_dpu;
  DPU_ASSERT(dpu_alloc(num_dpus, NULL, &set));
  DPU_ASSERT(dpu_load(set, dpu_binary, NULL));

  // Calculate tile grid dimensions
  int num_tile_rows = (seq1_len + TILESZ - 1) / TILESZ;
  int num_tile_cols = (seq2_len + TILESZ - 1) / TILESZ;
  int num_waves = num_tile_rows + num_tile_cols - 1;
  // Allocate DP table for DPU results
  int **dpu_dp = malloc((seq1_len + 1) * sizeof(int *));
  for (int i = 0; i <= seq1_len; i++)
    dpu_dp[i] = calloc(seq2_len + 1, sizeof(int));
  // Initialize first row and column
  for (int i = 0; i <= seq1_len; i++)
    dpu_dp[i][0] = i * GAP_PENALTY;
  for (int j = 0; j <= seq2_len; j++)
    dpu_dp[0][j] = j * GAP_PENALTY;

  // Process tiles in wavefront order
  for (int wave = 0; wave < num_waves; wave++) {
    // Collect tiles in current wave
    int tiles_in_wave = 0;
    tile_info_t *wave_tiles = malloc(num_dpus * sizeof(tile_info_t));
    tile_deps_t *wave_deps = malloc(num_dpus * sizeof(tile_deps_t));

    for (int tile_row = 0; tile_row < num_tile_rows; tile_row++) {
      if (tiles_in_wave > num_dpus)
        return fputs("Insufficient DPUs", stderr);
      int tile_col = wave - tile_row;
      if (tile_col < 0 || tile_col >= num_tile_cols)
        continue;

      // Set tile info
      wave_tiles[tiles_in_wave].tile_row = tile_row;
      wave_tiles[tiles_in_wave].tile_col = tile_col;
      // Calculate actual tile dimensions (may be smaller at edges)
      int start_i = tile_row * TILESZ + 1;
      int start_j = tile_col * TILESZ + 1;
      int end_i =
          (start_i + TILESZ - 1 < seq1_len) ? start_i + TILESZ - 1 : seq1_len;
      int end_j =
          (start_j + TILESZ - 1 < seq2_len) ? start_j + TILESZ - 1 : seq2_len;
      wave_tiles[tiles_in_wave].tile_height = end_i - start_i + 1;
      wave_tiles[tiles_in_wave].tile_width = end_j - start_j + 1;

      // Prepare dependencies
      // North row (from tile above)
      for (int j = 0; j < wave_tiles[tiles_in_wave].tile_width; j++)
        wave_deps[tiles_in_wave].north_row[j] = dpu_dp[start_i - 1][start_j + j];
      // West column (from tile to the left)
      for (int i = 0; i < wave_tiles[tiles_in_wave].tile_height; i++)
        wave_deps[tiles_in_wave].west_col[i] = dpu_dp[start_i + i][start_j - 1];
      // NW corner
      wave_deps[tiles_in_wave].corner_value = dpu_dp[start_i - 1][start_j - 1];
      tiles_in_wave++;
    }

    // Send tiles to DPUs using bulk transfers
    // Only prepare DPUs that have work - others are automatically disabled
    // Send tile info
    DPU_FOREACH(set, dpu, each_dpu) {
      if (each_dpu < tiles_in_wave)
        dpu_prepare_xfer(dpu, &wave_tiles[each_dpu]);
    }
    DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, "tile_info", 0,
                             sizeof(tile_info_t), DPU_XFER_DEFAULT));
    // Send dependencies
    DPU_FOREACH(set, dpu, each_dpu) {
      if (each_dpu < tiles_in_wave)
        dpu_prepare_xfer(dpu, &wave_deps[each_dpu]);
    }
    DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, "tile_deps", 0,
                             sizeof(tile_deps_t), DPU_XFER_DEFAULT));
    // Send sequence chunks - direct pointers, no memcpy needed!
    DPU_FOREACH(set, dpu, each_dpu) {
      if (each_dpu >= tiles_in_wave) continue;
      int start_i = wave_tiles[each_dpu].tile_row * TILESZ;
      dpu_prepare_xfer(dpu, &seq1[start_i]);
    }
    DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, "seq1_chunk", 0,
                             TILESZ, DPU_XFER_DEFAULT));
    DPU_FOREACH(set, dpu, each_dpu) {
      if (each_dpu >= tiles_in_wave) continue;
      int start_j = wave_tiles[each_dpu].tile_col * TILESZ;
      dpu_prepare_xfer(dpu, &seq2[start_j]);
    }
    DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, "seq2_chunk", 0, TILESZ,
                             DPU_XFER_DEFAULT));
    // Launch DPUs
    DPU_ASSERT(dpu_launch(set, DPU_SYNCHRONOUS));

    // Collect results using bulk transfers
    tile_output_t *outputs = malloc(tiles_in_wave * sizeof(tile_output_t));
    int (*tile_dps)[TILESZ][TILESZ] =
        malloc(tiles_in_wave * TILESZ * TILESZ * sizeof(int));

    // Prepare and bulk transfer outputs
    DPU_FOREACH(set, dpu, each_dpu) {
      if (each_dpu < tiles_in_wave)
        dpu_prepare_xfer(dpu, &outputs[each_dpu]);
    }
    dpu_push_xfer(set, DPU_XFER_FROM_DPU, "tile_output", 0,
                  sizeof(tile_output_t), DPU_XFER_DEFAULT);

    // NEEDED FOR CHECKING ONLY:
    // Prepare and bulk transfer DP tables
    DPU_FOREACH(set, dpu, each_dpu) {
      if (each_dpu < tiles_in_wave)
        dpu_prepare_xfer(dpu, tile_dps[each_dpu]);
    }
    DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_FROM_DPU, "tile_dp", 0,
                             TILESZ * TILESZ * sizeof(int), DPU_XFER_DEFAULT));
    // Store results in global DP table
    for (int t = 0; t < tiles_in_wave; t++) {
      int start_i = wave_tiles[t].tile_row * TILESZ + 1;
      int start_j = wave_tiles[t].tile_col * TILESZ + 1;
      int height = wave_tiles[t].tile_height;
      int width = wave_tiles[t].tile_width;
      for (int i = 0; i < height; i++)
        for (int j = 0; j < width; j++)
          dpu_dp[start_i + i][start_j + j] = tile_dps[t][i][j];
    }

    free(outputs); free(tile_dps);
    free(wave_tiles); free(wave_deps);
  }

  // Verify results
  for (int i = 0; i <= seq1_len; i++)
    for (int j = 0; j <= seq2_len; j++)
      if (ref_dp[i][j] != dpu_dp[i][j])
        return fprintf(stderr, "Mismatch at [%d,%d]: ref=%d, dpu=%d\n", i, j,
                       ref_dp[i][j], dpu_dp[i][j]);

  // Cleanup
  free(seq1); free(seq2);
  for (int i = 0; i <= seq1_len; i++) { free(ref_dp[i]); free(dpu_dp[i]); }
  free(ref_dp); free(dpu_dp);
  DPU_ASSERT(dpu_free(set));
  return 0;
}
