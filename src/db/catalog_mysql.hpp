// C-Otter -- db/catalog_mysql.hpp
//
// Leitura de metadados do MySQL/MariaDB. Mapa em docs/MYSQL-MAP.md.
//
// A mesma forma do PostgresCatalog, e as mesmas estruturas de db/catalog.hpp
// -- o que muda e' a consulta, nao o que a arvore precisa saber. Duas
// diferencas estruturais valem destaque:
//
//   1. Nao existe schema dentro do banco. "database" e "schema" sao a mesma
//      coisa, e `load_schemas()` devolve os BANCOS. Inventar um nivel "public"
//      como no PostgreSQL apontaria para um schema que nao existe.
//
//   2. Nao existe OID. A origem de uma coluna e' identificada por (banco,
//      tabela), que e' o que o ColumnDefinition41 traz -- por isso
//      `TableMeta::oid` fica zerado aqui e a grade editavel usa
//      `ColumnInfo::source_table`.
#pragma once

#include "db/catalog.hpp"

namespace otter::db {

class MysqlCatalog {
public:
    explicit MysqlCatalog(Holt& holt);

    // Os BANCOS do servidor. Os internos (information_schema, performance_
    // schema, mysql, sys) ficam de fora por padrao, como no DBeaver: sao
    // dezenas de tabelas que ninguem navega no dia a dia.
    [[nodiscard]] Result<std::vector<SchemaMeta>> load_schemas();

    [[nodiscard]] Result<std::vector<TableMeta>>  load_tables(std::string_view schema);
    [[nodiscard]] Result<std::vector<ColumnMeta>> load_columns(std::string_view schema,
                                                               std::string_view table);
    [[nodiscard]] Result<std::vector<ForeignKeyMeta>> load_foreign_keys(
        std::string_view schema);

    [[nodiscard]] Result<std::vector<ConstraintMeta>> load_constraints(
        std::string_view schema, std::string_view table);

    [[nodiscard]] Result<std::vector<IndexMeta>> load_indexes(
        std::string_view schema, std::string_view table);

    [[nodiscard]] Result<std::vector<ForeignKeyMeta>> load_table_foreign_keys(
        std::string_view schema, std::string_view table);

    [[nodiscard]] Result<std::vector<ForeignKeyMeta>> load_references(
        std::string_view schema, std::string_view table);

    [[nodiscard]] Result<std::vector<TriggerMeta>> load_triggers(
        std::string_view schema, std::string_view table);

    // Sequences existem so' no MariaDB 10.3+. Em MySQL devolve lista vazia,
    // e' o que faz a pasta nao aparecer na arvore.
    [[nodiscard]] Result<std::vector<SequenceMeta>> load_sequences(
        std::string_view schema);

    [[nodiscard]] Result<std::vector<RoutineMeta>> load_routines(
        std::string_view schema);

    // O MySQL nao tem tipos definidos pelo usuario -- ENUM e SET sao
    // atributos de COLUNA, nao tipos nomeados. Devolve vazio sempre, e a
    // pasta nao aparece. Existe para a interface bater com o PostgresCatalog.
    [[nodiscard]] Result<std::vector<DataTypeMeta>> load_types(
        std::string_view schema);

    [[nodiscard]] Result<std::string> load_routine_definition(
        std::string_view schema, std::string_view name,
        std::string_view arguments);

    [[nodiscard]] Result<std::string> load_view_definition(
        std::string_view schema, std::string_view name);

    // --- Particoes, eventos e informacao do servidor -------------------------

    // Particoes de uma tabela. Lista vazia quando ela nao e' particionada --
    // o information_schema devolve UMA linha com PARTITION_NAME nulo nesse
    // caso, e trata-la como particao criaria um no' fantasma na arvore.
    [[nodiscard]] Result<std::vector<PartitionMeta>> load_partitions(
        std::string_view schema, std::string_view table);

    // Eventos agendados. Exige o event scheduler, que vem DESLIGADO por
    // padrao -- a lista pode estar vazia com eventos definidos.
    [[nodiscard]] Result<std::vector<EventMeta>> load_events(
        std::string_view schema);

    // As quatro pastas de System Info. `global` escolhe entre a sessao e o
    // servidor: sao numeros DIFERENTES, e confundi-los levaria a diagnostico
    // errado (uma sessao ociosa tem zero queries; o servidor tem milhoes).
    [[nodiscard]] Result<std::vector<ServerVariable>> load_status(bool global);
    [[nodiscard]] Result<std::vector<ServerVariable>> load_variables(bool global);

    // Contas do servidor. Exige privilegio SELECT em mysql.user -- sem ele o
    // servidor recusa, e a lista vem vazia com o erro propagado.
    [[nodiscard]] Result<std::vector<UserMeta>> load_users();

    // SHOW GRANTS de uma conta. Separado da lista porque e' uma consulta por
    // usuario: com 50 contas, carregar tudo junto seriam 50 idas ao servidor
    // para uma arvore que talvez nem seja expandida.
    [[nodiscard]] Result<std::vector<std::string>> load_grants(
        std::string_view user, std::string_view host);

    [[nodiscard]] Result<std::vector<ServerVariable>> load_engines();
    [[nodiscard]] Result<std::vector<ServerVariable>> load_charsets();
    // "User privileges" e "Plugins" do System Info do DBeaver.
    [[nodiscard]] Result<std::vector<ServerVariable>> load_privileges();
    [[nodiscard]] Result<std::vector<ServerVariable>> load_plugins();

    [[nodiscard]] ServerVersion version() const noexcept { return version_; }
    [[nodiscard]] bool is_mariadb() const noexcept { return mariadb_; }

private:
    Holt&         holt_;
    ServerVersion version_;
    bool          mariadb_ = false;
};

// Delimita com crase e escapa a crase interna.
//
// Publica para ser testada: o MySQL usa crase, nao aspas duplas. Aspas duplas
// so' funcionam com ANSI_QUOTES ligado, que nao e' o padrao -- e um nome de
// tabela vindo da arvore nao pode ser interpolado cru.
[[nodiscard]] std::string mysql_quote(std::string_view identifier);

// Literal de string para a clausula WHERE das consultas de catalogo.
//
// Nomes de objeto vem do servidor, mas tambem do filtro digitado pelo
// usuario. Interpolar cru seria injecao.
[[nodiscard]] std::string mysql_literal(std::string_view text);

} // namespace otter::db
