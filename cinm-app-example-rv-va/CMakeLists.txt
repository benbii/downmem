cmake_minimum_required(VERSION 3.10)
project(rvhostva LANGUAGES C)

# 设置基础路径变量
get_filename_component(PROJECT_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(SOURCE_BASE_DIR "${PROJECT_BASE_DIR}")
set(RVISA_DIR "${PROJECT_BASE_DIR}/rvisa")
set(UPMEMISA_DIR "${PROJECT_BASE_DIR}/upmemisa")
set(THRD_UNSAFE_HASH_DIR "${PROJECT_BASE_DIR}/thrdUnsafeHash")

set(DMM_MRAMXFER "interleaveLut" CACHE STRING "Select the mram simulation variant")

find_package(OpenMP REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PCRE2 REQUIRED libpcre2-8)

# 使用绝对路径
add_subdirectory("${RVISA_DIR}/ummrv-rt" "${CMAKE_CURRENT_BINARY_DIR}/ummrv-rt")

add_library(dmm
  "${PROJECT_BASE_DIR}/ummHostApi.c"
  "${PROJECT_BASE_DIR}/mramTiming.c"
  "${PROJECT_BASE_DIR}/wramxfer.c"
  "${THRD_UNSAFE_HASH_DIR}/hashmap.c"
  "${THRD_UNSAFE_HASH_DIR}/slicehash.c"
  "${UPMEMISA_DIR}/processor.c"
  "${UPMEMISA_DIR}/program.c"
  "${UPMEMISA_DIR}/timing.c"
  "${UPMEMISA_DIR}/lookupTbls.c"
  "${RVISA_DIR}/processor.c"
  "${RVISA_DIR}/program.c"
  "${RVISA_DIR}/timing.c"
  "${RVISA_DIR}/lookupTbls.c"
)

target_include_directories(dmm INTERFACE
  $<BUILD_INTERFACE:${PROJECT_BASE_DIR}>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(dmm PUBLIC OpenMP::OpenMP_C ${PCRE2_LIBRARIES} elf)
target_compile_options(dmm PRIVATE -mlzcnt -mpopcnt -mbmi -mbmi2)

target_compile_definitions(dmm PUBLIC __DMM_NUMA)
target_link_libraries(dmm PUBLIC numa)

if(DMM_MRAMXFER STREQUAL "analytical")
  target_sources(dmm PRIVATE "${PROJECT_BASE_DIR}/interleaveAnalytical-mramxfer.c")
  target_compile_options(dmm PUBLIC -mavx512vbmi -mavx512vl)
elseif(DMM_MRAMXFER STREQUAL "upmemLut")
  target_sources(dmm PRIVATE "${PROJECT_BASE_DIR}/upmemLut-mramxfer.c")
elseif(DMM_MRAMXFER STREQUAL "interleaveLut")
  target_sources(dmm PRIVATE "${PROJECT_BASE_DIR}/interleaveLut-mramxfer.c")
else() #none
  target_compile_definitions(dmm PUBLIC __DMM_NOXFER)
endif()

add_executable(dmmVA vahost.c)
target_link_libraries(dmmVA PRIVATE dmm)

add_subdirectory(devApp EXCLUDE_FROM_ALL)