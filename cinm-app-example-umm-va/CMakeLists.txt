cmake_minimum_required(VERSION 3.10)
project(dmmAppExample)

find_package(Dmm REQUIRED)
add_executable(va vahost.c)
target_link_libraries(va PRIVATE Dmm::dmm)
