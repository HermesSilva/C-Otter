// Índices, constraints e chaves estrangeiras (docs/DDL-WRITE.md).
//
// A diferença entre os dois SGBDs é maior aqui do que em qualquer outro canto
// do DDL: no PostgreSQL o índice é objeto do SCHEMA e tem comando próprio; no
// MySQL ele pertence à TABELA e nasce dentro de um ALTER. Gerar a sintaxe de
// um no outro produz erro -- ou, no caso do DROP INDEX de uma constraint,
// algo pior: o MySQL aceita e remove a chave junto, em silêncio.
#include "test_main.hpp"

#include "db/alter.hpp"
#include "db/ddl.hpp"

#include <string>

using namespace otter::db;

namespace {

bool has(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string first(const AlterScript& script) {
    return script.statements.empty() ? std::string{} : script.statements[0];
}

struct DialectGuard {
    QuoteStyle previous = sql_dialect();
    explicit DialectGuard(QuoteStyle style) { set_sql_dialect(style); }
    ~DialectGuard() { set_sql_dialect(previous); }
};

} // namespace

// --- Índices --------------------------------------------------------------------

OTTER_TEST(index_postgres_is_a_schema_object_with_its_own_command) {
    const DialectGuard guard(QuoteStyle::double_quotes);

    NewIndex index;
    index.name    = "ix_cliente_nome";
    index.columns = {"nome", "situacao"};
    index.method  = "btree";

    const AlterScript script =
        generate_create_index("otter_test", "cliente", index);
    OTTER_CHECK(script.ok());

    const std::string sql = first(script);
    OTTER_CHECK(has(sql, "CREATE INDEX ix_cliente_nome"));
    OTTER_CHECK(has(sql, "ON otter_test.cliente"));

    // USING vem ANTES das colunas no PostgreSQL.
    OTTER_CHECK(sql.find("USING btree") < sql.find("(nome, situacao)"));
}

OTTER_TEST(index_mysql_lives_inside_alter_table_with_using_after_the_columns) {
    const DialectGuard guard(QuoteStyle::backticks);

    NewIndex index;
    index.name    = "ix_cliente_nome";
    index.columns = {"nome", "situacao"};
    index.method  = "BTREE";

    const std::string sql =
        first(generate_create_index("otter_test", "cliente", index));

    OTTER_CHECK(has(sql, "ALTER TABLE otter_test.cliente ADD INDEX"));
    OTTER_CHECK(!has(sql, "CREATE INDEX"));

    // E aqui o USING vem DEPOIS -- a ordem inversa do PostgreSQL.
    OTTER_CHECK(sql.find("(nome, situacao)") < sql.find("USING BTREE"));
}

OTTER_TEST(index_unique_is_marked_in_both_dialects) {
    NewIndex index;
    index.name    = "uq_documento";
    index.columns = {"documento"};
    index.unique  = true;

    {
        const DialectGuard guard(QuoteStyle::double_quotes);
        OTTER_CHECK(has(first(generate_create_index("s", "t", index)),
                        "CREATE UNIQUE INDEX"));
    }
    {
        const DialectGuard guard(QuoteStyle::backticks);
        OTTER_CHECK(has(first(generate_create_index("s", "t", index)),
                        "ADD UNIQUE INDEX"));
    }
}

OTTER_TEST(index_concurrently_warns_about_the_transaction) {
    // CONCURRENTLY não roda dentro de transação, e a UI envolve scripts de
    // vários comandos em BEGIN/COMMIT. Sem o aviso, o usuário receberia
    // "cannot run inside a transaction block" sem saber o que fazer.
    const DialectGuard guard(QuoteStyle::double_quotes);

    NewIndex index;
    index.name         = "ix";
    index.columns      = {"c"};
    index.concurrently = true;

    const AlterScript script = generate_create_index("s", "t", index);
    OTTER_CHECK(has(first(script), "CONCURRENTLY"));
    OTTER_CHECK(!script.warnings.empty());
}

OTTER_TEST(index_concurrently_is_not_emitted_in_mysql) {
    // O MySQL não tem CONCURRENTLY. Emitir a palavra daria erro de sintaxe.
    const DialectGuard guard(QuoteStyle::backticks);

    NewIndex index;
    index.name         = "ix";
    index.columns      = {"c"};
    index.concurrently = true;

    const AlterScript script = generate_create_index("s", "t", index);
    OTTER_CHECK(!has(first(script), "CONCURRENTLY"));
    OTTER_CHECK(!script.warnings.empty());
}

OTTER_TEST(index_drop_uses_the_right_owner_in_each_dialect) {
    {
        // No PostgreSQL o índice é objeto do SCHEMA: o comando não menciona a
        // tabela.
        const DialectGuard guard(QuoteStyle::double_quotes);
        const std::string sql =
            first(generate_drop_index("otter_test", "cliente", "ix_nome", false));
        OTTER_CHECK_EQ(sql, std::string{"DROP INDEX otter_test.ix_nome"});
    }
    {
        // No MySQL ele pertence à TABELA: sem ela o servidor não sabe onde
        // procurar.
        const DialectGuard guard(QuoteStyle::backticks);
        const std::string sql =
            first(generate_drop_index("otter_test", "cliente", "ix_nome", false));
        OTTER_CHECK(has(sql, "ALTER TABLE otter_test.cliente DROP INDEX ix_nome"));
    }
}

OTTER_TEST(index_from_a_constraint_is_refused_in_both_dialects) {
    // O PostgreSQL RECUSA com "cannot drop index because constraint requires
    // it". O MySQL ACEITA -- e remove a constraint junto, em SILÊNCIO. O
    // segundo caso é pior, e é por ele que a recusa é nossa, não do servidor.
    for (const QuoteStyle style : {QuoteStyle::double_quotes,
                                   QuoteStyle::backticks}) {
        const DialectGuard guard(style);

        const AlterScript script =
            generate_drop_index("otter_test", "cliente", "PRIMARY", true);

        OTTER_CHECK(!script.ok());
        OTTER_CHECK(has(script.error, "constraint"));
        OTTER_CHECK(script.statements.empty());   // nada a executar
    }
}

OTTER_TEST(index_refuses_without_columns) {
    NewIndex index;
    index.name = "ix";
    OTTER_CHECK(!generate_create_index("s", "t", index).ok());
}

// --- Constraints -----------------------------------------------------------------

OTTER_TEST(constraint_primary_key_warns_about_nulls_and_duplicates) {
    const DialectGuard guard(QuoteStyle::double_quotes);

    NewConstraint constraint;
    constraint.name    = "pk_cliente";
    constraint.kind    = ConstraintKind::primary_key;
    constraint.columns = {"id"};

    const AlterScript script =
        generate_add_constraint("otter_test", "cliente", constraint);

    OTTER_CHECK(script.ok());
    OTTER_CHECK(has(first(script),
                    "ADD CONSTRAINT pk_cliente PRIMARY KEY (id)"));
    OTTER_CHECK(!script.warnings.empty());
}

OTTER_TEST(constraint_name_is_optional) {
    // Sem nome, o SGBD gera um. Forçar o usuário a inventar um nome para uma
    // PK seria atrito sem ganho.
    const DialectGuard guard(QuoteStyle::double_quotes);

    NewConstraint constraint;
    constraint.kind    = ConstraintKind::unique;
    constraint.columns = {"documento"};

    const std::string sql =
        first(generate_add_constraint("s", "t", constraint));

    OTTER_CHECK(has(sql, "ADD UNIQUE (documento)"));
    OTTER_CHECK(!has(sql, "CONSTRAINT"));
}

OTTER_TEST(constraint_check_warns_that_old_mysql_ignores_it) {
    // O MySQL só PASSOU A APLICAR o CHECK no 8.0.16: antes ele aceitava a
    // sintaxe e ignorava a restrição -- uma constraint que finge funcionar.
    NewConstraint constraint;
    constraint.kind       = ConstraintKind::check;
    constraint.expression = "limite >= 0";

    {
        const DialectGuard guard(QuoteStyle::backticks);
        const AlterScript script = generate_add_constraint("s", "t", constraint);
        OTTER_CHECK(has(first(script), "CHECK (limite >= 0)"));
        OTTER_CHECK(!script.warnings.empty());
    }
    {
        const DialectGuard guard(QuoteStyle::double_quotes);
        const AlterScript script = generate_add_constraint("s", "t", constraint);
        OTTER_CHECK(has(first(script), "CHECK (limite >= 0)"));
        OTTER_CHECK(script.warnings.empty());   // no PostgreSQL sempre valeu
    }
}

OTTER_TEST(constraint_check_refuses_without_an_expression) {
    NewConstraint constraint;
    constraint.kind = ConstraintKind::check;
    OTTER_CHECK(!generate_add_constraint("s", "t", constraint).ok());
}

OTTER_TEST(constraint_drop_uses_the_mysql_specific_syntax) {
    {
        // No MySQL a chave primária nem tem nome: é sempre "PRIMARY", e o
        // comando é DROP PRIMARY KEY. DROP CONSTRAINT genérico só existe a
        // partir do 8.0.19 e quebraria em servidor mais antigo.
        const DialectGuard guard(QuoteStyle::backticks);

        const std::string pk = first(generate_drop_constraint(
            "otter_test", "cliente", "PRIMARY", ObjKind::primary_key));
        OTTER_CHECK(has(pk, "DROP PRIMARY KEY"));
        OTTER_CHECK(!has(pk, "PRIMARY KEY PRIMARY"));

        const std::string uq = first(generate_drop_constraint(
            "otter_test", "cliente", "uq_doc", ObjKind::unique_key));
        OTTER_CHECK(has(uq, "DROP KEY uq_doc"));
    }
    {
        const DialectGuard guard(QuoteStyle::double_quotes);
        const std::string sql = first(generate_drop_constraint(
            "otter_test", "cliente", "cliente_pkey", ObjKind::primary_key));
        OTTER_CHECK(has(sql, "DROP CONSTRAINT cliente_pkey"));
    }
}

OTTER_TEST(constraint_drop_of_a_primary_key_warns_the_grid_stops_editing) {
    // Sem PK a grade deixa de editar (ADR 0014). Dizer antes evita a surpresa
    // de descobrir ao tentar alterar uma célula.
    const DialectGuard guard(QuoteStyle::double_quotes);

    const AlterScript script = generate_drop_constraint(
        "s", "t", "pk", ObjKind::primary_key);

    OTTER_CHECK(script.has_destructive());
    OTTER_CHECK(!script.warnings.empty());

    // Uma constraint única não recebe o aviso: ela não é o que a grade usa
    // quando há PK.
    const AlterScript unique = generate_drop_constraint(
        "s", "t", "uq", ObjKind::unique_key);
    OTTER_CHECK(unique.warnings.empty());
}

// --- Chaves estrangeiras -----------------------------------------------------------

OTTER_TEST(foreign_key_builds_both_sides) {
    const DialectGuard guard(QuoteStyle::double_quotes);

    NewForeignKey key;
    key.name           = "fk_pedido_cliente";
    key.columns        = {"cliente_id"};
    key.target_table   = "cliente";
    key.target_columns = {"id"};
    key.on_delete      = "CASCADE";
    key.on_update      = "RESTRICT";

    const AlterScript script =
        generate_add_foreign_key("otter_test", "pedido", key);

    OTTER_CHECK(script.ok());

    const std::string sql = first(script);
    OTTER_CHECK(has(sql, "ALTER TABLE otter_test.pedido"));
    OTTER_CHECK(has(sql, "CONSTRAINT fk_pedido_cliente"));
    OTTER_CHECK(has(sql, "FOREIGN KEY (cliente_id)"));
    OTTER_CHECK(has(sql, "REFERENCES otter_test.cliente (id)"));
    OTTER_CHECK(has(sql, "ON DELETE CASCADE"));
    OTTER_CHECK(has(sql, "ON UPDATE RESTRICT"));
}

OTTER_TEST(foreign_key_refuses_mismatched_column_counts) {
    // 2 colunas de origem e 1 de destino é sintaxe válida que o servidor
    // recusa com uma mensagem obscura. Recusar aqui é mais útil.
    NewForeignKey key;
    key.columns        = {"a", "b"};
    key.target_table   = "t";
    key.target_columns = {"id"};

    const AlterScript script = generate_add_foreign_key("s", "x", key);
    OTTER_CHECK(!script.ok());
    OTTER_CHECK(has(script.error, "must match"));
}

OTTER_TEST(foreign_key_warns_postgres_does_not_index_the_source) {
    // O MySQL cria o índice sozinho; o PostgreSQL não -- e sem ele todo
    // DELETE na tabela de destino varre a de origem inteira. É a causa mais
    // comum de DELETE lento num banco com muitas FKs.
    NewForeignKey key;
    key.columns        = {"cliente_id"};
    key.target_table   = "cliente";
    key.target_columns = {"id"};

    {
        const DialectGuard guard(QuoteStyle::double_quotes);
        const AlterScript script = generate_add_foreign_key("s", "pedido", key);
        bool mentions_index = false;
        for (const std::string& warning : script.warnings) {
            if (has(warning, "index")) mentions_index = true;
        }
        OTTER_CHECK(mentions_index);
    }
    {
        const DialectGuard guard(QuoteStyle::backticks);
        const AlterScript script = generate_add_foreign_key("s", "pedido", key);
        for (const std::string& warning : script.warnings) {
            OTTER_CHECK(!has(warning, "does not index"));
        }
    }
}

OTTER_TEST(foreign_key_drop_uses_the_mysql_specific_syntax) {
    {
        const DialectGuard guard(QuoteStyle::backticks);
        OTTER_CHECK(has(first(generate_drop_foreign_key("s", "pedido", "fk")),
                        "DROP FOREIGN KEY fk"));
    }
    {
        // No PostgreSQL a FK é uma constraint como as outras.
        const DialectGuard guard(QuoteStyle::double_quotes);
        OTTER_CHECK(has(first(generate_drop_foreign_key("s", "pedido", "fk")),
                        "DROP CONSTRAINT fk"));
    }
}

OTTER_TEST(foreign_key_can_point_to_another_schema) {
    const DialectGuard guard(QuoteStyle::double_quotes);

    NewForeignKey key;
    key.columns        = {"cliente_id"};
    key.target_schema  = "outro";
    key.target_table   = "cliente";
    key.target_columns = {"id"};

    OTTER_CHECK(has(first(generate_add_foreign_key("otter_test", "pedido", key)),
                    "REFERENCES outro.cliente"));
}

// --- View, sequence e trigger ------------------------------------------------------

OTTER_TEST(view_uses_or_replace_to_keep_permissions) {
    // DROP + CREATE perderia as permissões concedidas sobre a view, em
    // silêncio -- e o usuário só descobriria quando alguém reclamasse de
    // acesso negado, dias depois.
    const DialectGuard guard(QuoteStyle::double_quotes);

    const AlterScript script = generate_create_view(
        "otter_test", "cliente_ativo",
        "SELECT * FROM otter_test.cliente WHERE ativo");

    OTTER_CHECK(script.ok());
    OTTER_CHECK(has(first(script), "CREATE OR REPLACE VIEW"));
    OTTER_CHECK(script.warnings.empty());

    // Sem OR REPLACE, o aviso diz o que vai acontecer.
    const AlterScript plain = generate_create_view(
        "s", "v", "SELECT 1", /*or_replace=*/false);
    OTTER_CHECK(!has(first(plain), "OR REPLACE"));
    OTTER_CHECK(!plain.warnings.empty());
}

OTTER_TEST(view_drops_the_trailing_semicolon_of_the_query) {
    // A consulta do usuário quase sempre termina em ';'. Dentro de
    // "CREATE VIEW ... AS <query>;" o ';' fica no meio do comando quando a UI
    // acrescenta o dela.
    const DialectGuard guard(QuoteStyle::double_quotes);

    const AlterScript script =
        generate_create_view("s", "v", "SELECT 1 FROM t;");
    OTTER_CHECK(!has(first(script), ";"));
}

OTTER_TEST(view_refuses_without_a_query) {
    OTTER_CHECK(!generate_create_view("s", "v", "").ok());
    OTTER_CHECK(!generate_create_view("s", "", "SELECT 1").ok());
}

OTTER_TEST(sequence_refuses_a_zero_increment) {
    // Incremento zero é aceito pela sintaxe e gera uma sequence que devolve
    // sempre o mesmo número. O servidor recusa, mas com uma mensagem que não
    // diz o que fazer.
    NewSequence sequence;
    sequence.name      = "seq";
    sequence.increment = 0;

    const AlterScript script = generate_create_sequence("s", sequence);
    OTTER_CHECK(!script.ok());
    OTTER_CHECK(has(script.error, "increment"));
}

OTTER_TEST(sequence_warns_that_mysql_has_none) {
    // No MySQL o equivalente é AUTO_INCREMENT, que é propriedade da COLUNA.
    // CREATE SEQUENCE ali dá erro de sintaxe -- e o aviso diz o que usar.
    NewSequence sequence;
    sequence.name = "seq";

    {
        const DialectGuard guard(QuoteStyle::backticks);
        const AlterScript script = generate_create_sequence("s", sequence);
        OTTER_CHECK(script.ok());
        OTTER_CHECK(!script.warnings.empty());
    }
    {
        const DialectGuard guard(QuoteStyle::double_quotes);
        OTTER_CHECK(generate_create_sequence("s", sequence).warnings.empty());
    }
}

OTTER_TEST(sequence_says_no_cycle_explicitly) {
    // NO CYCLE é o padrão nos dois SGBDs, mas dizer torna o DDL legível sem
    // consultar o manual.
    const DialectGuard guard(QuoteStyle::double_quotes);

    NewSequence sequence;
    sequence.name  = "seq";
    sequence.start = 100;

    const std::string sql = first(generate_create_sequence("s", sequence));
    OTTER_CHECK(has(sql, "START WITH 100"));
    OTTER_CHECK(has(sql, "NO CYCLE"));

    // Com CYCLE vem um aviso: a sequence pode repetir um valor já devolvido.
    sequence.cycle = true;
    const AlterScript cycling = generate_create_sequence("s", sequence);
    OTTER_CHECK(has(first(cycling), "CYCLE"));
    OTTER_CHECK(!has(first(cycling), "NO CYCLE"));
    OTTER_CHECK(!cycling.warnings.empty());
}

OTTER_TEST(trigger_mysql_needs_a_use_statement_first) {
    // O CREATE TRIGGER do MySQL NÃO aceita nome qualificado: é preciso
    // `USE <banco>` antes. Sem ele, a trigger nasceria no banco errado -- ou
    // o comando falharia, conforme o banco corrente.
    const DialectGuard guard(QuoteStyle::backticks);

    NewTrigger trigger;
    trigger.name   = "trg_antes";
    trigger.table  = "pedido";
    trigger.timing = "BEFORE";
    trigger.event  = "INSERT";
    trigger.body   = "SET NEW.criado = NOW()";

    const AlterScript script = generate_create_trigger("otter_test", trigger);
    OTTER_CHECK(script.ok());
    OTTER_CHECK_EQ(script.statements.size(), std::size_t{2});
    OTTER_CHECK(has(script.statements[0], "USE otter_test"));
    OTTER_CHECK(has(script.statements[1], "CREATE TRIGGER trg_antes BEFORE INSERT"));

    // E o nome da tabela vai SEM o banco, porque o USE já o definiu.
    OTTER_CHECK(!has(script.statements[1], "otter_test.pedido"));
}

OTTER_TEST(trigger_postgres_calls_a_function) {
    // No PostgreSQL a trigger chama uma FUNÇÃO que precisa existir antes -- o
    // corpo é o nome dela, não código. Dizer isso evita que o usuário cole um
    // bloco PL/pgSQL que o servidor recusa.
    const DialectGuard guard(QuoteStyle::double_quotes);

    NewTrigger trigger;
    trigger.name   = "trg_antes";
    trigger.table  = "pedido";
    trigger.timing = "BEFORE";
    trigger.event  = "INSERT";
    trigger.body   = "fn_preenche()";

    const AlterScript script = generate_create_trigger("otter_test", trigger);
    OTTER_CHECK(script.ok());
    OTTER_CHECK_EQ(script.statements.size(), std::size_t{1});   // sem USE
    OTTER_CHECK(has(first(script), "EXECUTE FUNCTION fn_preenche()"));
    OTTER_CHECK(has(first(script), "ON otter_test.pedido"));
    OTTER_CHECK(!script.warnings.empty());
}

OTTER_TEST(trigger_refuses_an_incomplete_spec) {
    NewTrigger trigger;
    trigger.name = "t";
    OTTER_CHECK(!generate_create_trigger("s", trigger).ok());

    trigger.table = "x";
    OTTER_CHECK(!generate_create_trigger("s", trigger).ok());   // sem corpo

    trigger.body = "SELECT 1";
    OTTER_CHECK(!generate_create_trigger("s", trigger).ok());   // sem timing
}

OTTER_TEST(view_warns_about_an_unqualified_query) {
    // A view guarda o corpo COMO ESCRITO. Um FROM sem banco depende do banco
    // corrente da conexão, e a nossa não tem um por padrão -- o MySQL recusa
    // com "No database selected", que não aponta para a causa.
    //
    // Encontrado ao rodar o spike contra o servidor real.
    const DialectGuard guard(QuoteStyle::double_quotes);

    const AlterScript unqualified =
        generate_create_view("s", "v", "SELECT * FROM cliente");
    OTTER_CHECK(unqualified.ok());
    OTTER_CHECK(!unqualified.warnings.empty());

    const AlterScript qualified =
        generate_create_view("s", "v", "SELECT * FROM otter_test.cliente");
    OTTER_CHECK(qualified.warnings.empty());
}
