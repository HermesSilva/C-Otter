# Testes que precisam de um PostgreSQL de verdade.
#
# Nao entram no ctest por padrao: dependem de um servidor externo e do schema
# otter_test criado por fixtures.sql. Uma suite que falha por falta de banco
# treina a ignorar falha -- pior que nao ter o teste.
#
# Para incluir no ctest, configurar com -DOTTER_LIVE_TESTS=ON.
add_executable(otter_tests_live test_catalog_live.cpp)

target_link_libraries(otter_tests_live PRIVATE
    otter_flags
    otter::otter_base
    otter::otter_db
    otter::otter_sql
)
set_target_properties(otter_tests_live PROPERTIES FOLDER "tests")

# O mesmo para o MySQL, com o banco otter_test de fixtures_mysql.sql.
add_executable(otter_tests_mysql_live test_catalog_mysql_live.cpp)

target_link_libraries(otter_tests_mysql_live PRIVATE
    otter_flags
    otter::otter_base
    otter::otter_db
)
set_target_properties(otter_tests_mysql_live PROPERTIES FOLDER "tests")

option(OTTER_LIVE_TESTS "Rodar os testes que precisam de um banco externo" OFF)

if(OTTER_LIVE_TESTS)
    add_test(NAME live COMMAND otter_tests_live)
    add_test(NAME live_mysql COMMAND otter_tests_mysql_live)
endif()
