// FIXME: being lazy with asserts here. Asserted expressions will run on release
#include <stddef.h>
#ifdef NDEBUG
#undef NDEBUG
#include <assert.h>
#define NDEBUG
#else
#include <assert.h>
#endif
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "simpleBFS.h"

// File format: magicIdentifier, nVert, nEdge
// then N pairs of ints follow, denoting source and destination
struct COODat readCOOGraph(FILE* fp) {
  struct COODat coo;
  // Initialize fields
  size_t magic;
  assert(fscanf(fp, "%zu", &coo.nVerts));
  assert(fscanf(fp, "%zu", &magic));
  if (coo.nVerts != magic)
    ERROR("COO data magic mismatch: %zu, expect: %zu", magic, coo.nEdges);
  if (coo.nVerts % 64 != 0) {
    WARNING("Adjacency matrix dimension is %zu which is not a "
                  "multiple of 64 nodes.", coo.nVerts);
    coo.nVerts += (64 - coo.nVerts % 64);
    WARNING("  Padding to %zu which is a multiple of 64 nodes.",
                  coo.nVerts);
  }
  assert(fscanf(fp, "%zu", &coo.nEdges));
  coo.froms = (uint32_t *)malloc(coo.nEdges * sizeof(uint32_t));
  coo.tos = (uint32_t *)malloc(coo.nEdges * sizeof(uint32_t));

  // Read the neighborIdxs
  for (uint32_t edgeIdx = 0; edgeIdx < coo.nEdges; ++edgeIdx) {
    uint32_t from, to;
    assert(fscanf(fp, "%u", &from));
    coo.froms[edgeIdx] = from;
    assert(fscanf(fp, "%u", &to));
    coo.tos[edgeIdx] = to;
  }
  // fclose(fp);
  return coo;
}

void freeCOO(struct COODat cooGraph) {
  free(cooGraph.froms);
  free(cooGraph.tos);
}

struct CSRDat covCOOtoCSR(struct COODat coo) {
  struct CSRDat csr;

  // Initialize fields
  csr.nVerts = coo.nVerts;
  csr.nEdges = coo.nEdges;
  csr.offsets = (uint32_t *)calloc(csr.nVerts + 1, sizeof(uint32_t));
  csr.trans = (uint32_t *)malloc(csr.nEdges * sizeof(uint32_t));

  // Histogram frequencies of fromIdxs; save to res->offset for now
  for (uint32_t i = 0; i < coo.nEdges; ++i) {
    uint32_t nodeIdx = coo.froms[i];
    csr.offsets[nodeIdx]++;
  }

  // Exclusive Scan that yields offset array
  uint32_t curSum = 0;
  for (uint32_t nodeIdx = 0; nodeIdx < csr.nVerts; ++nodeIdx) {
    uint32_t prevSum = curSum;
    curSum += csr.offsets[nodeIdx];
    csr.offsets[nodeIdx] = prevSum;
  }
  csr.offsets[csr.nVerts] = curSum;

  // Write `toIdxs` in original graph to approperiate positions of `trans` in CSR
  // graph. The `offset` pointer indicates the next unwritten position in `trans`
  for (uint32_t i = 0; i < coo.nEdges; ++i) {
    uint32_t from = coo.froms[i];
    uint32_t transPosToWrite = csr.offsets[from]++;
    csr.trans[transPosToWrite] = coo.tos[i];
  }

  // Now that every edge having been transformed, the offset array, i.e., "next
  // position to write" is the offset of the next vertex. Shift the offset array
  // left by 1 to restore it.
  for (size_t i = csr.nVerts - 1; i > 0; --i)
    csr.offsets[i] = csr.offsets[i - 1];
  csr.offsets[0] = 0;
  return csr;
}

void writeCSRToFile(struct CSRDat csr, FILE* f) {
  fprintf(f, "%zu %zu %zu\n", csr.nVerts, csr.nEdges, csr.nEdges);
  // TODO: loop unroll
  // Offsets
  for (size_t i = 0; i <= csr.nVerts; ++i) {
    if ((i & 7) == 0)
      assert(fputc('\n', f));
    assert(fprintf(f, "%u ", csr.offsets[i]));
  }
  // Transitions (destination edges)
  assert(fputc('\n', f));
  for (size_t i = 0; i < csr.nEdges; ++i) {
    if ((i & 7) == 0)
      assert(fputc('\n', f));
    assert(fprintf(f, "%u ", csr.trans[i]));
  }
}

struct CSRDat readCSRFromFile(FILE* f) {
  struct CSRDat csrDat;
  size_t magic;
  assert(fscanf(f, "%zu %zu %zu\n", &csrDat.nVerts, &csrDat.nEdges, &magic));
  if (magic != csrDat.nEdges)
    ERROR("CSR data magic mismatch: %zu, expect: %zu", magic, csrDat.nEdges);
  // TODO: Pad to multiple of 64?
  csrDat.offsets = malloc(sizeof(uint32_t) * csrDat.nVerts + 1);
  csrDat.trans = malloc(sizeof(uint32_t) * csrDat.nEdges);
  for (size_t i = 0; i <= csrDat.nVerts; ++i)
    assert(fscanf(f, "%u", csrDat.offsets + i));
  for (size_t i = 0; i < csrDat.nEdges; ++i)
    assert(fscanf(f, "%u", csrDat.trans + i));
  return csrDat;
}

void freeCSRGraph(struct CSRDat csrGraph) {
  free(csrGraph.offsets);
  free(csrGraph.trans);
}

void bfsHostRef(struct CSRDat csrG, uint32_t* outLvls, size_t srcNode) {
  memset(outLvls, 0, csrG.nVerts * sizeof(uint32_t));
  uint64_t* nextFrontier = calloc(csrG.nVerts / 8, 1);
  uint64_t* currentFrontier = calloc(csrG.nVerts / 8, 1);
  uint64_t* visited = calloc(csrG.nVerts / 8, 1);
  setOneBit(nextFrontier[srcNode / 64], srcNode % 64);
  _Bool nxtFrtEmpty = 0;
  uint32_t level = 1;

  while (!nxtFrtEmpty) {
    // Update current frontier and visited list based on the next frontier from
    // the previous iteration
    for (size_t tileIdx = 0; tileIdx < csrG.nVerts / 64; ++tileIdx) {
      uint64_t nxtTile = nextFrontier[tileIdx];
      currentFrontier[tileIdx] = nxtTile;
      if (nxtTile) {
        visited[tileIdx] |= nxtTile;
        nextFrontier[tileIdx] = 0;
        for (size_t node = tileIdx * 64; node < (tileIdx + 1) * 64; ++node)
          if (bitIsSet(nxtTile, node % 64))
            outLvls[node] = level;
      }
    }

    // Visit neighbors of the current frontier
    nxtFrtEmpty = 1;
    for (size_t tileIdx = 0; tileIdx < csrG.nVerts / 64; ++tileIdx) {
      uint64_t curTile = currentFrontier[tileIdx];
      if (!curTile)
        continue;
      for (size_t curVert = tileIdx * 64; curVert < (tileIdx + 1) * 64; ++curVert) {
        if (!bitIsSet(curTile, curVert % 64))
          continue;
        // If the node is in the current frontier, visit its neighbors
        uint32_t curVertOff = csrG.offsets[curVert];
        uint32_t nxtVertOff = csrG.offsets[curVert + 1];
        for (uint32_t i = curVertOff; i < nxtVertOff; ++i) {
          uint32_t to = csrG.trans[i];
          if (bitIsSet(visited[to / 64], to % 64))
            continue;
          // Neighbor not previously visited, add neighbor to next frontier
          setOneBit(nextFrontier[to / 64], to % 64);
          nxtFrtEmpty = 0;
        }
      }
    }
    ++level;
  }

  free(nextFrontier);
  free(currentFrontier);
  free(visited);
}
