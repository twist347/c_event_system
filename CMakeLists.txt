cmake_minimum_required(VERSION 3.20)

project(c_event_system LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(c_event_system STATIC ${CMAKE_CURRENT_SOURCE_DIR}/src/event_system.c)
target_include_directories(c_event_system PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(c_event_system PRIVATE -Wall -Wextra -Wpedantic)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/examples)
