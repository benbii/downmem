cmake_minimum_required(VERSION 3.16)
project(ummrv-rt LANGUAGES ASM C)

# Set the target architecture for RISC-V 32-bit with custom extensions
set(CMAKE_C_FLAGS "--target=riscv32 -mcpu=umm")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
set(CMAKE_ASM_FLAGS "${CMAKE_C_FLAGS}")

# Source files for the runtime library
set(RUNTIME_SOURCES
  mutex.c barrier.c semaphore.c alloc.c handshake.c
  memxxx.c memcpy.c memmove.c rand_r.c)
# Header files to install
set(RUNTIME_HEADERS
  syslib.h mutex.h barrier.h semaphore.h alloc.h handshake.h string.h stdlib.h
  defs.h mram.h)

add_library(c STATIC ${RUNTIME_SOURCES})
target_compile_options(c PRIVATE -flto=thin -fno-builtin)
target_link_options(c PRIVATE -flto=thin)
set_target_properties(c PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ummrv-rt/lib)
add_library(crt0 OBJECT crt0.S)

# Installation rules
install(FILES $<TARGET_OBJECTS:crt0>
  DESTINATION lib/clang-runtimes/riscv32/lib RENAME crt0.o)
install(TARGETS c
  ARCHIVE DESTINATION lib/clang-runtimes/riscv32/lib
  LIBRARY DESTINATION lib/clang-runtimes/riscv32/lib)
install(FILES ${RUNTIME_HEADERS}
  DESTINATION lib/clang-runtimes/riscv32/include)
# Install linker script
install(FILES device.ld
  DESTINATION lib/clang-runtimes/riscv32/lib)

# Print build information
message(STATUS "Building UmmRV Runtime Library")
message(STATUS "Compiler: ${CMAKE_C_COMPILER}")
