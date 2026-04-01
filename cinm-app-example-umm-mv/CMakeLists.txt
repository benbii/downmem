cmake_minimum_required(VERSION 3.10)
project(dmmAppExample)

# 禁用PIE以避免与静态库的链接问题
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -no-pie")

find_package(Dmm REQUIRED)
add_executable(mv mvhost.c)
target_link_libraries(mv PRIVATE Dmm::dmm)

