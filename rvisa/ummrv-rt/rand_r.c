/*
 * musl libc PRNG functions
 * Copyright © 2005-2020 Rich Felker, et al.
 * SPDX-License-Identifier: MIT
 */

#include "stdlib.h"
#include "syslib.h"

// MT19937-inspired tempering function for better statistical quality
static unsigned temper(unsigned x) {
  x ^= x >> 11;
  x ^= x << 7 & 0x9D2C5680;
  x ^= x << 15 & 0xEFC60000;
  x ^= x >> 18;
  return x;
}
int rand_r(unsigned *seed) {
  return temper(*seed = *seed * 1103515245 + 12345) / 2;
}

// Global per-tasklet seeds (initialized to random values)
static unsigned global_seeds[24] = {
    0x6B8B4567, 0x327B23C6, 0x643C9869, 0x66334873, 0x74B0DC51, 0x19495CFF,
    0x2AE8944A, 0x625558EC, 0x238E1F29, 0x46E87CCD, 0x3D1B58BA, 0x507ED7AB,
    0x2EB141F2, 0x41B71EFB, 0x79E2A9E3, 0x7545E146, 0x515F007C, 0x5BD062C2,
    0x12200854, 0x4DB127F8, 0x1F16E9E8, 0x1190CDE7, 0x39E95A51, 0x2BAA1A14};
void srand(unsigned s) { global_seeds[me()] = s; }
int rand(void) { return rand_r(&global_seeds[me()]); }
