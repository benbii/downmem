# **DISCLAIMER**
**Aside from this disclaimer**, this README.md is entirely written by AI.  
This repo is the rewritten version of the one presented in the paper and does
NOT depend on uPIMulator.  
For some reason compiling `libdmm` with Clang 20 yields *much* faster (more than
1x) binaries than GCC 15 on my system.

Also the boilerplate directory is outdated.

**IMPORTANT**: You don't have to, and are not recommended to unless knowing
exactly what you're doing, source the `upmem_env.sh` provided by UPMEM toolchain
distribution when working with downmem. Sourcing its content often messes things
up. RV is the recommended ISA when exclusively working with downmem.

# Downmem

A C library for simulating and benchmarking memory transfers on
processing-in-memory (PIM) architectures. This repository provides DPU
instruction set simulators for both **UPMEM** and **hypothetical RISC-V based
DPUs** with multiple MRAM transfer simulation variants for performance analysis
and research.

## Features

- **Dual ISA Support**: Complete simulation of both UPMEM DPU and hypothetical RISC-V DPU instruction execution
- **RISC-V DPU Extension**: Custom RISC-V implementation with specialized PIM instructions and runtime libraries
- **Unified C API**: All host-side APIs remain identical across both architectures, enabling easy comparison and tinkering
- **Memory Transfer Simulation**: Multiple MRAM transfer variants (analytical, lookup table-based)
- **Benchmarks**: Comprehensive PrIM benchmark suite
- **Timing Analysis**: Detailed performance metrics and memory access patterns  
- **Host-Device Integration**: APIs for simulating host-DPU communication

## HOW TO INSTALL AND RUN

### Architecture Selection

Downmem supports two DPU architectures. Choose your target using the CMake parameter `DMM_ISA`:

- **UPMEM DPU**: `cmake -DDMM_ISA=upmem` (default)
- **RISC-V DPU**: `cmake -DDMM_ISA=riscv` (recommended)

All host-side C APIs remain identical between architectures, allowing easy comparison and experimentation.

### Prerequisites

**For RISC-V DPU (Recommended for new users):**
1. **This repository**: Must be a git clone (required for LLVM submodule integration)
2. **LLVM Project**: A git clone of llvm-project repository
   ```bash
   git clone https://github.com/llvm/llvm-project.git
   ```
3. **System Dependencies**:
   ```bash
   # Ubuntu/Debian
   sudo apt-get install cmake ninja-build libelf-dev libomp-dev build-essential
   ```

**For UPMEM DPU:**
1. **UPMEM SDK**: Download and install from official UPMEM website
   ```bash
   # Default installation path (adjust if different):
   ~/.local/upmem-2024.2.0-Linux-x86_64/
   ```
2. **System Dependencies**:
   ```bash
   # Ubuntu/Debian  
   sudo apt-get install cmake ninja-build clang libpcre2-dev libomp-dev
   ```

### Quick Start (RISC-V DPU)

For new users wanting to get started quickly:

1. **One-step toolchain build**:
   ```bash
   cmake/rvupmem-toolchainbuild.sh /path/to/install /path/to/llvm-project [optional_host_compiler]
   ```
   
   Examples:
   ```bash
   # With system clang
   cmake/rvupmem-toolchainbuild.sh ~/risc-v-toolchain ~/llvm-project clang
   
   # With custom clang path  
   cmake/rvupmem-toolchainbuild.sh ~/risc-v-toolchain ~/llvm-project /usr/bin/clang-18
   
   # Bootstrap with system cc (if no clang available)
   cmake/rvupmem-toolchainbuild.sh ~/risc-v-toolchain ~/llvm-project
   ```

2. **Run tests**:
   ```bash
   bash rvRunTests.sh /path/to/install
   ```

### Building Downmem

**Option 1: RISC-V DPU (with custom toolchain)**
```bash
# Clone repositories
git clone <this-repository-url>
git clone https://github.com/llvm/llvm-project.git

# Build toolchain and run tests (one command)
cmake/rvupmem-toolchainbuild.sh ~/risc-v-toolchain ~/llvm-project clang

# Or run tests separately
bash rvRunTests.sh ~/risc-v-toolchain
```

**Option 2: UPMEM DPU (with official SDK)**
```bash
# Basic build (host only, uses pre-compiled objdumps):
bash runTests.sh

# Full build with DPU compilation (requires UPMEM SDK):
bash runTests.sh /path/to/upmem-sdk

# Example with UPMEM SDK path:
bash runTests.sh ~/.local/upmem-2024.2.0-Linux-x86_64/
```

**Option 3: Manual build for custom configurations**
```bash
# Choose ISA and build
cmake -GNinja -DCMAKE_BUILD_TYPE=Release -DDMM_ISA=riscv -S. -Bbuild
cmake --build build

# Or for UPMEM
cmake -GNinja -DCMAKE_BUILD_TYPE=Release -DDMM_ISA=upmem -S. -Bbuild  
cmake --build build
```

### Build Scripts and Toolchain Details

The `cmake/` directory contains build scripts for both architectures:

- **`rvupmem-toolchainbuild.sh`**: Complete RISC-V toolchain build including custom LLVM with PIM extensions and compiler-rt
- **`upmem-toolchainbuild.sh`**: UPMEM toolchain build (if not using official SDK)  
- **`rvupmem-llvm.patch`**: RISC-V LLVM patches for custom PIM instructions
- **`upmem-llvm.patch.zst`**: UPMEM LLVM patches and TableGen definitions

**Compiler Requirements:**
- **With system clang**: Pass `clang` or `/path/to/clang` to toolchain script
- **Without clang**: Script automatically bootstraps LLVM 15, then builds LLVM 20 with extensions
- **Must be git clone**: Repository requires git history for LLVM integration

### Running the Provided Programs

**For RISC-V DPU:**
```bash
# After toolchain build, run all benchmarks
bash rvRunTests.sh /path/to/install
```

**For UPMEM DPU:**
```bash
# Basic run (uses pre-compiled objdumps):
bash runTests.sh

# Full run with DPU compilation:
bash runTests.sh /path/to/upmem-sdk
```

Both scripts automatically:
- Build all host applications in `build/`
- Compile DPU programs (if toolchain path provided) to `devApp/rvbins/` or `devApp/bins/`
- Generate objdump files for instruction analysis  
- Execute all benchmarks with timing measurements

### Available Benchmarks

| Executable | Algorithm | Example Parameters |
|------------|-----------|---------------------|
| `dmmBfs` | Breadth-First Search | `simpleBFSDpu 0 hostApp/BFS/csr.txt /tmp/out devApp/bins/BFS 192` |
| `dmmBs` | Binary Search | `5242880 640 devApp/bins/BS` |  
| `dmmGemv` | Matrix-Vector Multiply | `8192 2048 devApp/bins/GEMV` |
| `dmmHst` | Histogram | `2621440 1280 devApp/bins/HST` |
| `dmmMlp` | Multi-Layer Perceptron | `1024 1024 devApp/bins/MLP` |
| `dmmNw` | Needleman-Wunsch | `1500 1000 64 devApp/bins/NW` |
| `dmmOpdemo` | OP Demo | `131072 256 devApp/bins/OPDEMO 3` |
| `dmmRed` | Reduction | `array_size num_dpus devApp/bins/RED` |
| `dmmScanrss/dmmScanssa` | Parallel Scan | `array_size num_dpus devApp/bins/SCAN` |
| `dmmSel` | Selection | `array_size num_dpus devApp/bins/SEL` |
| `dmmTrns` | Matrix Transpose | `rows cols devApp/bins/TRNS` |
| `dmmTs` | Time Series | `series_length num_dpus devApp/bins/TS` |
| `dmmUni` | Unique Elements | `array_size num_dpus devApp/bins/UNI` |  
| `dmmVa` | Vector Addition | `array_size num_dpus devApp/bins/VA` |
| `dmmVaSimple` | Simple Vector Addition | `131072 256 devApp/bins/VA-SIMPLE` |

**Notes:**
- For RISC-V DPU: Replace `devApp/bins/` with `devApp/rvbins/`  
- BFS benchmark downloads graph data (~6MB) automatically on first run
- All benchmarks include verification against host-computed reference results

## HOW TO USE DOWNMEM ON YOUR OWN UPMEM APPS

The `dmm-app-boilerplate/` directory provides a template for integrating
Downmem with your own UPMEM applications.

### Project Structure

```
your-project/
├── CMakeLists.txt          # Build configuration
├── build.sh               # Build and compile script
├── myhost.c               # Host-side application
└── devApp/
    ├── mydev.c            # DPU-side application
    ├── bins/              # Compiled DPU binaries
    └── objdumps/          # DPU object dumps for analysis
```

### Step-by-Step Integration

1. **Copy the boilerplate**:
   ```bash
   cp -r dmm-app-boilerplate/ my-upmem-app/
   cd my-upmem-app/
   ```

2. **Configure your CMakeLists.txt**:
   ```cmake
   cmake_minimum_required(VERSION 3.10)
   project(myUpmemApp)
   find_package(Dmm REQUIRED)
   add_executable(myhost myhost.c)
   target_link_libraries(myhost PRIVATE Dmm::dmm)
   ```

3. **Write your host application** (`myhost.c`):
   ```c
   #include <dpu.h>
   #include <downmem.h>
   
   int main(int argc, char **argv) {
       // Standard UPMEM DPU allocation and loading
       struct dpu_set_t set;
       DPU_ASSERT(dpu_alloc(num_dpus, NULL, &set));
       DPU_ASSERT(dpu_load(set, argv[1], NULL));  // Load objdump file
       
       // Your host-side logic here
       // Use DPU API for data transfers and execution
       
       DPU_ASSERT(dpu_free(set));
       return 0;
   }
   ```

4. **Write your DPU application** (`devApp/mydev.c`):
   ```c
   #include <defs.h>
   #include <mram.h>
   #include <alloc.h>
   
   // Host-accessible variables
   __host uint32_t input_data[1024];
   __host uint32_t result;
   
   int main() {
       // Your DPU-side algorithm here
       // Use tasklets with me() and NR_TASKLETS
       
       return 0;
   }
   ```

5. **Build and run**:
   ```bash
   # Basic build (host only):
   ./build.sh
   
   # Full build with DPU compilation:
   ./build.sh mydev 16  # device_name, num_tasklets
   
   # With custom paths:
   ./build.sh mydev 16 /path/to/dmm/cmake /path/to/upmem_env.sh
   
   # Execute:
   ./build/myhost <parameters> devApp/objdumps/mydev.objdump
   ```

### Configuration Options

- **MRAM Transfer Simulation**: Configure via CMake option `DMM_MRAMXFER`:
  - `"analytical"` - Mathematical model (requires AVX-512)
  - `"upmemLut"` - UPMEM lookup tables
  - `"interleaveLut"` - Interleaved lookup tables (default)
  - `"none"` - Disable transfer simulation

- **Build Example**:
  ```bash
  cmake -DDMM_MRAMXFER=analytical -S. -Bbuild
  ```

### Integration Tips

1. **Use the provided headers**: Include `devApp/highlight/` headers in your
   DPU code for UPMEM-compatible definitions
2. **Object dump analysis**: The simulator requires objdump files generated by
   `llvm-objdump` for instruction analysis  
3. **Memory layout**: Follow UPMEM memory model (WRAM, MRAM, atomic sections)
4. **Tasklet programming**: Use standard UPMEM tasklet patterns with `me()`,
   `NR_TASKLETS`, and synchronization primitives

<function_calls>
<invoke name="TodoWrite">
<parameter name="todos">[{"content": "Examine runTests.sh to understand build and execution process", "id": "1", "status": "completed"}, {"content": "Analyze dmm-app-boilerplate directory structure and usage", "id": "2", "status": "completed"}, {"content": "Check CMakeLists.txt for build dependencies and configuration", "id": "3", "status": "completed"}, {"content": "Write HOW TO INSTALL AND RUN section", "id": "4", "status": "completed"}, {"content": "Write HOW TO USE DOWNMEM ON YOUR OWN UPMEM APPS section", "id": "5", "status": "completed"}, {"content": "Create complete README.md file", "id": "6", "status": "completed"}]
