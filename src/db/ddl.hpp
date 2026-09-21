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

#include <cstdint>
#include <string>
#include <string_view>

namespace otter::db {

// Estilo de delimitador de identificador do SGBD corrente.
//
// Estado de modulo em vez de parametro nas ~20 funcoes que geram SQL: o
// dialeto e' propriedade da CONEXAO, nao de cada chamada, e passa-lo adiante
// em toda a cadeia (UI -> ddl -> edit -> aggregate) so' criaria pontos onde
// esquecer de repassar.
//
// Definido uma vez por conexao, em set_sql_dialect(). Sem isto, um UPDATE
// gerado para o MySQL sairia com "aspas duplas" -- que o MySQL le como
// STRING, nao como identificador, e o comando falha ou, pior, compara uma
// coluna com um texto literal.
enum class QuoteStyle : std::uint8_t {
    double_quotes,   // "nome" -- PostgreSQL, padrao SQL
    backticks,       // `nome` -- MySQL, MariaDB
    brackets,        // [nome] -- SQL Server
};

void set_sql_dialect(QuoteStyle style);
[[nodiscard]] QuoteStyle sql_dialect() noexcept;

// Define o dialeto a partir do identificador de driver do perfil.
void set_sql_dialect_for(std::string_view driver_id);

// Escapa um identificador quando ele precisa: nome com maiuscula, espaco ou
// palavra reservada sai entre aspas duplas.
//
// Sempre citar tambem funcionaria, mas "SELECT "id" FROM "cliente"" e' ruido
// visual em 99% dos casos -- e o SQL gerado e' para o usuario ler e editar.
[[nodiscard]] std::string quote_if_needed(std::string_view identifier);

// Literal de string no dialeto corrente, para COMMENT e DEFAULT.
//
// A diferenca importa: o MySQL trata a barra invertida como ESCAPE por padrao,
// ao contrario do padrao SQL. Escapar so' a aspa deixaria passar uma barra
// invertida final, que engoliria a aspa de fechamento -- um comentario de
// tabela vindo da UI viraria injecao.
[[nodiscard]] std::string quote_literal(std::string_view text);

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
