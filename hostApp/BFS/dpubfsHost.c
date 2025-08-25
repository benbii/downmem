#include "simpleBFS.h"
#include <dpu.h>
#include <string.h>
#include <stdlib.h>

#define DPU_CAPACITY (64 << 20) // A DPU's capacity is 64 MiB

struct mram_heap_allocator_t {
  uint32_t totalAllocated;
};
static void init_allocator(struct mram_heap_allocator_t *allocator) {
  allocator->totalAllocated = 0;
}
static uint32_t mram_heap_alloc(struct mram_heap_allocator_t *allocator,
                                uint32_t size) {
  uint32_t ret = allocator->totalAllocated;
  allocator->totalAllocated += ceilMult8(size);
  if (allocator->totalAllocated > DPU_CAPACITY) {
    ERROR("Total memory allocated is %d bytes which exceeds the "
                "DPU capacity (%d bytes)!",
                allocator->totalAllocated, DPU_CAPACITY);
    exit(0);
  }
  return ret;
}
static void toDpuWrap(struct dpu_set_t dpu, uint8_t *hostPtr, uint32_t mramIdx,
                      uint32_t size) {
  DPU_ASSERT(dpu_copy_to(dpu, DPU_MRAM_HEAP_POINTER_NAME, mramIdx, hostPtr,
                         ceilMult8(size)));
}
static void fromDpuWrap(struct dpu_set_t dpu, uint32_t mramIdx,
                        uint8_t *hostPtr, uint32_t size) {
  DPU_ASSERT(dpu_copy_from(dpu, DPU_MRAM_HEAP_POINTER_NAME, mramIdx, hostPtr,
                           ceilMult8(size)));
}

void bfsDPU(struct CSRDat csr, uint32_t *nodeLevel, size_t srcNode,
            uint32_t numDPUs, const char* dpuBin) {
  // Allocate DPUs and load binary
  struct dpu_set_t dpuSet, curDpu;
  DPU_ASSERT(dpu_alloc(numDPUs, NULL, &dpuSet));
  DPU_ASSERT(dpu_load(dpuSet, dpuBin, NULL));
  DPU_ASSERT(dpu_get_nr_dpus(dpuSet, &numDPUs));
  INFO("Allocated %d DPU(s)", numDPUs);

  // Initialize BFS data structures
  INFO("  Graph has %zu nodes and %zu edges", csr.nVerts,
             csr.nEdges);
  // Node's BFS level (initially all 0 meaning not reachable)
  // uint32_t *nodeLevel = calloc(numNodes, sizeof(uint32_t));
  memset(nodeLevel, 0, csr.nVerts * sizeof(uint32_t));
  // Bit vector with one bit per node
  uint64_t *visited = calloc(csr.nVerts / 64, sizeof(uint64_t));
  // Bit vector with one bit per node
  uint64_t *curFrontier = calloc(csr.nVerts / 64, sizeof(uint64_t));
  // Bit vector with one bit per node
  uint64_t *nextFrontier = calloc(csr.nVerts / 64, sizeof(uint64_t));
  setOneBit(nextFrontier[srcNode / 64], srcNode % 64);
  uint32_t level = 1;

  // Partition data structure across DPUs
  uint32_t nVertPerDpuHint = ceilMult64((csr.nVerts - 1) / numDPUs + 1);
  INFO("Assigning %u nodes per DPU", nVertPerDpuHint);
  struct SimpleBFSArgs args[numDPUs];
  uint32_t argsMramPos[numDPUs];
  unsigned int dpuIdx = 0;

  DPU_FOREACH(dpuSet, curDpu) {
    // Allocate parameters
    struct mram_heap_allocator_t allocator;
    init_allocator(&allocator);
    argsMramPos[dpuIdx] = mram_heap_alloc(&allocator, sizeof(struct SimpleBFSArgs));

    // Find DPU's nodes
    uint32_t dpuStartNodeIdx = dpuIdx * nVertPerDpuHint;
    uint32_t nVertInDpu;
    if (dpuStartNodeIdx > csr.nVerts) {
      nVertInDpu = 0;
    } else if (dpuStartNodeIdx + nVertPerDpuHint > csr.nVerts) {
      nVertInDpu = csr.nVerts - dpuStartNodeIdx;
    } else {
      nVertInDpu = nVertPerDpuHint;
    }
    args[dpuIdx].nVertInDpu = nVertInDpu;
    INFO("DPU %u: Receives %u nodes", dpuIdx, nVertInDpu);

    // Partition edges and copy data
    if (nVertInDpu > 0) {

      // Find DPU's CSR graph partition
      uint32_t *dpuNodePtrs_h = &csr.offsets[dpuStartNodeIdx];
      uint32_t dpuNodePtrsOffset = dpuNodePtrs_h[0];
      uint32_t *dpuNeighborIdxs_h = csr.trans + dpuNodePtrsOffset;
      uint32_t dpuNumNeighbors = dpuNodePtrs_h[nVertInDpu] - dpuNodePtrsOffset;
      uint32_t *dpuNodeLevel_h = &nodeLevel[dpuStartNodeIdx];

      // Allocate MRAM
      uint32_t dpuNodePtrs_m =
          mram_heap_alloc(&allocator, (nVertInDpu + 1) * sizeof(uint32_t));
      uint32_t dpuNeighborIdxs_m =
          mram_heap_alloc(&allocator, dpuNumNeighbors * sizeof(uint32_t));
      uint32_t dpuNodeLevel_m =
          mram_heap_alloc(&allocator, nVertInDpu * sizeof(uint32_t));
      uint32_t dpuVisited_m =
          mram_heap_alloc(&allocator, csr.nVerts / 64 * sizeof(uint64_t));
      uint32_t dpuCurrentFrontier_m =
          mram_heap_alloc(&allocator, nVertInDpu / 64 * sizeof(uint64_t));
      uint32_t dpuNextFrontier_m =
          mram_heap_alloc(&allocator, csr.nVerts / 64 * sizeof(uint64_t));
      INFO("  Total memory allocated is %d bytes",
                 allocator.totalAllocated);

      // Set up DPU parameters
      args[dpuIdx].nVertTot = csr.nVerts;
      args[dpuIdx].vertBeg = dpuStartNodeIdx;
      args[dpuIdx].vertBegOff = dpuNodePtrsOffset;
      args[dpuIdx].level = level;
      args[dpuIdx].nodePtrOff = dpuNodePtrs_m;
      args[dpuIdx].neighborIdxsOff = dpuNeighborIdxs_m;
      args[dpuIdx].nodeLvlOff = dpuNodeLevel_m;
      args[dpuIdx].visitedOff = dpuVisited_m;
      args[dpuIdx].curFrontOff = dpuCurrentFrontier_m;
      args[dpuIdx].nxtFrontOff = dpuNextFrontier_m;

      // Send data to DPU
      toDpuWrap(curDpu, (uint8_t *)dpuNodePtrs_h, dpuNodePtrs_m,
                (nVertInDpu + 1) * sizeof(uint32_t));
      toDpuWrap(curDpu, (uint8_t *)dpuNeighborIdxs_h, dpuNeighborIdxs_m,
                dpuNumNeighbors * sizeof(uint32_t));
      toDpuWrap(curDpu, (uint8_t *)dpuNodeLevel_h, dpuNodeLevel_m,
                nVertInDpu * sizeof(uint32_t));
      toDpuWrap(curDpu, (uint8_t *)visited, dpuVisited_m,
                csr.nVerts / 64 * sizeof(uint64_t));
      toDpuWrap(curDpu, (uint8_t *)nextFrontier, dpuNextFrontier_m,
                csr.nVerts / 64 * sizeof(uint64_t));
      // NOTE: No need to copy current frontier because it is written before
      // being read *in the DPU binary*
    }

    // Send parameters to DPU
    toDpuWrap(curDpu, (uint8_t *)&args[dpuIdx], argsMramPos[dpuIdx],
              sizeof(struct SimpleBFSArgs));
    ++dpuIdx;
  } // End of partitioning and transferring graph to DPU

  // Start running DPU binary. Iterate until next frontier is empty.
  uint32_t frontEmpty = 0;
  while (!frontEmpty) {
    INFO("Processing current frontier for level %u", level);
    // Run all DPUs
    DPU_ASSERT(dpu_launch(dpuSet, DPU_SYNCHRONOUS));

    // Copy back next frontier from all DPUs and compute their union as the
    // current frontier
    dpuIdx = 0;
    DPU_FOREACH(dpuSet, curDpu) {
      uint32_t dpuNumNodes = args[dpuIdx].nVertInDpu;
      if (dpuIdx == 0) {
        fromDpuWrap(curDpu, args[dpuIdx].nxtFrontOff,
                    (uint8_t *)curFrontier, csr.nVerts / 8);
      } else if (dpuNumNodes > 0) {
        fromDpuWrap(curDpu, args[dpuIdx].nxtFrontOff,
                    (uint8_t *)nextFrontier, csr.nVerts / 8);
        for (uint32_t i = 0; i < csr.nVerts / 64; ++i)
          curFrontier[i] |= nextFrontier[i];
      }
      ++dpuIdx;
    }

    // Check if the next frontier is empty, and copy data to DPU if not empty
    frontEmpty = 1;
    for (uint32_t i = 0; i < csr.nVerts / 64; ++i) {
      if (curFrontier[i]) {
        frontEmpty = 0;
        break;
      }
    }
    if (!frontEmpty) {
      ++level;
      dpuIdx = 0;
      DPU_FOREACH(dpuSet, curDpu) {
        if (args[dpuIdx].nVertInDpu == 0)
          continue;
        // Copy current frontier to all DPUs (place in next frontier and DPU
        // will update visited and copy to current frontier)
        toDpuWrap(curDpu, (uint8_t *)curFrontier,
                  args[dpuIdx].nxtFrontOff,
                  csr.nVerts / 64 * sizeof(uint64_t));
        // Copy new level to DPU
        args[dpuIdx].level = level;
        toDpuWrap(curDpu, (uint8_t *)&args[dpuIdx], argsMramPos[dpuIdx],
                  sizeof(struct SimpleBFSArgs));
        ++dpuIdx;
      }
    }
  }

  // Copy back node levels
  dpuIdx = 0;
  DPU_FOREACH(dpuSet, curDpu) {
    uint32_t dpuNumNodes = args[dpuIdx].nVertInDpu;
    if (dpuNumNodes > 0) {
      uint32_t dpuStartNodeIdx = dpuIdx * nVertPerDpuHint;
      // uint32_t dpuStartNodeIdx = SimpleBFSArgs[dpuIdx].dpuStartNodeIdx;
      // assert(dpuStartNodeIdx == SimpleBFSArgs[dpuIdx].dpuStartNodeIdx);
      fromDpuWrap(curDpu, args[dpuIdx].nodeLvlOff,
                  (uint8_t *)(nodeLevel + dpuStartNodeIdx),
                  dpuNumNodes * sizeof(float));
    }
    ++dpuIdx;
  }

  dpu_free(dpuSet);
  free(visited);
  free(curFrontier);
  free(nextFrontier);
}
