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

# O editor de objeto: cada consulta, cada ALTER/GRANT/DROP e o DDL mostrado,
# contra o servidor. Tudo em BEGIN ... ROLLBACK.
add_executable(otter_tests_object_live test_object_info_live.cpp)

target_link_libraries(otter_tests_object_live PRIVATE
    otter_flags
    otter::otter_base
    otter::otter_db
)
set_target_properties(otter_tests_object_live PROPERTIES FOLDER "tests")

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

# O perfil MySQL do editor de objeto, das ferramentas e do cliente nativo:
# cria e apaga os bancos de rascunho otter_scratch*, so' contra localhost.
add_executable(otter_tests_mysql_object_live test_mysql_object_live.cpp)
target_link_libraries(otter_tests_mysql_object_live PRIVATE
    otter_flags
    otter::otter_base
    otter::otter_db
)
set_target_properties(otter_tests_mysql_object_live PROPERTIES FOLDER "tests")

# O perfil SQL Server: catalogo, editor de objeto, ALTER em T-SQL, paginacao
# por OFFSET/FETCH, grade e importacao. Conecta com a conta do Windows; cria e
# apaga o banco de rascunho otter_scratch_ms e um login, so' contra localhost.
add_executable(otter_tests_mssql_live test_mssql_live.cpp)
target_link_libraries(otter_tests_mssql_live PRIVATE
    otter_flags
    otter::otter_base
    otter::otter_db
    otter::otter_sql
)
set_target_properties(otter_tests_mssql_live PROPERTIES FOLDER "tests")

# O perfil SQL Anywhere: protocolo TDS 5.0, catalogo, editor de objeto, ALTER,
# paginacao por TOP / START AT, grade e importacao. Le' o banco demo sem
# escrever nele; a escrita vai num banco de rascunho (otter_scratch_sa) criado
# na pasta temporaria e apagado no fim. So' contra localhost.
add_executable(otter_tests_sqlanywhere_live test_sqlanywhere_live.cpp)
target_link_libraries(otter_tests_sqlanywhere_live PRIVATE
    otter_flags
    otter::otter_base
    otter::otter_db
    otter::otter_sql
)
set_target_properties(otter_tests_sqlanywhere_live PROPERTIES FOLDER "tests")
