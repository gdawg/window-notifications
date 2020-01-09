cmake_minimum_required(VERSION 3.3 FATAL_ERROR)
project(macOS_logtools)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcolor-diagnostics")

include_directories("${CMAKE_SOURCE_DIR}")

add_executable(tojson tojson.cpp)
