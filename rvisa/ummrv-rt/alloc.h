#ifndef ALLOC_H
#define ALLOC_H

#include <stdint.h>
#include <stddef.h>
#include "syslib.h"

#ifdef __cplusplus
extern "C" {
#endif

void* mem_alloc(size_t size);
void mem_reset(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif // ALLOC_H