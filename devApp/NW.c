#include "moredefs.h"
#include <alloc.h>
#include <barrier.h>
#include <mram.h>
#include <stddef.h>
#include <stdint.h>

#define MATCH_SCORE 2
#define MISMATCH_SCORE -1
#define GAP_PENALTY -1
#define MAX_TILE_SIZE 64
ALL_THREADS_BARRIER_INIT();

// Tile dependency data
typedef struct {
  int corner_value;             // NW corner value
  int north_row[MAX_TILE_SIZE]; // Bottom row from north tile
  int west_col[MAX_TILE_SIZE];  // Right column from west tile
} tile_deps_t;

// Tile output data
typedef struct {
  int corner_value;             // SE corner value
  int south_row[MAX_TILE_SIZE]; // Bottom row for south neighbor
  int east_col[MAX_TILE_SIZE];  // Right column for east neighbor
} tile_output_t;

// Tile information
typedef struct {
  uint16_t tile_row;
  uint16_t tile_col;
  uint16_t tile_height; // May be smaller for edge tiles
  uint16_t tile_width;  // May be smaller for edge tiles
} tile_info_t;

// Host inputs
__host tile_info_t tile_info;
__host tile_deps_t tile_deps;
__host char seq1_chunk[MAX_TILE_SIZE];
__host char seq2_chunk[MAX_TILE_SIZE];

// Output
__host tile_output_t tile_output;
__host int tile_dp[MAX_TILE_SIZE][MAX_TILE_SIZE];
_Static_assert(sizeof(tile_info) + sizeof(tile_deps) + sizeof(seq1_chunk) +
               sizeof(seq2_chunk) + sizeof(tile_output) + sizeof(tile_dp) +
               24 * 1024 < 65536, "wram exhausted");

static inline int max3(int a, int b, int c) {
  int max = a;
  if (b > max) max = b;
  if (c > max) max = c;
  return max;
}

int main() {
  uint32_t tasklet_id = me();
  // Check if this DPU has work
  if (tile_info.tile_height == 0 || tile_info.tile_width == 0) {
    return 0; // No work for this DPU
  }
  // no sync cause no global init :)

  int height = tile_info.tile_height;
  int width = tile_info.tile_width;

  // Process tile using anti-diagonal parallelization
  // Total number of anti-diagonals = height + width - 1
  int num_diagonals = height + width - 1;

  for (int diag = 0; diag < num_diagonals; diag++) {
    // Determine range of cells in this diagonal
    int start_i = (diag < width) ? 0 : (diag - width + 1);
    int end_i = (diag < height) ? diag : (height - 1);
    int num_cells = end_i - start_i + 1;

    // Distribute cells among tasklets
    int cells_per_tasklet = num_cells / NR_TASKLETS;
    int remainder = num_cells % NR_TASKLETS;

    int my_start = start_i + tasklet_id * cells_per_tasklet;
    if (tasklet_id < remainder) {
      my_start += tasklet_id;
      cells_per_tasklet++;
    } else {
      my_start += remainder;
    }

    // Process assigned cells in this diagonal
    for (int k = 0; k < cells_per_tasklet; k++) {
      int i = my_start + k;
      int j = diag - i;

      if (i < height && j < width) {
        // Get values from dependencies or previous cells
        int north_val, west_val, diag_val;

        if (i == 0 && j == 0) {
          // Top-left corner of tile
          diag_val = tile_deps.corner_value;
          north_val = tile_deps.north_row[0];
          west_val = tile_deps.west_col[0];
        } else if (i == 0) {
          // First row - use north dependency
          north_val = tile_deps.north_row[j];
          west_val = tile_dp[i][j - 1];
          diag_val =
              (j > 0) ? tile_deps.north_row[j - 1] : tile_deps.corner_value;
        } else if (j == 0) {
          // First column - use west dependency
          west_val = tile_deps.west_col[i];
          north_val = tile_dp[i - 1][j];
          diag_val =
              (i > 0) ? tile_deps.west_col[i - 1] : tile_deps.corner_value;
        } else {
          // Interior cell
          north_val = tile_dp[i - 1][j];
          west_val = tile_dp[i][j - 1];
          diag_val = tile_dp[i - 1][j - 1];
        }

        // Compute DP value
        int match_score =
            (seq1_chunk[i] == seq2_chunk[j]) ? MATCH_SCORE : MISMATCH_SCORE;
        int diagonal_score = diag_val + match_score;
        int from_left = west_val + GAP_PENALTY;
        int from_top = north_val + GAP_PENALTY;

        tile_dp[i][j] = max3(diagonal_score, from_left, from_top);
      }
    }

    // Synchronize after each diagonal
    all_threads_barrier_wait();
  }

  // Prepare output halos
  // Bottom row (for south neighbor)
  for (int j = tasklet_id; j < width; j += NR_TASKLETS)
    tile_output.south_row[j] = tile_dp[height - 1][j];
  // Right column (for east neighbor)
  for (int i = tasklet_id; i < height; i += NR_TASKLETS)
    tile_output.east_col[i] = tile_dp[i][width - 1];
  // SE corner
  if (tasklet_id == 0)
    tile_output.corner_value = tile_dp[height - 1][width - 1];
  return 0;
}
