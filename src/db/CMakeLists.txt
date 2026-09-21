# Camada de acesso a dados.
#
# Sem libpq nem qualquer biblioteca de cliente de SGBD: os protocolos vivem em
# lib/ e falam o socket diretamente (ADR 0009). Compilar o C-Otter nao exige
# PostgreSQL instalado.
otter_add_library(otter_db
    SOURCES
        types.cpp
        holt.cpp
        result_set.cpp
        aggregate.cpp
        catalog.cpp
        connection_config.cpp
        connection_store.cpp
        ddl.cpp
        edit.cpp
        export.cpp
        plan.cpp
        drivers/mysql.cpp
        alter.cpp
        coloring.cpp
        pivot.cpp
        catalog_mysql.cpp
        catalog_reader.cpp
        registry.cpp
        drivers/postgres.cpp
    DEPS
        otter::otter_base
        otter::otter_mywire
        otter::otter_pgwire
)

