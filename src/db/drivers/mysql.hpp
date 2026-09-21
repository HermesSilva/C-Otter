// C-Otter -- db/drivers/mysql.hpp
//
// Driver MySQL/MariaDB sobre lib/mywire -- protocolo nativo, sem
// libmysqlclient (GPL, proibida pelo ADR 0002).
#pragma once

#include "db/holt.hpp"

namespace otter::db {

[[nodiscard]] Driver& mysql_driver();

} // namespace otter::db
