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
  set(RISCV_COMMON_FLAGS --target=riscv32 -DNR_TASKLETS=${NR_TASKLETS} -mcpu=umm -fno-builtin)
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

if(DMM_RV)
  foreach(PROGRAM NW SCAN TS)
    add_executable(rv${PROGRAM} ${PROGRAM}.c)
    rvbin_make(rv${PROGRAM} 16 -flto -Og)
    add_dependencies(dpuExamples rv${PROGRAM})
  endforeach()
  foreach(PROGRAM BFS BS COMPACT GEMV HST MLP OPDEMO OPDEMOF RED SPMV TRNS UNI VA)
    add_executable(rv${PROGRAM} ${PROGRAM}.c)
    rvbin_make(rv${PROGRAM} 16 -flto -O3)
    add_dependencies(dpuExamples rv${PROGRAM})
  endforeach()
endif()

# =================================================================
# UPMEM DPU applications
# =================================================================

# upmembin_make(TARGET NR_TASKLETS [EXTRA_FLAGS])
function(upmembin_make TARGET NR_TASKLETS)
  set(EXTRA_FLAGS ${ARGN})
  # Get UPMEM objdump tool from compiler directory
  get_filename_component(UPMEM_COMPILER_DIR ${CMAKE_C_COMPILER} DIRECTORY)
  if(UPMEM_COMPILER_DIR STREQUAL "")
    # Fallback: assume llvm-objdump is in PATH
    set(UPMEM_OBJDUMP llvm-objdump)
  else()
    set(UPMEM_OBJDUMP ${UPMEM_COMPILER_DIR}/llvm-objdump)
  endif()
  # Common UPMEM compile flags
  set(UPMEM_COMMON_FLAGS --target=dpu-upmem-dpurte -mcpu=v1A -g -DNR_TASKLETS=${NR_TASKLETS})
  # Apply compile and link options
  target_compile_options(${TARGET} PRIVATE ${UPMEM_COMMON_FLAGS} ${EXTRA_FLAGS})
  target_link_options(${TARGET} PRIVATE ${UPMEM_COMMON_FLAGS} ${EXTRA_FLAGS})

  # Extract program name from target
  string(REPLACE "ummbin" "" PROGRAM_NAME ${TARGET})
  # Set target properties
  set_target_properties(${TARGET} PROPERTIES
            OUTPUT_NAME ${PROGRAM_NAME}.ummbin
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bins
  )

  # Generate objdump file using toolchain-specific llvm-objdump
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/objdumps/${PROGRAM_NAME}.objdump
    COMMAND ${UPMEM_OBJDUMP} -t -d $<TARGET_FILE:${TARGET}> > ${CMAKE_CURRENT_BINARY_DIR}/objdumps/${PROGRAM_NAME}.objdump
    COMMAND ${UPMEM_OBJDUMP} -s -j .atomic -j .data -j .data.__sys_host -j .data.stacks -j .mram $<TARGET_FILE:${TARGET}> >> ${CMAKE_CURRENT_BINARY_DIR}/objdumps/${PROGRAM_NAME}.objdump
    DEPENDS ${TARGET}
    COMMENT "Objdumping ${PROGRAM_NAME}"
  )
  # Create target for objdump file
  add_custom_target(umm${PROGRAM_NAME} ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/objdumps/${PROGRAM_NAME}.objdump)
endfunction()

if(DMM_UPMEM)
  # Build UPMEM programs using wrapper function
  foreach(A BS COMPACT GEMV HST MLP OPDEMO OPDEMOF SPMV NW RED SCAN TRNS TS UNI VA VA-SIMPLE BFS)
    # Add executable is not in the function to allow for dev apps with multiple files
    add_executable(ummbin${A} ${A}.c)
    upmembin_make(ummbin${A} 16)
    add_dependencies(dpuExamples umm${A})
  endforeach()
endif()

