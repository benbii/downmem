cmake_minimum_required(VERSION 3.10)
project(dmmAppExample)

find_package(Dmm REQUIRED)
add_executable(myhost1 myhost1.c)
target_link_libraries(myhost1 PRIVATE Dmm::dmm)
add_executable(myhost2 myhost2.c)
target_link_libraries(myhost2 PRIVATE Dmm::dmm)
