cmake_minimum_required(VERSION 3.10)
project(downmem LANGUAGES C)
find_package(OpenMP REQUIRED)

add_library(dmm
  lookupTbls.c processor.c program.c ummHostApi.c
  thrdUnsafeHash/hashmap.c thrdUnsafeHash/slicehash.c
  mramTiming.c timing.c wramxfer.c 
)
target_include_directories(dmm INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}> $<INSTALL_INTERFACE:include>)
target_link_libraries(dmm PUBLIC OpenMP::OpenMP_C pcre2-8)
target_compile_options(dmm PUBLIC -mlzcnt -mpopcnt -mbmi -mbmi2)
install(FILES downmem.h dpu.h dpu_error.h DESTINATION include)
install(DIRECTORY devApp/highlight DESTINATION include)

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
add_executable(dmmScanrss hostApp/SCAN-RSS.c)
target_link_libraries(dmmScanrss PRIVATE dmm)
add_executable(dmmScanssa hostApp/SCAN-SSA.c)
target_link_libraries(dmmScanssa PRIVATE dmm)
add_executable(dmmHstl hostApp/HST-L.c)
target_link_libraries(dmmHstl PRIVATE dmm)
add_executable(dmmHsts hostApp/HST-S.c)
target_link_libraries(dmmHsts PRIVATE dmm)
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
add_executable(dmmSel hostApp/SEL.c)
target_link_libraries(dmmSel PRIVATE dmm)
add_executable(dmmVa hostApp/VA.c)
target_link_libraries(dmmVa PRIVATE dmm)
add_executable(dmmBfs hostApp/BFS/cpubfs.c
  hostApp/BFS/dpubfsHost.c hostApp/BFS/main.c)
target_link_libraries(dmmBfs PRIVATE dmm)
