#include "sql/dialect.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace otter::sql {
namespace {

std::string upper(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

// Keywords comuns ao SQL padrao -- base de todos os dialetos.
const std::unordered_set<std::string_view>& core_keywords() {
    static const std::unordered_set<std::string_view> words = {
        "ADD", "ALL", "ALTER", "AND", "ANY", "AS", "ASC", "BEGIN", "BETWEEN",
        "BY", "CASE", "CAST", "CHECK", "COLUMN", "COMMIT", "CONSTRAINT",
        "CREATE", "CROSS", "CURRENT", "DEFAULT", "DELETE", "DESC", "DISTINCT",
        "DROP", "ELSE", "END", "ESCAPE", "EXCEPT", "EXISTS", "FALSE", "FETCH",
        "FOR", "FOREIGN", "FROM", "FULL", "GRANT", "GROUP", "HAVING", "IN",
        "INDEX", "INNER", "INSERT", "INTERSECT", "INTO", "IS", "JOIN", "KEY",
        "LEFT", "LIKE", "LIMIT", "NATURAL", "NOT", "NULL", "OFFSET", "ON",
        "OR", "ORDER", "OUTER", "PRIMARY", "REFERENCES", "REVOKE", "RIGHT",
        "ROLLBACK", "SELECT", "SET", "TABLE", "THEN", "TRUE", "UNION",
        "UNIQUE", "UPDATE", "USING", "VALUES", "VIEW", "WHEN", "WHERE", "WITH",
    };
    return words;
}

const std::unordered_set<std::string_view>& core_functions() {
    static const std::unordered_set<std::string_view> names = {
        "ABS", "AVG", "CEIL", "CEILING", "COALESCE", "CONCAT", "COUNT",
        "CURRENT_DATE", "CURRENT_TIME", "CURRENT_TIMESTAMP", "DENSE_RANK",
        "EXTRACT", "FLOOR", "GREATEST", "LEAST", "LENGTH", "LOWER", "LTRIM",
        "MAX", "MIN", "MOD", "NULLIF", "NOW", "POWER", "RANK", "REPLACE",
        "ROUND", "ROW_NUMBER", "RTRIM", "SQRT", "SUBSTRING", "SUM", "TRIM",
        "UPPER",
    };
    return names;
}

void merge(std::unordered_set<std::string_view>& target,
           const std::unordered_set<std::string_view>& source) {
    target.insert(source.begin(), source.end());
}

} // namespace

bool Dialect::is_keyword(std::string_view word) const {
    // Keywords sao guardadas em maiusculas; a busca normaliza.
    static thread_local std::string buffer;
    buffer = upper(word);
    return keywords.contains(buffer);
}

bool Dialect::is_function(std::string_view word) const {
    static thread_local std::string buffer;
    buffer = upper(word);
    return functions.contains(buffer);
}

bool Dialect::is_type(std::string_view word) const {
    static thread_local std::string buffer;
    buffer = upper(word);
    return types.contains(buffer);
}

bool Dialect::needs_quoting(std::string_view identifier) const {
    if (identifier.empty()) return true;

    // Comeca com digito, ou contem caractere fora de [A-Za-z0-9_].
    if (std::isdigit(static_cast<unsigned char>(identifier.front()))) return true;
    for (char c : identifier) {
        const bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        if (!ok) return true;
    }

    if (is_keyword(identifier)) return true;

    // Caixa nao canonica: "Tabela" em PostgreSQL vira "tabela" sem aspas.
    if (unquoted_case == FoldCase::lower) {
        const bool has_upper =
            std::any_of(identifier.begin(), identifier.end(), [](char c) {
                return std::isupper(static_cast<unsigned char>(c)) != 0;
            });
        if (has_upper) return true;
    } else if (unquoted_case == FoldCase::upper) {
        const bool has_lower =
            std::any_of(identifier.begin(), identifier.end(), [](char c) {
                return std::islower(static_cast<unsigned char>(c)) != 0;
            });
        if (has_lower) return true;
    }
    return false;
}

std::string Dialect::quote_identifier(std::string_view identifier) const {
    const QuoteStyle style = identifier_quotes.empty()
                                 ? QuoteStyle::double_quotes
                                 : identifier_quotes.front();

    std::string out;
    switch (style) {
        case QuoteStyle::backticks:
            out.push_back('`');
            for (char c : identifier) {
                if (c == '`') out.push_back('`');   // dobra para escapar
                out.push_back(c);
            }
            out.push_back('`');
            break;

        case QuoteStyle::brackets:
            out.push_back('[');
            for (char c : identifier) {
                if (c == ']') out.push_back(']');
                out.push_back(c);
            }
            out.push_back(']');
            break;

        case QuoteStyle::double_quotes:
        default:
            out.push_back('"');
            for (char c : identifier) {
                if (c == '"') out.push_back('"');
                out.push_back(c);
            }
            out.push_back('"');
            break;
    }
    return out;
}

const Dialect& standard_dialect() {
    static const Dialect dialect = [] {
        Dialect d;
        d.name = "SQL";
        d.keywords  = core_keywords();
        d.functions = core_functions();
        d.types = {"BIGINT", "BOOLEAN", "CHAR", "DATE", "DECIMAL", "DOUBLE",
                   "FLOAT", "INTEGER", "NUMERIC", "REAL", "SMALLINT", "TIME",
                   "TIMESTAMP", "VARCHAR"};
        return d;
    }();
    return dialect;
}

const Dialect& postgres_dialect() {
    static const Dialect dialect = [] {
        Dialect d;
        d.name = "PostgreSQL";
        d.unquoted_case          = Dialect::FoldCase::lower;
        d.identifier_quotes      = {QuoteStyle::double_quotes};
        d.dollar_quoted_strings  = true;    // $$ ... $$
        d.nested_block_comments  = true;
        d.backslash_escapes      = false;   // padrao desde a versao 9.1

        d.keywords = core_keywords();
        merge(d.keywords, {
            "ANALYZE", "ARRAY", "CONFLICT", "DO", "EXPLAIN", "ILIKE",
            "LATERAL", "MATERIALIZED", "NOTHING", "OVER", "PARTITION",
            "RECURSIVE", "RETURNING", "SCHEMA", "SEQUENCE", "SIMILAR",
            "TABLESAMPLE", "VACUUM", "WINDOW",
        });

        d.functions = core_functions();
        merge(d.functions, {
            "ARRAY_AGG", "GENERATE_SERIES", "JSONB_AGG", "JSONB_BUILD_OBJECT",
            "JSON_AGG", "REGEXP_REPLACE", "STRING_AGG", "TO_CHAR", "TO_DATE",
            "TO_NUMBER", "TO_TIMESTAMP", "UNNEST", "AGE", "DATE_TRUNC",
        });

        d.types = {"BIGINT", "BIGSERIAL", "BOOLEAN", "BYTEA", "CHAR", "DATE",
                   "DECIMAL", "DOUBLE PRECISION", "INET", "INTEGER", "INTERVAL",
                   "JSON", "JSONB", "NUMERIC", "REAL", "SERIAL", "SMALLINT",
                   "TEXT", "TIME", "TIMESTAMP", "TIMESTAMPTZ", "UUID", "VARCHAR",
                   "XML"};
        return d;
    }();
    return dialect;
}

const Dialect& mysql_dialect() {
    static const Dialect dialect = [] {
        Dialect d;
        d.name = "MySQL";
        d.unquoted_case      = Dialect::FoldCase::none;
        d.identifier_quotes  = {QuoteStyle::backticks, QuoteStyle::double_quotes};
        d.backslash_escapes  = true;    // diferenca importante do padrao
        d.hash_line_comments = true;

        d.keywords = core_keywords();
        merge(d.keywords, {
            "AUTO_INCREMENT", "CHANGE", "DATABASE", "DELAYED", "DELIMITER",
            "DUPLICATE", "ENGINE", "ENUM", "IGNORE", "INDEX", "LOCK",
            "REPLACE", "SHOW", "STRAIGHT_JOIN", "UNSIGNED", "ZEROFILL",
        });

        d.functions = core_functions();
        merge(d.functions, {
            "CONCAT_WS", "CURDATE", "CURTIME", "DATE_ADD", "DATE_FORMAT",
            "DATE_SUB", "GROUP_CONCAT", "IFNULL", "LAST_INSERT_ID", "LOCATE",
            "STR_TO_DATE", "UNIX_TIMESTAMP",
        });

        d.types = {"BIGINT", "BLOB", "BOOLEAN", "CHAR", "DATE", "DATETIME",
                   "DECIMAL", "DOUBLE", "ENUM", "FLOAT", "INT", "JSON",
                   "LONGBLOB", "LONGTEXT", "MEDIUMINT", "SET", "SMALLINT",
                   "TEXT", "TIME", "TIMESTAMP", "TINYINT", "VARBINARY",
                   "VARCHAR", "YEAR"};
        return d;
    }();
    return dialect;
}

const Dialect& mssql_dialect() {
    static const Dialect dialect = [] {
        Dialect d;
        d.name = "SQL Server";
        d.unquoted_case     = Dialect::FoldCase::none;
        d.identifier_quotes = {QuoteStyle::brackets, QuoteStyle::double_quotes};
        d.go_batch_separator = true;

        d.keywords = core_keywords();
        merge(d.keywords, {
            "CLUSTERED", "DECLARE", "EXEC", "EXECUTE", "GO", "IDENTITY",
            "MERGE", "NOLOCK", "NVARCHAR", "OUTPUT", "PIVOT", "TOP", "TRY",
            "UNPIVOT", "WHILE",
        });

        d.functions = core_functions();
        merge(d.functions, {
            "CHARINDEX", "CONVERT", "DATEADD", "DATEDIFF", "DATEPART",
            "GETDATE", "ISNULL", "LEN", "NEWID", "PATINDEX", "STUFF",
        });

        d.types = {"BIGINT", "BIT", "CHAR", "DATE", "DATETIME", "DATETIME2",
                   "DECIMAL", "FLOAT", "INT", "MONEY", "NCHAR", "NVARCHAR",
                   "REAL", "SMALLINT", "TEXT", "TIME", "TINYINT",
                   "UNIQUEIDENTIFIER", "VARBINARY", "VARCHAR", "XML"};
        return d;
    }();
    return dialect;
}

const Dialect& sqlanywhere_dialect() {
    static const Dialect dialect = [] {
        Dialect d;
        d.name = "SQL Anywhere";
        // Nomes sem distincao de maiusculas (o padrao de um banco criado sem
        // -c), entre aspas ou nao.
        d.unquoted_case         = Dialect::FoldCase::none;
        d.quoted_case_sensitive = false;
        d.identifier_quotes  = {QuoteStyle::double_quotes, QuoteStyle::brackets};
        // `go` separa lotes, como no dbisql; um lote com CREATE PROCEDURE vai
        // inteiro -- o corpo tem ';' dentro.
        d.go_batch_separator  = true;
        d.slash_line_comments = true;
        // Sem escape por barra invertida: o protocolo TDS liga o servidor com
        // escape_character desligado ('c:\new' tem seis caracteres).
        d.backslash_escapes   = false;

        d.keywords = core_keywords();
        merge(d.keywords, {
            "AT", "CALL", "CHECKPOINT", "COMMENT", "DECLARE", "ELSEIF", "ENDIF",
            "EXEC", "EXECUTE", "FIRST", "FORWARD", "GO", "IF", "LOOP",
            "MESSAGE", "OUTPUT", "PROCEDURE", "RESULT", "START", "TOP",
            "TRIGGER", "TRUNCATE", "VALIDATE", "WHILE",
        });

        d.functions = core_functions();
        merge(d.functions, {
            "CHARINDEX", "CONNECTION_PROPERTY", "DATEADD", "DATEDIFF",
            "DATEFORMAT", "DB_NAME", "DB_PROPERTY", "GETDATE", "IFNULL",
            "ISNULL", "LEFT", "LIST", "LOCATE", "NEWID", "PROPERTY", "REPEAT",
            "RIGHT", "STRING", "TODAY", "USER_NAME",
        });

        d.types = {"BIGINT", "BINARY", "BIT", "CHAR", "DATE", "DATETIME", "DECIMAL",
                   "DOUBLE", "FLOAT", "INT", "INTEGER", "LONG BINARY", "LONG NVARCHAR",
                   "LONG VARCHAR", "MONEY", "NCHAR", "NUMERIC", "NVARCHAR", "REAL",
                   "SMALLINT", "TIME", "TIMESTAMP", "TINYINT", "UNIQUEIDENTIFIER",
                   "VARBINARY", "VARCHAR", "XML"};
        return d;
    }();
    return dialect;
}

const Dialect& oracle_dialect() {
    static const Dialect dialect = [] {
        Dialect d;
        d.name = "Oracle";
        // Sem aspas, o nome vira MAIUSCULAS -- o contrario do PostgreSQL.
        d.unquoted_case     = Dialect::FoldCase::upper;
        d.identifier_quotes = {QuoteStyle::double_quotes};
        d.plsql_units       = true;
        d.backslash_escapes = false;

        d.keywords = core_keywords();
        merge(d.keywords, {
            "BODY", "BULK", "COLLECT", "CONNECT", "CURSOR", "DECLARE", "ELSIF",
            "EXCEPTION", "EXECUTE", "EXIT", "FETCH", "FORALL", "FUNCTION", "IF",
            "IMMEDIATE", "LEVEL", "LOOP", "MERGE", "MINUS", "NOCOPY", "NOWAIT",
            "PACKAGE", "PIVOT", "PRAGMA", "PRIOR", "PROCEDURE", "PURGE", "RAISE",
            "RETURN", "RETURNING", "ROWNUM", "SEQUENCE", "START", "SYNONYM",
            "TRIGGER", "TRUNCATE", "TYPE", "UNPIVOT", "WHILE",
        });

        d.functions = core_functions();
        merge(d.functions, {
            "ADD_MONTHS", "DECODE", "EXTRACT", "GREATEST", "INSTR", "LAST_DAY",
            "LEAST", "LISTAGG", "LPAD", "MONTHS_BETWEEN", "NVL", "NVL2",
            "REGEXP_LIKE", "REGEXP_REPLACE", "REGEXP_SUBSTR", "RPAD", "SUBSTR",
            "SYSDATE", "SYSTIMESTAMP", "SYS_CONTEXT", "SYS_GUID", "TO_CHAR",
            "TO_CLOB", "TO_DATE", "TO_NUMBER", "TO_TIMESTAMP", "TRUNC", "USER",
        });

        d.types = {"BFILE", "BINARY_DOUBLE", "BINARY_FLOAT", "BLOB", "BOOLEAN", "CHAR",
                   "CLOB", "DATE", "FLOAT", "INTEGER", "INTERVAL", "JSON", "LONG",
                   "NCHAR", "NCLOB", "NUMBER", "NVARCHAR2", "PLS_INTEGER", "RAW",
                   "ROWID", "TIMESTAMP", "UROWID", "VARCHAR", "VARCHAR2", "XMLTYPE"};
        return d;
    }();
    return dialect;
}

const Dialect& sqlite_dialect() {
    static const Dialect dialect = [] {
        Dialect d;
        d.name = "SQLite";
        d.unquoted_case     = Dialect::FoldCase::none;
        d.identifier_quotes = {QuoteStyle::double_quotes, QuoteStyle::brackets,
                               QuoteStyle::backticks};

        d.keywords = core_keywords();
        merge(d.keywords, {
            "ABORT", "ATTACH", "AUTOINCREMENT", "CONFLICT", "DETACH",
            "GLOB", "PRAGMA", "REINDEX", "REPLACE", "VACUUM", "WITHOUT",
        });

        d.functions = core_functions();
        merge(d.functions, {"IFNULL", "INSTR", "JULIANDAY", "RANDOM",
                            "STRFTIME", "TYPEOF", "ZEROBLOB"});

        // SQLite usa afinidade de tipo, nao tipos rigidos.
        d.types = {"BLOB", "INTEGER", "NUMERIC", "REAL", "TEXT"};
        return d;
    }();
    return dialect;
}

const Dialect& dialect_for(std::string_view driver_id) {
    if (driver_id == "postgresql") return postgres_dialect();
    if (driver_id == "mysql" || driver_id == "mariadb") return mysql_dialect();
    if (driver_id == "mssql" || driver_id == "sqlserver") return mssql_dialect();
    if (driver_id == "sqlanywhere") return sqlanywhere_dialect();
    if (driver_id == "oracle") return oracle_dialect();
    if (driver_id == "sqlite") return sqlite_dialect();
    return standard_dialect();
}

} // namespace otter::sql

