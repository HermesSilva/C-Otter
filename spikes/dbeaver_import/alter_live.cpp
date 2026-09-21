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

    (void)(*holt)->execute("DROP TABLE otter_test.alter_probe");

    std::printf("\n%s\n", failures == 0 ? "tudo passou" : "HOUVE FALHAS");
    return failures == 0 ? 0 : 1;
}
