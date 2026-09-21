// C-Otter -- db/registry.hpp
//
// Registro estatico de drivers (ADR 0001 #5: sem OSGi, sem carga dinamica).
//
// Existe porque a UI escolhia o driver por chamada direta a
// `postgres_driver()`. Com dois SGBDs isso vira um `if` espalhado por cada
// ponto de conexao -- e cada ponto esquecido conecta ao banco errado.
#pragma once

#include "db/holt.hpp"

#include <span>
#include <string_view>

namespace otter::db {

// Todos os drivers compilados, na ordem em que a UI deve lista-los.
[[nodiscard]] std::span<Driver* const> all_drivers();

// Driver pelo identificador ("postgresql", "mysql"). Nulo quando nao existe
// -- quem chama precisa dizer isso ao usuario, nao cair no padrao: conectar
// silenciosamente com o driver errado da' erro de protocolo incompreensivel.
[[nodiscard]] Driver* find_driver(std::string_view id) noexcept;

} // namespace otter::db
