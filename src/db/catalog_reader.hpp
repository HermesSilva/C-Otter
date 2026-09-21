// C-Otter -- db/catalog_reader.hpp
//
// Interface comum aos leitores de catalogo, para que a UI nao precise saber
// qual SGBD esta' do outro lado.
//
// Existe porque a Session chamava `PostgresCatalog` por nome. Com dois
// SGBDs isso viraria um `if` em cada ponto que le metadados -- e cada ponto
// esquecido consultaria o pg_catalog num servidor MySQL, dando um erro
// incompreensivel para o usuario.
//
// Interface virtual, nao template: a Session guarda o leitor por ponteiro e
// so' descobre o SGBD em tempo de execucao, ao conectar.
#pragma once

#include "db/catalog.hpp"

#include <memory>

namespace otter::db {

class CatalogReader {
public:
    virtual ~CatalogReader() = default;

    CatalogReader(const CatalogReader&)            = delete;
    CatalogReader& operator=(const CatalogReader&) = delete;

    [[nodiscard]] virtual Result<std::vector<SchemaMeta>> load_schemas() = 0;
    [[nodiscard]] virtual Result<std::vector<TableMeta>>  load_tables(
        std::string_view schema) = 0;
    [[nodiscard]] virtual Result<std::vector<ColumnMeta>> load_columns(
        std::string_view schema, std::string_view table) = 0;
    [[nodiscard]] virtual Result<std::vector<ForeignKeyMeta>> load_foreign_keys(
        std::string_view schema) = 0;

    [[nodiscard]] virtual Result<std::vector<ConstraintMeta>> load_constraints(
        std::string_view schema, std::string_view table) = 0;
    [[nodiscard]] virtual Result<std::vector<IndexMeta>> load_indexes(
        std::string_view schema, std::string_view table) = 0;
    [[nodiscard]] virtual Result<std::vector<ForeignKeyMeta>> load_table_foreign_keys(
        std::string_view schema, std::string_view table) = 0;
    [[nodiscard]] virtual Result<std::vector<ForeignKeyMeta>> load_references(
        std::string_view schema, std::string_view table) = 0;
    [[nodiscard]] virtual Result<std::vector<TriggerMeta>> load_triggers(
        std::string_view schema, std::string_view table) = 0;

    // Particoes de uma tabela. Lista vazia quando ela nao e' particionada --
    // e a pasta nao aparece, como no `visibleIf` do DBeaver.
    [[nodiscard]] virtual Result<std::vector<PartitionMeta>> load_partitions(
        std::string_view schema, std::string_view table) = 0;

    // Eventos agendados. So' o MySQL tem; no PostgreSQL devolve vazio.
    [[nodiscard]] virtual Result<std::vector<EventMeta>> load_events(
        std::string_view schema) = 0;

    [[nodiscard]] virtual Result<std::vector<SequenceMeta>> load_sequences(
        std::string_view schema) = 0;
    [[nodiscard]] virtual Result<std::vector<RoutineMeta>> load_routines(
        std::string_view schema) = 0;
    [[nodiscard]] virtual Result<std::vector<DataTypeMeta>> load_types(
        std::string_view schema) = 0;

    [[nodiscard]] virtual Result<std::string> load_routine_definition(
        std::string_view schema, std::string_view name,
        std::string_view arguments) = 0;
    [[nodiscard]] virtual Result<std::string> load_view_definition(
        std::string_view schema, std::string_view name) = 0;

    // O schema onde a UI deve comecar a navegar. "public" no PostgreSQL; no
    // MySQL, o banco da conexao -- la' nao existe nivel de schema, e apontar
    // para "public" levaria a um schema inexistente.
    [[nodiscard]] virtual std::string default_schema() const = 0;

    // Nomes dos conceitos, para a UI rotular. O MySQL chama de "Database" o
    // que o PostgreSQL chama de "Schema"; usar o rotulo errado confunde quem
    // conhece o SGBD.
    [[nodiscard]] virtual std::string_view schema_label() const noexcept = 0;

    // Quais pastas este SGBD tem. Equivale aos `visibleIf` do `<tree>` do
    // DBeaver.
    //
    // Uma pasta que o SGBD nao tem NAO deve aparecer vazia: "Sequences (0)"
    // num MySQL sugere que ele poderia ter uma, e o usuario fica procurando
    // o que nao existe. E' o mesmo raciocinio que ja' esconde "Constraints"
    // de uma view.
    [[nodiscard]] virtual bool has_sequences() const noexcept = 0;
    [[nodiscard]] virtual bool has_user_types() const noexcept = 0;
    [[nodiscard]] virtual bool has_events() const noexcept = 0;

protected:
    CatalogReader() = default;
};

// Cria o leitor adequado ao driver da conexao.
//
// `driver_id` vem do perfil ("postgresql", "mysql", "mariadb"). Um driver
// desconhecido devolve nulo, e quem chama precisa dizer isso -- devolver um
// leitor de PostgreSQL por padrao consultaria o pg_catalog num MySQL.
[[nodiscard]] std::unique_ptr<CatalogReader> make_catalog_reader(
    std::string_view driver_id, Holt& holt);

} // namespace otter::db
