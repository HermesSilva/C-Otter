add_executable(otter_tests
    main.cpp
    test_arena.cpp
    test_error.cpp
)

target_include_directories(otter_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(otter_tests PRIVATE otter_flags otter::otter_base)
set_target_properties(otter_tests PROPERTIES FOLDER "tests")

add_test(NAME unit COMMAND otter_tests)
