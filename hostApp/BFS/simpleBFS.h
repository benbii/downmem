#pragma once
#include <stdio.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define ceilMult2(x)    ((((x) + 1)/2)*2)
#define ceilMult8(x)    ((((x) + 7)/8)*8)
#define ceilMult64(x)   ((((x) + 63)/64)*64)
#define setOneBit(val, idx) (val) |= (1ull << (idx))
#define bitIsSet(val, idx)  ((val) & (1ull << (idx)))
#define INFO(fmt, ...)
#define ERROR(fmt, ...)                                                        \
  fprintf(stderr, "\033[0;31mERROR:\033[0m   " fmt "\n", ##__VA_ARGS__)
#define WARNING(fmt, ...)                                                      \
  fprintf(stderr, "\033[0;35mWARNING:\033[0m " fmt "\n", ##__VA_ARGS__)
// #define INFO(fmt, ...)                                                         \
//   printf("\033[0;32mINFO:\033[0m    " fmt "\n", ##__VA_ARGS__);

struct SimpleBFSArgs {
  uint32_t nVertInDpu; /* The number of nodes assigned to this DPU */
  uint32_t nVertTot; /* Total number of nodes in the graph  */
  uint32_t vertBeg; /* The index of the first node assigned to this DPU  */
  uint32_t vertBegOff; /* Offset of the node pointers */
  uint32_t level; /* The current BFS level */
  uint32_t nodePtrOff;
  uint32_t neighborIdxsOff;
  uint32_t nodeLvlOff;
  uint32_t visitedOff;
  uint32_t curFrontOff;
  uint32_t nxtFrontOff;
  uint32_t pad;
};

struct COODat {
  size_t nVerts;
  size_t nEdges;
  uint32_t *froms;
  uint32_t *tos;
};
struct CSRDat {
  size_t nVerts;
  size_t nEdges;
  uint32_t *offsets;
  uint32_t *trans;
};

struct COODat readCOOGraph(FILE* fp);
void freeCOO(struct COODat cooGraph);
struct CSRDat covCOOtoCSR(struct COODat cooGraph);
void freeCSRGraph(struct CSRDat csrGraph);
void writeCSRToFile(struct CSRDat csrDat, FILE* f);
struct CSRDat readCSRFromFile(FILE* f);

void bfsHostRef(struct CSRDat csrG, uint32_t* outLvls, size_t srcNode);
void bfsDPU(struct CSRDat csrG, uint32_t *outLvls, size_t srcNode,
            uint32_t nrDPU, const char *dpuBin);

#ifdef __cplusplus
}
#endif

