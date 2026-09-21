// Aplica um ALTER gerado contra o MySQL REAL e confere o efeito.
//
// Existe porque o teste unitario compara STRINGS: ele prova que a palavra
// AUTO_INCREMENT esta' no comando, nao que a coluna continua auto-incrementando
// depois de aplicado. So' o servidor responde isso (diretiva 2).
//
// Usa o perfil MySQL salvo no armazenamento do C-Otter -- sem senha em linha
// de comando. Cria e descarta a propria tabela: nao toca nas fixtures.
#include "db/alter.hpp"
#include "db/catalog_mysql.hpp"
#include "db/connection_store.hpp"
#include "db/ddl.hpp"
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

} // namespace

int main() {
    auto profiles = otter::db::load_profiles(otter::db::otter_store_location());
    if (!profiles || profiles->empty()) {
        std::printf("sem perfis salvos\n");
        return 2;
    }

    const otter::db::StoredProfile* mysql = nullptr;
    for (const auto& stored : *profiles) {
        if (stored.profile.driver_id == "mysql") { mysql = &stored; break; }
    }
    if (mysql == nullptr) {
        std::printf("sem perfil MySQL salvo\n");
        return 2;
    }

    otter::db::Driver* driver = otter::db::find_driver("mysql");
    auto holt = driver->connect(mysql->profile.to_conn_config());
    if (!holt) {
        std::printf("conexao falhou: %s\n", holt.error().to_string().c_str());
        return 2;
    }
    otter::db::set_sql_dialect_for("mysql");

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
        for (const std::string& statement : script.statements) {
            std::printf("    %s\n", statement.c_str());
            if (!run(statement)) return false;
        }
        return true;
    };

    otter::db::MysqlCatalog catalog(**holt);

    auto reload = [&](otter::db::TableMeta& table) {
        auto columns = catalog.load_columns("otter_test", "alter_probe");
        if (!columns) {
            std::printf("  catalogo falhou\n");
            ++failures;
            return false;
        }
        table = {};
        table.name           = "alter_probe";
        table.columns        = std::move(*columns);
        table.columns_loaded = true;
        table.estimated_rows = 3;
        return true;
    };

    std::printf("Servidor: %s\n\n", (*holt)->server_version().c_str());

    (void)(*holt)->execute("DROP TABLE IF EXISTS otter_test.alter_probe");

    if (!run("CREATE TABLE otter_test.alter_probe ("
             "  id INT AUTO_INCREMENT PRIMARY KEY,"
             "  nome VARCHAR(50) NOT NULL COMMENT 'o nome',"
             "  valor DECIMAL(10,2) DEFAULT 1.00"
             ") ENGINE=InnoDB")) {
        return 1;
    }
    if (!run("INSERT INTO otter_test.alter_probe (nome) VALUES ('a'), ('b')")) {
        return 1;
    }

    otter::db::TableAlteration base;
    base.schema = "otter_test";
    base.table  = "alter_probe";

    // --- O que o teste unitario NAO prova --------------------------------------

    std::printf("mudar int -> bigint na coluna auto_increment\n");
    {
        otter::db::TableMeta current;
        if (!reload(current)) return 1;

        otter::db::TableAlteration wanted = base;
        otter::db::ColumnChange change;
        change.name      = "id";
        change.type_name = "bigint";
        wanted.alter_columns.push_back(change);

        const otter::db::AlterScript script =
            otter::db::generate_alter(current, wanted);
        check(script.ok(), "script gerado");
        if (!apply(script)) return 1;

        otter::db::TableMeta after;
        if (!reload(after)) return 1;

        const otter::db::ColumnMeta* id = column_named(after, "id");
        check(id != nullptr, "coluna id ainda existe");
        check(id != nullptr && id->type_name == "bigint", "tipo virou bigint");
        check(id != nullptr &&
                  id->default_value.find("auto_increment") != std::string::npos,
              "AUTO_INCREMENT SOBREVIVEU");
        check(id != nullptr && !id->nullable, "NOT NULL sobreviveu");

        // A prova final: a coluna ainda GERA id.
        if (run("INSERT INTO otter_test.alter_probe (nome) VALUES ('c')")) {
            auto rs = (*holt)->query("SELECT MAX(id) FROM otter_test.alter_probe");
            check(rs.has_value() && rs->row_count() == 1 && rs->text(0, 0) == "3",
                  "id continua sendo gerado");
        }
    }

    std::printf("\nmudar nulidade sem perder o comentario\n");
    {
        otter::db::TableMeta current;
        if (!reload(current)) return 1;

        otter::db::TableAlteration wanted = base;
        otter::db::ColumnChange change;
        change.name     = "nome";
        change.nullable = true;
        wanted.alter_columns.push_back(change);

        if (!apply(otter::db::generate_alter(current, wanted))) return 1;

        otter::db::TableMeta after;
        if (!reload(after)) return 1;

        const otter::db::ColumnMeta* nome = column_named(after, "nome");
        check(nome != nullptr && nome->nullable, "coluna virou anulavel");
        check(nome != nullptr && nome->comment == "o nome",
              "COMENTARIO SOBREVIVEU");
        check(nome != nullptr && nome->type_name == "varchar(50)",
              "tipo sobreviveu");
    }

    std::printf("\nacrescentar coluna na posicao pedida\n");
    {
        otter::db::TableMeta current;
        if (!reload(current)) return 1;

        otter::db::TableAlteration wanted = base;
        otter::db::NewColumn column;
        column.name          = "ativo";
        column.type_name     = "tinyint(1)";
        column.default_value = "1";
        column.after         = "id";
        wanted.add_columns.push_back(column);

        if (!apply(otter::db::generate_alter(current, wanted))) return 1;

        otter::db::TableMeta after;
        if (!reload(after)) return 1;
        check(after.columns.size() == 4, "4 colunas");
        check(after.columns.size() > 1 && after.columns[1].name == "ativo",
              "coluna foi para a posicao pedida (AFTER id)");
    }

    std::printf("\nrenomear coluna sem perder o DEFAULT\n");
    {
        otter::db::TableMeta current;
        if (!reload(current)) return 1;

        otter::db::TableAlteration wanted = base;
        otter::db::ColumnChange change;
        change.name     = "valor";
        change.new_name = "montante";
        wanted.alter_columns.push_back(change);

        if (!apply(otter::db::generate_alter(current, wanted))) return 1;

        otter::db::TableMeta after;
        if (!reload(after)) return 1;

        const otter::db::ColumnMeta* renamed = column_named(after, "montante");
        check(renamed != nullptr, "coluna renomeada");
        check(renamed != nullptr &&
                  renamed->default_value.find("1.00") != std::string::npos,
              "DEFAULT sobreviveu ao rename");
        check(column_named(after, "valor") == nullptr, "nome antigo sumiu");
    }

    std::printf("\nremover coluna\n");
    {
        otter::db::TableMeta current;
        if (!reload(current)) return 1;

        otter::db::TableAlteration wanted = base;
        wanted.drop_columns.push_back("ativo");

        const otter::db::AlterScript script =
            otter::db::generate_alter(current, wanted);
        check(script.has_destructive(), "marcado como destrutivo");
        if (!apply(script)) return 1;

        otter::db::TableMeta after;
        if (!reload(after)) return 1;
        check(column_named(after, "ativo") == nullptr, "coluna removida");
    }

    // --- Indices, constraints e chaves estrangeiras --------------------------

    std::printf("\ncriar e remover indice\n");
    {
        otter::db::NewIndex index;
        index.name    = "ix_probe_nome";
        index.columns = {"nome"};

        if (!apply(otter::db::generate_create_index("otter_test", "alter_probe",
                                                    index))) {
            return 1;
        }

        auto rs = (*holt)->query(
            "SELECT COUNT(*) FROM information_schema.STATISTICS "
            " WHERE TABLE_SCHEMA='otter_test' AND TABLE_NAME='alter_probe'"
            "   AND INDEX_NAME='ix_probe_nome'");
        check(rs.has_value() && rs->row_count() == 1 && rs->text(0, 0) == "1",
              "indice existe no servidor");

        const otter::db::AlterScript drop = otter::db::generate_drop_index(
            "otter_test", "alter_probe", "ix_probe_nome",
            /*from_constraint=*/false);
        check(drop.has_destructive(), "remocao marcada como destrutiva");
        if (!apply(drop)) return 1;

        auto gone = (*holt)->query(
            "SELECT COUNT(*) FROM information_schema.STATISTICS "
            " WHERE TABLE_SCHEMA='otter_test' AND TABLE_NAME='alter_probe'"
            "   AND INDEX_NAME='ix_probe_nome'");
        check(gone.has_value() && gone->text(0, 0) == "0", "indice removido");
    }

    std::printf("\nconstraint unica que RESTRINGE de verdade\n");
    {
        otter::db::NewConstraint constraint;
        constraint.name    = "uq_probe_nome";
        constraint.kind    = otter::db::ConstraintKind::unique;
        constraint.columns = {"nome"};

        if (!apply(otter::db::generate_add_constraint("otter_test", "alter_probe",
                                                      constraint))) {
            return 1;
        }

        auto rs = (*holt)->query(
            "SELECT CONSTRAINT_TYPE FROM information_schema.TABLE_CONSTRAINTS "
            " WHERE TABLE_SCHEMA='otter_test' AND TABLE_NAME='alter_probe'"
            "   AND CONSTRAINT_NAME='uq_probe_nome'");
        check(rs.has_value() && rs->row_count() == 1 && rs->text(0, 0) == "UNIQUE",
              "constraint unica criada");

        // Criar a constraint nao basta: ela precisa RESTRINGIR. Um duplicado
        // tem de falhar -- e' a diferenca entre a constraint existir e a
        // constraint funcionar.
        auto duplicate = (*holt)->execute(
            "INSERT INTO otter_test.alter_probe (nome) VALUES ('a')");
        check(!duplicate.has_value(), "constraint RESTRINGE duplicado");

        const otter::db::AlterScript drop = otter::db::generate_drop_constraint(
            "otter_test", "alter_probe", "uq_probe_nome",
            otter::db::ObjKind::unique_key);
        if (!apply(drop)) return 1;

        auto after = (*holt)->query(
            "SELECT COUNT(*) FROM information_schema.TABLE_CONSTRAINTS "
            " WHERE TABLE_SCHEMA='otter_test' AND TABLE_NAME='alter_probe'"
            "   AND CONSTRAINT_NAME='uq_probe_nome'");
        check(after.has_value() && after->text(0, 0) == "0",
              "constraint removida");
    }

    std::printf("\nchave estrangeira que RESTRINGE de verdade\n");
    {
        if (!run("CREATE TABLE otter_test.alter_child ("
                 "  id INT AUTO_INCREMENT PRIMARY KEY,"
                 "  probe_id BIGINT NOT NULL,"
                 "  KEY ix_probe (probe_id)"
                 ") ENGINE=InnoDB")) {
            return 1;
        }

        otter::db::NewForeignKey key;
        key.name           = "fk_child_probe";
        key.columns        = {"probe_id"};
        key.target_table   = "alter_probe";
        key.target_columns = {"id"};
        key.on_delete      = "CASCADE";

        if (!apply(otter::db::generate_add_foreign_key("otter_test",
                                                       "alter_child", key))) {
            return 1;
        }

        auto rs = (*holt)->query(
            "SELECT DELETE_RULE FROM information_schema.REFERENTIAL_CONSTRAINTS "
            " WHERE CONSTRAINT_SCHEMA='otter_test'"
            "   AND CONSTRAINT_NAME='fk_child_probe'");
        check(rs.has_value() && rs->row_count() == 1 && rs->text(0, 0) == "CASCADE",
              "FK criada com ON DELETE CASCADE");

        auto orphan = (*holt)->execute(
            "INSERT INTO otter_test.alter_child (probe_id) VALUES (9999)");
        check(!orphan.has_value(), "FK RESTRINGE linha orfa");

        if (!apply(otter::db::generate_drop_foreign_key(
                "otter_test", "alter_child", "fk_child_probe"))) {
            return 1;
        }

        auto gone = (*holt)->query(
            "SELECT COUNT(*) FROM information_schema.REFERENTIAL_CONSTRAINTS "
            " WHERE CONSTRAINT_SCHEMA='otter_test'"
            "   AND CONSTRAINT_NAME='fk_child_probe'");
        check(gone.has_value() && gone->text(0, 0) == "0", "FK removida");

        (void)(*holt)->execute("DROP TABLE otter_test.alter_child");
    }

    std::printf("\nindice de constraint e' recusado ANTES do servidor\n");
    {
        // O MySQL ACEITARIA o DROP INDEX de uma chave primaria e removeria a
        // chave junto, em silencio. A recusa e' NOSSA -- e por isso precisa
        // ser testada contra o servidor, provando que a chave sobrevive.
        const otter::db::AlterScript script = otter::db::generate_drop_index(
            "otter_test", "alter_probe", "PRIMARY", /*from_constraint=*/true);

        check(!script.ok(), "recusado antes de chegar ao servidor");
        check(script.statements.empty(), "nada a executar");

        auto rs = (*holt)->query(
            "SELECT COUNT(*) FROM information_schema.TABLE_CONSTRAINTS "
            " WHERE TABLE_SCHEMA='otter_test' AND TABLE_NAME='alter_probe'"
            "   AND CONSTRAINT_TYPE='PRIMARY KEY'");
        check(rs.has_value() && rs->text(0, 0) == "1", "chave primaria intacta");
    }

    std::printf("\nview com OR REPLACE\n");
    {
        const otter::db::AlterScript script = otter::db::generate_create_view(
            "otter_test", "v_probe", "SELECT id, nome FROM otter_test.alter_probe");
        if (!apply(script)) return 1;

        auto rs = (*holt)->query(
            "SELECT COUNT(*) FROM information_schema.VIEWS "
            " WHERE TABLE_SCHEMA='otter_test' AND TABLE_NAME='v_probe'");
        check(rs.has_value() && rs->text(0, 0) == "1", "view criada");

        // OR REPLACE precisa funcionar sobre uma view que JA' existe -- e' o
        // ponto todo de usa-lo em vez de DROP + CREATE.
        const otter::db::AlterScript again = otter::db::generate_create_view(
            "otter_test", "v_probe", "SELECT id FROM otter_test.alter_probe");
        check(apply(again), "OR REPLACE sobre view existente");

        auto columns = (*holt)->query(
            "SELECT COUNT(*) FROM information_schema.COLUMNS "
            " WHERE TABLE_SCHEMA='otter_test' AND TABLE_NAME='v_probe'");
        check(columns.has_value() && columns->text(0, 0) == "1",
              "a definicao foi SUBSTITUIDA (1 coluna, nao 2)");

        (void)(*holt)->execute("DROP VIEW otter_test.v_probe");
    }

    std::printf("\ntrigger com USE antes\n");
    {
        // O CREATE TRIGGER do MySQL NAO aceita nome qualificado. Sem o USE, a
        // trigger nasceria no banco errado -- ou o comando falharia, conforme
        // o banco corrente da conexao.
        (void)(*holt)->execute("DROP TRIGGER IF EXISTS otter_test.trg_probe");

        otter::db::NewTrigger trigger;
        trigger.name   = "trg_probe";
        trigger.table  = "alter_probe";
        trigger.timing = "BEFORE";
        trigger.event  = "INSERT";
        trigger.body   = "SET NEW.nome = UPPER(NEW.nome)";

        const otter::db::AlterScript script =
            otter::db::generate_create_trigger("otter_test", trigger);

        check(script.statements.size() == 2, "dois comandos (USE + CREATE)");
        if (!apply(script)) return 1;

        auto rs = (*holt)->query(
            "SELECT ACTION_TIMING, EVENT_MANIPULATION "
            "  FROM information_schema.TRIGGERS "
            " WHERE TRIGGER_SCHEMA='otter_test' AND TRIGGER_NAME='trg_probe'");
        check(rs.has_value() && rs->row_count() == 1,
              "trigger criada NO BANCO CERTO");
        check(rs.has_value() && rs->row_count() == 1 &&
                  rs->text(0, 0) == "BEFORE",
              "momento correto");

        // E ela DISPARA: inserir em minusculas precisa sair em maiusculas.
        if (run("INSERT INTO otter_test.alter_probe (nome) VALUES ('zz')")) {
            auto value = (*holt)->query(
                "SELECT nome FROM otter_test.alter_probe "
                " WHERE nome IN ('zz','ZZ')");
            check(value.has_value() && value->row_count() == 1 &&
                      value->text(0, 0) == "ZZ",
                  "a trigger DISPARA de verdade");
        }

        (void)(*holt)->execute("DROP TRIGGER otter_test.trg_probe");
    }

    (void)(*holt)->execute("DROP TABLE otter_test.alter_probe");

    std::printf("\n%s\n", failures == 0 ? "tudo passou" : "HOUVE FALHAS");
    return failures == 0 ? 0 : 1;
}
