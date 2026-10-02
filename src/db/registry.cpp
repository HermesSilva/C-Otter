#include "db/registry.hpp"

#include "db/drivers/mssql.hpp"
#include "db/drivers/mysql.hpp"
#include "db/drivers/postgres.hpp"
#include "db/drivers/sqlanywhere.hpp"

#include <array>

namespace otter::db {
namespace {

// Ordem de exibicao. PostgreSQL primeiro por ser o driver mais completo hoje.
std::array<Driver*, 4>& drivers() {
    static std::array<Driver*, 4> list = {
        &postgres_driver(),
        &mysql_driver(),
        &mssql_driver(),
        &sqlanywhere_driver(),
    };
    return list;
}

} // namespace

std::span<Driver* const> all_drivers() { return drivers(); }

Driver* find_driver(std::string_view id) noexcept {
    for (Driver* driver : drivers()) {
        if (driver->id() == id) return driver;
    }
    // MariaDB fala o mesmo protocolo e e' servido pelo mesmo driver; o perfil
    // importado do DBeaver, porem, guarda "mariadb" como provider.
    if (id == "mariadb") return &mysql_driver();
    // O DBeaver chama o provider de "sqlserver"; "mssql" e' o nome que as
    // outras partes do programa (dialeto, icone) tambem aceitam.
    if (id == "mssql") return &mssql_driver();
    return nullptr;
}

} // namespace otter::db
