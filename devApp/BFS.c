#include <alloc.h>
#include <barrier.h>
#include <defs.h>
#include <mram.h>
#include <mutex.h>

#define ceilMult2(x)    ((((x) + 1)/2)*2)
#define ceilMult8(x)    ((((x) + 7)/8)*8)
#define ceilMult64(x)   ((((x) + 63)/64)*64)
#define setOneBit(val, idx) (val) |= (1ull << (idx))
#define bitIsSet(val, idx)  ((val) & (1ull << (idx)))

struct SimpleBFSArgs {
  uint32_t nVertInDpu; /* The number of nodes assigned to this DPU */
  uint32_t nVertTot; /* Total number of nodes in the graph  */
  uint32_t vertBeg; /* The index of the first node assigned to this DPU  */
  uint32_t vertBegOff; /* Offset of the node pointers */
  uint32_t level; /* The current BFS level */
  __mram_ptr uint32_t* nodePtrOff;
  __mram_ptr uint32_t* neighborIdxsOff;
  __mram_ptr uint32_t* nodeLvlOff;
  __mram_ptr uint64_t* visitedOff;
  __mram_ptr uint64_t* curFrontOff;
  __mram_ptr uint64_t* nxtFrontOff;
  uint32_t pad;
};
_Static_assert(sizeof(__mram_ptr uint32_t*) == 4, "UPMEM ptr must be 4 bytes");

#if NR_TASKLETS < 12
#error Too few tasklets!
#endif

BARRIER_INIT(bfsBarrier, NR_TASKLETS);
MUTEX_INIT(nextFrontierMutex);

int main() {
  // Load parameters
  struct SimpleBFSArgs arg;
  mram_read(DPU_MRAM_HEAP_POINTER, &arg, sizeof(struct SimpleBFSArgs));
  if (arg.nVertInDpu <= 0) return 0;
  arg.nodePtrOff = DPU_MRAM_HEAP_POINTER + (uintptr_t)arg.nodePtrOff;
  arg.neighborIdxsOff = DPU_MRAM_HEAP_POINTER + (uintptr_t)arg.neighborIdxsOff;
  arg.nodeLvlOff = DPU_MRAM_HEAP_POINTER + (uintptr_t)arg.nodeLvlOff;
  arg.visitedOff = DPU_MRAM_HEAP_POINTER + (uintptr_t)arg.visitedOff;
  arg.curFrontOff = DPU_MRAM_HEAP_POINTER + (uintptr_t)arg.curFrontOff;
  arg.nxtFrontOff = DPU_MRAM_HEAP_POINTER + (uintptr_t)arg.nxtFrontOff;

  // Update current frontier and visited list based on the next frontier from
  // the previous iteration
  for (uint32_t nodeTileIdx = me(); nodeTileIdx < arg.nVertTot / 64;
       nodeTileIdx += NR_TASKLETS) {
    // Get the next frontier tile from MRAM
    uint64_t nextFrontierTile = arg.nxtFrontOff[nodeTileIdx];

    // Process next frontier tile if it is not empty
    if (nextFrontierTile) {
      // Mark everything that was previously added to the next frontier as visited
      uint64_t visitedTile = arg.visitedOff[nodeTileIdx];
      visitedTile |= nextFrontierTile;
      arg.visitedOff[nodeTileIdx] = visitedTile;
      // Clear the next frontier
      arg.nxtFrontOff[nodeTileIdx] = 0;
    }

    // Extract the current frontier from the previous next frontier and update
    // node levels
    uint32_t startTileIdx = arg.vertBeg / 64;
    uint32_t numTiles = arg.nVertInDpu / 64;
    if (startTileIdx <= nodeTileIdx && nodeTileIdx < startTileIdx + numTiles) {

      // Update current frontier
      arg.curFrontOff[nodeTileIdx - startTileIdx] = nextFrontierTile;

      // Update node levels
      if (nextFrontierTile) {
        uint64_t mask = 1;
        for (uint32_t node = nodeTileIdx * 64; node < (nodeTileIdx + 1) * 64;
             ++node) {
          if (nextFrontierTile & mask) {
            // No false sharing so no need for locks
            arg.nodeLvlOff[node - arg.vertBeg] = arg.level;
          }
          mask <<= 1;
        }
      }
    }
  }

  // Wait until all tasklets have updated the current frontier
  barrier_wait(&bfsBarrier);

  // Identify tasklet's nodes
  uint32_t numNodesPerTasklet = (arg.nVertInDpu + NR_TASKLETS - 1) / NR_TASKLETS;
  uint32_t taskletNodesStart = me() * numNodesPerTasklet;
  uint32_t taskletNumNodes;
  if (taskletNodesStart > arg.nVertInDpu) {
    taskletNumNodes = 0;
  } else if (taskletNodesStart + numNodesPerTasklet > arg.nVertInDpu) {
    taskletNumNodes = arg.nVertInDpu - taskletNodesStart;
  } else {
    taskletNumNodes = numNodesPerTasklet;
  }

  // Visit neighbors of the current frontier
  mutex_id_t mutexID = MUTEX_GET(nextFrontierMutex);
  for (uint32_t node = taskletNodesStart;
  node < taskletNodesStart + taskletNumNodes; ++node) {
    uint32_t nodeTileIdx = node / 32;
    // TODO: Optimize: load tile then loop over nodes in the tile
    uint32_t currentFrontierTile =
        ((__mram_ptr uint32_t *)arg.curFrontOff)[nodeTileIdx];

    // If the node is in the current frontier
    if (1 & (currentFrontierTile >> node % 32)) {
      // Visit its neighbors
      uint32_t nodePtr = arg.nodePtrOff[node] - arg.vertBegOff;
      // TODO: Optimize: might be in the same 8B as nodePtr
      uint32_t nextNodePtr = arg.nodePtrOff[node + 1] - arg.vertBegOff;
      for (uint32_t i = nodePtr; i < nextNodePtr; ++i) {
        // TODO: Optimize: sequential access to neighbors can use sequential reader
        uint32_t neighbor = arg.neighborIdxsOff[i];
        uint32_t neighborTileIdx = neighbor / 64;
        uint64_t visitedTile = arg.visitedOff[neighborTileIdx];
        if (!bitIsSet(visitedTile, neighbor % 64)) {
          // Neighbor not previously visited
          // Add neighbor to next frontier
          mutex_lock(mutexID);
          // TODO: Optimize: use more locks to reduce contention
          uint64_t nextFrontierTile = arg.nxtFrontOff[neighborTileIdx];
          setOneBit(nextFrontierTile, neighbor % 64);
          arg.nxtFrontOff[neighborTileIdx] = nextFrontierTile;
          mutex_unlock(mutexID);
        }
      }
    }
  }

  return 0;
}
