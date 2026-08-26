cmake_minimum_required(VERSION 3.21)

project(c_event_bus LANGUAGES C)

if (PROJECT_IS_TOP_LEVEL)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif ()

add_library(c_event_bus STATIC src/event_bus.c)
add_library(eb::event_bus ALIAS c_event_bus)

target_compile_features(c_event_bus PUBLIC c_std_23)

set_target_properties(c_event_bus PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
)

target_include_directories(c_event_bus PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# GCC/Clang only
target_compile_options(c_event_bus PRIVATE -Wall -Wextra -Wpedantic)

if (PROJECT_IS_TOP_LEVEL)
    add_subdirectory(examples)

    enable_testing()
    add_subdirectory(test)
endif ()
