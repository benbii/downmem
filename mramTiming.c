#include "downmem.h"
#include <stdlib.h>
#include <assert.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

// Dynamic array functions
static void _sliceinit(_memcmdSlice* arr) {
  arr->capacity = 16;
  arr->size = 0;
  arr->offset = 0;
  arr->data = (_memcmd*)malloc(arr->capacity * sizeof(_memcmd));
}
static void _slicefree(_memcmdSlice* arr) {
  free(arr->data);
}
static inline _memcmd _get(_memcmdSlice* arr, size_t index) {
  return arr->data[arr->offset + index];
}
static size_t _sz(_memcmdSlice* arr) {
  return arr->size;
}
static void _popfront(_memcmdSlice* arr) {
  assert(arr->size > 0);
  arr->offset++;
  arr->size--;
}

static void _push(_memcmdSlice* arr, _memcmd cmd) {
  // Check if we need to resize or compact
  if (arr->offset + arr->size >= arr->capacity) {
    // If more than half the array is unused due to offset, compact
    if (arr->offset > arr->capacity / 2) {
      memmove(arr->data, arr->data + arr->offset, arr->size * sizeof(_memcmd));
      arr->offset = 0;
    } else {
      // Otherwise, resize
      size_t new_capacity = arr->capacity * 2;
      _memcmd* new_data = (_memcmd*)malloc(new_capacity * sizeof(_memcmd));
      memcpy(new_data, arr->data + arr->offset, arr->size * sizeof(_memcmd));
      free(arr->data);
      arr->data = new_data;
      arr->capacity = new_capacity;
      arr->offset = 0;
    }
  }
  arr->data[arr->offset + arr->size] = cmd;
  arr->size++;
}

static _memcmd _rm(_memcmdSlice* arr, size_t index) {
  assert(index < arr->size);
  _memcmd removed = arr->data[arr->offset + index];
  if (index < arr->size - 1) {
    // Move the element at index to the front, then remove front
    arr->data[arr->offset + index] = arr->data[arr->offset];
    arr->data[arr->offset] = removed;
  }
  // Remove front element
  arr->offset++;
  arr->size--;
  return removed;
}

// MramTiming functions
void DmmMramTimingInit(DmmMramTiming* mt) {
  mt->ScheRowAddr = noAddr;
  _sliceinit(&mt->ScheRob);
  _sliceinit(&mt->ScheReadyQ);

  mt->RowbufAddr = noAddr;
  mt->RowbufPrechSince = 9999999;
  mt->RowbufBusSince = 9999999;
  mt->RowbufIoSince = 9999999;
  mt->RowbufInSlot.address = noAddr;
  mt->RowbufIoSlot.address = noAddr;
  mt->RowbufBusSlot.address = noAddr;
  mt->ReadyId = -1;

  mt->NrWait = 0;
  memset(mt->WaitIds, 0, sizeof(mt->WaitIds));
  memset(mt->AckLeft, 0, sizeof(mt->AckLeft));

  mt->StatMemoryCycle = 0;
  mt->StatNrFr = 0;
  mt->StatNrFcfs = 0;
  mt->StatNrAccess = 0;
}

void DmmMramTimingFini(DmmMramTiming* mt) {
  _slicefree(&mt->ScheRob);
  _slicefree(&mt->ScheReadyQ);
}

static long _wordline_addr(long address) {
  return address / WordlineSz * WordlineSz;
}

void DmmMramTimingPush(DmmMramTiming *mt, long begin_addr, long size,
                       long thrd_id) {
  long end_addr = begin_addr + size;
  long ack_nr = 0;

  for (long address = begin_addr; address < end_addr; ) {
    long wordline_addr = _wordline_addr(address);
    long sz = MIN(MIN(address + 8, wordline_addr + WordlineSz),
                  end_addr) - address;

    _memcmd memory_command;
    memory_command.address = address;
    memory_command.thrd_id = thrd_id;

    _push(&mt->ScheRob, memory_command);
    address += sz;
    ack_nr++;
  }

  mt->AckLeft[thrd_id] = ack_nr;
  mt->WaitIds[mt->NrWait] = thrd_id;
  mt->NrWait++;
}

static void _serveMramSched(DmmMramTiming* mt) {
  if (mt->ScheRowAddr != noAddr) {
    for (long i = 0; _sz(&mt->ScheRob) >= i + 1 && i < ReorderWinSz; i++) {
      _memcmd memory_command = _get(&mt->ScheRob, i);
      if (_wordline_addr(memory_command.address) == mt->ScheRowAddr) {
        _rm(&mt->ScheRob, i);
        _push(&mt->ScheReadyQ, memory_command);
        mt->StatNrFr++;
        return;
      }
    }
  }

  if (_sz(&mt->ScheRob) >= 1) {
    _memcmd memcmd = _get(&mt->ScheRob, 0);
    _popfront(&mt->ScheRob);
    long wordline_addr = _wordline_addr(memcmd.address);
    _push(&mt->ScheReadyQ, memcmd);
    mt->ScheRowAddr = wordline_addr;
    mt->StatNrFcfs++;
  }
}

static void _serveRowBuf(DmmMramTiming* mt) {
  if (mt->RowbufInSlot.address != noAddr) {
    _memcmd memory_command = mt->RowbufInSlot;
    long line_addr = memory_command.address / WordlineSz * WordlineSz;

    if (line_addr == mt->RowbufAddr) {
      if (mt->RowbufPrechSince > TRp + 1 + TRcd &&
          mt->RowbufIoSlot.address == noAddr) {
        mt->RowbufIoSlot = memory_command;
        mt->RowbufIoSince = 0;
        mt->RowbufInSlot.address = noAddr;
      }
    } else if (mt->RowbufPrechSince > TRp + 1 + TRas &&
               mt->RowbufIoSince > TCl && mt->RowbufBusSince > TBl) {
      mt->RowbufAddr = line_addr;
      mt->RowbufPrechSince = 0;
    }
  }

  if (mt->RowbufIoSince >= TCl && mt->RowbufIoSlot.address != noAddr &&
      mt->RowbufBusSince > TBl) {
    _memcmd memory_command = mt->RowbufIoSlot;
    mt->RowbufIoSlot.address = noAddr;
    mt->RowbufBusSlot = memory_command;
    mt->RowbufBusSince = 0;
  } else if (mt->RowbufBusSince == TBl) {
    mt->AckLeft[mt->RowbufBusSlot.thrd_id]--;
    mt->StatNrAccess++;
  }

  mt->RowbufPrechSince++;
  mt->RowbufBusSince++;
  mt->RowbufIoSince++;
}

void DmmMramTimingCycle(DmmMramTiming* mt) {
  // Move scheduling results into row buffer
  if (mt->RowbufInSlot.address == noAddr && _sz(&mt->ScheReadyQ) >= 1) {
    mt->RowbufInSlot = _get(&mt->ScheReadyQ, 0);
    _popfront(&mt->ScheReadyQ);
  }

  // Move fulfilled DMA memory requests into output
  for (long i = 0; i < mt->NrWait; i++) {
    long id = mt->WaitIds[i];
    if (mt->AckLeft[id] == 0) {
      mt->NrWait--;
      mt->WaitIds[i] = mt->WaitIds[mt->NrWait];
      mt->ReadyId = id;
      break;
    }
  }

  _serveMramSched(mt);
  _serveRowBuf(mt);
  mt->StatMemoryCycle += 1;
}
