// C-Otter -- db/drivers/mssql.hpp
//
// Driver do SQL Server sobre lib/tdswire -- protocolo TDS nativo, sem ODBC e
// sem FreeTDS (LGPL, proibido pelo ADR 0002). Decisao no ADR 0024.
#pragma once

#include "db/holt.hpp"

namespace otter::db {

[[nodiscard]] Driver& mssql_driver();

} // namespace otter::db
