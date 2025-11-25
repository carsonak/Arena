include_directories(${PROJECT_SOURCE_DIR}/tau)

add_executable(test_arena test_arena.c)
target_link_libraries(test_arena PRIVATE arena)
add_test(NAME test_arena COMMAND test_arena --failed-output-only)

set_property(TEST test_arena PROPERTY TIMEOUT 5)
