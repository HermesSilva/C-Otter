#include "db/connection_import.hpp"

#include "base/aes.hpp"
#include "base/json.hpp"
#include "base/os_secret.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace otter::db {
namespace {

namespace fs = std::filesystem;

std::string read_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
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

// --- Leitor de SQLite --------------------------------------------------------------

class SqliteFile {
public:
    explicit SqliteFile(std::string_view bytes) : bytes_(bytes) {}

    [[nodiscard]] Status open() {
        if (bytes_.size() < 100 || std::memcmp(bytes_.data(), "SQLite format 3", 16) != 0) {
            return std::unexpected(Error{Errc::parse_error, "not a SQLite database"});
        }
        page_size_ = be16(16);
        if (page_size_ == 1) page_size_ = 65536;
        if (page_size_ < 512 || (page_size_ & (page_size_ - 1)) != 0) {
            return std::unexpected(Error{Errc::parse_error, "invalid SQLite page size"});
        }
        usable_ = page_size_ - static_cast<std::uint8_t>(bytes_[20]);
        if (be32(56) != 1) {
            return std::unexpected(Error{Errc::not_supported,
                                         "only UTF-8 SQLite databases are read"});
        }
        return {};
    }

    using Values = std::vector<std::pair<bool, std::string>>;   // (presente, texto)

    // Todas as linhas da arvore cuja raiz e' `root`, como valores posicionais.
    [[nodiscard]] Status scan(std::uint32_t root, std::vector<Values>& rows) {
        visited_ = 0;
        return walk(root, rows, 0);
    }

private:
    [[nodiscard]] std::uint32_t be16(std::size_t at) const {
        return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes_[at])) << 8) |
               static_cast<std::uint8_t>(bytes_[at + 1]);
    }
    [[nodiscard]] std::uint32_t be32(std::size_t at) const {
        return (be16(at) << 16) | be16(at + 2);
    }

    // Varint do SQLite: ate' 9 bytes, 7 bits por byte, o nono com 8.
    [[nodiscard]] bool varint(std::string_view data, std::size_t& at,
                              std::uint64_t& value) const {
        value = 0;
        for (int i = 0; i < 9; ++i) {
            if (at >= data.size()) return false;
            const auto byte = static_cast<std::uint8_t>(data[at++]);
            if (i == 8) {
                value = (value << 8) | byte;
                return true;
            }
            value = (value << 7) | (byte & 0x7F);
            if ((byte & 0x80) == 0) return true;
        }
        return true;
    }

    [[nodiscard]] std::string_view page(std::uint32_t number) const {
        const std::size_t start = static_cast<std::size_t>(number - 1) * page_size_;
        if (number == 0 || start + page_size_ > bytes_.size()) return {};
        return bytes_.substr(start, page_size_);
    }

    Status walk(std::uint32_t number, std::vector<Values>& rows, int depth) {
        // Arquivo corrompido nao pode virar laco: profundidade e total de
        // paginas tem teto.
        if (depth > 20 || ++visited_ > 200000) {
            return std::unexpected(Error{Errc::parse_error, "SQLite b-tree is too deep"});
        }
        const std::string_view data = page(number);
        if (data.empty()) {
            return std::unexpected(Error{Errc::parse_error, "SQLite page out of range"});
        }

        // A pagina 1 comeca com os 100 bytes do cabecalho do arquivo.
        const std::size_t header = number == 1 ? 100 : 0;
        const std::size_t base =
            static_cast<std::size_t>(number - 1) * page_size_;
        const auto type  = static_cast<std::uint8_t>(data[header]);
        const std::size_t cells = be16(base + header + 3);

        if (type == 0x05) {   // interior de tabela
            const std::size_t pointers = header + 12;
            for (std::size_t i = 0; i < cells; ++i) {
                const std::size_t at = pointers + i * 2;
                if (at + 2 > data.size()) break;
                const std::size_t cell = be16(base + at);
                if (cell + 4 > data.size()) continue;
                OTTER_RETURN_IF_ERROR(walk(be32(base + cell), rows, depth + 1));
            }
            return walk(be32(base + header + 8), rows, depth + 1);
        }
        if (type != 0x0D) {   // nao e' folha de tabela (indice, pagina livre)
            return {};
        }

        const std::size_t pointers = header + 8;
        for (std::size_t i = 0; i < cells; ++i) {
            const std::size_t at = pointers + i * 2;
            if (at + 2 > data.size()) break;
            std::size_t cell = be16(base + at);

            std::uint64_t payload_size = 0;
            std::uint64_t rowid = 0;
            if (!varint(data, cell, payload_size) || !varint(data, cell, rowid)) continue;

            // Quanto do registro cabe na folha; o resto vai em paginas de
            // transbordo encadeadas.
            const std::size_t usable = usable_;
            const std::size_t max_local = usable - 35;
            std::size_t local = static_cast<std::size_t>(payload_size);
            if (payload_size > max_local) {
                const std::size_t min_local = (usable - 12) * 32 / 255 - 23;
                const std::size_t wanted = min_local +
                    static_cast<std::size_t>((payload_size - min_local) % (usable - 4));
                local = wanted <= max_local ? wanted : min_local;
            }
            if (cell + local > data.size()) continue;

            std::string payload(data.substr(cell, local));
            if (payload_size > local) {
                if (cell + local + 4 > data.size()) continue;
                std::uint32_t next = be32(base + cell + local);
                std::size_t hops = 0;
                while (next != 0 && payload.size() < payload_size && ++hops < 100000) {
                    const std::string_view overflow = page(next);
                    if (overflow.empty()) break;
                    const std::size_t take = std::min<std::size_t>(
                        usable - 4, static_cast<std::size_t>(payload_size) - payload.size());
                    payload.append(overflow.substr(4, take));
                    next = be32(static_cast<std::size_t>(next - 1) * page_size_);
                }
            }
            rows.push_back(record(payload, rowid));
        }
        return {};
    }

    // Um registro: o cabecalho diz o tipo de cada coluna, o corpo traz os
    // valores na mesma ordem.
    [[nodiscard]] Values record(std::string_view payload, std::uint64_t rowid) const {
        Values values;
        std::size_t at = 0;
        std::uint64_t header_size = 0;
        if (!varint(payload, at, header_size) || header_size > payload.size()) return values;

        std::size_t body = static_cast<std::size_t>(header_size);
        bool first = true;
        while (at < header_size) {
            std::uint64_t serial = 0;
            if (!varint(payload, at, serial)) break;

            std::size_t size = 0;
            bool present = true;
            std::string text;
            if (serial == 0) {
                // NULL -- salvo a primeira coluna de uma tabela com INTEGER
                // PRIMARY KEY, que e' o proprio rowid.
                present = first;
                if (first) text = std::to_string(rowid);
            } else if (serial <= 6) {
                static constexpr std::size_t kSizes[] = {0, 1, 2, 3, 4, 6, 8};
                size = kSizes[serial];
                if (body + size > payload.size()) break;
                std::int64_t value =
                    (static_cast<std::uint8_t>(payload[body]) & 0x80) != 0 ? -1 : 0;
                for (std::size_t i = 0; i < size; ++i) {
                    value = static_cast<std::int64_t>(
                        (static_cast<std::uint64_t>(value) << 8) |
                        static_cast<std::uint8_t>(payload[body + i]));
                }
                text = std::to_string(value);
            } else if (serial == 7) {
                size    = 8;
                present = false;   // nenhum campo que lemos e' real
            } else if (serial == 8 || serial == 9) {
                text = serial == 8 ? "0" : "1";
            } else if (serial >= 12) {
                size = static_cast<std::size_t>((serial - 12) / 2);
                if (body + size > payload.size()) break;
                if (serial % 2 == 1) text = std::string(payload.substr(body, size));
                else                 present = false;   // BLOB
            } else {
                present = false;   // 10 e 11: reservados
            }
            values.emplace_back(present, std::move(text));
            body += size;
            first = false;
        }
        return values;
    }

    std::string_view bytes_;
    std::size_t      page_size_ = 0;
    std::size_t      usable_    = 0;
    std::size_t      visited_   = 0;
};

// Os nomes das colunas, do texto do CREATE TABLE guardado em sqlite_master.
std::vector<std::string> column_names(std::string_view sql) {
    std::vector<std::string> names;
    const std::size_t open = sql.find('(');
    const std::size_t close = sql.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close <= open) {
        return names;
    }
    const std::string_view body = sql.substr(open + 1, close - open - 1);

    int depth = 0;
    std::size_t start = 0;
    const auto take = [&names](std::string_view piece) {
        piece = trim(piece);
        if (piece.empty()) return;

        std::string name;
        if (piece.front() == '"' || piece.front() == '`' || piece.front() == '[') {
            const char end = piece.front() == '[' ? ']' : piece.front();
            const std::size_t stop = piece.find(end, 1);
            name = std::string(piece.substr(1, stop == std::string_view::npos
                                                   ? stop : stop - 1));
        } else {
            std::size_t i = 0;
            while (i < piece.size() &&
                   std::isspace(static_cast<unsigned char>(piece[i])) == 0) {
                ++i;
            }
            name = std::string(piece.substr(0, i));
            std::string upper = name;
            std::transform(upper.begin(), upper.end(), upper.begin(), [](char c) {
                return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            });
            // Restricoes de tabela nao sao colunas.
            if (upper == "PRIMARY" || upper == "UNIQUE" || upper == "CHECK" ||
                upper == "FOREIGN" || upper == "CONSTRAINT") {
                return;
            }
        }
        names.push_back(std::move(name));
    };
    for (std::size_t i = 0; i < body.size(); ++i) {
        if (body[i] == '(') ++depth;
        else if (body[i] == ')') --depth;
        else if (body[i] == ',' && depth == 0) {
            take(body.substr(start, i - start));
            start = i + 1;
        }
    }
    take(body.substr(start));
    return names;
}

std::string get(const SqliteRow& row, const char* key) {
    const auto found = row.find(key);
    return found == row.end() ? std::string{} : found->second;
}

// O texto entre <tag> e </tag>, a partir de `from`. Vazio se nao ha'.
std::string_view xml_text(std::string_view xml, std::string_view tag, std::size_t from = 0) {
    const std::string open  = "<" + std::string(tag) + ">";
    const std::string close = "</" + std::string(tag) + ">";
    const std::size_t start = xml.find(open, from);
    if (start == std::string_view::npos) return {};
    const std::size_t end = xml.find(close, start);
    if (end == std::string_view::npos) return {};
    return trim(xml.substr(start + open.size(), end - start - open.size()));
}

std::string target_key(const StoredProfile& stored) {
    const ConnectionProfile& p = stored.profile;
    return stored.provider + "|" + p.host + "|" + std::to_string(p.port) + "|" +
           p.database + "|" + p.user;
}

} // namespace

Result<std::vector<SqliteRow>> read_sqlite_table(std::string_view file_bytes,
                                                 std::string_view table) {
    SqliteFile file(file_bytes);
    OTTER_RETURN_IF_ERROR(file.open());

    // sqlite_master mora na pagina 1: type, name, tbl_name, rootpage, sql.
    std::vector<SqliteFile::Values> master;
    OTTER_RETURN_IF_ERROR(file.scan(1, master));

    std::uint32_t root = 0;
    std::string   sql;
    for (const SqliteFile::Values& entry : master) {
        if (entry.size() < 5 || entry[0].second != "table" || entry[1].second != table) {
            continue;
        }
        root = static_cast<std::uint32_t>(std::strtoul(entry[3].second.c_str(), nullptr, 10));
        sql  = entry[4].second;
    }
    if (root == 0) {
        return std::unexpected(
            Error{Errc::not_found, "table not found: " + std::string(table)});
    }

    const std::vector<std::string> names = column_names(sql);
    std::vector<SqliteFile::Values> raw;
    OTTER_RETURN_IF_ERROR(file.scan(root, raw));

    std::vector<SqliteRow> rows;
    rows.reserve(raw.size());
    for (const SqliteFile::Values& values : raw) {
        SqliteRow row;
        for (std::size_t c = 0; c < values.size() && c < names.size(); ++c) {
            if (values[c].first) row[names[c]] = values[c].second;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

// --- pgAdmin -----------------------------------------------------------------------

namespace {

int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int base64_value(char c) noexcept {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// Base64 padrao, com ou sem o '=' do fim. Falso se houver caractere de fora.
bool decode_base64(std::string_view text, std::vector<std::uint8_t>& out) {
    while (!text.empty() && text.back() == '=') text.remove_suffix(1);

    std::uint32_t buffer = 0;
    int bits = 0;
    for (const char c : text) {
        const int value = base64_value(c);
        if (value < 0) return false;
        buffer = (buffer << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xFF));
        }
    }
    return true;
}

// Texto que pode ser uma senha: UTF-8 valido, sem caractere de controle.
bool plausible_secret(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) return false;
    for (std::size_t i = 0; i < bytes.size();) {
        const std::uint8_t lead = bytes[i];
        if (lead < 0x20 || lead == 0x7F) return false;
        std::size_t extra = 0;
        if (lead < 0x80) extra = 0;
        else if ((lead & 0xE0) == 0xC0 && lead >= 0xC2) extra = 1;
        else if ((lead & 0xF0) == 0xE0) extra = 2;
        else if ((lead & 0xF8) == 0xF0 && lead <= 0xF4) extra = 3;
        else return false;
        if (extra > 0 && i + extra >= bytes.size()) return false;   // cortado no meio
        for (std::size_t k = 1; k <= extra; ++k) {
            if ((bytes[i + k] & 0xC0) != 0x80) return false;
        }
        i += extra + 1;
    }
    return true;
}

} // namespace

Result<std::string> pgadmin_decrypt(std::string_view stored, std::string_view master_key) {
    if (stored.empty()) return fail(Errc::invalid_argument, "no saved password");
    if (master_key.empty()) return fail(Errc::invalid_argument, "no pgAdmin key");

    // 1. O modelo do pgAdmin grava a coluna em HEXADECIMAL (PgAdminDbBinaryString):
    //    o que esta' no banco e' o hex do texto base64. Bancos antigos tem o
    //    base64 direto -- por isso so' desfaz o hex quando o valor inteiro e' hex.
    std::string text(stored);
    const bool is_hex = text.size() % 2 == 0 &&
                        std::all_of(text.begin(), text.end(),
                                    [](char c) { return hex_value(c) >= 0; });
    if (is_hex) {
        std::string unhexed;
        unhexed.reserve(text.size() / 2);
        for (std::size_t i = 0; i < text.size(); i += 2) {
            unhexed.push_back(static_cast<char>(hex_value(text[i]) * 16 + hex_value(text[i + 1])));
        }
        text = std::move(unhexed);
    }

    // 2. Base64 de IV || texto cifrado.
    std::vector<std::uint8_t> cipher;
    if (!decode_base64(text, cipher)) {
        return fail(Errc::invalid_argument, "the saved password is not base64");
    }

    // 3. A chave, ajustada como o `pad` do pgAdmin.
    std::vector<std::uint8_t> key(master_key.begin(), master_key.end());
    if (key.size() > 32) key.resize(32);
    if (key.size() != 16 && key.size() != 24 && key.size() != 32) key.resize(32, '}');

    OTTER_ASSIGN_OR_RETURN(auto plain, crypto::aes_cfb8_decrypt(cipher, key));

    // 4. CFB8 com a chave errada devolve lixo, sem erro: uma "senha" de bytes
    //    aleatorios gravada no perfil seria pior que senha nenhuma.
    if (!plausible_secret(plain)) {
        return fail(Errc::invalid_argument,
                    "the pgAdmin key does not decrypt this password");
    }
    return std::string(plain.begin(), plain.end());
}

std::string pgadmin_master_key() {
    // KEY_RING_SERVICE_NAME e KEY_RING_USER_NAME de pgadmin/utils/constants.py.
    auto key = read_keyring_secret("pgAdmin4", "pgadmin4-master-password");
    return key ? *key : std::string{};
}

std::vector<StoredProfile> pgadmin_profiles(std::string_view database_bytes,
                                            std::string_view master_key) {
    std::vector<StoredProfile> out;

    const auto servers = read_sqlite_table(database_bytes, "server");
    if (!servers) return out;

    for (const SqliteRow& row : *servers) {
        const std::string host = get(row, "host");
        if (host.empty()) continue;   // servidor definido por `service`, sem host

        StoredProfile stored;
        stored.provider = "postgresql";
        stored.driver   = "postgres-jdbc";

        ConnectionProfile& profile = stored.profile;
        profile.driver_id = "postgresql";
        profile.name      = get(row, "name");
        profile.host      = host;
        profile.database  = get(row, "maintenance_db");
        profile.user      = get(row, "username");
        profile.description = get(row, "comment");
        profile.folder    = "pgAdmin";
        profile.postgres.session_role = get(row, "role");
        // O pgAdmin lista todos os bancos do servidor; aqui e' a caixa
        // "Show all databases".
        profile.postgres.show_non_default_databases = true;

        const long port = std::strtol(get(row, "port").c_str(), nullptr, 10);
        if (port > 0 && port <= 65535) profile.port = static_cast<std::uint16_t>(port);

        // A senha, quando o pgAdmin a guardou e a chave dele esta' ao alcance.
        // Uma que nao decifra fica de fora, e a conexao entra sem senha.
        if (auto password = pgadmin_decrypt(get(row, "password"), master_key)) {
            profile.password      = std::move(*password);
            profile.save_password = true;
        }

        if (get(row, "use_ssh_tunnel") == "1") {
            profile.ssh.enabled = true;
            profile.ssh.host    = get(row, "tunnel_host");
            profile.ssh.user    = get(row, "tunnel_username");
            const long tunnel_port =
                std::strtol(get(row, "tunnel_port").c_str(), nullptr, 10);
            if (tunnel_port > 0 && tunnel_port <= 65535) {
                profile.ssh.port = static_cast<std::uint16_t>(tunnel_port);
            }
            // 0 = senha, 1 = arquivo de identidade.
            if (get(row, "tunnel_authentication") == "1") {
                profile.ssh.auth             = SshAuthType::public_key;
                profile.ssh.private_key_path = get(row, "tunnel_identity_file");
            }
        }

        // {"sslmode": "prefer", "connect_timeout": 10}
        if (const auto params = json::parse(get(row, "connection_params"));
            params && params->is_object()) {
            const std::string_view mode = (*params)["sslmode"].as_string();
            if (!mode.empty()) {
                profile.ssl.mode    = ssl_mode_from_string(mode);
                // `prefer` e `allow` nao EXIGEM TLS: marcar a caixa exigiria.
                profile.ssl.enabled = profile.ssl.mode == SslMode::require ||
                                      profile.ssl.mode == SslMode::verify_ca ||
                                      profile.ssl.mode == SslMode::verify_full;
            }
            const std::int64_t timeout = (*params)["connect_timeout"].as_int(0);
            if (timeout > 0) profile.connect_timeout = std::chrono::seconds(timeout);
        }

        resolve_driver(stored);
        out.push_back(std::move(stored));
    }
    return out;
}

// --- SSMS ---------------------------------------------------------------------------

namespace {

// Database Engine. Os outros tipos de servidor do SSMS (Analysis, Reporting,
// Integration Services) nao sao bancos SQL.
constexpr std::string_view kDatabaseEngine = "8c91a03d-f9b4-46c0-a305-b5dcc79ff907";

} // namespace

std::string ssms_credential_target(std::string_view major_version, std::string_view instance,
                                   std::string_view user, std::string_view method) {
    return "Microsoft:SSMS:" + std::string(major_version) + ":" + std::string(instance) +
           ":" + std::string(user) + ":" + std::string(kDatabaseEngine) + ":" +
           std::string(method);
}

std::vector<StoredProfile> ssms_profiles(std::string_view xml,
                                         const SsmsSecretLookup& secret) {
    constexpr std::string_view kOpen  = "<ServerConnectionItem>";
    constexpr std::string_view kClose = "</ServerConnectionItem>";

    std::vector<StoredProfile> out;
    std::size_t at = 0;
    while ((at = xml.find(kOpen, at)) != std::string_view::npos) {
        const std::size_t end = xml.find(kClose, at);
        if (end == std::string_view::npos) break;
        const std::string_view block = xml.substr(at, end - at);
        at = end + kClose.size();

        const std::string_view type = xml_text(block, "ServerType");
        if (!type.empty() && type != kDatabaseEngine) continue;

        std::string instance(xml_text(block, "Instance"));
        if (instance.empty()) continue;

        StoredProfile stored;
        stored.provider = "sqlserver";
        stored.driver   = "microsoft";   // o id do driver da Microsoft no DBeaver

        ConnectionProfile& profile = stored.profile;
        profile.name   = instance;
        profile.folder = "SQL Server Management Studio";
        profile.port   = 1433;
        profile.user   = std::string(xml_text(block, "UserName"));
        // `<Database />` vazio e' o comum; mais abaixo no bloco ha' uma LISTA
        // `<Databases>` com elementos -- so' vale um valor simples.
        if (const std::string_view database = xml_text(block, "Database");
            database.find('<') == std::string_view::npos) {
            profile.database = std::string(database);
        }

        // A senha, quando o SSMS a guardou (so' com usuario e senha: metodo
        // diferente de 0). A instancia vai como esta' no XML -- e' assim que
        // o alvo da credencial foi escrito.
        const std::string method(xml_text(block, "AuthenticationMethod"));
        if (secret && method != "0" && !profile.user.empty()) {
            if (std::string password = secret(instance, profile.user, method);
                !password.empty()) {
                profile.password      = std::move(password);
                profile.save_password = true;
            }
        }

        // "tcp:host,porta" e "host,porta"; "host\instancia" fica inteiro no
        // host -- a instancia nomeada e' resolvida pelo SQL Browser, nao por
        // uma porta que se possa adivinhar.
        if (instance.starts_with("tcp:")) instance.erase(0, 4);
        if (const std::size_t comma = instance.find(','); comma != std::string::npos) {
            const long port = std::strtol(instance.c_str() + comma + 1, nullptr, 10);
            if (port > 0 && port <= 65535) profile.port = static_cast<std::uint16_t>(port);
            instance.erase(comma);
        }
        profile.host = instance;

        // 0 = autenticacao do Windows: nao ha' usuario a guardar.
        if (xml_text(block, "AuthenticationMethod") == "0") {
            profile.user.clear();
            profile.auth_model = AuthModel::windows;
        }

        resolve_driver(stored);
        out.push_back(std::move(stored));
    }
    return out;
}

// --- Primeira execucao ----------------------------------------------------------------

bool store_is_fresh(const StoreLocation& location) {
    std::error_code ec;
    const fs::path directory(location.directory);
    if (!fs::exists(directory, ec)) return true;
    return !fs::exists(location.data_sources, ec) &&
           !fs::exists(directory / "settings.json", ec);
}

std::string tool_folder(std::string_view tool, std::string_view original) {
    if (original.empty() || original == tool) return std::string(tool);
    // Ja' esta' sob o grupo (uma segunda importacao do mesmo arquivo).
    if (original.size() > tool.size() && original.starts_with(tool) &&
        original[tool.size()] == '/') {
        return std::string(original);
    }
    return std::string(tool) + "/" + std::string(original);
}

ExternalProfiles external_profiles() {
    ExternalProfiles found;
    std::set<std::string> seen;

    const auto add = [&](std::vector<StoredProfile> profiles, std::size_t& counter) {
        for (StoredProfile& stored : profiles) {
            if (!seen.insert(target_key(stored)).second) continue;
            // O id e' do arquivo de origem; aqui a conexao e' nova.
            stored.id.clear();
            stored.raw_json.clear();
            found.profiles.push_back(std::move(stored));
            ++counter;
        }
    };

    // DBeaver: cada workspace/projeto.
    for (const StoreLocation& location : dbeaver_store_locations()) {
        if (auto profiles = load_profiles(location)) {
            // No grupo "DBeaver", como as do pgAdmin e do SSMS vao para os
            // delas: na raiz, misturavam-se com as criadas aqui.
            for (StoredProfile& stored : *profiles) {
                stored.profile.folder = tool_folder("DBeaver", stored.profile.folder);
            }
            add(std::move(*profiles), found.from_dbeaver);
        }
    }

    // Onde as OUTRAS ferramentas guardam a configuracao delas -- e' a pasta
    // do usuario, sim: a regra do produto portatil (ADR 0020) vale para o que
    // o C-Otter grava, e aqui so' se le'.
    const fs::path root(user_config_directory());
    std::error_code ec;

    // pgAdmin 4: %APPDATA%\pgAdmin\pgadmin4.db (Linux: ~/.pgadmin).
    for (const fs::path& candidate :
         {root / "pgAdmin" / "pgadmin4.db", root / ".pgadmin" / "pgadmin4.db"}) {
        if (!fs::exists(candidate, ec)) continue;
        add(pgadmin_profiles(read_bytes(candidate), pgadmin_master_key()),
            found.from_pgadmin);
    }

    // SSMS 18 a 21: uma pasta por versao, a mais nova por ultimo.
    const fs::path ssms = root / "Microsoft" / "SQL Server Management Studio";
    if (fs::exists(ssms, ec)) {
        std::vector<fs::path> versions;
        for (const auto& entry : fs::directory_iterator(ssms, ec)) {
            if (entry.is_directory(ec)) versions.push_back(entry.path());
        }
        std::sort(versions.rbegin(), versions.rend());
        for (const fs::path& version : versions) {
            const fs::path settings = version / "UserSettings.xml";
            if (!fs::exists(settings, ec)) continue;

            // "20.0" -> "20". A credencial pode ter sido gravada por outra
            // versao (o SSMS 21 herda a lista do 20, e as senhas ficam sob o
            // 20): tenta a da pasta e depois as vizinhas.
            std::string major = version.filename().string();
            if (const std::size_t dot = major.find('.'); dot != std::string::npos) {
                major.erase(dot);
            }
            const SsmsSecretLookup vault = [major](std::string_view instance,
                                                   std::string_view user,
                                                   std::string_view method) {
                for (const std::string& candidate :
                     {major, std::string("22"), std::string("21"), std::string("20")}) {
                    auto password = read_generic_credential(
                        ssms_credential_target(candidate, instance, user, method));
                    if (password && !password->empty()) return *password;
                }
                return std::string{};
            };
            add(ssms_profiles(read_bytes(settings), vault), found.from_ssms);
        }
    }

    make_names_unique(found.profiles);
    return found;
}

} // namespace otter::db
