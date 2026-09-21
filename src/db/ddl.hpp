// C-Otter -- db/ddl.hpp
//
// Gera SQL a partir dos metadados ja' carregados: SELECT, INSERT, UPDATE,
// DELETE e CREATE TABLE.
//
// Por que aqui e nao na UI: e' logica de modelo, testavel sem janela. E por
// que gerar em vez de perguntar ao servidor: o PostgreSQL nao tem
// SHOW CREATE TABLE -- montar a partir do catalogo e' o que o DBeaver faz.
#pragma once

#include "db/catalog.hpp"

#include <string>
#include <string_view>

namespace otter::db {

// Escapa um identificador quando ele precisa: nome com maiuscula, espaco ou
// palavra reservada sai entre aspas duplas.
//
// Sempre citar tambem funcionaria, mas "SELECT "id" FROM "cliente"" e' ruido
// visual em 99% dos casos -- e o SQL gerado e' para o usuario ler e editar.
[[nodiscard]] std::string quote_if_needed(std::string_view identifier);

// schema.tabela, com cada parte citada so' se precisar.
[[nodiscard]] std::string qualified_name(std::string_view schema,
                                         std::string_view table);

// SELECT com as colunas explicitas, nao `*`.
//
// Listar as colunas e' mais util como ponto de partida: o usuario apaga as
// que nao quer em vez de digitar as que quer. `limit` 0 omite a clausula.
[[nodiscard]] std::string generate_select(std::string_view schema,
                                          const TableMeta& table,
                                          std::size_t limit = 200);

// INSERT com placeholders nomeados pelo tipo de cada coluna.
//
// Colunas com DEFAULT (serial, timestamp default now()) entram comentadas:
// preencher a PK a mao e' quase sempre engano, mas apagar a linha e' pior
// que descomenta-la quando for de proposito.
[[nodiscard]] std::string generate_insert(std::string_view schema,
                                          const TableMeta& table);

// UPDATE com WHERE pela chave primaria.
//
// Sem PK conhecida, o WHERE vem comentado com um aviso: gerar um UPDATE sem
// WHERE que o usuario execute por reflexo seria o pior defeito possivel
// nesta funcao.
[[nodiscard]] std::string generate_update(std::string_view schema,
                                          const TableMeta& table);

[[nodiscard]] std::string generate_delete(std::string_view schema,
                                          const TableMeta& table);

// CREATE TABLE com colunas, tipos, NOT NULL, DEFAULT, constraints e indices.
//
// Requer que colunas e constraints ja' tenham sido carregadas; o que faltar
// simplesmente nao aparece, com um comentario dizendo isso.
[[nodiscard]] std::string generate_ddl(std::string_view schema,
                                       const TableMeta& table);

[[nodiscard]] std::string generate_count(std::string_view schema,
                                         const TableMeta& table);

} // namespace otter::db
