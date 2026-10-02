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
        app_tools.cpp
        catalog.cpp
        catalog_lists.cpp
        connection_config.cpp
        connection_store.cpp
        connection_import.cpp
        ddl.cpp
        edit.cpp
        export.cpp
        grid_ops.cpp
        import.cpp
        native_tools.cpp
        mysql_object.cpp
        mssql_object.cpp
        sqlanywhere_object.cpp
        object_ddl.cpp
        object_info.cpp
        object_info_load.cpp
        plan.cpp
        drivers/mysql.cpp
        drivers/mssql.cpp
        drivers/sqlanywhere.cpp
        drivers/oracle.cpp
        alter.cpp
        coloring.cpp
        sparkline.cpp
        ssh_tunnel.cpp
        pivot.cpp
        value_view.cpp
        catalog_mysql.cpp
        catalog_mssql.cpp
        catalog_sqlanywhere.cpp
        catalog_oracle.cpp
        catalog_reader.cpp
        registry.cpp
        drivers/postgres.cpp
    DEPS
        otter::otter_base
        otter::otter_mywire
        otter::otter_tdswire
        otter::otter_orawire
        otter::otter_pgwire
)

