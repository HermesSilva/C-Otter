// C-Otter -- db/catalog_sqlanywhere.hpp
//
// Leitura de metadados do SQL Anywhere. Mapa em docs/SQLANYWHERE-MAP.md.
//
// A mesma forma dos outros tres catalogos, e as mesmas estruturas de
// db/catalog.hpp. O que e' proprio daqui:
//
//   1. Uma conexao = UM banco. O servidor pode ter varios bancos abertos, mas
//      cada um e' um arquivo a' parte, com os proprios usuarios: nao ha' nivel
//      de "banco" na arvore, como no MySQL.
//
//   2. Nao existe "schema": o dono do objeto (um usuario) faz esse papel, e e'
//      o que a arvore mostra como schema -- `GROUPO.Customers`.
//
//   3. Chave primaria, chave estrangeira e UNIQUE sao INDICES com categoria
//      (SYSIDX.index_category). O nome de uma chave estrangeira e' o nome do
//      indice dela; as acoes ON UPDATE/ON DELETE moram em SYSTRIGGER.
//
//   4. As views de catalogo sao lidas com [colchetes] nos nomes reservados
//      (`[unique]`, `[default]`, `[count]`): valem com quoted_identifier ligado
//      ou desligado.
//
// As consultas valem do SQL Anywhere 12 em diante (SYSTAB, SYSIDX, list() com
// ORDER BY); SYSSEQUENCE existe do 12 em diante, os papeis do 16.
#pragma once

#include "db/catalog.hpp"

namespace otter::db {

class SqlAnywhereCatalog {
public:
    explicit SqlAnywhereCatalog(Holt& holt);

    // Os donos que tem objeto (tabela, view, rotina, sequence, tipo), mais o
    // usuario da conexao. Primeiro ele, depois os de usuario, por ultimo os do
    // sistema (SYS, dbo).
    [[nodiscard]] Result<std::vector<SchemaMeta>> load_schemas();

    [[nodiscard]] Result<std::vector<TableMeta>>  load_tables(std::string_view schema);
    [[nodiscard]] Result<std::vector<ColumnMeta>> load_columns(std::string_view schema,
                                                               std::string_view table);
    [[nodiscard]] Result<std::vector<ForeignKeyMeta>> load_foreign_keys(
        std::string_view schema);
    [[nodiscard]] Result<std::vector<ConstraintMeta>> load_constraints(
        std::string_view schema, std::string_view table);
    [[nodiscard]] Result<std::vector<IndexMeta>> load_indexes(std::string_view schema,
                                                              std::string_view table);
    [[nodiscard]] Result<std::vector<ForeignKeyMeta>> load_table_foreign_keys(
        std::string_view schema, std::string_view table);
    [[nodiscard]] Result<std::vector<ForeignKeyMeta>> load_references(
        std::string_view schema, std::string_view table);
    [[nodiscard]] Result<std::vector<TriggerMeta>> load_triggers(std::string_view schema,
                                                                 std::string_view table);
    [[nodiscard]] Result<std::vector<SequenceMeta>> load_sequences(std::string_view schema);
    [[nodiscard]] Result<std::vector<RoutineMeta>> load_routines(std::string_view schema);
    // Os dominios (CREATE DOMAIN) do dono.
    [[nodiscard]] Result<std::vector<DataTypeMeta>> load_types(std::string_view schema);

    [[nodiscard]] Result<std::string> load_routine_definition(std::string_view schema,
                                                              std::string_view name,
                                                              std::string_view arguments);
    [[nodiscard]] Result<std::string> load_view_definition(std::string_view schema,
                                                           std::string_view name);

    // Nao ha' particionamento de tabela no SQL Anywhere.
    [[nodiscard]] Result<std::vector<PartitionMeta>> load_partitions(
        std::string_view schema, std::string_view table);
    // CREATE EVENT: por agenda ou por evento do sistema.
    [[nodiscard]] Result<std::vector<EventMeta>> load_events(std::string_view schema);

    // Indices do dono, parametros de rotina, dependencias de view, usuarios e
    // papeis, dbspaces e opcoes do banco.
    [[nodiscard]] Result<std::vector<CatalogItem>> load_list(CatalogList list,
                                                             std::string_view a,
                                                             std::string_view b,
                                                             std::string_view c);

    // Os usuarios que entram no banco, e o que cada um recebeu.
    [[nodiscard]] Result<std::vector<UserMeta>> load_users();
    [[nodiscard]] Result<std::vector<std::string>> load_grants(std::string_view user);

    // O DDL de uma tabela, do proprio servidor (sa_get_table_definition):
    // CREATE TABLE, comentarios, permissoes e triggers.
    //
    // A funcao TROCA opcoes da conexao e nao as devolve -- `chained` vira On,
    // `date_format` vira 'yyyy-mm-dd hh:nn', entre outras. Aqui as opcoes sao
    // lidas antes e depois, e o que mudou e' reposto.
    [[nodiscard]] Result<std::string> load_table_definition(std::string_view schema,
                                                            std::string_view table);

    // O CREATE EVENT inteiro: o catalogo guarda so' o corpo, a agenda e o tipo.
    [[nodiscard]] Result<std::string> load_event_definition(std::string_view name);

    // Propriedades da conexao, do servidor e do banco, e as opcoes em vigor.
    [[nodiscard]] Result<std::vector<ServerVariable>> load_properties(
        std::string_view procedure);
    [[nodiscard]] Result<std::vector<ServerVariable>> load_options();

    [[nodiscard]] ServerVersion version() const noexcept { return version_; }
    // O usuario da conexao: e' o "schema" onde a arvore comeca.
    [[nodiscard]] const std::string& user() const noexcept { return user_; }

private:
    [[nodiscard]] Result<std::vector<ForeignKeyMeta>> foreign_keys_where(
        const std::string& condition);

    Holt&         holt_;
    ServerVersion version_;
    std::string   user_;
};

// "nome", com a aspa dobrada. Vale com quoted_identifier ligado, que o driver
// liga ao conectar.
[[nodiscard]] std::string sqlanywhere_quote(std::string_view identifier);

// 'texto', com a aspa dobrada. Sem escape por barra: a conexao TDS deixa
// escape_character desligado.
[[nodiscard]] std::string sqlanywhere_literal(std::string_view text);

// "t.creator = user_id('dono') AND t.table_name = 'tabela'", para o apelido
// `alias` de SYS.SYSTAB.
[[nodiscard]] std::string sqlanywhere_table_filter(std::string_view alias,
                                                   std::string_view schema,
                                                   std::string_view table);

// A classe do tipo, pelo nome do dominio ("integer", "long varchar",
// "timestamp with time zone") ou pelo tipo com tamanho ("char(20)").
[[nodiscard]] DataKind sqlanywhere_kind(std::string_view type) noexcept;

// As letras de SYSTRIGGER: quando dispara e por que.
[[nodiscard]] std::string_view sqlanywhere_trigger_timing(std::string_view code) noexcept;
[[nodiscard]] std::string_view sqlanywhere_trigger_events(std::string_view code) noexcept;
// A acao referencial ('C', 'D', 'N', 'R'); vazio = RESTRICT, o padrao.
[[nodiscard]] std::string_view sqlanywhere_referential_action(std::string_view code) noexcept;

} // namespace otter::db
