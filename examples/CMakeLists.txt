cmake_minimum_required(VERSION 3.20)

set(T main)

add_executable(${T} main.c)
target_link_libraries(${T} event_system)