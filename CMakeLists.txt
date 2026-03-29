cmake_minimum_required(VERSION 3.10)
project(downmem LANGUAGES C)
include(GNUInstallDirs)

set(DMM_MRAMXFER "interleaveLut" CACHE STRING "Select the mram simulation variant")
option(DMM_UPMEM "upmem" ON)
option(DMM_RV "hypothetical riscv upmem" ON)
option(DMM_NUMA "Enable NUMA-aware memory allocation and thread binding" ON)
option(DMM_TSCDUMP "Enable per-instruction overhead dumping" OFF)
option(DMM_FUNCTIONAL_ONLY "Disable timing when on" OFF)

find_package(OpenMP REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PCRE2 REQUIRED libpcre2-8)
if(DMM_RV)
  add_subdirectory(rvisa/ummrv-rt)
endif()

add_library(dmm
  ummHostApi.c mramTiming.c wramxfer.c
  thrdUnsafeHash/hashmap.c thrdUnsafeHash/slicehash.c
  upmemisa/processor.c upmemisa/program.c upmemisa/timing.c upmemisa/lookupTbls.c
  rvisa/processor.c rvisa/program.c rvisa/timing.c rvisa/lookupTbls.c
)
target_include_directories(dmm INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(dmm PUBLIC OpenMP::OpenMP_C ${PCRE2_LIBRARIES} elf)
target_compile_options(dmm PRIVATE -mlzcnt -mpopcnt -mbmi -mbmi2)

add_library(dmmShared SHARED
  ummHostApi.c mramTiming.c wramxfer.c
  thrdUnsafeHash/hashmap.c thrdUnsafeHash/slicehash.c
  upmemisa/processor.c upmemisa/program.c upmemisa/timing.c upmemisa/lookupTbls.c
  rvisa/processor.c rvisa/program.c rvisa/timing.c rvisa/lookupTbls.c
)
target_include_directories(dmmShared INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(dmmShared PUBLIC OpenMP::OpenMP_C ${PCRE2_LIBRARIES} elf)
target_compile_options(dmmShared PRIVATE -mlzcnt -mpopcnt -mbmi -mbmi2)

if(DMM_NUMA)
  target_compile_definitions(dmm PUBLIC __DMM_NUMA)
  target_compile_definitions(dmmShared PUBLIC __DMM_NUMA)
  target_link_libraries(dmm PUBLIC numa)
  target_link_libraries(dmmShared PUBLIC numa)
endif()
if (DMM_FUNCTIONAL_ONLY)
  target_compile_definitions(dmm PUBLIC __DMM_FUNCTIONAL_ONLY)
  target_compile_definitions(dmmShared PUBLIC __DMM_FUNCTIONAL_ONLY)
endif()
if(DMM_TSCDUMP)
  target_compile_definitions(dmm PUBLIC __DMM_TSCDUMP)
  target_compile_definitions(dmmShared PUBLIC __DMM_TSCDUMP)
endif()

install(FILES rvisa/dmminternal.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rvisa)
install(FILES upmemisa/dmminternal.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/upmemisa)
install(FILES dpu.h dpu_error.h dmm_common.h downmem.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(DMM_MRAMXFER STREQUAL "analytical")
  target_sources(dmm PRIVATE interleaveAnalytical-mramxfer.c)
  target_sources(dmmShared PRIVATE interleaveAnalytical-mramxfer.c)
  target_compile_options(dmm PUBLIC -mavx512vbmi -mavx512vl)
  target_compile_options(dmmShared PUBLIC -mavx512vbmi -mavx512vl)
elseif(DMM_MRAMXFER STREQUAL "upmemLut")
  target_sources(dmm PRIVATE upmemLut-mramxfer.c)
  target_sources(dmmShared PRIVATE upmemLut-mramxfer.c)
elseif(DMM_MRAMXFER STREQUAL "interleaveLut")
  target_sources(dmm PRIVATE interleaveLut-mramxfer.c)
  target_sources(dmmShared PRIVATE interleaveLut-mramxfer.c)
else() #none
  target_compile_definitions(dmm PUBLIC __DMM_NOXFER)
  target_compile_definitions(dmmShared PUBLIC __DMM_NOXFER)
endif()

# Boilerplate for working with find_package()
install(TARGETS dmm dmmShared EXPORT DmmTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(EXPORT DmmTargets FILE DmmTargets.cmake
        NAMESPACE Dmm:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Dmm)
include(CMakePackageConfigHelpers)
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DmmConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/DmmConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Dmm)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/DmmConfigVersion.cmake"
  VERSION 0.2.0 COMPATIBILITY SameMinorVersion)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/DmmConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/DmmConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Dmm)
install(FILES cmake/DmmDeviceHelpers.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Dmm)

foreach(A BS COMPACT GEMV HST MLP OPDEMO OPDEMOF SPMV NW RED SCAN TRNS TS UNI VA VA-SIMPLE)
  add_executable(dmm${A} hostApp/${A}.c)
  target_link_libraries(dmm${A} PRIVATE dmm)
endforeach()

add_executable(dmmBFS hostApp/BFS/cpubfs.c
  hostApp/BFS/dpubfsHost.c hostApp/BFS/main.c)
target_link_libraries(dmmBFS PRIVATE dmm)
target_link_libraries(dmmTS PRIVATE m)
target_link_libraries(dmmOPDEMOF PRIVATE m)

if(DMM_RV)
  set(DMM_RV_RUNTIME_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/rvisa/ummrv-rt")
  set(DMM_RV_RUNTIME_LIBRARY_DIR "$<TARGET_FILE_DIR:ummrv_rt_c>")
  set(DMM_RV_RUNTIME_LINKER_SCRIPT "${PROJECT_SOURCE_DIR}/rvisa/ummrv-rt/device.ld")
  set(DMM_RV_RUNTIME_CRT0_OBJECT "$<TARGET_OBJECTS:ummrv_rt_crt0>")
endif()
add_subdirectory(devApp EXCLUDE_FROM_ALL)
