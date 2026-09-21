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

} // namespace otter::db
