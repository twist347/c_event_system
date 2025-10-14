cmake_minimum_required(VERSION 3.20)

set(T main)

add_executable(${T} main.c)
target_link_libraries(${T} c_event_system)
target_compile_options(${T} PRIVATE -Wall -Wextra -Wpedantic)