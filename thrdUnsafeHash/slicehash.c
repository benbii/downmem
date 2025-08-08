#include "hashmap.h"
#include <string.h>

struct user {
  const void* name;
  size_t sz;
  uint_fast32_t value;
};
static uint64_t user_hash(const void *item, uint64_t seed0, uint64_t seed1) {
  const struct user *user = item;
  return hashmap_sip(user->name, user->sz, seed0, seed1);
}
static int user_compare(const void *a, const void *b, void *udata) {
  const struct user *ua = a;
  const struct user *ub = b;
  // 0 for equal
  if (ua->sz - ub->sz)
    return ua->sz - ub->sz;
  return memcmp(ua->name, ub->name, ua->sz);
}

typedef struct hashmap* DmmMap;
DmmMap DmmMapInit(size_t initCap) {
  static uint64_t seed1 = 1234567, seed2 = 0x890abcd;
  struct hashmap *map = hashmap_new(sizeof(struct user), initCap, seed1, seed2,
                                    user_hash, user_compare, NULL, NULL);
  seed1 *= seed2;
  seed2 ^= seed1;
  return map;
}
void DmmMapAssign(DmmMap map, const void *str, size_t sz,
                       uint_fast32_t val) {
  hashmap_set(map, &(struct user){ .name=str, .sz=sz, .value=val });
}
uint_fast32_t DmmMapFetch(DmmMap map, const void* str, size_t sz) {
  struct user u = { .name=str, .sz=sz };
  struct user *a = (struct user*)hashmap_get(map, &u);
  return a != NULL ? a->value : 0x44f8a1ef;
}
void DmmMapFini(DmmMap map) { hashmap_free(map); }

