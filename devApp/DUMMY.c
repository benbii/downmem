#include <defs.h>
#include <mram.h>
__host uint8_t dummyWram[60000];

int main() {
  // for (uint32_t i = 0; i < 60000; ++i)
  //   ++dummyWram[i];
  ++dummyWram[59999];
  return 0;
}

