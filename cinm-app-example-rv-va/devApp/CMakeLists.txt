# Device-side applications CMakeLists.txt
# Builds DPU programs for both RISC-V and UPMEM ISA targets
# Create a meta-target to build all DPU examples
add_custom_target(dpuExamples)

# =================================================================
# RISC-V DPU applications
# =================================================================

# rvbin_make(TARGET NR_TASKLETS [EXTRA_FLAGS])
function(rvbin_make TARGET NR_TASKLETS)
  set(EXTRA_FLAGS ${ARGN})
  # Common RISC-V compile flags (without optimization flags)
  set(RISCV_COMMON_FLAGS --target=riscv32 -DNR_TASKLETS=1 -DCOMPILE_va_8 -mcpu=umm -fno-builtin)
  # Apply compile and link options
  target_compile_options(${TARGET} PRIVATE ${RISCV_COMMON_FLAGS} ${EXTRA_FLAGS})
  target_link_options(${TARGET} PRIVATE ${RISCV_COMMON_FLAGS} ${EXTRA_FLAGS})
  # Extract program name from target (strip rv prefix if present)
  string(REPLACE "rv" "" PROGRAM_NAME ${TARGET})
  # Set target properties
  set_target_properties(${TARGET} PROPERTIES
            OUTPUT_NAME ${PROGRAM_NAME}
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/rvbins
  )
endfunction()

add_executable(rvVA va.dpu.c)
rvbin_make(rvVA 1 -flto -O3)
add_dependencies(dpuExamples rvVA)



