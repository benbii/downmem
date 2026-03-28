include_guard(GLOBAL)

function(_dmm_get_rv_compiler_rt_builtins OUT_VAR)
  get_property(DMM_RV_COMPILER_RT_BUILTINS GLOBAL PROPERTY DMM_RV_COMPILER_RT_BUILTINS)
  if(NOT DMM_RV_COMPILER_RT_BUILTINS)
    execute_process(
      COMMAND ${CMAKE_C_COMPILER}
        --target=riscv32-unknown-elf
        -march=rv32im_zbb
        -mabi=ilp32
        -rtlib=compiler-rt
        --print-libgcc-file-name
      OUTPUT_VARIABLE DMM_RV_COMPILER_RT_BUILTINS
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE DMM_RV_COMPILER_RT_BUILTINS_RESULT)
    if(NOT DMM_RV_COMPILER_RT_BUILTINS_RESULT EQUAL 0 OR
       NOT EXISTS "${DMM_RV_COMPILER_RT_BUILTINS}")
      message(FATAL_ERROR "Unable to locate RISC-V compiler-rt builtins for ${CMAKE_C_COMPILER}")
    endif()
    set_property(GLOBAL PROPERTY DMM_RV_COMPILER_RT_BUILTINS "${DMM_RV_COMPILER_RT_BUILTINS}")
  endif()
  set(${OUT_VAR} "${DMM_RV_COMPILER_RT_BUILTINS}" PARENT_SCOPE)
endfunction()

function(rvbin_make TARGET NR_TASKLETS)
  set(EXTRA_FLAGS ${ARGN})

  if(NOT DMM_RV AND NOT TARGET ummrv_rt_c)
    message(FATAL_ERROR "RISC-V device support is not enabled in this Dmm build.")
  endif()
  if(NOT DEFINED DMM_RV_RUNTIME_INCLUDE_DIR)
    set(DMM_RV_RUNTIME_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/rvisa/ummrv-rt")
  endif()
  if(NOT DEFINED DMM_RV_RUNTIME_LINKER_SCRIPT)
    set(DMM_RV_RUNTIME_LINKER_SCRIPT "${PROJECT_SOURCE_DIR}/rvisa/ummrv-rt/device.ld")
  endif()
  if(NOT DEFINED DMM_RV_RUNTIME_LIBRARY_DIR AND TARGET ummrv_rt_c)
    set(DMM_RV_RUNTIME_LIBRARY_DIR "$<TARGET_FILE_DIR:ummrv_rt_c>")
  endif()
  if(NOT DEFINED DMM_RV_RUNTIME_CRT0_OBJECT AND TARGET ummrv_rt_crt0)
    set(DMM_RV_RUNTIME_CRT0_OBJECT "$<TARGET_OBJECTS:ummrv_rt_crt0>")
  endif()

  if(NOT EXISTS "${DMM_RV_RUNTIME_INCLUDE_DIR}")
    message(FATAL_ERROR "Missing RV runtime headers: ${DMM_RV_RUNTIME_INCLUDE_DIR}")
  endif()
  if(NOT EXISTS "${DMM_RV_RUNTIME_LINKER_SCRIPT}")
    message(FATAL_ERROR "Missing RV linker script: ${DMM_RV_RUNTIME_LINKER_SCRIPT}")
  endif()
  if(NOT DEFINED DMM_RV_RUNTIME_LIBRARY_DIR)
    message(FATAL_ERROR "Missing RV runtime library directory.")
  endif()
  if(NOT DEFINED DMM_RV_RUNTIME_CRT0_OBJECT)
    message(FATAL_ERROR "Missing RV crt0 object.")
  endif()

  _dmm_get_rv_compiler_rt_builtins(DMM_RV_COMPILER_RT_BUILTINS)

  set(RISCV_COMMON_FLAGS
    --target=riscv32-unknown-elf
    -march=rv32im_zbb
    -mabi=ilp32
    -DNR_TASKLETS=${NR_TASKLETS}
    -ffreestanding
    -fno-builtin
    -ffunction-sections
    -fdata-sections
  )
  target_compile_options(${TARGET} PRIVATE ${RISCV_COMMON_FLAGS} ${EXTRA_FLAGS})
  target_include_directories(${TARGET} PRIVATE "${DMM_RV_RUNTIME_INCLUDE_DIR}")

  target_link_options(${TARGET} PRIVATE
    --target=riscv32-unknown-elf
    -march=rv32im_zbb
    -mabi=ilp32
    -fuse-ld=lld
    -nostdlib
    "LINKER:-T,${DMM_RV_RUNTIME_LINKER_SCRIPT}"
    "LINKER:--defsym=NR_TASKLETS=${NR_TASKLETS}"
    "LINKER:--gc-sections"
    ${EXTRA_FLAGS}
  )

  if(TARGET ummrv_rt_c)
    add_dependencies(${TARGET} ummrv_rt_c)
  endif()
  if(TARGET ummrv_rt_crt0)
    add_dependencies(${TARGET} ummrv_rt_crt0)
  endif()
  target_sources(${TARGET} PRIVATE ${DMM_RV_RUNTIME_CRT0_OBJECT})
  if(TARGET ummrv_rt_c)
    target_link_libraries(${TARGET} PRIVATE ummrv_rt_c "${DMM_RV_COMPILER_RT_BUILTINS}")
  else()
    target_link_libraries(${TARGET} PRIVATE
      "${DMM_RV_RUNTIME_LIBRARY_DIR}/libc.a"
      "${DMM_RV_COMPILER_RT_BUILTINS}")
  endif()

  string(REPLACE "rv" "" PROGRAM_NAME ${TARGET})
  set_target_properties(${TARGET} PROPERTIES
    OUTPUT_NAME ${PROGRAM_NAME}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/rvbins
  )
endfunction()
