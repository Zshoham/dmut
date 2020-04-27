cmake_minimum_required(VERSION 3.0.0)

project(dmut)
SET(CMAKE_CXX_STANDARD 11)

add_library(dmut INTERFACE)

find_package (Threads)
add_executable(dmut_test src/dmut.h src/test.cpp)
target_link_libraries(dmut_test Threads::Threads)