#include "db/mysql_object.hpp"

#include "db/catalog_mysql.hpp"   // mysql_quote, mysql_literal

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace otter::db {
namespace {

AlterScript single(std::string statement) {
    AlterScript script;
    script.statements.push_back(std::move(statement));
    return script;
}

AlterScript refused(std::string reason) {
    AlterScript script;
    script.error = std::move(reason);
    return script;
}

std::string full_name(const ObjectRef& ref) {
    return ref.schema.empty() ? mysql_quote(ref.name)
                              : mysql_quote(ref.schema) + "." + mysql_quote(ref.name);
}

std::string table_of(const ObjectRef& ref) {
    return ref.schema.empty() ? mysql_quote(ref.parent)
                              : mysql_quote(ref.schema) + "." + mysql_quote(ref.parent);
}

std::string upper(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](char c) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    });
    return out;
}

bool only_digits(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) != 0;
    });
}

const char* routine_keyword(ObjectType type) {
    return type == ObjectType::procedure ? "PROCEDURE" : "FUNCTION";
}

} // namespace

// --- Nomes --------------------------------------------------------------------------

std::string mysql_account(std::string_view grantee) {
    // Ja' citada: 'ana'@'localhost', como o information_schema devolve.
    if (!grantee.empty() && grantee.front() == '\'') return std::string(grantee);

    const std::size_t at = grantee.rfind('@');
    const std::string_view user = grantee.substr(0, at);
    const std::string_view host =
        at == std::string_view::npos ? std::string_view("%") : grantee.substr(at + 1);
    return mysql_literal(user) + "@" + mysql_literal(host.empty() ? "%" : host);
}

std::string mysql_object_name(const ObjectRef& ref) {
    switch (ref.type) {
        case ObjectType::database:
        case ObjectType::schema:
            return mysql_quote(ref.name);
        case ObjectType::role:
            return mysql_literal(ref.name) + "@" +
                   mysql_literal(ref.parent.empty() ? "%" : ref.parent);
        case ObjectType::column:
        case ObjectType::index:
        case ObjectType::constraint:
        case ObjectType::foreign_key:
            return table_of(ref) + "." + mysql_quote(ref.name);
        default:
            return full_name(ref);
    }
}

// --- Consultas ----------------------------------------------------------------------

std::string mysql_properties_query(const ObjectRef& ref) {
    const std::string schema = mysql_literal(ref.schema);
    const std::string name   = mysql_literal(ref.name);

    switch (ref.type) {
        case ObjectType::database:
        case ObjectType::schema:
            return "SELECT SCHEMA_NAME AS `Name`,"
                   "       DEFAULT_CHARACTER_SET_NAME AS `Charset`,"
                   "       DEFAULT_COLLATION_NAME AS `Collation`,"
                   "       (SELECT COUNT(*) FROM information_schema.TABLES t"
                   "         WHERE t.TABLE_SCHEMA = s.SCHEMA_NAME) AS `Tables`,"
                   "       (SELECT COALESCE(SUM(t.DATA_LENGTH + t.INDEX_LENGTH), 0)"
                   "          FROM information_schema.TABLES t"
                   "         WHERE t.TABLE_SCHEMA = s.SCHEMA_NAME) AS `Size`"
                   "  FROM information_schema.SCHEMATA s"
                   " WHERE SCHEMA_NAME = " + name;

        case ObjectType::table:
            return "SELECT TABLE_NAME AS `Name`, TABLE_SCHEMA AS `Schema`,"
                   "       ENGINE AS `Engine`, ROW_FORMAT AS `Row format`,"
                   "       TABLE_ROWS AS `Row count estimate`,"
                   "       DATA_LENGTH AS `Data size`, INDEX_LENGTH AS `Index size`,"
                   "       AUTO_INCREMENT AS `Auto increment`,"
                   "       TABLE_COLLATION AS `Collation`,"
                   "       CREATE_TIME AS `Created`, UPDATE_TIME AS `Updated`,"
                   "       TABLE_COMMENT AS `Comment`"
                   "  FROM information_schema.TABLES"
                   " WHERE TABLE_SCHEMA = " + schema + " AND TABLE_NAME = " + name;

        case ObjectType::view:
            return "SELECT TABLE_NAME AS `Name`, TABLE_SCHEMA AS `Schema`,"
                   "       DEFINER AS `Definer`, SECURITY_TYPE AS `Security`,"
                   "       CHECK_OPTION AS `Check option`,"
                   "       IS_UPDATABLE AS `Updatable`"
                   "  FROM information_schema.VIEWS"
                   " WHERE TABLE_SCHEMA = " + schema + " AND TABLE_NAME = " + name;

        case ObjectType::function:
        case ObjectType::procedure:
            return "SELECT ROUTINE_NAME AS `Name`, ROUTINE_SCHEMA AS `Schema`,"
                   "       ROUTINE_TYPE AS `Kind`, DTD_IDENTIFIER AS `Returns`,"
                   "       DEFINER AS `Definer`, SECURITY_TYPE AS `Security`,"
                   "       IS_DETERMINISTIC AS `Deterministic`,"
                   "       SQL_DATA_ACCESS AS `Data access`,"
                   "       CREATED AS `Created`, LAST_ALTERED AS `Updated`,"
                   "       ROUTINE_COMMENT AS `Comment`"
                   "  FROM information_schema.ROUTINES"
                   " WHERE ROUTINE_SCHEMA = " + schema + " AND ROUTINE_NAME = " + name +
                   "   AND ROUTINE_TYPE = '" + routine_keyword(ref.type) + "'";

        case ObjectType::trigger:
            return "SELECT TRIGGER_NAME AS `Name`, EVENT_OBJECT_TABLE AS `Table`,"
                   "       ACTION_TIMING AS `Timing`, EVENT_MANIPULATION AS `Event`,"
                   "       DEFINER AS `Definer`, CREATED AS `Created`"
                   "  FROM information_schema.TRIGGERS"
                   " WHERE TRIGGER_SCHEMA = " + schema + " AND TRIGGER_NAME = " + name;

        case ObjectType::event:
            return "SELECT EVENT_NAME AS `Name`, EVENT_SCHEMA AS `Schema`,"
                   "       STATUS AS `Status`, EVENT_TYPE AS `Type`,"
                   "       EXECUTE_AT AS `Execute at`,"
                   "       CONCAT_WS(' ', INTERVAL_VALUE, INTERVAL_FIELD) AS `Every`,"
                   "       STARTS AS `Starts`, ENDS AS `Ends`,"
                   "       ON_COMPLETION AS `On completion`, DEFINER AS `Definer`,"
                   "       LAST_EXECUTED AS `Last executed`,"
                   "       CREATED AS `Created`, LAST_ALTERED AS `Updated`,"
                   "       EVENT_COMMENT AS `Comment`"
                   "  FROM information_schema.EVENTS"
                   " WHERE EVENT_SCHEMA = " + schema + " AND EVENT_NAME = " + name;

        case ObjectType::role:
            // mysql.user: exige privilegio de leitura no banco `mysql` -- o
            // mesmo que a pasta Users ja' pede. Nunca a coluna da senha.
            return "SELECT User AS `Name`, Host AS `Host`, plugin AS `Plugin`,"
                   "       account_locked AS `Locked`,"
                   "       password_expired AS `Password expired`,"
                   "       max_questions AS `Max queries`,"
                   "       max_updates AS `Max updates`,"
                   "       max_connections AS `Max connections`,"
                   "       max_user_connections AS `Max user connections`"
                   "  FROM mysql.user"
                   " WHERE User = " + name + " AND Host = " +
                   mysql_literal(ref.parent.empty() ? "%" : ref.parent);

        default:
            return {};
    }
}

MysqlShowCreate mysql_show_create(const ObjectRef& ref) {
    switch (ref.type) {
        case ObjectType::database:
        case ObjectType::schema:
            return {"SHOW CREATE DATABASE " + mysql_quote(ref.name), 1};
        case ObjectType::table:
            return {"SHOW CREATE TABLE " + full_name(ref), 1};
        case ObjectType::view:
            return {"SHOW CREATE VIEW " + full_name(ref), 1};
        case ObjectType::function:
        case ObjectType::procedure:
            return {std::string("SHOW CREATE ") + routine_keyword(ref.type) + " " +
                        full_name(ref), 2};
        case ObjectType::trigger:
            return {"SHOW CREATE TRIGGER " + full_name(ref), 2};
        case ObjectType::event:
            return {"SHOW CREATE EVENT " + full_name(ref), 3};
        case ObjectType::role:
            return {"SHOW CREATE USER " + mysql_object_name(ref), 0};
        default:
            return {};
    }
}

std::string mysql_permissions_query(const ObjectRef& ref) {
    switch (ref.type) {
        case ObjectType::table:
        case ObjectType::view:
            return "SELECT GRANTEE, PRIVILEGE_TYPE, IS_GRANTABLE = 'YES', ''"
                   "  FROM information_schema.TABLE_PRIVILEGES"
                   " WHERE TABLE_SCHEMA = " + mysql_literal(ref.schema) +
                   "   AND TABLE_NAME = " + mysql_literal(ref.name) +
                   " ORDER BY GRANTEE, PRIVILEGE_TYPE";
        case ObjectType::database:
        case ObjectType::schema:
            return "SELECT GRANTEE, PRIVILEGE_TYPE, IS_GRANTABLE = 'YES', ''"
                   "  FROM information_schema.SCHEMA_PRIVILEGES"
                   " WHERE TABLE_SCHEMA = " + mysql_literal(ref.name) +
                   " ORDER BY GRANTEE, PRIVILEGE_TYPE";
        default:
            return {};
    }
}

ObjectEdit mysql_editable_property(ObjectType type, std::string_view label) noexcept {
    if (label == "Name") {
        // RENAME TABLE (tabela e view), ALTER EVENT ... RENAME TO, RENAME
        // USER. Banco, rotina e trigger nao tem RENAME no MySQL.
        return type == ObjectType::table || type == ObjectType::view ||
                       type == ObjectType::event || type == ObjectType::role
                   ? ObjectEdit::name
                   : ObjectEdit::none;
    }
    if (label == "Comment") {
        return type == ObjectType::table || type == ObjectType::function ||
                       type == ObjectType::procedure || type == ObjectType::event
                   ? ObjectEdit::comment
                   : ObjectEdit::none;
    }
    return ObjectEdit::none;
}

std::vector<std::string_view> mysql_privileges_for(ObjectType type) {
    switch (type) {
        case ObjectType::table:
        case ObjectType::view:
            return {"SELECT", "INSERT", "UPDATE", "DELETE", "CREATE", "DROP",
                    "REFERENCES", "INDEX", "ALTER", "CREATE VIEW", "SHOW VIEW", "TRIGGER"};
        case ObjectType::database:
        case ObjectType::schema:
            return {"SELECT", "INSERT", "UPDATE", "DELETE", "CREATE", "DROP",
                    "REFERENCES", "INDEX", "ALTER", "CREATE TEMPORARY TABLES",
                    "LOCK TABLES", "EXECUTE", "CREATE VIEW", "SHOW VIEW",
                    "CREATE ROUTINE", "ALTER ROUTINE", "EVENT", "TRIGGER"};
        case ObjectType::function:
        case ObjectType::procedure:
            return {"EXECUTE", "ALTER ROUTINE"};
        default:
            return {};
    }
}

// --- Alteracoes ---------------------------------------------------------------------

AlterScript mysql_object_rename(const ObjectRef& ref, std::string_view new_name) {
    if (new_name.empty()) return refused("the new name is required");
    if (new_name == ref.name) return refused("the name is unchanged");

    switch (ref.type) {
        case ObjectType::table:
        case ObjectType::view:
            return single("RENAME TABLE " + full_name(ref) + " TO " +
                          mysql_quote(ref.schema) + "." + mysql_quote(new_name));
        case ObjectType::event:
            return single("ALTER EVENT " + full_name(ref) + " RENAME TO " +
                          mysql_quote(ref.schema) + "." + mysql_quote(new_name));
        case ObjectType::index:
            return single("ALTER TABLE " + table_of(ref) + " RENAME INDEX " +
                          mysql_quote(ref.name) + " TO " + mysql_quote(new_name));
        case ObjectType::role:
            return single("RENAME USER " + mysql_object_name(ref) + " TO " +
                          mysql_literal(new_name) + "@" +
                          mysql_literal(ref.parent.empty() ? "%" : ref.parent));
        case ObjectType::column:
            // CHANGE COLUMN exige a definicao inteira: e' o formulario de
            // coluna que a tem (db/alter.hpp).
            return refused("rename the column in the column form: MySQL needs the "
                           "full definition");
        case ObjectType::database:
        case ObjectType::schema:
            return refused("MySQL cannot rename a database");
        default:
            return refused("MySQL has no RENAME for this object type: drop and "
                           "create it again");
    }
}

AlterScript mysql_object_comment(const ObjectRef& ref, std::string_view comment) {
    switch (ref.type) {
        case ObjectType::table:
            return single("ALTER TABLE " + full_name(ref) + " COMMENT = " +
                          mysql_literal(comment));
        case ObjectType::function:
        case ObjectType::procedure:
            return single(std::string("ALTER ") + routine_keyword(ref.type) + " " +
                          full_name(ref) + " COMMENT " + mysql_literal(comment));
        case ObjectType::event:
            return single("ALTER EVENT " + full_name(ref) + " COMMENT " +
                          mysql_literal(comment));
        default:
            return refused("MySQL has no comment for this object type");
    }
}

AlterScript mysql_object_drop(const ObjectRef& ref) {
    AlterScript script;
    switch (ref.type) {
        case ObjectType::database:
        case ObjectType::schema:
            script = single("DROP DATABASE " + mysql_quote(ref.name));
            script.warnings.emplace_back(
                "drops every table, view, routine and event of the database");
            break;
        case ObjectType::table:
            script = single("DROP TABLE " + full_name(ref));
            break;
        case ObjectType::view:
            script = single("DROP VIEW " + full_name(ref));
            break;
        case ObjectType::function:
        case ObjectType::procedure:
            script = single(std::string("DROP ") + routine_keyword(ref.type) + " " +
                            full_name(ref));
            break;
        case ObjectType::trigger:
            script = single("DROP TRIGGER " + full_name(ref));
            break;
        case ObjectType::event:
            script = single("DROP EVENT " + full_name(ref));
            break;
        case ObjectType::sequence:   // MariaDB 10.3+
            script = single("DROP SEQUENCE " + full_name(ref));
            break;
        case ObjectType::role:
            script = single("DROP USER " + mysql_object_name(ref));
            break;
        case ObjectType::column:
            script = single("ALTER TABLE " + table_of(ref) + " DROP COLUMN " +
                            mysql_quote(ref.name));
            break;
        case ObjectType::index:
            script = single("ALTER TABLE " + table_of(ref) + " DROP INDEX " +
                            mysql_quote(ref.name));
            break;
        case ObjectType::foreign_key:
            script = single("ALTER TABLE " + table_of(ref) + " DROP FOREIGN KEY " +
                            mysql_quote(ref.name));
            break;
        case ObjectType::constraint:
            // A chave primaria nao tem nome proprio: chama-se sempre PRIMARY.
            script = single(ref.name == "PRIMARY"
                                ? "ALTER TABLE " + table_of(ref) + " DROP PRIMARY KEY"
                                : "ALTER TABLE " + table_of(ref) + " DROP CONSTRAINT " +
                                      mysql_quote(ref.name));
            break;
        default:
            return refused("this object type cannot be dropped on MySQL");
    }
    script.destructive.push_back(0);
    script.warnings.emplace_back("DROP cannot be undone: MySQL commits DDL implicitly");
    return script;
}

namespace {

// "ON `banco`.`tabela`", "ON `banco`.*", "ON PROCEDURE `banco`.`p`".
std::string grant_target(const ObjectRef& ref, std::string& error) {
    switch (ref.type) {
        case ObjectType::table:
        case ObjectType::view:
            return full_name(ref);
        case ObjectType::database:
        case ObjectType::schema:
            return mysql_quote(ref.name) + ".*";
        case ObjectType::function:
        case ObjectType::procedure:
            return std::string(routine_keyword(ref.type)) + " " + full_name(ref);
        default:
            error = "this object type has no privileges on MySQL";
            return {};
    }
}

std::string privilege_keyword(std::string_view privilege) {
    // "ALL" do botao "Grant All" e' ALL PRIVILEGES.
    const std::string name = upper(privilege);
    return name == "ALL" ? "ALL PRIVILEGES" : name;
}

bool known_privilege(std::string_view privilege) {
    // So' letras e espacos: o nome vai no comando sem aspas.
    return !privilege.empty() &&
           std::all_of(privilege.begin(), privilege.end(), [](char c) {
               return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == ' ';
           });
}

} // namespace

AlterScript mysql_grant(const ObjectRef& ref, std::string_view privilege,
                        std::string_view grantee, bool with_grant_option) {
    if (grantee.empty() || grantee == "PUBLIC") {
        return refused("choose an account: MySQL has no PUBLIC");
    }
    if (!known_privilege(privilege)) return refused("invalid privilege name");

    std::string error;
    const std::string target = grant_target(ref, error);
    if (!error.empty()) return refused(error);

    return single("GRANT " + privilege_keyword(privilege) + " ON " + target + " TO " +
                  mysql_account(grantee) +
                  (with_grant_option ? " WITH GRANT OPTION" : ""));
}

AlterScript mysql_revoke(const ObjectRef& ref, std::string_view privilege,
                         std::string_view grantee) {
    if (grantee.empty() || grantee == "PUBLIC") {
        return refused("choose an account: MySQL has no PUBLIC");
    }
    if (!known_privilege(privilege)) return refused("invalid privilege name");

    std::string error;
    const std::string target = grant_target(ref, error);
    if (!error.empty()) return refused(error);

    return single("REVOKE " + privilege_keyword(privilege) + " ON " + target +
                  " FROM " + mysql_account(grantee));
}

namespace {

// Um pedaco do cabecalho do CREATE: onde comeca, onde termina, e o que e'.
struct HeadToken {
    std::size_t begin = 0;
    std::size_t end   = 0;
    bool        identifier = false;   // palavra ou `nome`
};

// Le o proximo pedaco a partir de `at`. Falso no fim do texto.
bool next_token(std::string_view text, std::size_t at, HeadToken& token) {
    while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at])) != 0) ++at;
    if (at >= text.size()) return false;

    token.begin      = at;
    token.identifier = false;
    const char c = text[at];
    if (c == '`' || c == '\'' || c == '"') {
        // `nome`, 'texto' ou "texto": ate' o fecho, com o fecho dobrado dentro.
        std::size_t i = at + 1;
        while (i < text.size()) {
            if (text[i] == c) {
                if (i + 1 < text.size() && text[i + 1] == c) { i += 2; continue; }
                break;
            }
            ++i;
        }
        token.end        = i < text.size() ? i + 1 : text.size();
        token.identifier = c == '`';
        return true;
    }
    if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '$' ||
        (static_cast<unsigned char>(c) & 0x80) != 0) {
        std::size_t i = at;
        while (i < text.size() &&
               (std::isalnum(static_cast<unsigned char>(text[i])) != 0 || text[i] == '_' ||
                text[i] == '$' || (static_cast<unsigned char>(text[i]) & 0x80) != 0)) {
            ++i;
        }
        token.end        = i;
        token.identifier = true;
        return true;
    }
    token.end = at + 1;   // pontuacao: um caractere
    return true;
}

bool word_is(std::string_view text, const HeadToken& token, std::string_view word) {
    if (token.end - token.begin != word.size()) return false;
    for (std::size_t i = 0; i < word.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(text[token.begin + i])) != word[i]) {
            return false;
        }
    }
    return true;
}

// Poe o banco na frente do nome do objeto -- e, no trigger, da tabela.
//
// `SHOW CREATE PROCEDURE` devolve "CREATE DEFINER=... PROCEDURE `p`()", SEM o
// banco: executado como esta', o objeto nasce no banco CORRENTE da sessao,
// que pode ser outro. O defeito so' apareceu no teste contra o servidor: o
// DROP apagava em `otter_scratch` e o CREATE recriava em `otter_test`.
std::string qualify_create(std::string_view source, std::string_view kind,
                           std::string_view schema) {
    if (schema.empty()) return std::string(source);

    std::string out(source);
    const std::string prefix = mysql_quote(schema) + ".";

    // Insere o banco antes do identificador em `at`, se ele ainda nao e'
    // qualificado. Devolve a posicao logo depois do nome (ja' com o prefixo).
    const auto qualify_at = [&out, &prefix](std::size_t at) -> std::size_t {
        HeadToken name;
        if (!next_token(out, at, name) || !name.identifier) return std::string::npos;
        HeadToken after;
        if (next_token(out, name.end, after) && out[after.begin] == '.') {
            // banco.nome: pula o ponto e o nome.
            HeadToken real;
            return next_token(out, after.end, real) ? real.end : std::string::npos;
        }
        out.insert(name.begin, prefix);
        return name.end + prefix.size();
    };

    // O cabecalho: CREATE [DEFINER=...] <KIND> nome ... O KIND e' a primeira
    // palavra igual a ele fora de aspas.
    std::size_t at = 0;
    HeadToken token;
    for (int guard = 0; guard < 64 && next_token(out, at, token); ++guard) {
        at = token.end;
        if (!token.identifier || out[token.begin] == '`' || !word_is(out, token, kind)) {
            continue;
        }
        std::size_t after_name = qualify_at(token.end);
        if (after_name == std::string::npos || kind != "TRIGGER") return out;

        // Trigger: "... ON tabela FOR EACH ROW". A tabela tem de estar no
        // MESMO banco do trigger; sem qualificar, valeria o banco corrente.
        at = after_name;
        for (int inner = 0; inner < 16 && next_token(out, at, token); ++inner) {
            at = token.end;
            if (token.identifier && out[token.begin] != '`' && word_is(out, token, "ON")) {
                (void)qualify_at(token.end);
                break;
            }
        }
        return out;
    }
    return out;
}

} // namespace

bool mysql_source_editable(ObjectType type) noexcept {
    return type == ObjectType::view || type == ObjectType::function ||
           type == ObjectType::procedure || type == ObjectType::trigger ||
           type == ObjectType::event;
}

AlterScript mysql_source_script(const ObjectRef& ref, std::string_view source) {
    // O texto como um comando so': sem o ';' final e sem espacos em volta.
    while (!source.empty() &&
           (std::isspace(static_cast<unsigned char>(source.back())) != 0 ||
            source.back() == ';')) {
        source.remove_suffix(1);
    }
    while (!source.empty() && std::isspace(static_cast<unsigned char>(source.front())) != 0) {
        source.remove_prefix(1);
    }
    if (source.empty()) return refused("the source is empty");

    if (ref.type == ObjectType::view) {
        // SHOW CREATE VIEW devolve "CREATE ALGORITHM=... VIEW": o OR REPLACE
        // entra logo depois do CREATE, se ja' nao estiver la'.
        std::string text(source);
        const std::string head = upper(text.substr(0, 17));
        if (head.starts_with("CREATE ") && !head.starts_with("CREATE OR REPLACE")) {
            text.insert(7, "OR REPLACE ");
        }
        return single(std::move(text));
    }

    const char* kind = nullptr;
    switch (ref.type) {
        case ObjectType::function:  kind = "FUNCTION";  break;
        case ObjectType::procedure: kind = "PROCEDURE"; break;
        case ObjectType::trigger:   kind = "TRIGGER";   break;
        case ObjectType::event:     kind = "EVENT";     break;
        default:
            return refused("this object type is not saved from its source on MySQL");
    }

    AlterScript script;
    script.statements.push_back(std::string("DROP ") + kind + " IF EXISTS " +
                                full_name(ref));
    script.statements.push_back(qualify_create(source, kind, ref.schema));
    script.destructive.push_back(0);
    script.warnings.emplace_back(
        "MySQL has no CREATE OR REPLACE for this object: it is dropped and created "
        "again. If the new text fails, the object is gone -- keep a copy");
    if (ref.type == ObjectType::function || ref.type == ObjectType::procedure) {
        script.warnings.emplace_back(
            "privileges granted on the routine itself are lost with the drop");
    }
    return script;
}

// --- CREATE -------------------------------------------------------------------------

AlterScript mysql_create_database(std::string_view name, std::string_view charset,
                                  std::string_view collation) {
    if (name.empty()) return refused("the database name is required");

    std::string sql = "CREATE DATABASE " + mysql_quote(name);
    // Nomes de charset e collation sao palavras do servidor (utf8mb4,
    // utf8mb4_0900_ai_ci): so' letras, digitos e '_' passam, sem aspas.
    const auto word = [](std::string_view text) {
        return std::all_of(text.begin(), text.end(), [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
        });
    };
    if (!word(charset) || !word(collation)) {
        return refused("invalid charset or collation name");
    }
    if (!charset.empty())   sql += " DEFAULT CHARACTER SET " + std::string(charset);
    if (!collation.empty()) sql += " DEFAULT COLLATE " + std::string(collation);
    return single(std::move(sql));
}

AlterScript mysql_create_user(const MysqlNewUser& user) {
    if (user.name.empty()) return refused("the user name is required");

    std::string sql = "CREATE USER " + mysql_literal(user.name) + "@" +
                      mysql_literal(user.host.empty() ? "%" : user.host);
    if (!user.password.empty()) sql += " IDENTIFIED BY " + mysql_literal(user.password);
    return single(std::move(sql));
}

AlterScript mysql_user_password(std::string_view user, std::string_view host,
                                std::string_view password) {
    const std::string account =
        user.empty() ? std::string("USER()")
                     : mysql_literal(user) + "@" + mysql_literal(host.empty() ? "%" : host);
    return single("ALTER USER " + account + " IDENTIFIED BY " + mysql_literal(password));
}

std::string mysql_routine_template(std::string_view schema, std::string_view name,
                                   bool procedure) {
    const std::string full = schema.empty()
                                 ? mysql_quote(name)
                                 : mysql_quote(schema) + "." + mysql_quote(name);
    if (procedure) {
        return "CREATE PROCEDURE " + full + "()\nBEGIN\n\nEND";
    }
    return "CREATE FUNCTION " + full + "()\nRETURNS INT\nDETERMINISTIC\nBEGIN\n"
           "    RETURN 0;\nEND";
}

std::string mysql_trigger_template(std::string_view schema, std::string_view table,
                                   std::string_view name, std::string_view timing,
                                   std::string_view event) {
    const auto qualified = [&schema](std::string_view what) {
        return schema.empty() ? mysql_quote(what)
                              : mysql_quote(schema) + "." + mysql_quote(what);
    };
    return "CREATE TRIGGER " + qualified(name) + "\n" +
           std::string(timing.empty() ? "BEFORE" : timing) + " " +
           std::string(event.empty() ? "INSERT" : event) + "\nON " + qualified(table) +
           " FOR EACH ROW\nBEGIN\n\nEND";
}

std::string mysql_event_template(std::string_view schema, std::string_view name) {
    const std::string full = schema.empty()
                                 ? mysql_quote(name)
                                 : mysql_quote(schema) + "." + mysql_quote(name);
    return "CREATE EVENT " + full + "\nON SCHEDULE EVERY 1 DAY\n"
           "STARTS CURRENT_TIMESTAMP\nDO\nBEGIN\n\nEND";
}

// --- Tools --------------------------------------------------------------------------

std::vector<std::string_view> mysql_table_tool_options(MysqlTableTool tool) {
    switch (tool) {
        case MysqlTableTool::check:
            return {"", "FOR UPGRADE", "QUICK", "FAST", "MEDIUM", "EXTENDED", "CHANGED"};
        case MysqlTableTool::repair:
            return {"", "QUICK", "EXTENDED", "USE_FRM"};
        case MysqlTableTool::analyze:
        case MysqlTableTool::optimize:
            return {""};
    }
    return {""};
}

std::string mysql_table_tool_sql(MysqlTableTool tool, std::string_view schema,
                                 std::string_view table, std::string_view option) {
    const char* verb = "ANALYZE";
    switch (tool) {
        case MysqlTableTool::analyze:  verb = "ANALYZE";  break;
        case MysqlTableTool::check:    verb = "CHECK";    break;
        case MysqlTableTool::optimize: verb = "OPTIMIZE"; break;
        case MysqlTableTool::repair:   verb = "REPAIR";   break;
    }
    std::string sql = std::string(verb) + " TABLE " + mysql_quote(schema) + "." +
                      mysql_quote(table);

    // So' as opcoes do combo: o texto vai no comando sem aspas.
    const std::vector<std::string_view> allowed = mysql_table_tool_options(tool);
    if (!option.empty() &&
        std::find(allowed.begin(), allowed.end(), option) != allowed.end()) {
        sql += " " + std::string(option);
    }
    return sql;
}

AlterScript mysql_truncate(std::string_view schema, std::string_view table) {
    AlterScript script =
        single("TRUNCATE TABLE " + mysql_quote(schema) + "." + mysql_quote(table));
    script.destructive.push_back(0);
    script.warnings.emplace_back(
        "removes every row and cannot be rolled back: TRUNCATE commits implicitly");
    return script;
}

AlterScript mysql_session_kill(std::string_view id, bool connection) {
    if (!only_digits(id)) return refused("select a session row first");
    AlterScript script = single(std::string(connection ? "KILL CONNECTION " : "KILL QUERY ") +
                                std::string(id));
    if (connection) {
        script.destructive.push_back(0);
        script.warnings.emplace_back(
            "closes the session: its open transaction is rolled back");
    }
    return script;
}

std::string_view mysql_sessions_query() noexcept {
    return "SHOW FULL PROCESSLIST";
}

// --- Cliente nativo -----------------------------------------------------------------

std::string find_mysql_tool(std::string_view name) {
    namespace fs = std::filesystem;

    if (std::string found = find_program(name); !found.empty()) return found;

    std::vector<fs::path> roots;
#ifdef _WIN32
    for (const char* variable : {"ProgramFiles", "ProgramW6432", "ProgramFiles(x86)"}) {
        if (const char* base = std::getenv(variable)) {
            roots.push_back(fs::path(base) / "MySQL");
            roots.push_back(fs::path(base) / "MariaDB");
        }
    }
    const std::string file = std::string(name) + ".exe";
#else
    roots = {"/usr/local/mysql", "/opt/homebrew/opt", "/usr/local/opt"};
    const std::string file(name);
#endif

    // "MySQL Server 8.4" antes de "MySQL Server 8.0": a mais nova primeiro --
    // um mysqldump mais velho que o servidor pode recusar o dump.
    std::vector<fs::path> candidates;
    for (const fs::path& root : roots) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        if (fs::exists(root / "bin" / file, ec)) candidates.push_back(root / "bin" / file);
        for (const fs::directory_entry& entry : fs::directory_iterator(root, ec)) {
            const fs::path candidate = entry.path() / "bin" / file;
            if (fs::exists(candidate, ec)) candidates.push_back(candidate);
        }
    }
    if (candidates.empty()) return {};
    std::sort(candidates.rbegin(), candidates.rend());
    return candidates.front().string();
}

namespace {

Status add_mysql_connection(ProcessOptions& command, const ConnConfig& connection) {
    if (!connection.proxy_host.empty()) {
        return fail(Errc::not_supported,
                    "the MySQL client tools cannot connect through a SOCKS proxy");
    }
    command.arguments.push_back("--host=" + connection.host);
    command.arguments.push_back("--port=" + std::to_string(connection.port));
    // TCP sempre: com "localhost" o cliente tentaria o canal nomeado/socket.
    command.arguments.emplace_back("--protocol=TCP");
    if (!connection.user.empty()) {
        command.arguments.push_back("--user=" + connection.user);
    }
    if (connection.ssl_enabled()) command.arguments.emplace_back("--ssl-mode=REQUIRED");

    command.environment.emplace_back("MYSQL_PWD", connection.password);
    return {};
}

} // namespace

Result<ProcessOptions> mysql_dump_command(const ConnConfig& connection,
                                          const MysqlDumpOptions& options,
                                          std::string program) {
    if (program.empty()) {
        return fail(Errc::not_found,
                    "mysqldump was not found: install the MySQL client tools");
    }
    if (connection.database.empty()) {
        return fail(Errc::invalid_argument, "the database name is required");
    }
    if (options.file.empty()) {
        return fail(Errc::invalid_argument, "the output file is required");
    }

    ProcessOptions command;
    command.program = std::move(program);
    OTTER_RETURN_IF_ERROR(add_mysql_connection(command, connection));

    switch (options.method) {
        case MysqlDumpOptions::Method::online:
            command.arguments.emplace_back("--single-transaction");
            break;
        case MysqlDumpOptions::Method::lock_all:
            command.arguments.emplace_back("--lock-all-tables");
            break;
        case MysqlDumpOptions::Method::normal:
            break;
    }
    const auto flag = [&command](bool on, const char* yes, const char* no) {
        command.arguments.emplace_back(on ? yes : no);
    };
    if (options.no_create) command.arguments.emplace_back("--no-create-info");
    flag(options.add_drop, "--add-drop-table", "--skip-add-drop-table");
    flag(options.disable_keys, "--disable-keys", "--skip-disable-keys");
    flag(options.extended_insert, "--extended-insert", "--skip-extended-insert");
    flag(options.comments, "--comments", "--skip-comments");
    if (options.events)   command.arguments.emplace_back("--events");
    if (options.routines) command.arguments.emplace_back("--routines");
    if (options.hex_blob) command.arguments.emplace_back("--hex-blob");
    if (options.no_data)  command.arguments.emplace_back("--no-data");

    command.arguments.push_back("--result-file=" + options.file);
    command.arguments.push_back(connection.database);
    for (const std::string& table : options.tables) command.arguments.push_back(table);
    return command;
}

Result<ProcessOptions> mysql_script_command(const ConnConfig& connection,
                                            std::string_view file, std::string program) {
    if (program.empty()) {
        return fail(Errc::not_found,
                    "mysql was not found: install the MySQL client tools");
    }
    if (file.empty()) return fail(Errc::invalid_argument, "the script file is required");

    ProcessOptions command;
    command.program = std::move(program);
    OTTER_RETURN_IF_ERROR(add_mysql_connection(command, connection));

    // `source` do cliente le' o arquivo -- nao ha' redirecionamento de
    // entrada a montar. A barra invertida e' escape dentro do comando: o
    // caminho vai com barras normais, que o Windows aceita.
    std::string path(file);
    std::replace(path.begin(), path.end(), '\\', '/');
    command.arguments.push_back("--execute=source " + path);
    if (!connection.database.empty()) command.arguments.push_back(connection.database);
    return command;
}

} // namespace otter::db
