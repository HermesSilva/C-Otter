add_executable(otter_tests
    main.cpp
    test_aggregate.cpp
    test_alter.cpp
    test_arena.cpp
    test_catalog.cpp
    test_connection_store.cpp
    test_crypto_json.cpp
    test_ddl.cpp
    test_edit.cpp
    test_error.cpp
    test_export.cpp
    test_format.cpp
    test_i18n.cpp
    test_icons.cpp
    test_mywire.cpp
    test_paging.cpp
    test_pgwire.cpp
    test_plan.cpp
    test_result_set.cpp
    test_sql.cpp
    test_theme.cpp
)

target_include_directories(otter_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(otter_tests PRIVATE
    otter_flags
    otter::otter_base
    otter::otter_db
    otter::otter_sql
    otter::otter_pgwire
    otter::otter_mywire
    otter::otter_ui
)
set_target_properties(otter_tests PROPERTIES FOLDER "tests")

add_test(NAME unit COMMAND otter_tests)

