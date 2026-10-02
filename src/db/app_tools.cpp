#include "db/app_tools.hpp"

#include "db/catalog_mssql.hpp"   // mssql_quote, mssql_type_text
#include "db/ddl.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <random>

namespace otter::db {
namespace {

char lower(char c) noexcept {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool word_char(char c) noexcept {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' ||
           (static_cast<unsigned char>(c) & 0x80) != 0;   // UTF-8: parte de letra
}

// %XX de uma URL. Sequencia invalida fica como esta'.
std::string url_decode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            unsigned value = 0;
            const auto [ptr, ec] =
                std::from_chars(text.data() + i + 1, text.data() + i + 3, value, 16);
            if (ec == std::errc{} && ptr == text.data() + i + 3) {
                out += static_cast<char>(value);
                i += 2;
                continue;
            }
        }
        out += text[i] == '+' ? ' ' : text[i];
    }
    return out;
}

// Identificador com aspas so' quando precisa: minusculas, digitos e `_`
// passam sem elas nos dois SGBDs.
std::string ident(std::string_view name, bool mysql) {
    bool plain = !name.empty() &&
                 std::isdigit(static_cast<unsigned char>(name.front())) == 0;
    for (const char c : name) {
        plain = plain && (std::islower(static_cast<unsigned char>(c)) != 0 ||
                          std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '_');
    }
    if (plain) return std::string(name);

    const char quote = mysql ? '`' : '"';
    std::string out(1, quote);
    for (const char c : name) {
        out += c;
        if (c == quote) out += c;
    }
    out += quote;
    return out;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

} // namespace

// --- URL ---------------------------------------------------------------------------

Result<ConnectionProfile> profile_from_url(std::string_view url) {
    url = trim(url);
    if (url.starts_with("jdbc:")) url.remove_prefix(5);

    // As duas formas com que o DBeaver chega a um SQL Anywhere, reescritas na
    // comum: "sybase:Tds:host:porta?ServiceName=banco" (jConnect, sem "//") e
    // "jtds:sybase://host:porta/banco" (jTDS).
    std::string sybase;
    {
        std::string head(url.substr(0, std::min<std::size_t>(url.size(), 14)));
        std::transform(head.begin(), head.end(), head.begin(), lower);
        if (head.starts_with("sybase:tds:")) {
            sybase = "sqlanywhere://" + std::string(url.substr(11));
        } else if (head.starts_with("jtds:sybase://")) {
            sybase = "sqlanywhere://" + std::string(url.substr(14));
        }
        if (!sybase.empty()) url = sybase;
    }

    // O Oracle: "oracle:thin:[usuario/senha]@//host:porta/servico" e a forma
    // antiga, por SID, "oracle:thin:@host:porta:SID". Tambem reescritas na
    // comum; o SID vira a propriedade "sid" do driver.
    std::string oracle;
    {
        std::string head(url.substr(0, std::min<std::size_t>(url.size(), 12)));
        std::transform(head.begin(), head.end(), head.begin(), lower);
        if (head == "oracle:thin:") {
            std::string_view tail = url.substr(12);
            std::string credentials;
            if (const std::size_t at = tail.find('@'); at != std::string_view::npos) {
                credentials = std::string(tail.substr(0, at));
                tail.remove_prefix(at + 1);
                // "usuario/senha" -> "usuario:senha@", a forma das outras URLs.
                if (const std::size_t slash = credentials.find('/');
                    slash != std::string::npos) {
                    credentials[slash] = ':';
                }
                if (!credentials.empty()) credentials += '@';
            }
            if (tail.starts_with("//")) {
                oracle = "oracle://" + credentials + std::string(tail.substr(2));
            } else if (const std::size_t colon = tail.rfind(':');
                       colon != std::string_view::npos && tail.find(':') != colon) {
                oracle = "oracle://" + credentials + std::string(tail.substr(0, colon)) +
                         "?sid=" + std::string(tail.substr(colon + 1));
            }
            if (!oracle.empty()) url = oracle;
        }
    }

    const std::size_t scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) {
        return std::unexpected(Error{Errc::invalid_argument,
            "not a connection URL: expected <driver>://host[:port]/database"});
    }

    std::string scheme(url.substr(0, scheme_end));
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), lower);
    std::string_view rest = url.substr(scheme_end + 3);

    ConnectionProfile profile;
    if (scheme == "postgresql" || scheme == "postgres" || scheme == "pgsql") {
        profile.driver_id = "postgresql";
        profile.port      = 5432;
    } else if (scheme == "mysql" || scheme == "mariadb") {
        profile.driver_id = "mysql";
        profile.port      = 3306;
    } else if (scheme == "sqlserver" || scheme == "mssql") {
        profile.driver_id = "sqlserver";
        profile.port      = 1433;
    } else if (scheme == "sqlanywhere") {
        profile.driver_id = "sqlanywhere";
        profile.port      = 2638;
    } else if (scheme == "oracle") {
        profile.driver_id = "oracle";
        profile.port      = 1521;
    } else {
        return std::unexpected(Error{Errc::invalid_argument,
            "no driver for '" + scheme +
                "': C-Otter connects to PostgreSQL, MySQL, SQL Server, SQL Anywhere "
                "and Oracle"});
    }

    // O SQL Server separa as propriedades com ';' em vez de '?' e '&':
    // "host:1433;databaseName=vendas;user=ana". Reescrito na forma comum.
    std::string rewritten;
    if (profile.driver_id == "sqlserver" && rest.find(';') != std::string_view::npos) {
        const std::size_t semicolon = rest.find(';');
        rewritten = std::string(rest.substr(0, semicolon)) + "?";
        for (const char c : rest.substr(semicolon + 1)) rewritten += c == ';' ? '&' : c;
        rest = rewritten;
    }

    std::string_view query;
    if (const std::size_t mark = rest.find('?'); mark != std::string_view::npos) {
        query = rest.substr(mark + 1);
        rest  = rest.substr(0, mark);
    }

    std::string_view database;
    if (const std::size_t slash = rest.find('/'); slash != std::string_view::npos) {
        database = rest.substr(slash + 1);
        rest     = rest.substr(0, slash);
    }

    // usuario:senha@ -- o ULTIMO '@', porque a senha pode conter um.
    if (const std::size_t at = rest.rfind('@'); at != std::string_view::npos) {
        const std::string_view credentials = rest.substr(0, at);
        rest = rest.substr(at + 1);
        const std::size_t colon = credentials.find(':');
        profile.user = url_decode(credentials.substr(0, colon));
        if (colon != std::string_view::npos) {
            profile.password = url_decode(credentials.substr(colon + 1));
        }
    }

    // host:porta, com IPv6 entre colchetes.
    std::string_view host = rest;
    std::string_view port;
    if (host.starts_with('[')) {
        const std::size_t close = host.find(']');
        if (close == std::string_view::npos) {
            return std::unexpected(Error{Errc::invalid_argument,
                                         "unterminated IPv6 address in the URL"});
        }
        if (close + 1 < host.size() && host[close + 1] == ':') {
            port = host.substr(close + 2);
        }
        host = host.substr(1, close - 1);
    } else if (const std::size_t colon = host.rfind(':');
               colon != std::string_view::npos) {
        port = host.substr(colon + 1);
        host = host.substr(0, colon);
    }

    if (!port.empty()) {
        unsigned value = 0;
        const auto [ptr, ec] =
            std::from_chars(port.data(), port.data() + port.size(), value);
        if (ec != std::errc{} || ptr != port.data() + port.size() || value == 0 ||
            value > 65535) {
            return std::unexpected(Error{Errc::invalid_argument,
                "invalid port in the URL: " + std::string(port)});
        }
        profile.port = static_cast<std::uint16_t>(value);
    }
    if (!host.empty()) profile.host = url_decode(host);
    profile.database = url_decode(database);

    while (!query.empty()) {
        const std::size_t amp = query.find('&');
        const std::string_view pair = query.substr(0, amp);
        const std::size_t eq = pair.find('=');
        const std::string key = url_decode(pair.substr(0, eq));
        const std::string value =
            eq == std::string_view::npos ? std::string{} : url_decode(pair.substr(eq + 1));

        if (key == "user") {
            profile.user = value;
        } else if (key == "password") {
            profile.password = value;
        } else if (key == "sslmode") {
            profile.ssl.mode    = ssl_mode_from_string(value);
            profile.ssl.enabled = profile.ssl.mode != SslMode::disable;
        } else if (key == "ssl" || key == "useSSL") {
            if (value == "true" || value == "1") {
                profile.ssl.enabled = true;
                profile.ssl.mode    = SslMode::require;
            }
        } else if (key == "databaseName" || key == "database" || key == "ServiceName") {
            profile.database = value;
        } else if (key == "integratedSecurity") {
            if (value == "true") profile.auth_model = AuthModel::windows;
        } else if (key == "encrypt") {
            if (value == "true") {
                profile.ssl.enabled = true;
                profile.ssl.mode    = SslMode::verify_full;
            }
        } else if (key == "trustServerCertificate") {
            // Cifrar sem validar o certificado: e' o `require` dos outros.
            if (value == "true" && profile.ssl.enabled) profile.ssl.mode = SslMode::require;
        } else if (key == "currentSchema") {
            profile.default_schema = value;
        } else if (!key.empty()) {
            profile.driver_properties[key] = value;
        }

        if (amp == std::string_view::npos) break;
        query.remove_prefix(amp + 1);
    }

    profile.save_password = !profile.password.empty();
    return profile;
}

// --- Pastas de conexao ---------------------------------------------------------------

std::string folder_normalize(std::string_view path) {
    std::string out;
    while (!path.empty()) {
        const std::size_t slash = path.find('/');
        const std::string_view part = trim(path.substr(0, slash));
        if (!part.empty()) {
            if (!out.empty()) out += '/';
            out += part;
        }
        if (slash == std::string_view::npos) break;
        path.remove_prefix(slash + 1);
    }
    return out;
}

std::string folder_parent(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string{} : std::string(path.substr(0, slash));
}

std::string folder_leaf(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

std::string folder_join(std::string_view parent, std::string_view name) {
    if (parent.empty()) return std::string(name);
    if (name.empty()) return std::string(parent);
    return std::string(parent) + "/" + std::string(name);
}

bool folder_contains(std::string_view folder, std::string_view path) {
    if (folder.empty()) return true;
    if (!path.starts_with(folder)) return false;
    return path.size() == folder.size() || path[folder.size()] == '/';
}

std::string folder_rebase(std::string_view path, std::string_view from,
                          std::string_view to) {
    if (from.empty() || !folder_contains(from, path)) return std::string(path);
    // O resto, sem a barra que o ligava a `from`.
    std::string_view rest = path.substr(from.size());
    if (!rest.empty()) rest.remove_prefix(1);
    return folder_join(to, rest);
}

// --- Copia avancada ------------------------------------------------------------------

std::string unescape_delimiter(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            const char next = text[++i];
            out += next == 't' ? '\t' : next == 'n' ? '\n' : next == 'r' ? '\r' : next;
        } else {
            out += text[i];
        }
    }
    return out;
}

std::string escape_delimiter(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '\t')      out += "\\t";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\\') out += "\\\\";
        else                out += c;
    }
    return out;
}

std::string advanced_copy(const ResultSet& rs, const EditBuffer& edits,
                          const GridSelection& selection, const CopyOptions& options) {
    const auto quoted = [&options](std::string_view value) {
        // Aspas quando pedido sempre, ou quando o valor quebraria a leitura:
        // contem o delimitador, a quebra de linha ou a propria aspa.
        const bool needs =
            !options.quote.empty() &&
            (options.quote_always ||
             (!options.column_delimiter.empty() &&
              value.find(options.column_delimiter) != std::string_view::npos) ||
             (!options.row_delimiter.empty() &&
              value.find(options.row_delimiter) != std::string_view::npos) ||
             value.find(options.quote) != std::string_view::npos);
        if (!needs) return std::string(value);

        std::string out = options.quote;
        std::size_t from = 0;
        while (true) {
            const std::size_t at = value.find(options.quote, from);
            out += value.substr(from, at == std::string_view::npos ? at : at - from);
            if (at == std::string_view::npos) break;
            out += options.quote;
            out += options.quote;   // a aspa dobra, como no CSV
            from = at + options.quote.size();
        }
        out += options.quote;
        return out;
    };

    std::string out;
    if (options.copy_header) {
        if (options.copy_row_numbers) out += "#" + options.column_delimiter;
        bool first = true;
        for (const std::size_t c : selection.columns) {
            if (!first) out += options.column_delimiter;
            first = false;
            out += quoted(rs.column(c).info().name);
        }
        out += options.row_delimiter;
    }

    for (std::size_t r = selection.row_first;
         r <= selection.row_last && r < rs.row_count(); ++r) {
        if (r != selection.row_first) out += options.row_delimiter;
        if (options.copy_row_numbers) {
            out += std::to_string(options.first_row_number + (r - selection.row_first));
            out += options.column_delimiter;
        }
        bool first = true;
        for (const std::size_t c : selection.columns) {
            if (!first) out += options.column_delimiter;
            first = false;
            const CellValue value = cell_value(rs, edits, r, c);
            out += value.is_null ? options.null_text : quoted(value.text);
        }
    }
    return out;
}

// --- UUID ---------------------------------------------------------------------------

std::string format_uuid_v4(std::array<std::uint8_t, 16> bytes) {
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40);   // versao 4
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80);   // variante RFC 4122

    char buffer[37];
    std::snprintf(buffer, sizeof buffer,
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6],
                  bytes[7], bytes[8], bytes[9], bytes[10], bytes[11], bytes[12],
                  bytes[13], bytes[14], bytes[15]);
    return buffer;
}

std::string generate_uuid() {
    static std::mt19937_64 generator{std::random_device{}()};
    std::array<std::uint8_t, 16> bytes{};
    for (std::size_t i = 0; i < bytes.size(); i += 8) {
        std::uint64_t value = generator();
        for (std::size_t b = 0; b < 8; ++b) {
            bytes[i + b] = static_cast<std::uint8_t>(value & 0xFF);
            value >>= 8;
        }
    }
    return format_uuid_v4(bytes);
}

// --- Chamada de rotina ---------------------------------------------------------------

namespace {

// "@a int, @total decimal OUTPUT" -> EXEC com um marcador por parametro de
// entrada e uma variavel por parametro de saida.
std::string mssql_routine_call(std::string_view schema, const RoutineMeta& routine) {
    struct Parameter {
        std::string name;     // com o @
        std::string type;
        bool        output = false;
    };
    std::vector<Parameter> parameters;

    std::string_view rest = routine.arguments;
    std::size_t start = 0;
    int depth = 0;
    const auto take = [&parameters](std::string_view piece) {
        piece = trim(piece);
        if (piece.empty() || piece.front() != '@') return;

        Parameter parameter;
        std::size_t i = 0;
        while (i < piece.size() && piece[i] != ' ') ++i;
        parameter.name = std::string(piece.substr(0, i));

        std::string_view tail = trim(piece.substr(i));
        constexpr std::string_view kOutput = " OUTPUT";
        if (tail.size() >= kOutput.size() &&
            tail.substr(tail.size() - kOutput.size()) == kOutput) {
            parameter.output = true;
            tail = trim(tail.substr(0, tail.size() - kOutput.size()));
        }
        parameter.type = std::string(tail);
        parameters.push_back(std::move(parameter));
    };
    for (std::size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] == '(') ++depth;
        else if (rest[i] == ')') --depth;
        else if (rest[i] == ',' && depth == 0) {
            take(rest.substr(start, i - start));
            start = i + 1;
        }
    }
    take(rest.substr(start));

    const std::string name = schema.empty()
                                 ? mssql_quote(routine.name)
                                 : mssql_quote(schema) + "." + mssql_quote(routine.name);

    if (routine.kind != ObjKind::procedure) {
        std::string arguments;
        for (const Parameter& parameter : parameters) {
            if (!arguments.empty()) arguments += ", ";
            arguments += ":" + parameter.name.substr(1);
        }
        // Funcao de tabela se consulta; a escalar se seleciona.
        return routine.return_type == "TABLE"
                   ? "SELECT * FROM " + name + "(" + arguments + ");"
                   : "SELECT " + name + "(" + arguments + ");";
    }

    std::string declare;
    std::string arguments;
    std::string outputs;
    for (const Parameter& parameter : parameters) {
        if (!arguments.empty()) arguments += ", ";
        if (parameter.output) {
            // Tipos de tamanho variavel chegam sem o tamanho (TYPE_NAME): sem
            // ele `varchar` seria varchar(1), e o valor sairia cortado.
            std::string type = parameter.type.empty() ? "sql_variant" : parameter.type;
            if (type == "varchar" || type == "nvarchar" || type == "varbinary") type += "(max)";
            declare += "DECLARE " + parameter.name + " " + type + ";\n";
            arguments += parameter.name + " = " + parameter.name + " OUTPUT";
            outputs += (outputs.empty() ? "" : ", ") + parameter.name + " AS " +
                       mssql_quote(parameter.name.substr(1));
        } else {
            arguments += parameter.name + " = :" + parameter.name.substr(1);
        }
    }

    std::string out = declare + "EXEC " + name;
    if (!arguments.empty()) out += " " + arguments;
    out += ";";
    if (!outputs.empty()) out += "\nSELECT " + outputs + ";";
    return out;
}

} // namespace

std::string routine_call_sql(std::string_view schema, const RoutineMeta& routine,
                             bool mysql) {
    if (sql_dialect() == QuoteStyle::brackets) return mssql_routine_call(schema, routine);

    // Os parametros de ENTRADA, da assinatura formatada: "IN a integer, OUT b
    // text, c text DEFAULT 'x'". Sem nome, o marcador e' p1, p2...
    std::vector<std::string> parameters;
    std::string_view rest = routine.arguments;
    int depth = 0;
    std::size_t start = 0;
    const auto take = [&](std::string_view piece) {
        piece = trim(piece);
        if (piece.empty()) return;

        std::vector<std::string_view> words;
        std::size_t i = 0;
        while (i < piece.size()) {
            while (i < piece.size() && piece[i] == ' ') ++i;
            const std::size_t begin = i;
            while (i < piece.size() && piece[i] != ' ') ++i;
            if (i > begin) words.push_back(piece.substr(begin, i - begin));
        }
        if (words.empty()) return;

        std::string mode;
        for (const char c : words.front()) {
            mode += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        std::size_t first = 0;
        if (mode == "OUT" || mode == "TABLE") return;   // nao se passa na chamada
        if (mode == "IN" || mode == "INOUT" || mode == "VARIADIC") first = 1;

        // "nome tipo": ha' nome quando sobram duas palavras antes de DEFAULT.
        std::size_t count = 0;
        for (std::size_t w = first; w < words.size(); ++w) {
            if (words[w] == "DEFAULT" || words[w] == "=") break;
            ++count;
        }
        std::string name;
        if (count >= 2) name = std::string(words[first]);
        if (name.empty() || name.front() == '"') {
            name = "p" + std::to_string(parameters.size() + 1);
        }
        parameters.push_back(std::move(name));
    };
    for (std::size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] == '(') ++depth;
        else if (rest[i] == ')') --depth;
        else if (rest[i] == ',' && depth == 0) {
            take(rest.substr(start, i - start));
            start = i + 1;
        }
    }
    take(rest.substr(start));

    std::string name = schema.empty()
                           ? ident(routine.name, mysql)
                           : ident(schema, mysql) + "." + ident(routine.name, mysql);

    std::string arguments;
    for (const std::string& parameter : parameters) {
        if (!arguments.empty()) arguments += ", ";
        arguments += ":" + parameter;
    }

    if (routine.kind == ObjKind::procedure) {
        return "CALL " + name + "(" + arguments + ");";
    }
    // SQL Anywhere: uma funcao devolve UM valor e so' se chama numa expressao.
    if (sql_dialect() == QuoteStyle::anywhere) {
        return "SELECT " + name + "(" + arguments + ");";
    }
    // No MySQL uma funcao so' devolve escalar: SELECT f(). No PostgreSQL
    // `SELECT * FROM f()` abre as colunas de uma funcao que devolve conjunto
    // e tambem serve para a escalar.
    return mysql ? "SELECT " + name + "(" + arguments + ");"
                 : "SELECT * FROM " + name + "(" + arguments + ");";
}

// --- DDL pelo resultado ---------------------------------------------------------------

std::string create_table_from_result(const ResultSet& rs, std::string_view table,
                                     bool mysql) {
    if (sql_dialect() == QuoteStyle::brackets) {
        // SQL Server: o tamanho chega em BYTES (-1 = max), e a nulidade vai
        // explicita -- sem ela depende de uma opcao da sessao.
        std::string out = "CREATE TABLE " + mssql_quote(table) + " (\n";
        for (std::size_t c = 0; c < rs.column_count(); ++c) {
            const ColumnInfo& info = rs.column(c).info();
            const std::string type =
                info.type_name.empty()
                    ? std::string("nvarchar(max)")
                    : mssql_type_text(info.type_name, info.size, info.precision, info.scale);
            out += "    " + mssql_quote(info.name.empty() ? "column" + std::to_string(c + 1)
                                                         : info.name);
            out += " " + type + (info.nullable ? " NULL" : " NOT NULL");
            out += c + 1 < rs.column_count() ? ",\n" : "\n";
        }
        out += ");";
        return out;
    }

    if (sql_dialect() == QuoteStyle::anywhere) {
        // SQL Anywhere: o tipo ja' chega completo ("char(20)", "numeric(15,2)")
        // quando o servidor descreveu a consulta. A nulidade vai explicita:
        // sem ela depende de uma opcao da conexao.
        std::string out = "CREATE TABLE " + quote_if_needed(table) + " (\n";
        for (std::size_t c = 0; c < rs.column_count(); ++c) {
            const ColumnInfo& info = rs.column(c).info();
            out += "    " + quote_if_needed(info.name.empty()
                                                ? "column" + std::to_string(c + 1)
                                                : info.name);
            out += " " + (info.type_name.empty() ? std::string("long varchar")
                                                  : info.type_name);
            out += info.nullable ? " NULL" : " NOT NULL";
            out += c + 1 < rs.column_count() ? ",\n" : "\n";
        }
        out += ");";
        return out;
    }

    std::string out = "CREATE TABLE " + ident(table, mysql) + " (\n";
    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        const ColumnInfo& info = rs.column(c).info();
        std::string type = info.type_name;
        if (type.empty()) type = "text";

        // numeric com precisao conhecida, e os tipos de texto com tamanho.
        if ((type == "numeric" || type == "decimal") && info.precision > 0) {
            type += "(" + std::to_string(info.precision) + "," +
                    std::to_string(info.scale) + ")";
        } else if ((type == "varchar" || type == "bpchar" || type == "char") &&
                   info.size > 0) {
            type = (type == "bpchar" ? std::string("char") : type) + "(" +
                   std::to_string(info.size) + ")";
        }

        out += "    " + ident(info.name.empty() ? "column" + std::to_string(c + 1)
                                                : info.name, mysql);
        out += " " + type;
        if (!info.nullable) out += " NOT NULL";
        out += c + 1 < rs.column_count() ? ",\n" : "\n";
    }
    out += ");";
    return out;
}

// --- Filtro de objetos ------------------------------------------------------------------

bool mask_matches(std::string_view mask, std::string_view name) {
    // Casamento com `*`/`%` (qualquer sequencia) e `?`/`_`... nao: `_` e'
    // letra comum em nome de tabela. So' `*`, `%` e `?`.
    std::size_t m = 0, n = 0, star = std::string_view::npos, mark = 0;
    while (n < name.size()) {
        if (m < mask.size() && (mask[m] == '*' || mask[m] == '%')) {
            star = m++;
            mark = n;
        } else if (m < mask.size() &&
                   (mask[m] == '?' || lower(mask[m]) == lower(name[n]))) {
            ++m;
            ++n;
        } else if (star != std::string_view::npos) {
            m = star + 1;
            n = ++mark;
        } else {
            return false;
        }
    }
    while (m < mask.size() && (mask[m] == '*' || mask[m] == '%')) ++m;
    return m == mask.size();
}

bool filter_accepts(const ObjectFilter& filter, std::string_view name) {
    if (!filter.enabled) return true;

    if (!filter.include.empty()) {
        const bool included = std::any_of(
            filter.include.begin(), filter.include.end(),
            [name](const std::string& mask) { return mask_matches(mask, name); });
        if (!included) return false;
    }
    return std::none_of(
        filter.exclude.begin(), filter.exclude.end(),
        [name](const std::string& mask) { return mask_matches(mask, name); });
}

std::vector<std::string> split_masks(std::string_view text) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == ',' || text[i] == ';' || text[i] == '\n') {
            const std::string_view piece = trim(text.substr(start, i - start));
            if (!piece.empty()) out.emplace_back(piece);
            start = i + 1;
        }
    }
    return out;
}

std::string join_masks(const std::vector<std::string>& masks) {
    std::string out;
    for (const std::string& mask : masks) {
        if (!out.empty()) out += ", ";
        out += mask;
    }
    return out;
}

// --- Editor ------------------------------------------------------------------------------

std::string join_lines(std::string_view first, std::string_view second) {
    while (!first.empty() && (first.back() == ' ' || first.back() == '\t')) {
        first.remove_suffix(1);
    }
    while (!second.empty() && (second.front() == ' ' || second.front() == '\t')) {
        second.remove_prefix(1);
    }
    std::string out(first);
    if (!out.empty() && !second.empty()) out += ' ';
    out += second;
    return out;
}

std::string complete_word(std::string_view text, std::string_view prefix,
                          std::string_view after) {
    if (prefix.empty()) return {};

    // As palavras distintas que comecam com o prefixo, na ordem do texto.
    std::vector<std::string_view> words;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && !word_char(text[i])) ++i;
        const std::size_t begin = i;
        while (i < text.size() && word_char(text[i])) ++i;
        const std::string_view word = text.substr(begin, i - begin);
        if (word.size() <= prefix.size()) continue;

        bool same = true;
        for (std::size_t k = 0; k < prefix.size() && same; ++k) {
            same = lower(word[k]) == lower(prefix[k]);
        }
        if (same && std::find(words.begin(), words.end(), word) == words.end()) {
            words.push_back(word);
        }
    }
    if (words.empty()) return {};

    const auto current = std::find(words.begin(), words.end(), after);
    if (current == words.end() || current + 1 == words.end()) {
        return std::string(words.front());
    }
    return std::string(*(current + 1));
}

// --- Dashboard ----------------------------------------------------------------------------

namespace {

// Os graficos padrao do DBeaver para o PostgreSQL (plugin.xml de
// org.jkiss.dbeaver.ext.postgresql, extensao ui.dashboard): sessoes,
// transacoes por segundo, E/S de blocos -- mais os de tuplas e de tamanho.
constexpr DashboardChart kPostgresCharts[] = {
    {"pg.sessions", "Server sessions",
     "SELECT count(*) FILTER (WHERE state = 'active') AS active,\n"
     "       count(*) FILTER (WHERE state = 'idle') AS idle,\n"
     "       count(*) FILTER (WHERE state LIKE 'idle in%') AS \"idle in txn\",\n"
     "       count(*) FILTER (WHERE wait_event_type = 'Lock') AS waiting\n"
     "  FROM pg_catalog.pg_stat_activity WHERE backend_type = 'client backend'",
     false, ""},
    {"pg.tps", "Transactions per second",
     "SELECT sum(xact_commit) AS commits, sum(xact_rollback) AS rollbacks\n"
     "  FROM pg_catalog.pg_stat_database",
     true, "/s"},
    {"pg.block_io", "Block IO",
     "SELECT sum(blks_read) AS \"blocks read\", sum(blks_hit) AS \"cache hits\"\n"
     "  FROM pg_catalog.pg_stat_database",
     true, "/s"},
    {"pg.tuples", "Rows per second",
     "SELECT sum(tup_fetched) AS fetched, sum(tup_inserted) AS inserted,\n"
     "       sum(tup_updated) AS updated, sum(tup_deleted) AS deleted\n"
     "  FROM pg_catalog.pg_stat_database",
     true, "/s"},
    {"pg.locks", "Locks",
     "SELECT count(*) FILTER (WHERE granted) AS granted,\n"
     "       count(*) FILTER (WHERE NOT granted) AS waiting\n"
     "  FROM pg_catalog.pg_locks",
     false, ""},
    {"pg.temp", "Temporary files",
     "SELECT sum(temp_bytes) AS bytes FROM pg_catalog.pg_stat_database",
     true, "bytes/s"},
    {"pg.size", "Database size",
     "SELECT pg_catalog.pg_database_size(current_database()) AS bytes",
     false, "bytes"},
};

// MySQL: performance_schema.global_status, uma linha por variavel -- girada
// para colunas com MAX(CASE).
constexpr DashboardChart kMysqlCharts[] = {
    {"my.sessions", "Server sessions",
     "SELECT MAX(CASE WHEN VARIABLE_NAME = 'Threads_connected' THEN VARIABLE_VALUE END) AS connected,\n"
     "       MAX(CASE WHEN VARIABLE_NAME = 'Threads_running' THEN VARIABLE_VALUE END) AS running\n"
     "  FROM performance_schema.global_status",
     false, ""},
    {"my.queries", "Queries per second",
     "SELECT MAX(CASE WHEN VARIABLE_NAME = 'Questions' THEN VARIABLE_VALUE END) AS questions\n"
     "  FROM performance_schema.global_status",
     true, "/s"},
    {"my.traffic", "Network traffic",
     "SELECT MAX(CASE WHEN VARIABLE_NAME = 'Bytes_received' THEN VARIABLE_VALUE END) AS received,\n"
     "       MAX(CASE WHEN VARIABLE_NAME = 'Bytes_sent' THEN VARIABLE_VALUE END) AS sent\n"
     "  FROM performance_schema.global_status",
     true, "bytes/s"},
    {"my.innodb", "InnoDB rows per second",
     "SELECT MAX(CASE WHEN VARIABLE_NAME = 'Innodb_rows_read' THEN VARIABLE_VALUE END) AS `read`,\n"
     "       MAX(CASE WHEN VARIABLE_NAME = 'Innodb_rows_inserted' THEN VARIABLE_VALUE END) AS inserted,\n"
     "       MAX(CASE WHEN VARIABLE_NAME = 'Innodb_rows_updated' THEN VARIABLE_VALUE END) AS updated,\n"
     "       MAX(CASE WHEN VARIABLE_NAME = 'Innodb_rows_deleted' THEN VARIABLE_VALUE END) AS deleted\n"
     "  FROM performance_schema.global_status",
     true, "/s"},
};

// SQL Server: o plugin do DBeaver nao traz graficos para ele. Estes sao os
// equivalentes dos outros dois, lidos das DMVs (exigem VIEW SERVER STATE).
constexpr DashboardChart kMssqlCharts[] = {
    {"ms.sessions", "Server sessions",
     "SELECT SUM(CASE WHEN s.status = 'running' THEN 1 ELSE 0 END) AS running,\n"
     "       SUM(CASE WHEN s.status = 'sleeping' THEN 1 ELSE 0 END) AS sleeping,\n"
     "       (SELECT COUNT(*) FROM sys.dm_exec_requests r\n"
     "         WHERE r.blocking_session_id <> 0) AS blocked\n"
     "  FROM sys.dm_exec_sessions s WHERE s.is_user_process = 1",
     false, ""},
    {"ms.batches", "Queries per second",
     "SELECT MAX(cntr_value) AS batches FROM sys.dm_os_performance_counters\n"
     " WHERE counter_name = 'Batch Requests/sec'",
     true, "/s"},
    {"ms.tps", "Transactions per second",
     "SELECT MAX(cntr_value) AS transactions FROM sys.dm_os_performance_counters\n"
     " WHERE counter_name = 'Transactions/sec' AND instance_name = '_Total'",
     true, "/s"},
    {"ms.io", "Block IO",
     "SELECT SUM(num_of_bytes_read) AS [read], SUM(num_of_bytes_written) AS written\n"
     "  FROM sys.dm_io_virtual_file_stats(NULL, NULL)",
     true, "bytes/s"},
    {"ms.size", "Database size",
     "SELECT SUM(CAST(size AS bigint)) * 8192 AS bytes FROM sys.database_files",
     false, "bytes"},
};

// SQL Anywhere: o DBeaver nao tem perfil para ele. Os equivalentes, lidos das
// propriedades do servidor e do banco (property / db_property), que sao
// contadores desde a partida.
constexpr DashboardChart kSqlAnywhereCharts[] = {
    {"sa.sessions", "Server sessions",
     "SELECT (SELECT count(*) FROM sa_conn_info()) AS connections,\n"
     "       CAST(property('ActiveReq') AS integer) AS active,\n"
     "       (SELECT count(*) FROM sa_conn_info() WHERE BlockedOn <> 0) AS blocked",
     false, ""},
    {"sa.requests", "Queries per second",
     "SELECT CAST(property('Req') AS bigint) AS requests",
     true, "/s"},
    {"sa.tps", "Transactions per second",
     "SELECT CAST(db_property('Commit') AS bigint) AS commits,\n"
     "       CAST(db_property('Rlbk') AS bigint) AS rollbacks",
     true, "/s"},
    {"sa.io", "Block IO",
     "SELECT CAST(db_property('DiskRead') AS bigint) AS [pages read],\n"
     "       CAST(db_property('DiskWrite') AS bigint) AS [pages written]",
     true, "/s"},
    {"sa.cache", "Cache size",
     "SELECT CAST(property('CurrentCacheSize') AS bigint) * 1024 AS bytes",
     false, "bytes"},
    {"sa.size", "Database size",
     "SELECT CAST(db_property('FileSize') AS bigint)\n"
     "         * CAST(db_property('PageSize') AS bigint) AS bytes",
     false, "bytes"},
};

} // namespace

std::span<const DashboardChart> dashboard_catalog(std::string_view driver_id) {
    if (driver_id == "sqlanywhere") return kSqlAnywhereCharts;
    if (driver_id == "sqlserver" || driver_id == "mssql") return kMssqlCharts;
    if (driver_id == "mysql" || driver_id == "mariadb") return kMysqlCharts;
    return kPostgresCharts;
}

double dashboard_value(bool delta, double previous, double current, double seconds) {
    if (!delta) return current;
    if (seconds <= 0.0 || current < previous) return 0.0;
    return (current - previous) / seconds;
}

} // namespace otter::db
