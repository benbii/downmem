#include "simpleBFS.h"
#include <stdlib.h>
#include <string.h>

static void printArr(const uint32_t* arr, size_t sz, FILE* stream) {
  for (size_t i = 0; i < sz; ++i) {
    fprintf(stream, "%u ", arr[i]);
    if ((i & 15) == 15)
      fputc('\n', stream);
  }
}

int main(int argc, char** argv) {
  if (argc == 1)
    return fputs("Too few arguments\n", stderr);
  if (0 == strcmp(argv[1], "genCSR")) {
    FILE *inF = fopen(argv[2], "r"), *outF = fopen(argv[3], "w");
    if (inF == NULL || outF == NULL)
      return fputs("Cannot open input or output file\n", stderr);

    struct COODat coo = readCOOGraph(inF);
    struct CSRDat csr = covCOOtoCSR(coo);
    writeCSRToFile(csr, outF);
    freeCOO(coo);
    freeCSRGraph(csr);
    fclose(inF); fclose(outF);

  } else if (argv[1] == strstr(argv[1], "simpleBFS")) {
    size_t srcVert = atol(argv[2]);
    INFO("Source: %zu\n", srcVert);
    FILE* stream = fopen(argv[3], "r");
    if (stream == NULL)
      return fputs("Cannot open input CSR file\n", stderr);

    struct CSRDat csr = readCSRFromFile(stream);
    fclose(stream);
    uint32_t* outLvls = malloc(sizeof(uint32_t) * csr.nVerts);
    if (strchr(argv[1], 'D') == NULL)
      bfsHostRef(csr, outLvls, srcVert);
    else
      bfsDPU(csr, outLvls, srcVert, atoi(argv[6]), argv[5]);

    // Print the results
    stream = fopen(argv[4], "w");
    stream = stream != NULL ? stream : stdout;
    printArr(outLvls, csr.nVerts, stream);
  } else
    return fputs("Incorrect subcommand\n", stderr);
  return 0;
}
