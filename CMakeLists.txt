cmake_minimum_required(VERSION 3.10)
project(downmem LANGUAGES C)
find_package(OpenMP REQUIRED)

# ISA Selection Option
option(DMM_ISA "Select ISA implementation" "upmem")
# ISA-specific source files
if(DMM_ISA STREQUAL "upmem")
  set(ISA_SOURCES upmemisa/processor.c upmemisa/program.c upmemisa/timing.c upmemisa/lookupTbls.c)
  set(ISA_HEADERS upmemisa/downmem.h)
  set(ISA_DIR upmemisa)
  # UPMEM needs PCRE2 for objdump parsing
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(PCRE2 REQUIRED libpcre2-8)
  set(ISA_LINK_LIBS ${PCRE2_LIBRARIES})
  set(ISA_COMPILE_OPTIONS -mlzcnt -mpopcnt -mbmi -mbmi2)

elseif(DMM_ISA STREQUAL "riscv")
  set(ISA_SOURCES rvisa/processor.c rvisa/program.c rvisa/timing.c rvisa/lookupTbls.c)
  set(ISA_HEADERS rvisa/downmem.h)
  set(ISA_DIR rvisa)
  # RISC-V needs libelf and minimal lookup tables
  set(ISA_LINK_LIBS elf)
  set(ISA_COMPILE_OPTIONS -mbmi2)
  add_subdirectory(rvisa/ummrv-rt)
else()
  message(FATAL_ERROR "Unknown ISA: ${DMM_ISA}. Valid options are: upmem, riscv")
endif()

add_library(dmm
  ummHostApi.c mramTiming.c wramxfer.c
  thrdUnsafeHash/hashmap.c thrdUnsafeHash/slicehash.c
  ${ISA_SOURCES}
)
target_include_directories(dmm INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}> $<INSTALL_INTERFACE:include>)
target_include_directories(dmm PRIVATE ${PROJECT_SOURCE_DIR}/${ISA_DIR})
target_link_libraries(dmm PUBLIC OpenMP::OpenMP_C ${ISA_LINK_LIBS})
target_compile_options(dmm PUBLIC ${ISA_COMPILE_OPTIONS})
install(FILES ${ISA_HEADERS} DESTINATION include RENAME downmem.h)
install(FILES dpu.h dpu_error.h dmm_common.h DESTINATION include)

option(DMM_MRAMXFER "Select the mram simulation variant" "interleaveLut")
if(DMM_MRAMXFER STREQUAL "analytical")
  target_sources(dmm PRIVATE interleaveAnalytical-mramxfer.c)
  target_compile_options(dmm PUBLIC -mavx512vbmi -mavx512vl)
elseif(DMM_MRAMXFER STREQUAL "upmemLut")
  target_sources(dmm PRIVATE upmemLut-mramxfer.c)
elseif(DMM_MRAMXFER STREQUAL "interleaveLut")
  target_sources(dmm PRIVATE interleaveLut-mramxfer.c)
else() #none
  target_compile_definitions(dmm PUBLIC __DMM_NOXFER)
endif()

# Boilerplate for working with find_package()
install(TARGETS dmm EXPORT DmmTargets
        RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
install(EXPORT DmmTargets FILE DmmTargets.cmake
        NAMESPACE Dmm:: DESTINATION lib/cmake/Dmm)
include(CMakePackageConfigHelpers)
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DmmConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/DmmConfig.cmake"
  INSTALL_DESTINATION lib/cmake/Dmm)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/DmmConfigVersion.cmake"
  VERSION 0.2.0 COMPATIBILITY SameMinorVersion)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/DmmConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/DmmConfigVersion.cmake"
  DESTINATION lib/cmake/Dmm)

add_executable(dmmBs hostApp/BS.c)
target_link_libraries(dmmBs PRIVATE dmm)
add_executable(dmmTs hostApp/TS.c)
target_link_libraries(dmmTs PRIVATE dmm m)
add_executable(dmmUni hostApp/UNI.c)
target_link_libraries(dmmUni PRIVATE dmm)
add_executable(dmmGemv hostApp/GEMV.c)
target_link_libraries(dmmGemv PRIVATE dmm)
add_executable(dmmSpmv hostApp/SPMV.c)
target_link_libraries(dmmSpmv PRIVATE dmm)
add_executable(dmmCompact hostApp/COMPACT.c)
target_link_libraries(dmmCompact PRIVATE dmm)
add_executable(dmmScan hostApp/SCAN.c)
target_link_libraries(dmmScan PRIVATE dmm)
add_executable(dmmTrns hostApp/TRNS.c)
target_link_libraries(dmmTrns PRIVATE dmm)
add_executable(dmmMlp hostApp/MLP.c)
target_link_libraries(dmmMlp PRIVATE dmm)
add_executable(dmmOpdemo hostApp/OPDEMO.c)
target_link_libraries(dmmOpdemo PRIVATE dmm)
add_executable(dmmOpdemof hostApp/OPDEMOF.c)
target_link_libraries(dmmOpdemof PRIVATE dmm m)
add_executable(dmmRed hostApp/RED.c)
target_link_libraries(dmmRed PRIVATE dmm)
add_executable(dmmVa hostApp/VA.c)
target_link_libraries(dmmVa PRIVATE dmm)
add_executable(dmmVaSimple hostApp/VA-SIMPLE.c)
target_link_libraries(dmmVaSimple PRIVATE dmm)
add_executable(dmmHst hostApp/HST.c)
target_link_libraries(dmmHst PRIVATE dmm)
add_executable(dmmNw hostApp/NW.c)
target_link_libraries(dmmNw PRIVATE dmm)
add_executable(dmmBfs hostApp/BFS/cpubfs.c
  hostApp/BFS/dpubfsHost.c hostApp/BFS/main.c)
target_link_libraries(dmmBfs PRIVATE dmm)
