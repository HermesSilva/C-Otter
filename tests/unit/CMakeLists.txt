add_executable(otter_tests
    main.cpp
    test_aggregate.cpp
    test_alter.cpp
    test_alter_index.cpp
    test_arena.cpp
    test_catalog.cpp
    test_coloring.cpp
    test_commands.cpp
    test_app_tools.cpp
    test_app_settings.cpp
    test_script_store.cpp
    test_mysql_object.cpp
    test_mssql.cpp
    test_sqlanywhere.cpp
    test_connection_import.cpp
    test_connection_store.cpp
    test_crypto_json.cpp
    test_ddl.cpp
    test_edit.cpp
    test_editing.cpp
    test_error.cpp
    test_export.cpp
    test_format.cpp
    test_i18n.cpp
    test_import.cpp
    test_grid_ops.cpp
    test_hint.cpp
    test_icons.cpp
    test_ssl_mode.cpp
    test_socks.cpp
    test_sparkline.cpp
    test_mywire.cpp
    test_native_tools.cpp
    test_object_ddl.cpp
    test_object_info.cpp
    test_paging.cpp
    test_pgwire.cpp
    test_pivot.cpp
    test_plan.cpp
    test_process.cpp
    test_result_set.cpp
    test_sql.cpp
    test_theme.cpp
    test_txn.cpp
    test_value_view.cpp
)

target_include_directories(otter_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(otter_tests PRIVATE
    otter_flags
    otter::otter_base
    otter::otter_db
    otter::otter_sql
    otter::otter_pgwire
    otter::otter_mywire
    otter::otter_tdswire
    otter::otter_ui
)
set_target_properties(otter_tests PROPERTIES FOLDER "tests")

add_test(NAME unit COMMAND otter_tests)

