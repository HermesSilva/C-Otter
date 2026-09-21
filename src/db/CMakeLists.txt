# Camada de acesso a dados.
#
# Sem libpq nem qualquer biblioteca de cliente de SGBD: os protocolos vivem em
# lib/ e falam o socket diretamente (ADR 0009). Compilar o C-Otter nao exige
# PostgreSQL instalado.
otter_add_library(otter_db
    SOURCES
        types.cpp
        result_set.cpp
        catalog.cpp
        drivers/postgres.cpp
    DEPS
        otter::otter_base
        otter::otter_pgwire
)
