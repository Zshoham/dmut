cmake_minimum_required(VERSION 3.0.0)

project(dmut)

add_library(dmut INTERFACE)

add_executable(dmut_test src/dmut.h src/test.cpp)