enable_language(ASM)

set(DMM_RV_RUNTIME_COMPILE_FLAGS
  --target=riscv32-unknown-elf
  -march=rv32im_zbb
  -mabi=ilp32
  -Wall
  -Wextra
  -ffreestanding
  -fno-builtin
  -flto=thin
)

# Source files for the runtime library
set(RUNTIME_SOURCES
  mutex.c barrier.c semaphore.c alloc.c handshake.c
  memxxx.c memcpy.c memmove.c rand_r.c)
# Header files to install
set(RUNTIME_HEADERS
  syslib.h mutex.h barrier.h semaphore.h alloc.h handshake.h string.h stdlib.h
  defs.h mram.h)

add_library(ummrv_rt_c STATIC ${RUNTIME_SOURCES})
target_compile_options(ummrv_rt_c PRIVATE ${DMM_RV_RUNTIME_COMPILE_FLAGS})
target_include_directories(ummrv_rt_c PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/dmm/rv32>)
set_target_properties(ummrv_rt_c PROPERTIES
  OUTPUT_NAME c
  ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib)

add_library(ummrv_rt_crt0 OBJECT crt0.S)
target_compile_options(ummrv_rt_crt0 PRIVATE
  --target=riscv32-unknown-elf
  -march=rv32im_zbb
  -mabi=ilp32)

# Installation rules
install(FILES $<TARGET_OBJECTS:ummrv_rt_crt0>
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/dmm/rv32 RENAME crt0.o)
install(TARGETS ummrv_rt_c
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}/dmm/rv32
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/dmm/rv32)
install(FILES ${RUNTIME_HEADERS}
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dmm/rv32)
install(FILES device.ld
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/dmm/rv32)
