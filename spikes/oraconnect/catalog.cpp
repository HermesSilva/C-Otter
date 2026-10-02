// Spike: o driver e o catalogo do Oracle contra o servidor de verdade.
//
// Cria um schema de rascunho (OTTER_SCRATCH), com um objeto de cada tipo que
// a arvore mostra, le tudo pelo CatalogReader -- o mesmo caminho da interface
// -- e apaga o schema no fim. Tambem passa pelas transacoes e pelo PL/SQL.
//
//   $env:ORA_PASSWORD = "..."       # usuario com CREATE USER (SYSTEM, no teste)
//   build\win-release\bin\spike_oracatalog.exe
//
// SO' contra o banco de teste local.
#include "db/catalog_reader.hpp"
#include "db/registry.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

using namespace otter;

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FALHOU: %s\n", what.c_str());
    }
}

std::string env_or(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return value && *value ? value : fallback;
}

bool run(db::Holt& holt, const std::string& sql) {
    const Status status = holt.execute(sql);
    if (!status) std::printf("  FALHOU: %s\n    %s\n", sql.c_str(), status.error().to_string().c_str());
    ++g_checks;
    if (!status) ++g_failures;
    return status.has_value();
}

} // namespace

int main() {
    db::ConnConfig config;
    config.driver_id = "oracle";
    config.host      = env_or("ORA_HOST", "localhost");
    config.port      = static_cast<std::uint16_t>(std::atoi(env_or("ORA_PORT", "1521").c_str()));
    config.database  = env_or("ORA_SERVICE", "FREEPDB1");
    config.user      = env_or("ORA_USER", "system");
    config.password  = env_or("ORA_PASSWORD", "");

    if (config.host != "localhost" && config.host != "127.0.0.1") {
        std::printf("spike_oracatalog so' roda contra o banco de teste local.\n");
        return 2;
    }

    db::Driver* driver = db::find_driver("oracle");
    auto connected = driver->connect(config);
    if (!connected) {
        std::printf("FALHOU: %s\n", connected.error().to_string().c_str());
        return 1;
    }
    db::Holt& holt = **connected;
    std::printf("conectado: %s, schema %s, container %s\n", holt.server_version().c_str(),
                holt.current_schema().c_str(), holt.current_database().c_str());
    check(holt.current_schema() == "SYSTEM", "schema corrente e' o do usuario");
    check(!holt.current_database().empty(), "o servidor diz o container");

    // --- senha errada ------------------------------------------------------------
    {
        db::ConnConfig wrong = config;
        wrong.password = "nao-e-a-senha";
        auto refused = driver->connect(wrong);
        check(!refused.has_value(), "senha errada e' recusada");
        if (!refused) {
            check(refused.error().code() == Errc::auth_failed, "com o codigo de autenticacao");
            std::printf("senha errada -> %s\n", refused.error().to_string().c_str());
        }
        wrong = config;
        wrong.database = "NAO_EXISTE";
        refused = driver->connect(wrong);
        check(!refused.has_value(), "servico inexistente e' recusado");
        if (!refused) std::printf("servico errado -> %s\n", refused.error().to_string().c_str());
    }

    // --- o schema de rascunho ------------------------------------------------------
    (void)holt.execute("DROP USER otter_scratch CASCADE");
    // Sem nomear tablespace: a imagem "lite" do Oracle Free nao tem o USERS.
    run(holt, "CREATE USER otter_scratch IDENTIFIED BY \"Scratch_1\"");
    run(holt, "GRANT UNLIMITED TABLESPACE TO otter_scratch");
    run(holt, "CREATE TABLE otter_scratch.cliente ("
              "  id NUMBER(10) PRIMARY KEY,"
              "  nome VARCHAR2(60 CHAR) NOT NULL,"
              "  email VARCHAR2(120) CONSTRAINT uq_cliente_email UNIQUE,"
              "  saldo NUMBER(12,2) DEFAULT 0 CONSTRAINT ck_cliente_saldo CHECK (saldo >= 0),"
              "  criado DATE DEFAULT SYSDATE,"
              "  foto BLOB, obs CLOB)");
    run(holt, "COMMENT ON TABLE otter_scratch.cliente IS 'Clientes de teste'");
    run(holt, "COMMENT ON COLUMN otter_scratch.cliente.nome IS 'Nome completo'");
    run(holt, "CREATE TABLE otter_scratch.pedido ("
              "  id NUMBER(10) PRIMARY KEY,"
              "  cliente_id NUMBER(10) NOT NULL,"
              "  total NUMBER(12,2),"
              "  CONSTRAINT fk_pedido_cliente FOREIGN KEY (cliente_id)"
              "    REFERENCES otter_scratch.cliente (id) ON DELETE CASCADE)");
    run(holt, "CREATE INDEX otter_scratch.ix_pedido_cliente ON otter_scratch.pedido (cliente_id)");
    run(holt, "CREATE SEQUENCE otter_scratch.seq_pedido START WITH 10 INCREMENT BY 5");
    run(holt, "CREATE VIEW otter_scratch.v_cliente AS "
              "SELECT id, nome FROM otter_scratch.cliente WHERE saldo > 0");
    run(holt, "CREATE OR REPLACE FUNCTION otter_scratch.fn_dobro(p_valor IN NUMBER) "
              "RETURN NUMBER IS\nBEGIN\n  RETURN p_valor * 2;\nEND;");
    run(holt, "CREATE OR REPLACE PROCEDURE otter_scratch.pr_nada(p_texto IN VARCHAR2) IS\n"
              "BEGIN\n  NULL;\nEND;");
    run(holt, "CREATE OR REPLACE TRIGGER otter_scratch.tg_pedido BEFORE INSERT ON "
              "otter_scratch.pedido FOR EACH ROW\nBEGIN\n  :NEW.total := NVL(:NEW.total, 0);\nEND;");

    // --- transacoes ------------------------------------------------------------------
    check(holt.txn_state() == db::TxnState::idle, "sem transacao depois do DDL");
    run(holt, "INSERT INTO otter_scratch.cliente (id, nome, email) VALUES (1, 'Ana', 'a@x')");
    check(holt.txn_state() == db::TxnState::idle, "auto-commit: nada pendente");

    check(holt.set_auto_commit(false).has_value(), "desliga o auto-commit");
    run(holt, "INSERT INTO otter_scratch.cliente (id, nome, email) VALUES (2, 'Bia', 'b@x')");
    check(holt.txn_state() == db::TxnState::active, "modo manual: transacao aberta");
    check(holt.uncommitted_changes() == 1, "uma alteracao pendente");
    check(holt.savepoint("antes").has_value(), "savepoint");
    run(holt, "INSERT INTO otter_scratch.cliente (id, nome, email) VALUES (3, 'Caio', 'c@x')");
    check(holt.rollback_to("antes").has_value(), "rollback ao savepoint");
    check(holt.commit().has_value(), "commit");
    check(holt.txn_state() == db::TxnState::idle, "nada pendente depois do commit");
    run(holt, "DELETE FROM otter_scratch.cliente WHERE id = 2");
    check(holt.rollback().has_value(), "rollback");
    check(holt.set_auto_commit(true).has_value(), "religa o auto-commit");

    if (auto rs = holt.query("SELECT id, nome FROM otter_scratch.cliente ORDER BY id")) {
        check(rs->row_count() == 2, "duas linhas: 3 voltou ao savepoint, 2 nao foi apagado");
        check(rs->row_count() == 2 && rs->text(1, 1) == "Bia", "a segunda e' Bia");
    } else {
        check(false, "consulta das linhas: " + rs.error().to_string());
    }

    // PL/SQL, com e sem o ';' final (o divisor de scripts o tira).
    run(holt, "BEGIN otter_scratch.pr_nada('x'); END;");
    run(holt, "BEGIN otter_scratch.pr_nada('y'); END");
    if (auto rs = holt.query("SELECT otter_scratch.fn_dobro(21) FROM dual;")) {
        check(rs->row_count() == 1 && rs->text(0, 0) == "42", "funcao devolve 42");
    } else {
        check(false, "chamada de funcao: " + rs.error().to_string());
    }
    if (auto bad = holt.query("SELECT * FROM otter_scratch.nao_existe")) {
        check(false, "tabela inexistente deveria falhar");
    } else {
        check(bad.error().message().find("ORA-00942") != std::string::npos,
              "erro do servidor chega com o codigo ORA");
    }

    // --- catalogo --------------------------------------------------------------------
    auto reader = db::make_catalog_reader("oracle", holt);
    check(reader != nullptr, "ha' leitor de catalogo para o Oracle");
    if (reader) {
        check(reader->default_schema() == "SYSTEM", "a arvore comeca no schema da conexao");
        check(!reader->has_database_level(), "sem nivel de banco");

        if (auto schemas = reader->load_schemas()) {
            bool found = false, sys = false;
            for (const db::SchemaMeta& schema : *schemas) {
                found = found || schema.name == "OTTER_SCRATCH";
                sys = sys || schema.name == "SYS";
            }
            std::printf("schemas: %zu\n", schemas->size());
            check(found, "o schema de rascunho aparece");
            check(!sys, "os schemas mantidos pela Oracle ficam de fora");
        } else {
            check(false, "load_schemas: " + schemas.error().to_string());
        }

        if (auto tables = reader->load_tables("OTTER_SCRATCH")) {
            check(tables->size() == 3, "duas tabelas e uma view");
            for (const db::TableMeta& table : *tables) {
                std::printf("  %s (%s) %s\n", table.name.c_str(),
                            std::string(db::to_string(table.kind)).c_str(), table.comment.c_str());
                if (table.name == "CLIENTE") check(table.comment == "Clientes de teste", "comentario da tabela");
                if (table.name == "V_CLIENTE") check(table.kind == db::ObjKind::view, "a view e' view");
            }
        } else {
            check(false, "load_tables: " + tables.error().to_string());
        }

        if (auto columns = reader->load_columns("OTTER_SCRATCH", "CLIENTE")) {
            check(columns->size() == 7, "sete colunas");
            for (const db::ColumnMeta& column : *columns) {
                std::printf("  %-8s %-18s %s%s default[%s] %s\n", column.name.c_str(),
                            column.type_name.c_str(), column.nullable ? "null" : "NOT NULL",
                            column.primary_key ? " PK" : "", column.default_value.c_str(),
                            column.comment.c_str());
            }
            if (columns->size() == 7) {
                check((*columns)[0].primary_key && (*columns)[0].type_name == "NUMBER(10)", "id e' PK NUMBER(10)");
                check((*columns)[1].type_name == "VARCHAR2(60 CHAR)" && !(*columns)[1].nullable, "nome VARCHAR2(60 CHAR) NOT NULL");
                check((*columns)[1].comment == "Nome completo", "comentario da coluna");
                check((*columns)[3].default_value == "0", "default do saldo");
                check((*columns)[4].default_value == "SYSDATE", "default da data (coluna LONG)");
            }
        } else {
            check(false, "load_columns: " + columns.error().to_string());
        }

        if (auto constraints = reader->load_constraints("OTTER_SCRATCH", "CLIENTE")) {
            for (const db::ConstraintMeta& c : *constraints) {
                std::printf("  constraint %s: %s\n", c.name.c_str(), c.definition.c_str());
            }
            // PK, UNIQUE e o CHECK do saldo; os NOT NULL nao contam.
            check(constraints->size() == 3, "tres constraints (os NOT NULL ficam de fora)");
        } else {
            check(false, "load_constraints: " + constraints.error().to_string());
        }

        if (auto indexes = reader->load_indexes("OTTER_SCRATCH", "PEDIDO")) {
            for (const db::IndexMeta& i : *indexes) {
                std::printf("  indice %s (%s)%s%s\n", i.name.c_str(), i.columns.c_str(),
                            i.unique ? " unico" : "", i.primary ? " PK" : "");
            }
            check(indexes->size() == 2, "indice da PK e o criado");
        } else {
            check(false, "load_indexes: " + indexes.error().to_string());
        }

        if (auto keys = reader->load_table_foreign_keys("OTTER_SCRATCH", "PEDIDO")) {
            check(keys->size() == 1, "uma chave estrangeira");
            if (!keys->empty()) {
                std::printf("  fk %s: %s\n", (*keys)[0].name.c_str(), (*keys)[0].definition.c_str());
                check((*keys)[0].target_table == "CLIENTE" && (*keys)[0].on_delete == "CASCADE", "aponta para CLIENTE, ON DELETE CASCADE");
            }
        } else {
            check(false, "load_table_foreign_keys: " + keys.error().to_string());
        }
        if (auto refs = reader->load_references("OTTER_SCRATCH", "CLIENTE")) {
            check(refs->size() == 1, "CLIENTE e' referenciada por uma chave");
        } else {
            check(false, "load_references: " + refs.error().to_string());
        }
        if (auto all = reader->load_foreign_keys("OTTER_SCRATCH")) {
            check(all->size() == 1, "uma chave no schema");
        } else {
            check(false, "load_foreign_keys: " + all.error().to_string());
        }

        if (auto triggers = reader->load_triggers("OTTER_SCRATCH", "PEDIDO")) {
            check(triggers->size() == 1 && (*triggers)[0].timing == "BEFORE" && (*triggers)[0].events == "INSERT", "trigger BEFORE INSERT");
        } else {
            check(false, "load_triggers: " + triggers.error().to_string());
        }

        if (auto sequences = reader->load_sequences("OTTER_SCRATCH")) {
            check(sequences->size() == 1 && (*sequences)[0].increment == 5, "sequence de incremento 5");
        } else {
            check(false, "load_sequences: " + sequences.error().to_string());
        }

        if (auto routines = reader->load_routines("OTTER_SCRATCH")) {
            check(routines->size() == 2, "uma funcao e uma procedure");
            for (const db::RoutineMeta& r : *routines) {
                std::printf("  rotina %s(%s) -> %s\n", r.name.c_str(), r.arguments.c_str(), r.return_type.c_str());
            }
        } else {
            check(false, "load_routines: " + routines.error().to_string());
        }
        if (auto source = reader->load_routine_definition("OTTER_SCRATCH", "FN_DOBRO", "")) {
            check(source->find("RETURN p_valor * 2") != std::string::npos, "fonte da funcao");
        } else {
            check(false, "load_routine_definition: " + source.error().to_string());
        }
        if (auto view = reader->load_view_definition("OTTER_SCRATCH", "V_CLIENTE")) {
            check(view->find("saldo > 0") != std::string::npos, "texto da view (coluna LONG)");
        } else {
            check(false, "load_view_definition: " + view.error().to_string());
        }

        db::ObjectRef ref;
        ref.type = db::ObjectType::table;
        ref.schema = "OTTER_SCRATCH";
        ref.name = "CLIENTE";
        const db::ObjectInfo info = reader->load_object_info(ref);
        check(info.error.empty(), "editor de objeto sem erro: " + info.error);
        check(info.properties.size() >= 6, "propriedades da tabela");
        check(info.ddl.find("CREATE TABLE") != std::string::npos, "DDL vem do servidor (CLOB)");
        std::printf("DDL: %zu bytes, %zu propriedades, %zu permissoes\n", info.ddl.size(),
                    info.properties.size(), info.permissions.size());
    }

    run(holt, "DROP USER otter_scratch CASCADE");

    std::printf("\n%d verificacoes, %d falharam\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
