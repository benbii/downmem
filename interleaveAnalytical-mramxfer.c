#define _GNU_SOURCE
#include <immintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const uint8_t tposeIdxBytes[64] = {
    63, 55, 47, 39, 31, 23, 15, 7, 62, 54, 46, 38, 30, 22, 14, 6,
    61, 53, 45, 37, 29, 21, 13, 5, 60, 52, 44, 36, 28, 20, 12, 4,
    59, 51, 43, 35, 27, 19, 11, 3, 58, 50, 42, 34, 26, 18, 10, 2,
    57, 49, 41, 33, 25, 17, 9,  1, 56, 48, 40, 32, 24, 16, 8,  0};

static void bcst8Bank(uint64_t *dest, const uint64_t *src,
                      size_t srcNQword, size_t nthrd, uint8_t _mask) {
  const __m256i idx[4] = {_mm256_set_epi8(
    3,3,3,3,3,3,3,3, 2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1, 0,0,0,0,0,0,0,0),
  _mm256_set_epi8(
    7,7,7,7,7,7,7,7, 6,6,6,6,6,6,6,6,
    5,5,5,5,5,5,5,5, 4,4,4,4,4,4,4,4),
  _mm256_set_epi8(
    11,11,11,11,11,11,11,11, 10,10,10,10,10,10,10,10,
    9,9,9,9,9,9,9,9, 8,8,8,8,8,8,8,8),
  _mm256_set_epi8(
    15,15,15,15,15,15,15,15, 14,14,14,14,14,14,14,14,
    13,13,13,13,13,13,13,13, 12,12,12,12,12,12,12,12),
  };
  __mmask32 mask = ((uint32_t)_mask << 8) | _mask;
  mask |= mask << 16; (void)mask;

  #pragma omp parallel for num_threads(nthrd)
  for (size_t i = 0; i < srcNQword; i += 4) {
    // Load 32 bytes (256 bits) of source data
    _mm_prefetch(src + i + 16, _MM_HINT_T0);
    __m256i src0123 = _mm256_loadu_si256((__m256i*)(src + i));
    __m256i src4567 = _mm256_permute2x128_si256(src0123, src0123, 0x33);
    src0123 = _mm256_permute2x128_si256(src0123, src0123, 0);
    // Apply shuffle masks to duplicate each group of 4 bytes
    __m256i dup0 = _mm256_shuffle_epi8(src0123, idx[0]);
    __m256i dup1 = _mm256_shuffle_epi8(src0123, idx[1]);
    __m256i dup2 = _mm256_shuffle_epi8(src0123, idx[2]);
    __m256i dup3 = _mm256_shuffle_epi8(src0123, idx[3]);
    __m256i dup4 = _mm256_shuffle_epi8(src4567, idx[0]);
    __m256i dup5 = _mm256_shuffle_epi8(src4567, idx[1]);
    __m256i dup6 = _mm256_shuffle_epi8(src4567, idx[2]);
    __m256i dup7 = _mm256_shuffle_epi8(src4567, idx[3]);

    if (_mask == 0xff) {
      _mm256_stream_si256((__m256i*)(dest + i * 8 + 0), dup0);
      _mm256_stream_si256((__m256i*)(dest + i * 8 + 4), dup1);
      if (i+1 >= srcNQword) continue;
      _mm256_stream_si256((__m256i*)(dest + i * 8 + 8), dup2);
      _mm256_stream_si256((__m256i*)(dest + i * 8 + 12), dup3);
      if (i+2 >= srcNQword) continue;
      _mm256_stream_si256((__m256i*)(dest + i * 8 + 16), dup4);
      _mm256_stream_si256((__m256i*)(dest + i * 8 + 20), dup5);
      if (i+3 >= srcNQword) continue;
      _mm256_stream_si256((__m256i*)(dest + i * 8 + 24), dup6);
      _mm256_stream_si256((__m256i*)(dest + i * 8 + 28), dup7);
    } else {
      _mm256_mask_storeu_epi8((__m256i*)(dest + i * 8 + 0), mask, dup0);
      _mm256_mask_storeu_epi8((__m256i*)(dest + i * 8 + 4), mask, dup1);
      if (i+1 >= srcNQword) continue;
      _mm256_mask_storeu_epi8((__m256i*)(dest + i * 8 + 8), mask, dup2);
      _mm256_mask_storeu_epi8((__m256i*)(dest + i * 8 + 12), mask, dup3);
      if (i+2 >= srcNQword) continue;
      _mm256_mask_storeu_epi8((__m256i*)(dest + i * 8 + 16), mask, dup4);
      _mm256_mask_storeu_epi8((__m256i*)(dest + i * 8 + 20), mask, dup5);
      if (i+3 >= srcNQword) continue;
      _mm256_mask_storeu_epi8((__m256i*)(dest + i * 8 + 24), mask, dup6);
      _mm256_mask_storeu_epi8((__m256i*)(dest + i * 8 + 28), mask, dup7);
    }
  }
}

// `dest` must be cacheline (64Byte) aligned
static void push8Bank(uint64_t *dest, uint64_t *const srcAddrs[8],
                      size_t srcNQword, size_t nthrd) {
  const __m512i tposeIdx = _mm512_loadu_si512(tposeIdxBytes);
  __mmask64 mask = (__mmask64)_mm512_cmpneq_epi64_mask(
    _mm512_loadu_si512(srcAddrs), _mm512_setzero_si512());
  mask |= mask << 8; mask |= mask << 16; mask |= mask << 32;

#pragma omp parallel for num_threads(nthrd)
  for (size_t i = 0; i < srcNQword; i += 8) {
    // read
    __m512i x0,x1,x2,x3,x4,x5,x6,x7;
    if (!(mask&128)) {
      _mm_prefetch(srcAddrs[0] + i + 16, _MM_HINT_T0);
      x0 = _mm512_loadu_si512((__m512i *)(srcAddrs[0] + i));
    }
    if (!(mask&64)) {
      _mm_prefetch(srcAddrs[1] + i + 16, _MM_HINT_T0);
      x1 = _mm512_loadu_si512((__m512i *)(srcAddrs[1] + i));
    }
    if (!(mask&32)) {
      _mm_prefetch(srcAddrs[2] + i + 16, _MM_HINT_T0);
      x2 = _mm512_loadu_si512((__m512i *)(srcAddrs[2] + i));
    }
    if (!(mask&16)) {
      _mm_prefetch(srcAddrs[3] + i + 16, _MM_HINT_T0);
      x3 = _mm512_loadu_si512((__m512i *)(srcAddrs[3] + i));
    }
    if (!(mask&8)) {
      _mm_prefetch(srcAddrs[4] + i + 16, _MM_HINT_T0);
      x4 = _mm512_loadu_si512((__m512i *)(srcAddrs[4] + i));
    }
    if (!(mask&4)) {
      _mm_prefetch(srcAddrs[5] + i + 16, _MM_HINT_T0);
      x5 = _mm512_loadu_si512((__m512i *)(srcAddrs[5] + i));
    }
    if (!(mask&2)) {
      _mm_prefetch(srcAddrs[6] + i + 16, _MM_HINT_T0);
      x6 = _mm512_loadu_si512((__m512i *)(srcAddrs[6] + i));
    }
    if (!(mask&1)) {
      _mm_prefetch(srcAddrs[7] + i + 16, _MM_HINT_T0);
      x7 = _mm512_loadu_si512((__m512i *)(srcAddrs[7] + i));
    }

    // compute
    // step 1: checkerboard permutation
    x1 = _mm512_permutexvar_epi64(_mm512_setr_epi64(7,0,1,2,3,4,5,6), x1);
    x2 = _mm512_permutexvar_epi64(_mm512_setr_epi64(6,7,0,1,2,3,4,5), x2);
    x3 = _mm512_permutexvar_epi64(_mm512_setr_epi64(5,6,7,0,1,2,3,4), x3);
    x4 = _mm512_permutexvar_epi64(_mm512_setr_epi64(4,5,6,7,0,1,2,3), x4);
    x5 = _mm512_permutexvar_epi64(_mm512_setr_epi64(3,4,5,6,7,0,1,2), x5);
    x6 = _mm512_permutexvar_epi64(_mm512_setr_epi64(2,3,4,5,6,7,0,1), x6);
    x7 = _mm512_permutexvar_epi64(_mm512_setr_epi64(1,2,3,4,5,6,7,0), x7);

    // step 2: stride 1 blend
    __m512i y0 = _mm512_mask_blend_epi64(0x55, x0, x1);
    __m512i y1 = _mm512_mask_blend_epi64(0x55, x1, x0);
    __m512i y2 = _mm512_mask_blend_epi64(0x55, x2, x3);
    __m512i y3 = _mm512_mask_blend_epi64(0x55, x3, x2);
    __m512i y4 = _mm512_mask_blend_epi64(0x55, x4, x5);
    __m512i y5 = _mm512_mask_blend_epi64(0x55, x5, x4);
    __m512i y6 = _mm512_mask_blend_epi64(0x55, x6, x7);
    __m512i y7 = _mm512_mask_blend_epi64(0x55, x7, x6);
    // step 3: stride 2 blend
    x0 = _mm512_mask_blend_epi64(0x33, y0, y2);
    x2 = _mm512_mask_blend_epi64(0x33, y2, y0);
    x1 = _mm512_mask_blend_epi64(0x99, y1, y3);
    x3 = _mm512_mask_blend_epi64(0x99, y3, y1);
    x4 = _mm512_mask_blend_epi64(0x33, y4, y6);
    x6 = _mm512_mask_blend_epi64(0x33, y6, y4);
    x5 = _mm512_mask_blend_epi64(0x99, y5, y7);
    x7 = _mm512_mask_blend_epi64(0x99, y7, y5);
    // step 4: stride 4 blend
    y0 = _mm512_mask_blend_epi64(0x0f, x0, x4);
    y4 = _mm512_mask_blend_epi64(0x0f, x4, x0);
    y1 = _mm512_mask_blend_epi64(0x87, x1, x5);
    y5 = _mm512_mask_blend_epi64(0x87, x5, x1);
    y2 = _mm512_mask_blend_epi64(0xc3, x2, x6);
    y6 = _mm512_mask_blend_epi64(0xc3, x6, x2);
    y3 = _mm512_mask_blend_epi64(0xe1, x3, x7);
    y7 = _mm512_mask_blend_epi64(0xe1, x7, x3);

    // step 5: final permute-back (todo: can be merged into one)
    y1 = _mm512_permutexvar_epi64(_mm512_setr_epi64(1,2,3,4,5,6,7,0), y1);
    y2 = _mm512_permutexvar_epi64(_mm512_setr_epi64(2,3,4,5,6,7,0,1), y2);
    y3 = _mm512_permutexvar_epi64(_mm512_setr_epi64(3,4,5,6,7,0,1,2), y3);
    y4 = _mm512_permutexvar_epi64(_mm512_setr_epi64(4,5,6,7,0,1,2,3), y4);
    y5 = _mm512_permutexvar_epi64(_mm512_setr_epi64(5,6,7,0,1,2,3,4), y5);
    y6 = _mm512_permutexvar_epi64(_mm512_setr_epi64(6,7,0,1,2,3,4,5), y6);
    y7 = _mm512_permutexvar_epi64(_mm512_setr_epi64(7,0,1,2,3,4,5,6), y7);
    y0 = _mm512_permutexvar_epi8(tposeIdx, y0);
    y1 = _mm512_permutexvar_epi8(tposeIdx, y1);
    y2 = _mm512_permutexvar_epi8(tposeIdx, y2);
    y3 = _mm512_permutexvar_epi8(tposeIdx, y3);
    y4 = _mm512_permutexvar_epi8(tposeIdx, y4);
    y5 = _mm512_permutexvar_epi8(tposeIdx, y5);
    y6 = _mm512_permutexvar_epi8(tposeIdx, y6);
    y7 = _mm512_permutexvar_epi8(tposeIdx, y7);

    // 64 qwords written per loop
    if (mask == 0xffffffffffffffff) {
      _mm512_stream_si512((__m512i *)(dest + i * 8 + 0), y0);
      if (i+1 >= srcNQword) continue;
      _mm512_stream_si512((__m512i *)(dest + i * 8 + 8), y1);
      if (i+2 >= srcNQword) continue;
      _mm512_stream_si512((__m512i *)(dest + i * 8 + 16), y2);
      if (i+3 >= srcNQword) continue;
      _mm512_stream_si512((__m512i *)(dest + i * 8 + 24), y3);
      if (i+4 >= srcNQword) continue;
      _mm512_stream_si512((__m512i *)(dest + i * 8 + 32), y4);
      if (i+5 >= srcNQword) continue;
      _mm512_stream_si512((__m512i *)(dest + i * 8 + 40), y5);
      if (i+6 >= srcNQword) continue;
      _mm512_stream_si512((__m512i *)(dest + i * 8 + 48), y6);
      if (i+7 >= srcNQword) continue;
      _mm512_stream_si512((__m512i *)(dest + i * 8 + 56), y7);
    } else {
      _mm512_mask_storeu_epi8((__m512i *)(dest + i * 8 + 0),  mask, y0);
      if (i+1 >= srcNQword) continue;
      _mm512_mask_storeu_epi8((__m512i *)(dest + i * 8 + 8),  mask, y1);
      if (i+2 >= srcNQword) continue;
      _mm512_mask_storeu_epi8((__m512i *)(dest + i * 8 + 16), mask, y2);
      if (i+3 >= srcNQword) continue;
      _mm512_mask_storeu_epi8((__m512i *)(dest + i * 8 + 24), mask, y3);
      if (i+4 >= srcNQword) continue;
      _mm512_mask_storeu_epi8((__m512i *)(dest + i * 8 + 32), mask, y4);
      if (i+5 >= srcNQword) continue;
      _mm512_mask_storeu_epi8((__m512i *)(dest + i * 8 + 40), mask, y5);
      if (i+6 >= srcNQword) continue;
      _mm512_mask_storeu_epi8((__m512i *)(dest + i * 8 + 48), mask, y6);
      if (i+7 >= srcNQword) continue;
      _mm512_mask_storeu_epi8((__m512i *)(dest + i * 8 + 56), mask, y7);
    }
  }
}

static void extract8Bank(const uint64_t *src, uint64_t* destAddrs[8],
                         size_t destNQword, size_t nthrd) {
  #pragma omp parallel for num_threads(nthrd)
  for (size_t i = 0; i < destNQword - 7; i += 8) {
    // read
    _mm_prefetch(src + i*8 + 128, _MM_HINT_T0);
    _mm_prefetch(src + i*8 + 136, _MM_HINT_T0);
    _mm_prefetch(src + i*8 + 144, _MM_HINT_T0);
    _mm_prefetch(src + i*8 + 152, _MM_HINT_T0);
    _mm_prefetch(src + i*8 + 160, _MM_HINT_T0);
    _mm_prefetch(src + i*8 + 168, _MM_HINT_T0);
    _mm_prefetch(src + i*8 + 176, _MM_HINT_T0);
    _mm_prefetch(src + i*8 + 184, _MM_HINT_T0);
    __m512i x[8];
    x[0] = _mm512_load_si512((__m512i *)(src + i*8 + 0));
    x[1] = _mm512_load_si512((__m512i *)(src + i*8 + 8));
    x[2] = _mm512_load_si512((__m512i *)(src + i*8 + 16));
    x[3] = _mm512_load_si512((__m512i *)(src + i*8 + 24));
    x[4] = _mm512_load_si512((__m512i *)(src + i*8 + 32));
    x[5] = _mm512_load_si512((__m512i *)(src + i*8 + 40));
    x[6] = _mm512_load_si512((__m512i *)(src + i*8 + 48));
    x[7] = _mm512_load_si512((__m512i *)(src + i*8 + 56));

    // There is a more efficient approach which very much resembles pushing into
    // memory banks but since pushing and extracting are memory bound, the
    // inefficient computation suffices.
    // Pre-computed indices for extracting each column (0-7)
    const __m512i extract_col[8] = {
        _mm512_set_epi8(56, 48, 40, 32, 24, 16, 8, 0, 56, 48, 40, 32, 24, 16, 8, 0, 56, 48, 40, 32, 24, 16, 8, 0, 56, 48, 40, 32, 24, 16, 8, 0, 56, 48, 40, 32, 24, 16, 8, 0, 56, 48, 40, 32, 24, 16, 8, 0, 56, 48, 40, 32, 24, 16, 8, 0, 56, 48, 40, 32, 24, 16, 8, 0),
        _mm512_set_epi8(57, 49, 41, 33, 25, 17, 9, 1, 57, 49, 41, 33, 25, 17, 9, 1, 57, 49, 41, 33, 25, 17, 9, 1, 57, 49, 41, 33, 25, 17, 9, 1, 57, 49, 41, 33, 25, 17, 9, 1, 57, 49, 41, 33, 25, 17, 9, 1, 57, 49, 41, 33, 25, 17, 9, 1, 57, 49, 41, 33, 25, 17, 9, 1),
        _mm512_set_epi8(58, 50, 42, 34, 26, 18, 10, 2, 58, 50, 42, 34, 26, 18, 10, 2, 58, 50, 42, 34, 26, 18, 10, 2, 58, 50, 42, 34, 26, 18, 10, 2, 58, 50, 42, 34, 26, 18, 10, 2, 58, 50, 42, 34, 26, 18, 10, 2, 58, 50, 42, 34, 26, 18, 10, 2, 58, 50, 42, 34, 26, 18, 10, 2),
        _mm512_set_epi8(59, 51, 43, 35, 27, 19, 11, 3, 59, 51, 43, 35, 27, 19, 11, 3, 59, 51, 43, 35, 27, 19, 11, 3, 59, 51, 43, 35, 27, 19, 11, 3, 59, 51, 43, 35, 27, 19, 11, 3, 59, 51, 43, 35, 27, 19, 11, 3, 59, 51, 43, 35, 27, 19, 11, 3, 59, 51, 43, 35, 27, 19, 11, 3),
        _mm512_set_epi8(60, 52, 44, 36, 28, 20, 12, 4, 60, 52, 44, 36, 28, 20, 12, 4, 60, 52, 44, 36, 28, 20, 12, 4, 60, 52, 44, 36, 28, 20, 12, 4, 60, 52, 44, 36, 28, 20, 12, 4, 60, 52, 44, 36, 28, 20, 12, 4, 60, 52, 44, 36, 28, 20, 12, 4, 60, 52, 44, 36, 28, 20, 12, 4),
        _mm512_set_epi8(61, 53, 45, 37, 29, 21, 13, 5, 61, 53, 45, 37, 29, 21, 13, 5, 61, 53, 45, 37, 29, 21, 13, 5, 61, 53, 45, 37, 29, 21, 13, 5, 61, 53, 45, 37, 29, 21, 13, 5, 61, 53, 45, 37, 29, 21, 13, 5, 61, 53, 45, 37, 29, 21, 13, 5, 61, 53, 45, 37, 29, 21, 13, 5),
        _mm512_set_epi8(62, 54, 46, 38, 30, 22, 14, 6, 62, 54, 46, 38, 30, 22, 14, 6, 62, 54, 46, 38, 30, 22, 14, 6, 62, 54, 46, 38, 30, 22, 14, 6, 62, 54, 46, 38, 30, 22, 14, 6, 62, 54, 46, 38, 30, 22, 14, 6, 62, 54, 46, 38, 30, 22, 14, 6, 62, 54, 46, 38, 30, 22, 14, 6),
        _mm512_set_epi8(63, 55, 47, 39, 31, 23, 15, 7, 63, 55, 47, 39, 31, 23, 15, 7, 63, 55, 47, 39, 31, 23, 15, 7, 63, 55, 47, 39, 31, 23, 15, 7, 63, 55, 47, 39, 31, 23, 15, 7, 63, 55, 47, 39, 31, 23, 15, 7, 63, 55, 47, 39, 31, 23, 15, 7, 63, 55, 47, 39, 31, 23, 15, 7)};

    // Pre-computed blend masks for each 8-byte position
    static const __mmask64 blend_masks[8] = {
        0x00000000000000FFULL, // First 8 bytes
        0x000000000000FF00ULL, // Bytes 8-15
        0x0000000000FF0000ULL, // Bytes 16-23
        0x00000000FF000000ULL, // Bytes 24-31
        0x000000FF00000000ULL, // Bytes 32-39
        0x0000FF0000000000ULL, // Bytes 40-47
        0x00FF000000000000ULL, // Bytes 48-55
        0xFF00000000000000ULL  // Bytes 56-63
    };

    // 64 qwords written per loop
    for (int col = 0; col < 8; col++) {
      if (!destAddrs[col])
        continue;
      __m512i result = _mm512_permutexvar_epi8(extract_col[col], x[0]);
      for (int reg = 1; reg < 8; reg++) {
        __m512i temp = _mm512_permutexvar_epi8(extract_col[col], x[reg]);
        result = _mm512_mask_blend_epi8(blend_masks[reg], result, temp);
      }
      _mm512_stream_si512((__m512i *)(destAddrs[col] + i), result);
    }
  }

  // "scalar" fallback on last few elements
  const __m512i tposeIdx = _mm512_loadu_si512(tposeIdxBytes);
  const __m512i destAddrV = _mm512_loadu_si512(destAddrs);
  __mmask8 mask = _mm512_cmpneq_epi64_mask(destAddrV, _mm512_setzero_si512());
  for (size_t i = destNQword / 8 * 8; i < destNQword; i++) {
    __m512i dat = _mm512_loadu_si512(src + i * 8);
    dat = _mm512_permutexvar_epi8(tposeIdx, dat);
    const __m512i curAddr = _mm512_add_epi64(destAddrV, _mm512_set1_epi64(i * 8));
    _mm512_mask_i64scatter_epi64(0, mask, curAddr, dat, 1);
  }
}

void DmmPrintMramXferTbl(FILE *out, size_t nthrd, long ty) {
  uint64_t *a = aligned_alloc(64, 33554432 * 8);
  uint64_t *b = aligned_alloc(64, 33554432 * 8);
  if (!a || !b) exit(99);
  uint64_t *hostAddrs[8];
  for (size_t i = 0; i < 8; i++)
    hostAddrs[i] = (ty & 4) && i == 2 ? 0 : &a[33554432 * i / 8];
  for (size_t i = 0; i < 33554432; i++) {
    a[i] = i | (i << 32);
    b[i] = i * i;
  }
  fprintf(out, "// %zu threads\nunsigned mram%s%s%s[21][80] = {\n ", nthrd,
          ty & 1 ? "DToH" : "HToD", ty & 2 ? "Bcst" : "", ty & 4 ? "Vacant" : "");
  const uint8_t msk = ty & 4 ? 0b11011111 : 0xff;
  ty &= 3;

  for (size_t qword_log = 2; qword_log <= 22; qword_log++) {
    const size_t qwords = 1 << qword_log;
    uint64_t usec_acc = 0;
    fprintf(out, " [%zu] = { // %zu bytes per DPU", qword_log - 2, qwords * 8);

    for (size_t i = 0; i < 80; ++i) {
      if (i % 8 == 0) 
        fprintf(out, "\n   ");
      struct timespec s, e;
      clock_gettime(CLOCK_MONOTONIC, &s);
      if (ty == 0) {
        push8Bank(b, hostAddrs, qwords, nthrd);
        push8Bank(b, hostAddrs, qwords, nthrd);
        push8Bank(b, hostAddrs, qwords, nthrd);
        push8Bank(b, hostAddrs, qwords, nthrd);
      } else if (ty == 1) {
        extract8Bank(b, hostAddrs, qwords, nthrd);
        extract8Bank(b, hostAddrs, qwords, nthrd);
        extract8Bank(b, hostAddrs, qwords, nthrd);
        extract8Bank(b, hostAddrs, qwords, nthrd);
      } else {
        bcst8Bank(b, hostAddrs[0], qwords, nthrd, msk);
        bcst8Bank(b, hostAddrs[0], qwords, nthrd, msk);
        bcst8Bank(b, hostAddrs[0], qwords, nthrd, msk);
        bcst8Bank(b, hostAddrs[0], qwords, nthrd, msk);
      }
      _mm_sfence();
      clock_gettime(CLOCK_MONOTONIC, &e);
      usec_acc += (e.tv_sec - s.tv_sec) * 999999 + (e.tv_nsec - s.tv_nsec) / 1000;
      fprintf(out, " %lu,", usec_acc);
    }

    fprintf(out, "\n  },");
  }
  fprintf(out, "\n};\n\n");
  free(a); free(b);
}

#ifdef __DMM_XFERTBL_MAIN
int main(int ac, char **av) {
  size_t nthreads = atol(av[1]);
  long ty = atol(av[2]);
  if (ty == 3 || ty >= 7) {
    DmmPrintMramXferTbl(stdout, nthreads, 0);
    DmmPrintMramXferTbl(stdout, nthreads, 1);
    DmmPrintMramXferTbl(stdout, nthreads, 2);
    DmmPrintMramXferTbl(stdout, nthreads, 4);
    DmmPrintMramXferTbl(stdout, nthreads, 5);
    DmmPrintMramXferTbl(stdout, nthreads, 6);
  } else 
    DmmPrintMramXferTbl(stdout, nthreads, ty);
}
#else

extern uint32_t wramDToH[13][80], wramHToD[13][80], wramHToDBcst[13][80];
uint64_t DmmXferOverhead(size_t nrDpu, void *xferAddrs[], uint64_t xferSz, long ty) {
  if (ty & 4) { // WRAM -> lookup table
    typeof(wramHToD) *lut;
    switch (ty & 3) {
    case 0: lut = &wramHToD; break;
    case 2: lut = &wramHToDBcst; break;
    case 1: case 3: lut = &wramDToH;
    }
    nrDpu = (nrDpu - 1) / 32;
    if (xferSz > 32768)
      return (*lut)[12][nrDpu] * xferSz / 32768;
    const uint64_t highPos = 63 - _lzcnt_u64(xferSz), highBit = 1 << highPos;
    const uint64_t lowBits = xferSz ^ highBit;
    return ((*lut)[highPos - 2][nrDpu] * lowBits +
            (*lut)[highPos - 3][nrDpu] * (highBit - lowBits)) >> highPos;
  }

  nrDpu = (nrDpu + 7) / 8;
  uint64_t* dpubank = aligned_alloc(64, xferSz * nrDpu);
  for (size_t i = 0; i < xferSz * nrDpu / 8; ++i)
    dpubank[i] = i * i;
  struct timespec s, e;
  clock_gettime(CLOCK_MONOTONIC, &s);

  // TODO: not locked at 4 threads
  for (size_t i = 0; i < nrDpu; i += 8) {
    switch (ty & 3) {
    case 0:
      push8Bank(dpubank + xferSz * i / 8, (uint64_t**)(xferAddrs + i),
                xferSz / 8, 4); break;
    case 2:
      bcst8Bank(dpubank + xferSz * i / 8, (uint64_t*)(xferAddrs[0]),
                xferSz / 8, 4, 0xff); break;
    case 1: case 3:
      extract8Bank(dpubank + xferSz * i / 8, (uint64_t**)(xferAddrs + i),
                   xferSz / 8, 4); break;
    }
  }
  clock_gettime(CLOCK_MONOTONIC, &e);
  _mm_sfence();
  free(dpubank);
  return (e.tv_sec - s.tv_sec) * 999999 + (e.tv_nsec - s.tv_nsec) / 1000;
}
#endif
