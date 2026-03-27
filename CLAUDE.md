# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

Downmem is a C library that simulates DPU/PIM (Processing-In-Memory) instruction execution and memory transfers. It supports two ISAs: UPMEM DPUs and a hypothetical RISC-V-based DPU. The host API (`dpu.h`) mirrors the real UPMEM SDK so benchmarks can run identically on either simulated architecture.

## Build Commands

CMake + Ninja. The actual CMake options are `DMM_UPMEM` and `DMM_RV` (booleans), not the `DMM_ISA` string mentioned in the README.

```bash
# Configure (both ISAs enabled by default)
cmake -GNinja -DCMAKE_BUILD_TYPE=Release -S. -Bbuild

# Build host executables only
ninja -C build

# Build device programs too (EXCLUDE_FROM_ALL, must be explicit)
ninja -C build dpuExamples
```

To use a custom toolchain compiler: `-DCMAKE_C_COMPILER=/path/to/toolchain/bin/clang`

## Running Tests

No unit-test framework. Testing is benchmark execution with self-validation (host reference comparison, BFS output diff).

```bash
bash rvRunTests.sh /path/to/toolchain    # RISC-V only
bash ummRunTests.sh /path/to/toolchain   # UPMEM only
bash runTests.sh /path/to/toolchain      # both ISAs
```

Run a single benchmark directly:
```bash
build/dmmVA 15728640 2560 build/devApp/rvbins/VA        # RV
build/dmmVA 15728640 2560 build/devApp/objdumps/VA.objdump  # UPMEM
```

The test scripts nuke and recreate `build/` when given a toolchain path.

## Architecture

### Dual-ISA simulation via tagged union
`DmmDpu` (`downmem.h`) holds either a `UmmDpu` or `RvDpu`. The host API in `ummHostApi.c` dispatches to the correct ISA simulator based on whether the loaded program is an objdump (UPMEM) or an ELF binary (RISC-V).

### Key code paths
- **Host API**: `dpu.h` (public API types/macros) → `ummHostApi.c` (allocation, loading, launch, transfers)
- **UPMEM simulator**: `upmemisa/processor.c` (instruction execution), `upmemisa/program.c` (objdump parsing), `upmemisa/timing.c`
- **RISC-V simulator**: `rvisa/processor.c`, `rvisa/program.c` (ELF loading), `rvisa/timing.c`
- **MRAM transfer models**: selected by `DMM_MRAMXFER` cmake option — `interleaveAnalytical-mramxfer.c` (requires AVX-512), `interleaveLut-mramxfer.c` (default), `upmemLut-mramxfer.c`
- **RV device runtime**: `rvisa/ummrv-rt/` — crt0, linker script, mutex/barrier/semaphore, allocator, memcpy

### Host/device benchmark pairs
Each benchmark has a host driver (`hostApp/X.c`) and a device program (`devApp/X.c`). The host driver allocates DPUs, loads the device binary, transfers data, launches, and validates results against a host-computed reference.

### UPMEM vs RISC-V pipeline difference
- UPMEM: device programs are compiled → `llvm-objdump` generates text objdumps → simulator parses objdump text
- RISC-V: device programs are compiled to ELF binaries → simulator loads ELF directly

## Dependencies

`libelf`, `libpcre2-8`, OpenMP, optionally `libnuma` (on by default via `DMM_NUMA`). The `dmm` library also requires x86 BMI/LZCNT/POPCNT (`-mlzcnt -mpopcnt -mbmi -mbmi2`).

## Style

2-space indentation, opening braces on same line. Public API uses `dpu_*` and `Dmm*` naming. Benchmark files use uppercase names (`VA.c`, `BS.c`). No formatter configured. Commit messages are short imperative subjects.

## Important Caveats

- Do not source UPMEM's `upmem_env.sh` — it interferes with this project's build.
- `devApp/` is `EXCLUDE_FROM_ALL` — `ninja -C build` alone won't build device programs.
- The `dmm-app-boilerplate/` directory is outdated per the README.
- Clang 20 produces significantly faster host binaries than GCC 15 for this codebase.
