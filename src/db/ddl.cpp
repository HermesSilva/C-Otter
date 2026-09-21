#include "db/ddl.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace otter::db {
namespace {

// Palavras reservadas do PostgreSQL que nao podem virar identificador nu.
//
// Lista curta de proposito: cobre o que aparece como nome de coluna na
// pratica ("order", "user", "group", "table"). A lista completa tem ~450
// entradas e a maioria nunca seria escolhida como nome.
const std::unordered_set<std::string>& reserved_words() {
    static const std::unordered_set<std::string> kWords = {
        "all", "analyse", "analyze", "and", "any", "array", "as", "asc",
        "asymmetric", "both", "case", "cast", "check", "collate", "column",
        "constraint", "create", "current_catalog", "current_date",
        "current_role", "current_time", "current_timestamp", "current_user",
        "default", "deferrable", "desc", "distinct", "do", "else", "end",
        "except", "false", "fetch", "for", "foreign", "from", "grant", "group",
        "having", "in", "initially", "intersect", "into", "lateral", "leading",
        "limit", "localtime", "localtimestamp", "not", "null", "offset", "on",
        "only", "or", "order", "placing", "primary", "references", "returning",
        "select", "session_user", "some", "symmetric", "table", "then", "to",
        "trailing", "true", "union", "unique", "user", "using", "variadic",
        "when", "where", "window", "with",
    };
    return kWords;
}

std::string to_lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

// Alinha os nomes de coluna numa largura comum, para o SQL gerado ficar
// legivel. Sem isso, um CREATE TABLE com nomes de tamanhos variados vira uma
// parede irregular.
std::size_t widest_name(const std::vector<ColumnMeta>& columns) {
    std::size_t width = 0;
    for (const ColumnMeta& column : columns) {
        width = std::max(width, quote_if_needed(column.name).size());
    }
    return width;
}

void append_padded(std::string& out, std::string_view text, std::size_t width) {
    out += text;
    for (std::size_t i = text.size(); i < width; ++i) out.push_back(' ');
}

// Colunas da chave primaria, na ordem em que aparecem na tabela.
std::vector<const ColumnMeta*> primary_key_columns(const TableMeta& table) {
    std::vector<const ColumnMeta*> keys;
    for (const ColumnMeta& column : table.columns) {
        if (column.primary_key) keys.push_back(&column);
    }
    return keys;
}

// Marcador de valor legivel: o tipo da coluna, nao "?" nem "$1".
//
// "?" nao diz o que preencher; ":nome_tipo" diz. O usuario substitui o texto
// inteiro, entao qualquer forma serve -- mas a que informa serve melhor.
std::string placeholder_for(const ColumnMeta& column) {
    return "<" + column.type_name + ">";
}

std::string header(std::string_view what, std::string_view schema,
                   std::string_view table) {
    return "-- " + std::string(what) + " " +
           qualified_name(schema, table) + "\n";
}

} // namespace

namespace {

// O dialeto corrente. Nao e' thread_local de proposito: a conexao ativa e' uma
// so' na interface, e um worker que gere SQL precisa do MESMO dialeto que a
// UI mostrou ao usuario.
QuoteStyle g_dialect = QuoteStyle::double_quotes;

struct Delimiters { char open; char close; };

Delimiters delimiters_for(QuoteStyle style) noexcept {
    switch (style) {
        case QuoteStyle::backticks: return {'`', '`'};
        case QuoteStyle::brackets:  return {'[', ']'};
        case QuoteStyle::double_quotes: break;
    }
    return {'"', '"'};
}

} // namespace

void set_sql_dialect(QuoteStyle style) { g_dialect = style; }

QuoteStyle sql_dialect() noexcept { return g_dialect; }

void set_sql_dialect_for(std::string_view driver_id) {
    if (driver_id == "mysql" || driver_id == "mariadb") {
        set_sql_dialect(QuoteStyle::backticks);
    } else if (driver_id == "mssql" || driver_id == "sqlserver") {
        set_sql_dialect(QuoteStyle::brackets);
    } else {
        set_sql_dialect(QuoteStyle::double_quotes);
    }
}

std::string quote_if_needed(std::string_view identifier) {
    const Delimiters d = delimiters_for(g_dialect);

    if (identifier.empty()) return std::string{d.open} + d.close;

    // Comeca por letra minuscula ou '_' e contem so' [a-z0-9_]? Entao nao
    // precisa de delimitador.
    const bool simple_start =
        (identifier[0] >= 'a' && identifier[0] <= 'z') || identifier[0] == '_';

    bool simple = simple_start;
    if (simple) {
        for (const char c : identifier) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                            c == '_';
            if (!ok) { simple = false; break; }
        }
    }

    if (simple && !reserved_words().contains(to_lower(identifier))) {
        return std::string(identifier);
    }

    std::string out;
    out.reserve(identifier.size() + 2);
    out.push_back(d.open);
    for (const char c : identifier) {
        // Delimitador dentro do nome e' dobrado -- em colchetes, so' o de
        // FECHAR precisa, porque '[' nao encerra nada.
        if (c == d.close) out.push_back(d.close);
        out.push_back(c);
    }
    out.push_back(d.close);
    return out;
}

std::string quote_literal(std::string_view text) {
    constexpr char kQuote     = '\'';
    constexpr char kBackslash = '\\';

    std::string out;
    out.reserve(text.size() + 2);
    out.push_back(kQuote);

    // O MySQL trata a barra invertida como escape por padrao (NO_BACKSLASH_
    // ESCAPES vem desligado); o padrao SQL, nao. Dobrar a barra no dialeto
    // errado produziria um texto com barras a mais -- visivel, mas errado --,
    // e NAO dobrar no MySQL abriria caminho para injecao: um comentario
    // terminado em `\` engoliria a aspa de fechamento.
    const bool escapes_backslash = g_dialect == QuoteStyle::backticks;

    for (const char c : text) {
        if (c == kQuote) {
            out.push_back(kQuote);
        } else if (c == kBackslash && escapes_backslash) {
            out.push_back(kBackslash);
        }
        out.push_back(c);
    }
    out.push_back(kQuote);
    return out;
}

std::string_view strip_trailing_semicolon(std::string_view sql) {
    const auto is_space = [](char c) {
        return c == ' ' || c == '\n' || c == '\r' || c == '\t';
    };

    std::size_t end = sql.size();

    // Espacos, depois UM ';', depois espacos de novo. Mais de um ';' seria um
    // script de varios comandos, que nao da' para envolver de jeito nenhum.
    while (end > 0 && is_space(sql[end - 1])) --end;

    if (end > 0 && sql[end - 1] == ';') {
        --end;
        while (end > 0 && is_space(sql[end - 1])) --end;
    }
    return sql.substr(0, end);
}

std::string qualified_name(std::string_view schema, std::string_view table) {
    if (schema.empty()) return quote_if_needed(table);
    return quote_if_needed(schema) + "." + quote_if_needed(table);
}

std::string generate_select(std::string_view schema, const TableMeta& table,
                            std::size_t limit) {
    std::string out = "SELECT ";

    if (table.columns.empty()) {
        // Colunas ainda nao carregadas: '*' e' melhor que um SELECT vazio, e
        // o comentario explica por que nao vieram listadas.
        out += "*\n";
        out = "-- expand the table to list the columns explicitly\n" + out;
    } else {
        for (std::size_t i = 0; i < table.columns.size(); ++i) {
            if (i > 0) out += ",\n       ";
            out += quote_if_needed(table.columns[i].name);
        }
        out += "\n";
    }

    out += "  FROM " + qualified_name(schema, table.name);
    if (limit > 0) out += "\n LIMIT " + std::to_string(limit);
    out += ";\n";
    return out;
}

std::string generate_insert(std::string_view schema, const TableMeta& table) {
    if (table.columns.empty()) {
        return header("INSERT into", schema, table.name) +
               "-- expand the table first: the columns are not loaded yet\n";
    }

    std::string columns;
    std::string values;
    bool has_defaults = false;

    for (const ColumnMeta& column : table.columns) {
        // Coluna com DEFAULT entra comentada: preencher um serial a mao e'
        // quase sempre engano, mas remover a linha impediria o caso legitimo.
        const bool skip = !column.default_value.empty();
        if (skip) has_defaults = true;

        const std::string prefix = skip ? "\n     -- " : "\n        ";
        if (!columns.empty()) {
            columns += ",";
            values  += ",";
        }
        columns += prefix + quote_if_needed(column.name);
        values  += prefix + placeholder_for(column);
    }

    std::string out = "INSERT INTO " + qualified_name(schema, table.name) +
                      " (" + columns + "\n     )\nVALUES (" + values + "\n     );\n";

    if (has_defaults) {
        out = "-- commented-out columns have a DEFAULT; uncomment to set them\n" +
              out;
    }
    return out;
}

std::string generate_update(std::string_view schema, const TableMeta& table) {
    if (table.columns.empty()) {
        return header("UPDATE", schema, table.name) +
               "-- expand the table first: the columns are not loaded yet\n";
    }

    const std::vector<const ColumnMeta*> keys = primary_key_columns(table);
    const std::size_t width = widest_name(table.columns);

    std::string out = "UPDATE " + qualified_name(schema, table.name) + "\n";

    bool first = true;
    for (const ColumnMeta& column : table.columns) {
        if (column.primary_key) continue;   // PK vai no WHERE, nao no SET

        out += first ? "   SET " : "     , ";
        first = false;
        append_padded(out, quote_if_needed(column.name), width);
        out += " = " + placeholder_for(column) + "\n";
    }

    if (keys.empty()) {
        // Um UPDATE sem WHERE executado por reflexo apaga a tabela inteira.
        // Vem comentado, e o aviso vai no topo, onde se le' primeiro.
        out = "-- WARNING: no primary key found -- the WHERE clause is up to you.\n"
              "-- Running this without a WHERE would update EVERY row.\n" + out;
        out += " -- WHERE <condition>;\n";
    } else {
        out += " WHERE ";
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (i > 0) out += "\n   AND ";
            out += quote_if_needed(keys[i]->name) + " = " +
                   placeholder_for(*keys[i]);
        }
        out += ";\n";
    }
    return out;
}

std::string generate_delete(std::string_view schema, const TableMeta& table) {
    const std::vector<const ColumnMeta*> keys = primary_key_columns(table);

    std::string out = "DELETE FROM " + qualified_name(schema, table.name) + "\n";

    if (keys.empty()) {
        out = "-- WARNING: no primary key found -- the WHERE clause is up to you.\n"
              "-- Running this without a WHERE would delete EVERY row.\n" + out;
        out += " -- WHERE <condition>;\n";
        return out;
    }

    out += " WHERE ";
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (i > 0) out += "\n   AND ";
        out += quote_if_needed(keys[i]->name) + " = " + placeholder_for(*keys[i]);
    }
    out += ";\n";
    return out;
}

std::string generate_ddl(std::string_view schema, const TableMeta& table) {
    const std::string full = qualified_name(schema, table.name);

    if (table.columns.empty()) {
        return "-- expand " + full +
               " first: the columns are not loaded yet\n";
    }

    std::string out;

    // View: o corpo e' a definicao, nao um CREATE TABLE montado.
    if (table.is_view()) {
        const char* kind = table.kind == ObjKind::materialized_view
                               ? "MATERIALIZED VIEW" : "VIEW";
        if (table.definition.empty()) {
            return std::string("-- expand the Definition node of ") + full +
                   " to load its body\n";
        }
        out = "CREATE OR REPLACE " + std::string(kind) + " " + full + " AS\n" +
              table.definition;
        if (out.back() != '\n') out.push_back('\n');
        return out;
    }

    out = "CREATE TABLE " + full + " (\n";

    const std::size_t width = widest_name(table.columns);

    for (std::size_t i = 0; i < table.columns.size(); ++i) {
        const ColumnMeta& column = table.columns[i];

        out += "    ";
        append_padded(out, quote_if_needed(column.name), width);
        out += " " + column.type_name;

        if (!column.nullable) out += " NOT NULL";
        if (!column.default_value.empty()) {
            out += " DEFAULT " + column.default_value;
        }
        if (i + 1 < table.columns.size() || !table.constraints.empty()) {
            out += ",";
        }
        out += "\n";
    }

    // Constraints com a definicao que o servidor devolveu: reescreve-las a
    // partir das colunas perderia EXCLUDE, CHECK e expressoes.
    for (std::size_t i = 0; i < table.constraints.size(); ++i) {
        const ConstraintMeta& constraint = table.constraints[i];
        out += "    CONSTRAINT " + quote_if_needed(constraint.name) + " " +
               constraint.definition;
        if (i + 1 < table.constraints.size()) out += ",";
        out += "\n";
    }

    out += ");\n";

    if (table.constraints.empty() && !table.constraints_loaded) {
        out += "\n-- constraints not loaded; expand the node to include them\n";
    }

    // Indices vao como comandos separados, que e' a forma do PostgreSQL. O
    // indice da PK e' criado junto com a constraint -- inclui-lo produziria
    // erro de duplicata.
    bool wrote_index_header = false;
    for (const IndexMeta& index : table.indexes) {
        if (index.primary) continue;
        if (index.definition.empty()) continue;

        if (!wrote_index_header) {
            out += "\n";
            wrote_index_header = true;
        }
        out += index.definition + ";\n";
    }

    if (!table.comment.empty()) {
        std::string escaped;
        for (const char c : table.comment) {
            if (c == '\'') escaped += "''";
            else           escaped.push_back(c);
        }
        out += "\nCOMMENT ON TABLE " + full + " IS '" + escaped + "';\n";
    }
    return out;
}

std::string generate_count(std::string_view schema, const TableMeta& table) {
    return "SELECT count(*) FROM " + qualified_name(schema, table.name) + ";\n";
}

} // namespace otter::db
