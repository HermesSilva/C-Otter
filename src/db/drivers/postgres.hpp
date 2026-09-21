// C-Otter -- db/drivers/postgres.hpp
//
// Driver PostgreSQL sobre libpq. Primeiro driver: paga o custo de provar a
// abstracao otter_db (docs/EFFORT.md secao 2.5).
#pragma once

#include "db/holt.hpp"

namespace otter::db {

[[nodiscard]] Driver& postgres_driver();

} // namespace otter::db
