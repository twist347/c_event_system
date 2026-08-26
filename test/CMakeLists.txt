add_executable(test_event_system test_event_system.c)
target_link_libraries(test_event_system PRIVATE es::event_system)
target_compile_options(test_event_system PRIVATE -Wall -Wextra -Wpedantic)

set_target_properties(test_event_system PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
)

add_test(NAME event_system COMMAND test_event_system)
