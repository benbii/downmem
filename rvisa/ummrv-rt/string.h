/*
 * RISC-V DPU Runtime - Standard C String Functions
 * Copyright © 2005-2020 Rich Felker, et al. (musl libc implementations)
 * SPDX-License-Identifier: MIT
 */

#ifndef DMMRV_STRING_H
#define DMMRV_STRING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

// Memory manipulation functions
void *memcpy(void *restrict dest, const void *restrict src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *dest, int c, size_t n);
int memcmp(const void *vl, const void *vr, size_t n);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // DMMRV_STRING_H
