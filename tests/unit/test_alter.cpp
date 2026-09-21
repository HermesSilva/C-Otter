// Geracao de DDL de alteracao (docs/DDL-WRITE.md).
//
// O risco aqui nao e' gerar SQL invalido -- isso o servidor recusa e alguem
// percebe. E' gerar SQL VALIDO que faz outra coisa: um MODIFY COLUMN sem o
// AUTO_INCREMENT o REMOVE em silencio, e o defeito so' aparece quando o id
// para de incrementar, semanas depois.
#include "test_main.hpp"

#include "db/alter.hpp"
#include "db/ddl.hpp"

#include <string>

using namespace otter::db;

namespace {

bool has(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

// Junta o script numa string, para as verificacoes de conteudo.
std::string joined(const AlterScript& script) {
    std::string out;
    for (const std::string& statement : script.statements) {
        out += statement + "\n";
    }
    return out;
}

// Restaura o dialeto ao sair: os testes o trocam, e vazar para o proximo
// daria falha que depende da ORDEM de execucao -- a pior de diagnosticar.
struct DialectGuard {
    QuoteStyle previous = sql_dialect();
    explicit DialectGuard(QuoteStyle style) { set_sql_dialect(style); }
    ~DialectGuard() { set_sql_dialect(previous); }
};

// Tabela `cliente` com id auto_increment e nome varchar.
TableMeta cliente() {
    ColumnMeta id;
    id.name          = "id";
    id.type_name     = "int";
    id.kind          = DataKind::integer;
    id.nullable      = false;
    id.primary_key   = true;
    id.default_value = "auto_increment";   // como o catalogo MySQL o entrega

    ColumnMeta nome;
    nome.name      = "nome";
    nome.type_name = "varchar(120)";
    nome.kind      = DataKind::string;
    nome.nullable  = false;
    nome.comment   = "Razao social";

    ColumnMeta limite;
    limite.name          = "limite";
    limite.type_name     = "decimal(12,2)";
    limite.kind          = DataKind::numeric;
    limite.default_value = "0.00";

    TableMeta table;
    table.name            = "cliente";
    table.columns         = {id, nome, limite};
    table.columns_loaded  = true;
    table.estimated_rows  = 3;
    return table;
}

TableAlteration target() {
    TableAlteration wanted;
    wanted.schema = "otter_test";
    wanted.table  = "cliente";
    return wanted;
}

} // namespace

// --- A armadilha do MySQL -----------------------------------------------------

OTTER_TEST(alter_mysql_keeps_auto_increment_when_changing_the_type) {
    // MODIFY COLUMN substitui a definicao INTEIRA: o que nao for repetido e'
    // REMOVIDO. Mudar o tipo de `int` para `bigint` sem repetir o
    // AUTO_INCREMENT o apaga, e a tabela para de gerar ids -- sem erro
    // nenhum na hora.
    const DialectGuard guard(QuoteStyle::backticks);

    TableAlteration wanted = target();
    ColumnChange change;
    change.name      = "id";
    change.type_name = "bigint";
    wanted.alter_columns.push_back(change);

    const AlterScript script = generate_alter(cliente(), wanted);
    OTTER_CHECK(script.ok());

    const std::string sql = joined(script);
    OTTER_CHECK(has(sql, "MODIFY COLUMN"));
    OTTER_CHECK(has(sql, "bigint"));
    OTTER_CHECK(has(sql, "AUTO_INCREMENT"));   // o ponto do teste
    OTTER_CHECK(has(sql, "NOT NULL"));          // e o resto da definicao
}

OTTER_TEST(alter_mysql_keeps_the_comment_when_changing_nullability) {
    // Mesma armadilha, outro atributo: tornar a coluna anulavel nao pode
    // apagar o comentario dela.
    const DialectGuard guard(QuoteStyle::backticks);

    TableAlteration wanted = target();
    ColumnChange change;
    change.name     = "nome";
    change.nullable = true;
    wanted.alter_columns.push_back(change);

    const std::string sql = joined(generate_alter(cliente(), wanted));
    OTTER_CHECK(has(sql, "MODIFY COLUMN"));
    OTTER_CHECK(has(sql, "varchar(120)"));
    OTTER_CHECK(has(sql, "Razao social"));
    OTTER_CHECK(!has(sql, "NOT NULL"));    // era NOT NULL, deixou de ser
}

OTTER_TEST(alter_refuses_when_the_columns_are_not_loaded) {
    // Sem o estado atual nao da' para montar um MODIFY completo. Gerar um
    // ALTER que apaga atributos seria pior que recusar (diretiva 6).
    const DialectGuard guard(QuoteStyle::backticks);

    TableMeta table = cliente();
    table.columns.clear();
    table.columns_loaded = false;

    TableAlteration wanted = target();
    ColumnChange change;
    change.name      = "id";
    change.type_name = "bigint";
    wanted.alter_columns.push_back(change);

    const AlterScript script = generate_alter(table, wanted);
    OTTER_CHECK(!script.ok());
    OTTER_CHECK(has(script.error, "not loaded"));
}

// --- PostgreSQL: um atributo por comando ---------------------------------------

OTTER_TEST(alter_postgres_emits_one_statement_per_attribute) {
    // Ao contrario do MySQL, o PostgreSQL altera atributo por atributo -- e
    // por isso NAO corre o risco de apagar o que nao foi mencionado.
    const DialectGuard guard(QuoteStyle::double_quotes);

    TableAlteration wanted = target();
    ColumnChange change;
    change.name          = "limite";
    change.type_name     = "numeric(14,4)";
    change.nullable      = false;
    change.default_value = "1.00";
    wanted.alter_columns.push_back(change);

    const AlterScript script = generate_alter(cliente(), wanted);
    OTTER_CHECK(script.ok());
    OTTER_CHECK_EQ(script.statements.size(), std::size_t{3});

    const std::string sql = joined(script);
    OTTER_CHECK(has(sql, "ALTER COLUMN limite TYPE numeric(14,4)"));
    OTTER_CHECK(has(sql, "SET NOT NULL"));
    OTTER_CHECK(has(sql, "SET DEFAULT 1.00"));
}

OTTER_TEST(alter_postgres_renames_before_using_the_new_name) {
    // O RENAME COLUMN vem primeiro, e os comandos seguintes usam o nome NOVO.
    // Na ordem inversa, o ALTER COLUMN falharia procurando uma coluna que ja'
    // nao existe com aquele nome.
    const DialectGuard guard(QuoteStyle::double_quotes);

    TableAlteration wanted = target();
    ColumnChange change;
    change.name      = "nome";
    change.new_name  = "razao_social";
    change.type_name = "text";
    wanted.alter_columns.push_back(change);

    const AlterScript script = generate_alter(cliente(), wanted);
    OTTER_CHECK(script.ok());
    OTTER_CHECK(has(script.statements[0], "RENAME COLUMN nome TO razao_social"));
    OTTER_CHECK(has(script.statements[1], "ALTER COLUMN razao_social TYPE text"));
}

OTTER_TEST(alter_empty_default_means_drop_default) {
    // Distinguir "nao mexer" de "apagar" e' o motivo do optional: um DEFAULT
    // que o usuario limpou vira DROP DEFAULT; um que ele nao tocou nao
    // aparece no ALTER.
    const DialectGuard guard(QuoteStyle::double_quotes);

    TableAlteration wanted = target();
    ColumnChange change;
    change.name          = "limite";
    change.default_value = "";          // limpou
    wanted.alter_columns.push_back(change);

    OTTER_CHECK(has(joined(generate_alter(cliente(), wanted)), "DROP DEFAULT"));

    // Sem tocar no default: nada sobre DEFAULT no script.
    TableAlteration untouched = target();
    ColumnChange other;
    other.name     = "limite";
    other.nullable = false;
    untouched.alter_columns.push_back(other);

    OTTER_CHECK(!has(joined(generate_alter(cliente(), untouched)), "DEFAULT"));
}

// --- Ordem e destrutividade ------------------------------------------------------

OTTER_TEST(alter_renames_the_table_last) {
    // Os outros comandos usam o nome ANTIGO. Renomear primeiro os quebraria
    // todos -- e o erro apareceria no meio do script, com parte ja' aplicada.
    const DialectGuard guard(QuoteStyle::double_quotes);

    TableAlteration wanted = target();
    wanted.new_name = "cliente_novo";

    NewColumn column;
    column.name      = "ativo";
    column.type_name = "boolean";
    wanted.add_columns.push_back(column);

    const AlterScript script = generate_alter(cliente(), wanted);
    OTTER_CHECK(script.ok());
    OTTER_CHECK(has(script.statements.front(), "ADD COLUMN"));
    OTTER_CHECK(has(script.statements.back(), "RENAME TO cliente_novo"));
}

OTTER_TEST(alter_marks_drop_column_as_destructive) {
    // DROP COLUMN nao tem desfazer. A UI precisa poder destacar e pedir
    // confirmacao, e para isso precisa saber QUAL comando e' o perigoso.
    const DialectGuard guard(QuoteStyle::double_quotes);

    TableAlteration wanted = target();
    wanted.drop_columns.push_back("limite");

    NewColumn column;
    column.name      = "ativo";
    column.type_name = "boolean";
    wanted.add_columns.push_back(column);

    const AlterScript script = generate_alter(cliente(), wanted);
    OTTER_CHECK(script.has_destructive());
    OTTER_CHECK_EQ(script.destructive.size(), std::size_t{1});

    // E aponta para o DROP, nao para o ADD.
    OTTER_CHECK(has(script.statements[script.destructive[0]], "DROP COLUMN"));
}

OTTER_TEST(alter_refuses_a_column_that_does_not_exist) {
    const DialectGuard guard(QuoteStyle::double_quotes);

    TableAlteration wanted = target();
    wanted.drop_columns.push_back("coluna_inventada");

    const AlterScript script = generate_alter(cliente(), wanted);
    OTTER_CHECK(!script.ok());
    OTTER_CHECK(has(script.error, "coluna_inventada"));
}

// --- Avisos ----------------------------------------------------------------------

OTTER_TEST(alter_warns_about_not_null_without_default_on_a_table_with_rows) {
    // ADD COLUMN NOT NULL sem DEFAULT falha nos dois SGBDs quando ja' ha'
    // linhas. Avisar antes e' mais util que deixar o servidor recusar.
    const DialectGuard guard(QuoteStyle::double_quotes);

    TableAlteration wanted = target();
    NewColumn column;
    column.name      = "obrigatorio";
    column.type_name = "text";
    column.nullable  = false;
    wanted.add_columns.push_back(column);

    const AlterScript script = generate_alter(cliente(), wanted);
    OTTER_CHECK(script.ok());
    OTTER_CHECK(!script.warnings.empty());

    // Tabela vazia nao ganha o aviso: ali o comando funciona.
    TableMeta empty = cliente();
    empty.estimated_rows = 0;
    OTTER_CHECK(generate_alter(empty, wanted).warnings.empty());
}

OTTER_TEST(alter_warns_that_postgres_ignores_column_position) {
    // FIRST e AFTER so' existem no MySQL. Aceitar em silencio faria o usuario
    // crer numa ordenacao que nao aconteceu.
    const DialectGuard guard(QuoteStyle::double_quotes);

    TableAlteration wanted = target();
    NewColumn column;
    column.name      = "ativo";
    column.type_name = "boolean";
    column.first     = true;
    wanted.add_columns.push_back(column);

    const AlterScript script = generate_alter(cliente(), wanted);
    OTTER_CHECK(script.ok());
    OTTER_CHECK(!script.warnings.empty());
    OTTER_CHECK(!has(joined(script), "FIRST"));
}

OTTER_TEST(alter_mysql_places_the_column_where_asked) {
    const DialectGuard guard(QuoteStyle::backticks);

    TableAlteration wanted = target();
    NewColumn column;
    column.name      = "ativo";
    column.type_name = "tinyint(1)";
    column.after     = "nome";
    wanted.add_columns.push_back(column);

    const AlterScript script = generate_alter(cliente(), wanted);
    OTTER_CHECK(script.ok());
    OTTER_CHECK(has(joined(script), "AFTER nome"));
    OTTER_CHECK(script.warnings.empty());
}

// --- CREATE TABLE -----------------------------------------------------------------

OTTER_TEST(create_table_emits_columns_and_primary_key) {
    const DialectGuard guard(QuoteStyle::double_quotes);

    NewColumn id;
    id.name      = "id";
    id.type_name = "serial";
    id.nullable  = false;

    NewColumn nome;
    nome.name      = "nome";
    nome.type_name = "text";
    nome.comment   = "nome do cliente";

    const AlterScript script = generate_create_table(
        "otter_test", "novo", {id, nome}, {"id"}, "tabela de teste");

    OTTER_CHECK(script.ok());

    const std::string sql = joined(script);
    OTTER_CHECK(has(sql, "CREATE TABLE otter_test.novo"));
    OTTER_CHECK(has(sql, "id serial NOT NULL"));
    OTTER_CHECK(has(sql, "PRIMARY KEY (id)"));

    // No PostgreSQL o comentario vai em comando separado.
    OTTER_CHECK(has(sql, "COMMENT ON TABLE"));
    OTTER_CHECK(has(sql, "COMMENT ON COLUMN"));
    OTTER_CHECK(script.warnings.empty());
}

OTTER_TEST(create_table_mysql_inlines_the_comment) {
    const DialectGuard guard(QuoteStyle::backticks);

    NewColumn id;
    id.name      = "id";
    id.type_name = "int";
    id.nullable  = false;
    id.comment   = "chave";

    const AlterScript script =
        generate_create_table("otter_test", "novo", {id}, {"id"}, "de teste");

    const std::string sql = joined(script);
    OTTER_CHECK(has(sql, "COMMENT 'chave'"));       // na propria coluna
    OTTER_CHECK(has(sql, "COMMENT = 'de teste'"));  // na tabela
    OTTER_CHECK(!has(sql, "COMMENT ON"));           // sintaxe do PostgreSQL
}

OTTER_TEST(create_table_warns_when_there_is_no_primary_key) {
    // Sem PK a grade nao edita (ADR 0014). Dizer isso na hora de criar e'
    // mais barato que o usuario descobrir depois de popular a tabela.
    const DialectGuard guard(QuoteStyle::double_quotes);

    NewColumn column;
    column.name      = "valor";
    column.type_name = "int";

    const AlterScript script =
        generate_create_table("otter_test", "sem_pk", {column}, {});

    OTTER_CHECK(script.ok());
    OTTER_CHECK(!script.warnings.empty());
}

OTTER_TEST(create_table_refuses_without_columns) {
    const AlterScript script = generate_create_table("s", "t", {}, {});
    OTTER_CHECK(!script.ok());
    OTTER_CHECK(has(script.error, "at least one column"));
}

// --- DROP ---------------------------------------------------------------------------

OTTER_TEST(drop_is_always_destructive) {
    const DialectGuard guard(QuoteStyle::double_quotes);

    const AlterScript script =
        generate_drop("otter_test", "cliente", ObjKind::table);

    OTTER_CHECK(script.ok());
    OTTER_CHECK(script.has_destructive());
    OTTER_CHECK(has(script.statements[0], "DROP TABLE otter_test.cliente"));
}

OTTER_TEST(drop_cascade_warns_in_postgres_and_is_refused_in_mysql) {
    {
        const DialectGuard guard(QuoteStyle::double_quotes);
        const AlterScript script = generate_drop("s", "t", ObjKind::table, true);
        OTTER_CHECK(has(script.statements[0], "CASCADE"));
        OTTER_CHECK(!script.warnings.empty());
    }
    {
        // O MySQL ACEITA a palavra CASCADE em DROP TABLE e a IGNORA. Emiti-la
        // faria o usuario crer numa cascata que nao acontece -- o campo que
        // finge funcionar da diretiva 6.
        const DialectGuard guard(QuoteStyle::backticks);
        const AlterScript script = generate_drop("s", "t", ObjKind::table, true);
        OTTER_CHECK(!has(script.statements[0], "CASCADE"));
        OTTER_CHECK(!script.warnings.empty());
    }
}

OTTER_TEST(drop_refuses_an_object_kind_it_cannot_drop) {
    const AlterScript script = generate_drop("s", "c", ObjKind::column);
    OTTER_CHECK(!script.ok());
}

// --- Literais -------------------------------------------------------------------------

OTTER_TEST(literals_escape_the_backslash_only_in_mysql) {
    // O MySQL trata a barra invertida como escape; o padrao SQL, nao. Um
    // comentario terminado em barra engoliria a aspa de fechamento.
    {
        const DialectGuard guard(QuoteStyle::backticks);
        OTTER_CHECK_EQ(quote_literal("a\\"), std::string{"'a\\\\'"});
        OTTER_CHECK_EQ(quote_literal("o'x"), std::string{"'o''x'"});
    }
    {
        const DialectGuard guard(QuoteStyle::double_quotes);
        // No PostgreSQL a barra NAO e' escape: dobra-la mudaria o texto.
        OTTER_CHECK_EQ(quote_literal("a\\"), std::string{"'a\\'"});
        OTTER_CHECK_EQ(quote_literal("o'x"), std::string{"'o''x'"});
    }
}
