include_directories(${PROJECT_SOURCE_DIR}/tau)
include_directories(${PROJECT_SOURCE_DIR}/internal_headers)

add_executable(test_arena test_arena.c)
target_link_libraries(test_arena PRIVATE arena)
add_test(NAME test_arena COMMAND test_arena --failed-output-only)

set_property(TEST test_arena PROPERTY TIMEOUT 5)

add_executable(arena_fuzzer arena_fuzzer.c)
target_compile_definitions(arena_fuzzer PUBLIC -DARENA_STATS)
