add_executable(otter_tests
    main.cpp
    test_arena.cpp
    test_error.cpp
    test_pgwire.cpp
    test_result_set.cpp
)

target_include_directories(otter_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(otter_tests PRIVATE
    otter_flags
    otter::otter_base
    otter::otter_db
    otter::otter_pgwire
)
set_target_properties(otter_tests PROPERTIES FOLDER "tests")

add_test(NAME unit COMMAND otter_tests)
