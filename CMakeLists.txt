cmake_minimum_required(VERSION 3.20)

set(T event_system)

project(${T})

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(${T} STATIC ${CMAKE_CURRENT_SOURCE_DIR}/src/event_system.c)
target_include_directories(${T} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(${T} PRIVATE -Wall -Wextra -Wpedantic)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/examples)