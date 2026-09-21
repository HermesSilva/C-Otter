// C-Otter -- db/alter.hpp
//
// Gera o DDL de ALTERACAO a partir de um par (estado atual, estado desejado).
//
// Mapa do que o DBeaver cobre em docs/DDL-WRITE.md.
//
// Por que um DIFF e nao um formulario que monta SQL direto: so' comparando os
// dois estados da' para emitir apenas o que MUDOU. Um formulario que gera
// sempre a definicao inteira reescreveria atributos que o usuario nao tocou --
// e no MySQL, onde MODIFY COLUMN exige repetir tudo, um atributo esquecido e'
// REMOVIDO em silencio.
//
// Nada aqui executa. A saida e' um script para a UI mostrar e o usuario
// confirmar: um DROP disparado por clique errado nao tem desfazer.
#pragma once

#include "db/catalog.hpp"

#include <optional>
#include <string>
#include <vector>

namespace otter::db {

// O que muda numa coluna. `std::optional` vazio significa "nao mexer".
//
// Distinguir "nao mexer" de "definir vazio" e' o ponto: um DEFAULT que o
// usuario apagou deve virar DROP DEFAULT, e um que ele nao tocou nao deve
// aparecer no ALTER.
struct ColumnChange {
    std::string name;                        // coluna a alterar (obrigatorio)

    std::optional<std::string> new_name;
    std::optional<std::string> type_name;
    std::optional<bool>        nullable;
    std::optional<std::string> default_value;   // vazio = DROP DEFAULT
    std::optional<std::string> comment;

    [[nodiscard]] bool empty() const noexcept {
        return !new_name && !type_name && !nullable && !default_value && !comment;
    }
};

// Uma coluna nova, para ADD COLUMN.
struct NewColumn {
    std::string name;
    std::string type_name;
    bool        nullable = true;
    std::string default_value;
    std::string comment;

    // Posicao no MySQL: FIRST, ou AFTER <coluna>. O PostgreSQL nao suporta --
    // la' a coluna nova vai sempre para o fim, e pedir posicao seria um campo
    // que finge funcionar.
    std::string after;
    bool        first = false;
};

// Tudo que muda numa tabela.
struct TableAlteration {
    std::string schema;
    std::string table;

    std::optional<std::string> new_name;
    std::optional<std::string> comment;

    std::vector<NewColumn>    add_columns;
    std::vector<std::string>  drop_columns;
    std::vector<ColumnChange> alter_columns;

    [[nodiscard]] bool empty() const noexcept {
        return !new_name && !comment && add_columns.empty() &&
               drop_columns.empty() && alter_columns.empty();
    }
};

// O script gerado, com o que a UI precisa dizer antes de executar.
struct AlterScript {
    std::vector<std::string> statements;

    // Comandos que APAGAM dados ou estrutura. A UI destaca e pede confirmacao
    // explicita -- DROP COLUMN nao tem desfazer.
    std::vector<std::size_t> destructive;

    // Avisos: coisas que o SGBD faz e o usuario talvez nao espere.
    std::vector<std::string> warnings;

    // Falha de geracao: o que faltou. Vazio quando deu certo.
    std::string error;

    [[nodiscard]] bool ok() const noexcept {
        return error.empty() && !statements.empty();
    }
    [[nodiscard]] bool has_destructive() const noexcept {
        return !destructive.empty();
    }
};

// Gera o ALTER.
//
// `current` e' a tabela como esta' no catalogo -- NECESSARIA, e nao opcional:
// no MySQL, MODIFY COLUMN exige repetir a definicao inteira da coluna, e sem o
// estado atual um atributo nao mencionado seria REMOVIDO em silencio (o caso
// classico e' o AUTO_INCREMENT sumir).
//
// O dialeto vem de set_sql_dialect_for(), como no resto da geracao de SQL.
[[nodiscard]] AlterScript generate_alter(const TableMeta& current,
                                         const TableAlteration& wanted);

// CREATE TABLE a partir de uma lista de colunas.
[[nodiscard]] AlterScript generate_create_table(
    std::string_view schema, std::string_view table,
    const std::vector<NewColumn>& columns,
    const std::vector<std::string>& primary_key,
    std::string_view comment = {});

// DROP do objeto. Sempre destrutivo, sempre com aviso.
[[nodiscard]] AlterScript generate_drop(std::string_view schema,
                                        std::string_view name,
                                        ObjKind kind,
                                        bool cascade = false);

// --- Índices e constraints -----------------------------------------------------

struct NewIndex {
    std::string name;
    std::vector<std::string> columns;
    bool        unique = false;

    // btree, hash, gin, gist no PostgreSQL; BTREE, HASH, FULLTEXT no MySQL.
    // Vazio usa o padrão do SGBD, que é btree nos dois.
    std::string method;

    // CREATE INDEX CONCURRENTLY: não bloqueia escrita, mas NÃO roda dentro de
    // transação e pode deixar um índice inválido se falhar. Só PostgreSQL.
    bool        concurrently = false;
};

[[nodiscard]] AlterScript generate_create_index(std::string_view schema,
                                                std::string_view table,
                                                const NewIndex& index);

// Remove um índice.
//
// `from_constraint` é o índice que existe SÓ porque uma constraint o criou --
// o de uma PRIMARY KEY ou UNIQUE. Removê-lo direto é recusado pelo PostgreSQL
// e, pior, ACEITO pelo MySQL, que remove a constraint junto sem avisar. Nos
// dois casos a operação correta é remover a CONSTRAINT, e é isso que a
// mensagem diz.
[[nodiscard]] AlterScript generate_drop_index(std::string_view schema,
                                              std::string_view table,
                                              std::string_view index,
                                              bool from_constraint);

enum class ConstraintKind : std::uint8_t {
    primary_key,
    unique,
    check,
};

struct NewConstraint {
    std::string    name;
    ConstraintKind kind = ConstraintKind::unique;

    std::vector<std::string> columns;   // para PK e UNIQUE
    std::string              expression; // para CHECK
};

[[nodiscard]] AlterScript generate_add_constraint(std::string_view schema,
                                                  std::string_view table,
                                                  const NewConstraint& constraint);

[[nodiscard]] AlterScript generate_drop_constraint(std::string_view schema,
                                                   std::string_view table,
                                                   std::string_view name,
                                                   ObjKind kind);

struct NewForeignKey {
    std::string name;

    std::vector<std::string> columns;
    std::string              target_table;
    std::string              target_schema;
    std::vector<std::string> target_columns;

    std::string on_delete;   // NO ACTION, CASCADE, SET NULL, RESTRICT
    std::string on_update;
};

[[nodiscard]] AlterScript generate_add_foreign_key(std::string_view schema,
                                                   std::string_view table,
                                                   const NewForeignKey& key);

[[nodiscard]] AlterScript generate_drop_foreign_key(std::string_view schema,
                                                    std::string_view table,
                                                    std::string_view name);

// --- View, sequence e trigger ----------------------------------------------------

// CREATE OR REPLACE VIEW.
//
// `or_replace` usa a forma que preserva as permissões concedidas sobre a view
// -- `DROP` + `CREATE` as perderia em silêncio, e o usuário só descobriria
// quando alguém reclamasse de acesso negado.
[[nodiscard]] AlterScript generate_create_view(std::string_view schema,
                                               std::string_view name,
                                               std::string_view definition,
                                               bool or_replace = true);

struct NewSequence {
    std::string  name;
    std::int64_t start = 1;
    std::int64_t increment = 1;
    std::int64_t minimum = 0;
    std::int64_t maximum = 0;     // 0 = sem limite explícito
    bool         cycle = false;
};

[[nodiscard]] AlterScript generate_create_sequence(std::string_view schema,
                                                   const NewSequence& sequence);

struct NewTrigger {
    std::string name;
    std::string table;
    std::string timing;      // BEFORE, AFTER
    std::string event;       // INSERT, UPDATE, DELETE
    std::string body;        // o corpo, sem o BEGIN/END quando for uma linha
};

// CREATE TRIGGER.
//
// No MySQL o comando NÃO aceita nome qualificado: é preciso `USE <banco>`
// antes. O script gerado traz o `USE` como primeiro comando, e por isso
// devolve dois em vez de um.
[[nodiscard]] AlterScript generate_create_trigger(std::string_view schema,
                                                  const NewTrigger& trigger);

} // namespace otter::db
