/*
 * RISC-V DPU Runtime - Minimal Standard C Library Functions
 */

#ifndef DMMRV_STDLIB_H
#define DMMRV_STDLIB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

// Division result types
typedef struct {
	int quot;
	int rem;
} div_t;

typedef struct {
	long quot;
	long rem;
} ldiv_t;

typedef struct {
	long long quot;
	long long rem;
} lldiv_t;

// Program termination
static inline void abort(void) {
	__asm__ volatile("ebreak");
	__builtin_unreachable();
}

static inline void exit(int status) {
	// Preserve status in a0 and halt current thread
	register uint32_t a0 __asm__("a0") = (uint32_t)status;
	register uint32_t thread_id;
	__asm__ volatile("myid %0" : "=r"(thread_id));
	register uint32_t mask = 1U << thread_id;
	__asm__ volatile("csrrc zero, 0x000, %0" : : "r"(mask), "r"(a0));
	__builtin_unreachable();
}

// Environment (not supported in freestanding environment)
static inline char *getenv(const char *name) {
	(void)name;
	return NULL;
}

// Random number generation (implemented in rand_r.c)
int rand_r(unsigned *seed);
void srand(unsigned seed);
int rand(void);

// Absolute value functions
static inline int abs(int x) {
	return x < 0 ? -x : x;
}

static inline long labs(long x) {
	return x < 0 ? -x : x;
}

static inline long long llabs(long long x) {
	return x < 0 ? -x : x;
}

// Division functions
static inline div_t div(int numer, int denom) {
	div_t result;
	result.quot = numer / denom;
	result.rem = numer % denom;
	return result;
}

static inline ldiv_t ldiv(long numer, long denom) {
	ldiv_t result;
	result.quot = numer / denom;
	result.rem = numer % denom;
	return result;
}

static inline lldiv_t lldiv(long long numer, long long denom) {
	lldiv_t result;
	result.quot = numer / denom;
	result.rem = numer % denom;
	return result;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // DMMRV_STDLIB_H
