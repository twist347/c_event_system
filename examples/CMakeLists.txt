add_executable(example main.c)
target_link_libraries(example PRIVATE eb::event_bus)
target_compile_options(example PRIVATE -Wall -Wextra -Wpedantic)

set_target_properties(example PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
)
