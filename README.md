# **DISCLAIMER**
**Aside from this paragraph**, this README.md is entirely written by AI.  
This repo is the rewritten version of the one presented in the paper and does
NOT depend on uPIMulator.  
For some reason compiling `libdmm` with Clang 20 yields *much* faster (more than
1x) binaries than GCC 15 on my system.

# Downmem

A C library for simulating and benchmarking memory transfers on
processing-in-memory (PIM) architectures, specifically UPMEM DPU systems. This
repository provides a standalone DPU instruction set simulator with multiple
MRAM transfer simulation variants for performance analysis and research.

## Features

- **DPU Instruction Set Simulator**: Complete simulation of UPMEM DPU
instruction execution
- **Memory Transfer Simulation**: Multiple MRAM transfer variants (analytical,
lookup table-based)
- **Benchmarks**: PrIM benchmark
- **Timing Analysis**: Detailed performance metrics and memory access patterns
- **Host-Device Integration**: APIs for simulating host-DPU communication

## HOW TO INSTALL AND RUN

### Prerequisites

1. **UPMEM SDK Installation**: Download and install the UPMEM SDK from the
   official UPMEM website
   ```bash
   # Default installation path (adjust if different):
   ~/.local/upmem-2024.2.0-Linux-x86_64/
   ```

2. **System Dependencies**:
   ```bash
   # Ubuntu/Debian
   sudo apt-get install cmake ninja-build clang libpcre2-dev libomp-dev
   ```

Or build dependencies as specified in CMakeLists.txt:
- CMake >= 3.10
- OpenMP
- PCRE2-8
- Clang compiler (recommended)

### Building Downmem

1. **Clone and build the library**:
   ```bash
   git clone <repository-url>
   cd downmem
   ./runTests.sh
   ```

2. **Install the library** (optional, for system-wide use):
   ```bash
   mkdir -p build
   cmake -GNinja -DCMAKE_BUILD_TYPE=Release -S. -Bbuild
   cmake --build build
   sudo cmake --install build
   ```

### Running the Provided Programs

The `runTests.sh` script handles the complete build and execution workflow:

1. **Run without DPU compilation** (uses pre-compiled objdumps):
   ```bash
   ./runTests.sh
   ```
   This builds the host applications and runs all benchmarks using existing
   objdump files.

2. **First run or after modifying devApp/** (requires UPMEM SDK):
   ```bash
   ./runTests.sh /path/to/your/upmem_env.sh
   ```
   This compiles DPU programs, generates new objdump files, then runs all
   benchmarks.

The script automatically:
- Builds all host applications in `build/`
- Compiles DPU programs (if UPMEM path provided) 
- Generates objdump files for instruction analysis
- Executes all benchmarks with timing measurements

### Available Benchmarks

| Executable | Algorithm | Parameters |
|------------|-----------|------------|
| `dmmBfs` | Breadth-First Search | `<algorithm> <start_node> <graph_file> <output_file> <objdump_file> <num_dpus>` |
| `dmmBs` | Binary Search | `<array_size> <num_dpus> <objdump_file>` |
| `dmmGemv` | Matrix-Vector Multiply | `<matrix_rows> <matrix_cols> <objdump_file>` |
| `dmmHstl/dmmHsts` | Histogram | `<data_size> <num_dpus> <objdump_file>` |
| `dmmMlp` | Multi-Layer Perceptron | `<input_size> <hidden_size> <objdump_file>` |
| `dmmRed` | Reduction | `<array_size> <num_dpus> <objdump_file>` |
| `dmmScanssa/dmmScanrss` | Parallel Scan | `<array_size> <num_dpus> <objdump_file>` |
| `dmmSel` | Selection | `<array_size> <num_dpus> <objdump_file>` |
| `dmmTrns` | Matrix Transpose | `<rows> <cols> <objdump_file>` |
| `dmmTs` | Time Series | `<series_length> <num_dpus> <objdump_file>` |
| `dmmUni` | Unique Elements | `<array_size> <num_dpus> <objdump_file>` |
| `dmmVa` | Vector Addition | `<array_size> <num_dpus> <objdump_file>` |

**Note:** The BFS benchmark automatically downloads graph data (~6MB compressed) on first run if not present. The expected output format is documented in `runTests.sh`.

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
