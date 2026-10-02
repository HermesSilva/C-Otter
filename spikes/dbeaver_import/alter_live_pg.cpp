// Aplica o DDL gerado contra o PostgreSQL REAL e confere o efeito.
//
// Irmao do alter_live.cpp (MySQL). Existe porque o ADR 0016 registrava "nao
// verificado contra PostgreSQL": os 20 testes unitarios comparam STRINGS, e
// um comando sintaticamente plausivel pode ser recusado pelo servidor -- foi
// o que aconteceu com load_routine_definition (ver test_catalog_live.cpp).
//
// Cobre tambem o que a grade grava (generate_changes) e o EXPLAIN, que usam o
// mesmo caminho de "gerar SQL e confiar que o servidor aceita".
//
// Usa o perfil PostgreSQL salvo no C-Otter -- sem senha em linha de comando --
// e troca o banco para ERP_TID (ou o primeiro argumento), onde fica o schema
// otter_test das fixtures. Cria e descarta as proprias tabelas.
#include "db/alter.hpp"
#include "db/catalog.hpp"
#include "db/connection_store.hpp"
#include "db/ddl.hpp"
#include "db/edit.hpp"
#include "db/plan.hpp"
#include "db/registry.hpp"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    std::printf(condition ? "  [ OK ] %s\n" : "  [FAIL] %s\n", what);
    if (!condition) ++failures;
}

const otter::db::ColumnMeta* column_named(const otter::db::TableMeta& table,
                                          std::string_view name) {
    for (const otter::db::ColumnMeta& column : table.columns) {
        if (column.name == name) return &column;
    }
    return nullptr;
}

constexpr const char* kSchema = "otter_test";

} // namespace

int main(int argc, char** argv) {
    const std::string database = argc > 1 ? argv[1] : "ERP_TID";

    auto profiles = otter::db::load_profiles(otter::db::otter_store_location());
    if (!profiles || profiles->empty()) {
        std::printf("sem perfis salvos\n");
        return 2;
    }

    const otter::db::StoredProfile* pg = nullptr;
    for (const auto& stored : *profiles) {
        // So' perfil LOCAL: este spike cria e altera tabelas. "O primeiro
        // perfil com senha" pode ser um servidor de producao salvo ao lado.
        const bool local = stored.profile.host == "localhost" ||
                           stored.profile.host == "127.0.0.1";
        if (stored.profile.driver_id == "postgresql" && local &&
            !stored.profile.password.empty()) {
            pg = &stored;
            break;
        }
    }
    if (pg == nullptr) {
        std::printf("sem perfil PostgreSQL LOCAL com senha salva\n");
        return 2;
    }

    otter::db::ConnConfig config = pg->profile.to_conn_config();
    config.database = database;

    otter::db::Driver* driver = otter::db::find_driver("postgresql");
    auto holt = driver->connect(config);
    if (!holt) {
        std::printf("conexao falhou: %s\n", holt.error().to_string().c_str());
        return 2;
    }
    otter::db::set_sql_dialect_for("postgresql");

    auto run = [&](const std::string& sql) {
        auto status = (*holt)->execute(sql);
        if (!status) {
            std::printf("  SQL FALHOU: %s\n    %s\n",
                        status.error().to_string().c_str(), sql.c_str());
            ++failures;
            return false;
        }
        return true;
    };

    auto apply = [&](const otter::db::AlterScript& script) {
        if (!script.ok()) {
            std::printf("  SCRIPT RECUSADO: %s\n", script.error.c_str());
            ++failures;
            return false;
        }
        for (const std::string& statement : script.statements) {
            std::printf("    %s\n", statement.c_str());
            if (!run(statement)) return false;
        }
        return true;
    };

    // Um valor escalar. Vazio quando a consulta falha ou nao traz linha --
    // o check seguinte falha com a mensagem certa.
    auto scalar = [&](const std::string& sql) -> std::string {
        auto rs = (*holt)->query(sql);
        if (!rs) {
            std::printf("  CONSULTA FALHOU: %s\n", rs.error().to_string().c_str());
            return {};
        }
        if (rs->row_count() == 0 || rs->is_null(0, 0)) return {};
        return std::string(rs->text(0, 0));
    };

    otter::db::PostgresCatalog catalog(**holt);

    auto reload = [&](otter::db::TableMeta& table, const char* name) {
        auto columns = catalog.load_columns(kSchema, name);
        if (!columns) {
            std::printf("  catalogo falhou: %s\n", columns.error().to_string().c_str());
            ++failures;
            return false;
        }
        table                = {};
        table.name           = name;
        table.columns        = std::move(*columns);
        table.columns_loaded = true;
        return true;
    };

    std::printf("Servidor: %s, banco %s\n\n",
                (*holt)->server_version().c_str(), database.c_str());

    (void)(*holt)->execute("DROP TABLE IF EXISTS otter_test.alter_child");
    (void)(*holt)->execute("DROP TABLE IF EXISTS otter_test.alter_probe CASCADE");
    (void)(*holt)->execute("DROP TABLE IF EXISTS otter_test.alter_probe2 CASCADE");

    // --- CREATE TABLE ------------------------------------------------------------

    std::printf("CREATE TABLE pelo gerador\n");
    {
        std::vector<otter::db::NewColumn> columns(3);
        columns[0].name      = "id";
        columns[0].type_name = "serial";
        columns[0].nullable  = false;
        columns[1].name      = "nome";
        columns[1].type_name = "varchar(50)";
        columns[1].nullable  = false;
        columns[1].comment   = "o nome";
        columns[2].name          = "valor";
        columns[2].type_name     = "numeric(10,2)";
        columns[2].default_value = "1.00";

        const otter::db::AlterScript script = otter::db::generate_create_table(
            kSchema, "alter_probe", columns, {"id"}, "tabela de prova");
        if (!apply(script)) return 1;

        otter::db::TableMeta after;
        if (!reload(after, "alter_probe")) return 1;
        check(after.columns.size() == 3, "3 colunas");
        const otter::db::ColumnMeta* nome = column_named(after, "nome");
        check(nome != nullptr && nome->comment == "o nome",
              "COMMENT ON COLUMN aplicado");
        check(nome != nullptr && !nome->nullable, "NOT NULL aplicado");
        const otter::db::ColumnMeta* id = column_named(after, "id");
        check(id != nullptr && id->primary_key, "PRIMARY KEY aplicada");
        check(scalar("SELECT obj_description('otter_test.alter_probe'::regclass,"
                     " 'pg_class')") == "tabela de prova",
              "COMMENT ON TABLE aplicado");

        check(run("INSERT INTO otter_test.alter_probe (nome) VALUES ('a'), ('b')"),
              "INSERT usa o serial e o DEFAULT");
        check(scalar("SELECT valor FROM otter_test.alter_probe WHERE nome = 'a'")
                  == "1.00",
              "DEFAULT 1.00 vale");
    }

    otter::db::TableAlteration base;
    base.schema = kSchema;
    base.table  = "alter_probe";

    // --- O que o teste unitario NAO prova ----------------------------------------

    std::printf("\nint -> bigint na coluna serial\n");
    {
        otter::db::TableMeta current;
        if (!reload(current, "alter_probe")) return 1;

        otter::db::TableAlteration wanted = base;
        otter::db::ColumnChange change;
        change.name      = "id";
        change.type_name = "bigint";
        wanted.alter_columns.push_back(change);

        if (!apply(otter::db::generate_alter(current, wanted))) return 1;

        otter::db::TableMeta after;
        if (!reload(after, "alter_probe")) return 1;
        const otter::db::ColumnMeta* id = column_named(after, "id");
        check(id != nullptr && id->type_name == "bigint", "tipo virou bigint");
        check(id != nullptr &&
                  id->default_value.find("nextval") != std::string::npos,
              "DEFAULT nextval SOBREVIVEU");
        if (run("INSERT INTO otter_test.alter_probe (nome) VALUES ('c')")) {
            check(scalar("SELECT max(id) FROM otter_test.alter_probe") == "3",
                  "id continua sendo gerado");
        }
    }

    std::printf("\nnulidade, default e comentario, um atributo por comando\n");
    {
        otter::db::TableMeta current;
        if (!reload(current, "alter_probe")) return 1;

        otter::db::TableAlteration wanted = base;
        otter::db::ColumnChange nome;
        nome.name     = "nome";
        nome.nullable = true;
        nome.comment  = "nome revisto";
        wanted.alter_columns.push_back(nome);
        otter::db::ColumnChange valor;
        valor.name          = "valor";
        valor.default_value = "";   // DROP DEFAULT
        wanted.alter_columns.push_back(valor);

        if (!apply(otter::db::generate_alter(current, wanted))) return 1;

        otter::db::TableMeta after;
        if (!reload(after, "alter_probe")) return 1;
        const otter::db::ColumnMeta* n = column_named(after, "nome");
        check(n != nullptr && n->nullable, "DROP NOT NULL");
        check(n != nullptr && n->comment == "nome revisto", "comentario trocado");
        check(n != nullptr && n->type_name.find("character varying(50)") == 0 ||
                  (n != nullptr && n->type_name.find("varchar(50)") == 0),
              "tipo sobreviveu");
        const otter::db::ColumnMeta* v = column_named(after, "valor");
        check(v != nullptr && v->default_value.empty(), "DROP DEFAULT");
    }

    std::printf("\nacrescentar, renomear e remover coluna\n");
    {
        otter::db::TableMeta current;
        if (!reload(current, "alter_probe")) return 1;

        otter::db::TableAlteration wanted = base;
        otter::db::NewColumn column;
        column.name          = "ativo";
        column.type_name     = "boolean";
        column.nullable      = false;
        column.default_value = "true";
        column.comment       = "liga/desliga";
        wanted.add_columns.push_back(column);
        if (!apply(otter::db::generate_alter(current, wanted))) return 1;

        otter::db::TableMeta after;
        if (!reload(after, "alter_probe")) return 1;
        const otter::db::ColumnMeta* ativo = column_named(after, "ativo");
        check(ativo != nullptr, "coluna acrescentada");
        check(ativo != nullptr && !ativo->nullable, "NOT NULL com DEFAULT aceito em tabela com linhas");
        check(ativo != nullptr && ativo->comment == "liga/desliga",
              "comentario da coluna nova");

        // A coluna acrescentada nao aceita posicao no PostgreSQL. O gerador
        // precisa avisar em vez de mandar um AFTER que o servidor recusa.
        otter::db::TableAlteration positioned = base;
        otter::db::NewColumn misplaced;
        misplaced.name      = "x";
        misplaced.type_name = "int";
        misplaced.after     = "id";
        positioned.add_columns.push_back(misplaced);
        const otter::db::AlterScript refused =
            otter::db::generate_alter(after, positioned);
        check(!refused.ok() || !refused.warnings.empty(),
              "posicao de coluna recusada ou avisada");

        otter::db::TableAlteration rename = base;
        otter::db::ColumnChange change;
        change.name     = "ativo";
        change.new_name = "habilitado";
        rename.alter_columns.push_back(change);
        if (!apply(otter::db::generate_alter(after, rename))) return 1;

        otter::db::TableMeta renamed;
        if (!reload(renamed, "alter_probe")) return 1;
        const otter::db::ColumnMeta* h = column_named(renamed, "habilitado");
        check(h != nullptr, "coluna renomeada");
        check(h != nullptr && h->default_value == "true",
              "DEFAULT sobreviveu ao rename");

        otter::db::TableAlteration drop = base;
        drop.drop_columns.push_back("habilitado");
        const otter::db::AlterScript script =
            otter::db::generate_alter(renamed, drop);
        check(script.has_destructive(), "DROP COLUMN marcado como destrutivo");
        if (!apply(script)) return 1;

        otter::db::TableMeta dropped;
        if (!reload(dropped, "alter_probe")) return 1;
        check(column_named(dropped, "habilitado") == nullptr, "coluna removida");
    }

    std::printf("\nDDL transacional: um erro no meio desfaz tudo\n");
    {
        // E' o que o PostgreSQL tem e o MySQL nao (DDL-WRITE.md, §3). Se o
        // ROLLBACK nao desfizer o ALTER, a promessa da janela de conferencia
        // e' falsa.
        check(run("BEGIN"), "BEGIN");
        check(run("ALTER TABLE otter_test.alter_probe ADD COLUMN temp int"),
              "ALTER dentro da transacao");
        auto bad = (*holt)->execute("ALTER TABLE otter_test.alter_probe "
                                    "ADD COLUMN temp int");
        check(!bad.has_value(), "segundo ALTER falha (coluna duplicada)");
        (void)(*holt)->execute("ROLLBACK");
        otter::db::TableMeta after;
        if (!reload(after, "alter_probe")) return 1;
        check(column_named(after, "temp") == nullptr,
              "ROLLBACK desfez o primeiro ALTER");
    }

    std::printf("\nrenomear tabela e trocar comentario\n");
    {
        otter::db::TableMeta current;
        if (!reload(current, "alter_probe")) return 1;

        otter::db::TableAlteration wanted = base;
        wanted.new_name = "alter_probe2";
        wanted.comment  = "renomeada";
        if (!apply(otter::db::generate_alter(current, wanted))) return 1;

        check(scalar("SELECT to_regclass('otter_test.alter_probe2') IS NOT NULL")
                  == "t",
              "tabela renomeada");
        check(scalar("SELECT obj_description('otter_test.alter_probe2'::regclass,"
                     " 'pg_class')") == "renomeada",
              "comentario aplicado no nome NOVO");

        // Volta ao nome original para o resto do roteiro.
        otter::db::TableMeta renamed;
        if (!reload(renamed, "alter_probe2")) return 1;
        otter::db::TableAlteration back;
        back.schema   = kSchema;
        back.table    = "alter_probe2";
        back.new_name = "alter_probe";
        if (!apply(otter::db::generate_alter(renamed, back))) return 1;
    }

    // --- Indices, constraints e chaves estrangeiras ------------------------------

    std::printf("\ncriar e remover indice (com metodo)\n");
    {
        otter::db::NewIndex index;
        index.name    = "ix_probe_nome";
        index.columns = {"nome"};
        index.method  = "hash";
        if (!apply(otter::db::generate_create_index(kSchema, "alter_probe", index))) {
            return 1;
        }
        check(scalar("SELECT am.amname FROM pg_class c JOIN pg_am am"
                     " ON am.oid = c.relam WHERE c.oid ="
                     " 'otter_test.ix_probe_nome'::regclass") == "hash",
              "indice criado com o metodo pedido (USING antes das colunas)");

        auto indexes = catalog.load_indexes(kSchema, "alter_probe");
        bool listed = false;
        if (indexes) {
            for (const auto& ix : *indexes) listed |= ix.name == "ix_probe_nome";
        }
        check(listed, "catalogo lista o indice novo");

        const otter::db::AlterScript drop = otter::db::generate_drop_index(
            kSchema, "alter_probe", "ix_probe_nome", /*from_constraint=*/false);
        check(drop.has_destructive(), "remocao marcada como destrutiva");
        if (!apply(drop)) return 1;
        check(scalar("SELECT to_regclass('otter_test.ix_probe_nome') IS NULL") == "t",
              "indice removido (DROP INDEX qualificado pelo schema)");
    }

    std::printf("\nCREATE INDEX CONCURRENTLY fora de transacao\n");
    {
        otter::db::NewIndex index;
        index.name         = "ix_probe_valor";
        index.columns      = {"valor"};
        index.concurrently = true;
        const otter::db::AlterScript script =
            otter::db::generate_create_index(kSchema, "alter_probe", index);
        if (apply(script)) {
            check(scalar("SELECT indisvalid FROM pg_index WHERE indexrelid ="
                         " 'otter_test.ix_probe_valor'::regclass") == "t",
                  "indice CONCURRENTLY valido");
            (void)apply(otter::db::generate_drop_index(
                kSchema, "alter_probe", "ix_probe_valor", false));
        }
    }

    std::printf("\nindice de chave primaria e' recusado ANTES do servidor\n");
    {
        const otter::db::AlterScript script = otter::db::generate_drop_index(
            kSchema, "alter_probe", "alter_probe_pkey", /*from_constraint=*/true);
        check(!script.ok(), "recusado antes de chegar ao servidor");
        check(script.statements.empty(), "nada a executar");
        check(scalar("SELECT count(*) FROM pg_constraint WHERE conrelid ="
                     " 'otter_test.alter_probe'::regclass AND contype = 'p'") == "1",
              "chave primaria intacta");
    }

    std::printf("\nUNIQUE e CHECK que RESTRINGEM de verdade\n");
    {
        otter::db::NewConstraint unique;
        unique.name    = "uq_probe_nome";
        unique.kind    = otter::db::ConstraintKind::unique;
        unique.columns = {"nome"};
        if (!apply(otter::db::generate_add_constraint(kSchema, "alter_probe", unique))) {
            return 1;
        }
        auto duplicate = (*holt)->execute(
            "INSERT INTO otter_test.alter_probe (nome) VALUES ('a')");
        check(!duplicate.has_value(), "UNIQUE restringe duplicado");

        otter::db::NewConstraint positive;
        positive.name       = "ck_probe_valor";
        positive.kind       = otter::db::ConstraintKind::check;
        positive.expression = "valor IS NULL OR valor >= 0";
        if (!apply(otter::db::generate_add_constraint(kSchema, "alter_probe",
                                                      positive))) {
            return 1;
        }
        auto negative = (*holt)->execute(
            "INSERT INTO otter_test.alter_probe (nome, valor) VALUES ('neg', -1)");
        check(!negative.has_value(), "CHECK restringe valor negativo");

        auto constraints = catalog.load_constraints(kSchema, "alter_probe");
        int found = 0;
        if (constraints) {
            for (const auto& c : *constraints) {
                found += c.name == "uq_probe_nome" || c.name == "ck_probe_valor";
            }
        }
        check(found == 2, "catalogo lista as duas constraints");

        if (!apply(otter::db::generate_drop_constraint(
                kSchema, "alter_probe", "uq_probe_nome",
                otter::db::ObjKind::unique_key))) {
            return 1;
        }
        if (!apply(otter::db::generate_drop_constraint(
                kSchema, "alter_probe", "ck_probe_valor",
                otter::db::ObjKind::check_constraint))) {
            return 1;
        }
        check(scalar("SELECT count(*) FROM pg_constraint WHERE conrelid ="
                     " 'otter_test.alter_probe'::regclass AND contype IN ('u','c')")
                  == "0",
              "constraints removidas");
    }

    std::printf("\nchave estrangeira que RESTRINGE e CASCATEIA\n");
    {
        if (!run("CREATE TABLE otter_test.alter_child ("
                 "  id serial PRIMARY KEY,"
                 "  probe_id bigint NOT NULL)")) {
            return 1;
        }

        otter::db::NewForeignKey key;
        key.name           = "fk_child_probe";
        key.columns        = {"probe_id"};
        key.target_table   = "alter_probe";
        key.target_schema  = kSchema;
        key.target_columns = {"id"};
        key.on_delete      = "CASCADE";
        const otter::db::AlterScript script =
            otter::db::generate_add_foreign_key(kSchema, "alter_child", key);
        check(!script.warnings.empty(),
              "avisa que o PostgreSQL nao indexa a coluna de origem");
        if (!apply(script)) return 1;

        auto orphan = (*holt)->execute(
            "INSERT INTO otter_test.alter_child (probe_id) VALUES (9999)");
        check(!orphan.has_value(), "FK restringe linha orfa");

        check(run("INSERT INTO otter_test.alter_child (probe_id) VALUES (1)"),
              "linha filha valida");
        check(run("DELETE FROM otter_test.alter_probe WHERE id = 1"),
              "apaga o pai");
        check(scalar("SELECT count(*) FROM otter_test.alter_child") == "0",
              "ON DELETE CASCADE levou a filha");

        auto refs = catalog.load_references(kSchema, "alter_probe");
        bool seen = false;
        if (refs) {
            for (const auto& fk : *refs) seen |= fk.name == "fk_child_probe";
        }
        check(seen, "References do pai lista a FK");

        if (!apply(otter::db::generate_drop_foreign_key(kSchema, "alter_child",
                                                        "fk_child_probe"))) {
            return 1;
        }
        check(scalar("SELECT count(*) FROM pg_constraint WHERE conname ="
                     " 'fk_child_probe'") == "0",
              "FK removida");
        (void)(*holt)->execute("DROP TABLE otter_test.alter_child");
    }

    // --- View, sequence, trigger -------------------------------------------------

    std::printf("\nview com OR REPLACE\n");
    {
        if (!apply(otter::db::generate_create_view(
                kSchema, "v_probe",
                "SELECT id, nome FROM otter_test.alter_probe"))) {
            return 1;
        }
        check(run("GRANT SELECT ON otter_test.v_probe TO PUBLIC"), "GRANT na view");

        // Trocar as colunas de uma view com OR REPLACE e' RECUSADO pelo
        // PostgreSQL ("cannot drop columns from view"). Acrescentar no fim, nao.
        const bool replaced = apply(otter::db::generate_create_view(
            kSchema, "v_probe",
            "SELECT id, nome, valor FROM otter_test.alter_probe"));
        check(replaced, "OR REPLACE acrescentando coluna no fim");
        check(scalar("SELECT count(*) FROM information_schema.columns"
                     " WHERE table_schema='otter_test' AND table_name='v_probe'")
                  == "3",
              "a definicao foi SUBSTITUIDA (3 colunas)");
        check(scalar("SELECT has_table_privilege('public', 'otter_test.v_probe',"
                     " 'SELECT')") == "t",
              "GRANT SOBREVIVEU ao OR REPLACE");
        (void)(*holt)->execute("DROP VIEW otter_test.v_probe");
    }

    std::printf("\nsequence\n");
    {
        (void)(*holt)->execute("DROP SEQUENCE IF EXISTS otter_test.sq_probe");
        otter::db::NewSequence sequence;
        sequence.name      = "sq_probe";
        sequence.start     = 100;
        sequence.increment = 5;
        sequence.maximum   = 110;
        sequence.cycle     = true;
        sequence.minimum   = 100;
        if (apply(otter::db::generate_create_sequence(kSchema, sequence))) {
            check(scalar("SELECT nextval('otter_test.sq_probe')") == "100", "START 100");
            check(scalar("SELECT nextval('otter_test.sq_probe')") == "105", "INCREMENT 5");
            (void)scalar("SELECT nextval('otter_test.sq_probe')");
            check(scalar("SELECT nextval('otter_test.sq_probe')") == "100",
                  "CYCLE volta ao MINVALUE");
            auto sequences = catalog.load_sequences(kSchema);
            bool listed = false;
            if (sequences) {
                for (const auto& s : *sequences) listed |= s.name == "sq_probe";
            }
            check(listed, "catalogo lista a sequence");
            (void)(*holt)->execute("DROP SEQUENCE otter_test.sq_probe");
        }
    }

    std::printf("\ntrigger chamando uma funcao\n");
    {
        if (!run("CREATE OR REPLACE FUNCTION otter_test.fn_probe_upper()"
                 " RETURNS trigger LANGUAGE plpgsql AS"
                 " $$BEGIN NEW.nome := upper(NEW.nome); RETURN NEW; END$$")) {
            return 1;
        }
        otter::db::NewTrigger trigger;
        trigger.name   = "trg_probe";
        trigger.table  = "alter_probe";
        trigger.timing = "BEFORE";
        trigger.event  = "INSERT";
        trigger.body   = "otter_test.fn_probe_upper()";
        if (apply(otter::db::generate_create_trigger(kSchema, trigger))) {
            check(run("INSERT INTO otter_test.alter_probe (nome) VALUES ('zz')"),
                  "insert com a trigger");
            check(scalar("SELECT nome FROM otter_test.alter_probe"
                         " WHERE nome IN ('zz','ZZ')") == "ZZ",
                  "a trigger DISPARA de verdade");
            auto triggers = catalog.load_triggers(kSchema, "alter_probe");
            bool listed = false;
            if (triggers) {
                for (const auto& t : *triggers) listed |= t.name == "trg_probe";
            }
            check(listed, "catalogo lista a trigger");
        }
        (void)(*holt)->execute("DROP TRIGGER IF EXISTS trg_probe ON otter_test.alter_probe");
        (void)(*holt)->execute("DROP FUNCTION IF EXISTS otter_test.fn_probe_upper()");
    }

    // --- Grade editavel (ADR 0014) -----------------------------------------------

    std::printf("\ngrade: UPDATE, INSERT e DELETE gerados a partir do resultado\n");
    {
        auto schemas = catalog.load_schemas();
        if (!schemas) { check(false, "load_schemas"); return 1; }
        for (auto& schema : *schemas) {
            if (schema.name != kSchema) continue;
            auto tables = catalog.load_tables(kSchema);
            if (!tables) { check(false, "load_tables"); return 1; }
            schema.tables        = std::move(*tables);
            schema.tables_loaded = true;
            for (auto& table : schema.tables) {
                if (table.name != "alter_probe") continue;
                auto columns     = catalog.load_columns(kSchema, table.name);
                auto constraints = catalog.load_constraints(kSchema, table.name);
                if (columns) table.columns = std::move(*columns);
                if (constraints) table.constraints = std::move(*constraints);
                table.constraints_loaded = constraints.has_value();
                table.columns_loaded = true;
            }
        }

        auto rs = (*holt)->query(
            "SELECT p.id, p.nome, p.valor FROM otter_test.alter_probe p ORDER BY p.id");
        if (!rs) { check(false, "SELECT da grade"); return 1; }

        const otter::db::EditTarget target = otter::db::find_edit_target(*rs, *schemas);
        check(target.editable(), "resultado com alias e' editavel pela PK");
        if (!target.editable()) {
            std::printf("    recusa: %s\n",
                        std::string(otter::db::to_string(target.refusal)).c_str());
        }

        if (target.editable() && rs->row_count() >= 2) {
            const std::string first_id(rs->text(0, 0));
            const std::string second_id(rs->text(1, 0));

            otter::db::EditBuffer buffer;
            buffer.set(0, 1, "O'Brien");          // aspas no literal
            buffer.set_null(0, 2);
            buffer.mark_deleted(1);
            const std::size_t fresh = buffer.add_row();
            buffer.set_new_value(fresh, 1, "nova");

            auto statements = otter::db::generate_changes(*rs, target, buffer);
            check(statements.has_value(), "comandos gerados");
            if (statements) {
                bool ok = run("BEGIN");
                for (const std::string& sql : *statements) {
                    std::printf("    %s\n", sql.c_str());
                    ok = ok && run(sql);
                }
                ok = ok && run("COMMIT");
                check(ok, "servidor aceitou a gravacao em transacao");

                check(scalar("SELECT nome FROM otter_test.alter_probe WHERE id = " +
                             first_id) == "O'Brien",
                      "UPDATE gravou o texto com aspas");
                check(scalar("SELECT valor IS NULL FROM otter_test.alter_probe"
                             " WHERE id = " + first_id) == "t",
                      "UPDATE gravou NULL de verdade, nao a palavra");
                check(scalar("SELECT count(*) FROM otter_test.alter_probe"
                             " WHERE id = " + second_id) == "0",
                      "DELETE removeu a linha pela chave");
                check(scalar("SELECT count(*) FROM otter_test.alter_probe"
                             " WHERE nome = 'nova'") == "1",
                      "INSERT da linha nova, com o id do serial");
            }
        }

        // Sem PK no SELECT nao ha' como identificar a linha: recusar e' o
        // correto, gerar UPDATE sem WHERE seria o desastre.
        auto no_key = (*holt)->query("SELECT nome FROM otter_test.alter_probe");
        if (no_key) {
            check(!otter::db::find_edit_target(*no_key, *schemas).editable(),
                  "sem a chave no SELECT, edicao recusada");
        }
    }

    // --- Plano de execucao (ADR 0013) --------------------------------------------

    std::printf("\nEXPLAIN ANALYZE em transacao desfeita\n");
    {
        // ANALYZE executa de verdade: um DELETE explicado apagaria as linhas
        // se o ROLLBACK nao viesse. E' a prova de que explain_statements
        // protege o usuario.
        const std::string before = scalar("SELECT count(*) FROM otter_test.alter_probe");
        const auto statements = otter::db::explain_statements(
            "DELETE FROM otter_test.alter_probe", /*analyze=*/true);
        std::string json;
        for (const std::string& sql : statements) {
            auto rs = (*holt)->query(sql);
            if (!rs) {
                std::printf("  FALHOU: %s\n    %s\n",
                            rs.error().to_string().c_str(), sql.c_str());
                ++failures;
                break;
            }
            if (rs->row_count() > 0 && rs->column_count() > 0) {
                json = std::string(rs->text(0, 0));
            }
        }
        auto plan = otter::db::parse_plan_json(json);
        check(plan.has_value(), "JSON do plano interpretado");
        check(scalar("SELECT count(*) FROM otter_test.alter_probe") == before,
              "ROLLBACK preservou as linhas");

        auto big = (*holt)->query(otter::db::explain_command(
            "SELECT * FROM otter_test.evento_volume WHERE evento_id < 1000", false));
        check(big.has_value() && big->row_count() > 0 &&
                  otter::db::parse_plan_json(big->text(0, 0)).has_value(),
              "EXPLAIN sem ANALYZE sobre 2 milhoes de linhas");
    }

    // --- Transacao manual e savepoint --------------------------------------------

    std::printf("\nauto-commit desligado, savepoint, rollback\n");
    {
        check((*holt)->set_auto_commit(false).has_value(), "auto-commit desligado");
        check(run("INSERT INTO otter_test.alter_probe (nome) VALUES ('t1')"),
              "insert 1");
        check((*holt)->savepoint("sp1").has_value(), "SAVEPOINT");
        check(run("INSERT INTO otter_test.alter_probe (nome) VALUES ('t2')"),
              "insert 2");
        check((*holt)->rollback_to("sp1").has_value(), "ROLLBACK TO SAVEPOINT");
        check((*holt)->commit().has_value(), "COMMIT");
        check(scalar("SELECT count(*) FROM otter_test.alter_probe"
                     " WHERE nome IN ('t1','t2')") == "1",
              "so' o insert antes do savepoint ficou");
        check(run("INSERT INTO otter_test.alter_probe (nome) VALUES ('t3')"),
              "insert 3");
        check((*holt)->rollback().has_value(), "ROLLBACK");
        check(scalar("SELECT count(*) FROM otter_test.alter_probe"
                     " WHERE nome = 't3'") == "0",
              "ROLLBACK desfez");
        (void)(*holt)->set_auto_commit(true);
    }

    // --- Remocao ------------------------------------------------------------------

    std::printf("\nDROP TABLE\n");
    {
        const otter::db::AlterScript drop = otter::db::generate_drop(
            kSchema, "alter_probe", otter::db::ObjKind::table);
        check(drop.has_destructive(), "DROP marcado como destrutivo");
        if (apply(drop)) {
            check(scalar("SELECT to_regclass('otter_test.alter_probe') IS NULL") == "t",
                  "tabela removida");
        }
    }

    std::printf("\n%s\n", failures == 0 ? "tudo passou" : "HOUVE FALHAS");
    return failures == 0 ? 0 : 1;
}
