// C-Otter -- db/drivers/mysql.hpp
//
// Driver MySQL/MariaDB sobre lib/mywire -- protocolo nativo, sem
// libmysqlclient (GPL, proibida pelo ADR 0002).
#pragma once

#include "db/holt.hpp"

namespace otter::db {

[[nodiscard]] Driver& mysql_driver();

// O banco de um comando `USE <banco>` (com ou sem crase, `;` final opcional);
// vazio se o comando nao e' um USE. O pacote OK do USE nao diz o banco novo
// sem CLIENT_SESSION_TRACK, entao o driver le' o proprio comando.
[[nodiscard]] std::string mysql_use_target(std::string_view sql);

} // namespace otter::db
