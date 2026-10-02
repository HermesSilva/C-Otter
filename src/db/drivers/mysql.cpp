// Driver MySQL/MariaDB sobre lib/mywire (ADR 0009). Adapta o protocolo ao
// contrato Holt/Driver, como postgres.cpp faz para o pgwire.
//
// As diferencas de comportamento em relacao ao PostgreSQL estao comentadas
// onde aparecem -- sao elas que impedem copiar o driver anterior:
//
//   - autocommit e' uma VARIAVEL do servidor, nao um estado implicito
//   - DDL faz commit implicito; nao existe DDL transacional
//   - nao ha' schema dentro do banco: "database" e "schema" sao a mesma coisa
//   - o estado da transacao vem em flags do pacote OK, nao num ReadyForQuery
#include "db/drivers/mysql.hpp"

#include "mywire/connection.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <string>

namespace otter::db {
namespace {

DataKind kind_from_field(const mywire::FieldDescription& field) noexcept {
    using mywire::FieldType;

    switch (field.type) {
        case FieldType::tiny:
        case FieldType::short_:
        case FieldType::long_:
        case FieldType::longlong:
        case FieldType::int24:
        case FieldType::year:
            return DataKind::integer;

        case FieldType::float_:
        case FieldType::double_:
            return DataKind::floating;

        case FieldType::decimal:
        case FieldType::newdecimal:
            return DataKind::numeric;

        case FieldType::date:
        case FieldType::newdate:
            return DataKind::date;

        case FieldType::time:
            return DataKind::time;

        case FieldType::timestamp:
        case FieldType::datetime:
            return DataKind::timestamp;

        case FieldType::json:
            return DataKind::json;

        case FieldType::geometry:
            return DataKind::geometry;

        case FieldType::bit:
            return DataKind::binary;

        // BLOB e TEXT chegam com o MESMO tipo. So' o charset 63 (binary)
        // distingue: mostrar um BLOB como texto encheria a grade de bytes
        // ilegiveis, e mostrar um TEXT como binario esconderia o conteudo.
        case FieldType::tiny_blob:
        case FieldType::medium_blob:
        case FieldType::long_blob:
        case FieldType::blob:
        case FieldType::var_string:
        case FieldType::string:
        case FieldType::varchar:
            return field.is_binary() ? DataKind::binary : DataKind::string;

        case FieldType::enum_:
        case FieldType::set:
            return DataKind::string;

        case FieldType::null:
            return DataKind::unknown;
    }
    return DataKind::unknown;
}

std::string type_name_for(const mywire::FieldDescription& field) {
    using mywire::FieldType;

    switch (field.type) {
        case FieldType::tiny:       return "tinyint";
        case FieldType::short_:     return "smallint";
        case FieldType::int24:      return "mediumint";
        case FieldType::long_:      return "int";
        case FieldType::longlong:   return "bigint";
        case FieldType::float_:     return "float";
        case FieldType::double_:    return "double";
        case FieldType::decimal:
        case FieldType::newdecimal: return "decimal";
        case FieldType::date:
        case FieldType::newdate:    return "date";
        case FieldType::time:       return "time";
        case FieldType::datetime:   return "datetime";
        case FieldType::timestamp:  return "timestamp";
        case FieldType::year:       return "year";
        case FieldType::bit:        return "bit";
        case FieldType::json:       return "json";
        case FieldType::enum_:      return "enum";
        case FieldType::set:        return "set";
        case FieldType::geometry:   return "geometry";
        case FieldType::tiny_blob:  return field.is_binary() ? "tinyblob"   : "tinytext";
        case FieldType::medium_blob:return field.is_binary() ? "mediumblob" : "mediumtext";
        case FieldType::long_blob:  return field.is_binary() ? "longblob"   : "longtext";
        case FieldType::blob:       return field.is_binary() ? "blob"       : "text";
        case FieldType::var_string:
        case FieldType::varchar:    return field.is_binary() ? "varbinary"  : "varchar";
        case FieldType::string:     return field.is_binary() ? "binary"     : "char";
        case FieldType::null:       return "null";
    }
    return "unknown";
}

class MysqlHolt final : public Holt {
public:
    explicit MysqlHolt(mywire::Connection conn) : conn_(std::move(conn)) {}

    [[nodiscard]] bool is_open() const noexcept override { return conn_.is_open(); }
    void close() override { conn_.close(); }

    [[nodiscard]] Result<ResultSet> query(std::string_view sql) override {
        return run(sql, /*internal=*/false);
    }

    [[nodiscard]] Status execute(std::string_view sql) override {
        auto result = run(sql, /*internal=*/false);
        if (!result) return std::unexpected(result.error());
        return {};
    }

    Status cancel() override { return conn_.cancel_current_query(); }

    // --- Transacoes ---------------------------------------------------------
    //
    // Aqui o autocommit e' uma VARIAVEL do servidor (`SET autocommit`), ao
    // contrario do PostgreSQL, onde nao existe modo e tudo depende de um
    // BEGIN explicito. Desligar basta -- nao e' preciso reabrir transacao
    // depois de cada commit, porque o servidor ja' abre a proxima sozinho.

    [[nodiscard]] bool auto_commit() const noexcept override {
        return auto_commit_;
    }

    Status set_auto_commit(bool enabled) override {
        if (enabled == auto_commit_) return {};

        // Saindo do modo manual: confirma o que estava pendente. Commit, e
        // nao rollback -- descartar trabalho do usuario sem pedir seria pior.
        if (enabled && (conn_.server_status() & mywire::status_in_transaction)) {
            OTTER_RETURN_IF_ERROR(run_silent("COMMIT"));
        }

        OTTER_RETURN_IF_ERROR(
            run_silent(enabled ? "SET autocommit=1" : "SET autocommit=0"));
        auto_commit_ = enabled;
        clear_changes();
        return {};
    }

    [[nodiscard]] TxnState txn_state() const noexcept override {
        // O MySQL nao tem o estado "failed" do PostgreSQL: um erro dentro de
        // transacao aborta a INSTRUCAO, nao a transacao, e as seguintes
        // continuam valendo. Por isso so' existem dois estados aqui.
        return (conn_.server_status() & mywire::status_in_transaction)
                   ? TxnState::active
                   : TxnState::idle;
    }

    Status commit() override {
        OTTER_RETURN_IF_ERROR(run_silent("COMMIT"));
        clear_changes();
        return {};
    }

    Status rollback() override {
        OTTER_RETURN_IF_ERROR(run_silent("ROLLBACK"));
        clear_changes();
        return {};
    }

    Status savepoint(std::string_view name) override {
        return run_silent("SAVEPOINT " + quote_identifier(name));
    }

    Status rollback_to(std::string_view name) override {
        return run_silent("ROLLBACK TO SAVEPOINT " + quote_identifier(name));
    }

    Status release_savepoint(std::string_view name) override {
        return run_silent("RELEASE SAVEPOINT " + quote_identifier(name));
    }

    [[nodiscard]] Result<IsolationLevel> isolation_level() override {
        // O nome da variavel mudou no 8.0: `tx_isolation` foi removida em
        // favor de `transaction_isolation`. Perguntar a errada devolve erro
        // 1193 ("unknown system variable"), entao a versao decide.
        const bool modern = !conn_.is_mariadb() && conn_.version_number() >= 80000;
        OTTER_ASSIGN_OR_RETURN(
            auto rs,
            run(modern ? "SELECT @@transaction_isolation" : "SELECT @@tx_isolation",
                true));

        if (rs.row_count() == 0) return IsolationLevel::repeatable_read;

        const std::string_view value = rs.text(0, 0);
        if (value == "READ-UNCOMMITTED") return IsolationLevel::read_uncommitted;
        if (value == "READ-COMMITTED")   return IsolationLevel::read_committed;
        if (value == "SERIALIZABLE")     return IsolationLevel::serializable;

        // REPEATABLE READ e' o padrao do InnoDB -- diferente do
        // READ COMMITTED do PostgreSQL.
        return IsolationLevel::repeatable_read;
    }

    Status set_isolation_level(IsolationLevel level) override {
        return run_silent("SET SESSION TRANSACTION ISOLATION LEVEL " +
                          std::string(to_string(level)));
    }

    [[nodiscard]] Capabilities capabilities() const noexcept override {
        Capabilities caps;
        caps.transactions = true;
        caps.savepoints   = true;

        // DDL NAO e' transacional: CREATE TABLE faz commit implicito e leva
        // junto tudo que estava pendente. Declarar `true` aqui faria a UI
        // oferecer um rollback que nao desfaz nada -- um botao que finge
        // funcionar (diretiva 6).
        caps.ddl_in_transaction = false;

        caps.server_cursors  = false;   // exige protocolo preparado
        caps.binary_transfer = false;   // COM_QUERY devolve tudo como texto
        caps.arrays          = false;   // MySQL nao tem tipo array
        caps.cancel_query    = true;    // via KILL QUERY em outra conexao
        caps.explain_plan    = true;
        return caps;
    }

    [[nodiscard]] std::string server_version() const override {
        return conn_.server_version();
    }

    [[nodiscard]] std::string secure_channel() const override {
        if (!conn_.tls_active()) return {};
        const auto& info = conn_.tls_info();
        std::string text = info.protocol;
        if (!info.cipher.empty()) text += ", " + info.cipher;
        return text;
    }

    // No MySQL o banco corrente FAZ o papel do schema: nao existe nivel
    // intermediario. Devolver "public" aqui, como no PostgreSQL, apontaria
    // para um schema que nao existe.
    [[nodiscard]] std::string current_schema() const override {
        const std::lock_guard<std::mutex> lock(database_mutex_);
        return database_;
    }

    // Propriedades do driver como variaveis de sessao.
    //
    // O handshake do MySQL nao carrega parametros arbitrarios, ao contrario
    // da StartupMessage do PostgreSQL -- entao sao aplicadas logo apos
    // conectar, antes de qualquer consulta do usuario.
    //
    // Um nome invalido FALHA a conexao: aplicar metade e seguir daria ao
    // usuario uma sessao que ele acha configurada e nao esta'.
    Status apply_driver_properties(
        const std::map<std::string, std::string>& properties) {
        for (const auto& [name, value] : properties) {
            if (name.empty()) continue;

            // Nome como identificador, valor como literal. Nao ha' elevacao
            // de privilegio a impedir -- e' a propria sessao do usuario --,
            // mas citar evita que um valor com aspas quebre a sintaxe.
            std::string quoted_value;
            quoted_value.reserve(value.size() + 2);
            quoted_value.push_back('\'');
            for (const char c : value) {
                if (c == '\'' || c == '\\') quoted_value.push_back('\\');
                quoted_value.push_back(c);
            }
            quoted_value.push_back('\'');

            OTTER_RETURN_IF_ERROR(run_silent(
                "SET @@" + quote_identifier(name) + " = " + quoted_value));
        }
        return {};
    }

    [[nodiscard]] Result<ResultSet> query_internal(std::string_view sql) override {
        return run(sql, /*internal=*/true);
    }

private:
    Status run_silent(std::string_view sql) {
        auto result = run(sql, /*internal=*/true);
        if (!result) return std::unexpected(result.error());
        return {};
    }

    // Identificador delimitado. O MySQL usa crase, nao aspas duplas -- aspas
    // duplas so' funcionam com ANSI_QUOTES ligado, que nao e' o padrao.
    static std::string quote_identifier(std::string_view name) {
        std::string out;
        out.reserve(name.size() + 2);
        out.push_back('`');
        for (char c : name) {
            if (c == '`') out.push_back('`');
            out.push_back(c);
        }
        out.push_back('`');
        return out;
    }

    Result<ResultSet> run(std::string_view sql, bool internal) {
        if (!conn_.is_open()) return fail(Errc::closed, "conexão fechada");

        const auto started = std::chrono::steady_clock::now();

        ResultSetBuilder builder;
        std::vector<mywire::FieldDescription> fields;
        bool        columns_ready = false;
        std::size_t rows = 0;

        auto add_columns = [&] {
            for (const mywire::FieldDescription& field : fields) {
                ColumnInfo info;
                info.name      = field.name;
                info.type_oid  = static_cast<std::uint32_t>(field.type);
                info.kind      = kind_from_field(field);
                info.type_name = type_name_for(field);
                info.size      = static_cast<std::int32_t>(
                    std::min<std::uint32_t>(field.length, 0x7FFFFFFFu));
                info.scale     = static_cast<std::int16_t>(field.decimals);

                // O MySQL nao manda OID de tabela. Para a grade editavel o que
                // identifica a origem e' o par (banco, tabela real) -- e' a
                // tabela REAL, nao o apelido: um UPDATE contra o apelido nao
                // existe.
                info.source_table = field.table;
                info.source_schema = field.database;
                info.source_column_name = field.original_name;

                builder.add_column(std::move(info));
            }
        };

        const Status status = conn_.query(
            sql,
            [&](std::span<const mywire::RawValue> row) {
                if (!columns_ready) {
                    add_columns();
                    columns_ready = true;
                }
                for (std::size_t c = 0; c < row.size(); ++c) {
                    if (row[c].null) builder.append_null(c);
                    else             builder.append(c, row[c].data);
                }
                ++rows;
            },
            &fields);

        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started);

        if (!status) {
            record(QueryLog{std::string(sql), elapsed, 0, internal, true,
                            status.error().message()});
            return std::unexpected(status.error());
        }

        // Resultado vazio ainda precisa das colunas, para a grade saber o
        // formato.
        if (!columns_ready) add_columns();

        builder.set_row_count(rows);
        builder.set_affected_rows(conn_.last_affected_rows());

        if (!internal && !auto_commit_ && conn_.last_affected_rows() > 0 &&
            rows == 0) {
            note_change();
        }

        record(QueryLog{std::string(sql), elapsed, rows, internal, false, {}});

        // Sem isto o banco corrente ficava o do perfil para sempre: a aba e a
        // barra de status diziam um banco enquanto as consultas iam a outro.
        // A barra de status le' current_schema() a cada quadro, de outra
        // thread: daqui a escrita passa pelo mutex.
        if (std::string target = mysql_use_target(sql); !target.empty()) {
            const std::lock_guard<std::mutex> lock(database_mutex_);
            database_ = std::move(target);
        }
        return builder.take();
    }

    mywire::Connection conn_;
    std::string        database_;
    mutable std::mutex database_mutex_;
    bool               auto_commit_ = true;

    friend class MysqlDriver;
};

class MysqlDriver final : public Driver {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "mysql"; }
    [[nodiscard]] std::string_view display_name() const noexcept override {
        return "MySQL / MariaDB";
    }
    [[nodiscard]] std::uint16_t default_port() const noexcept override { return 3306; }

    [[nodiscard]] Result<std::unique_ptr<Holt>> connect(
        const ConnConfig& config) override {
        mywire::ConnectParams params;
        params.host     = config.host;
        params.port     = config.port != 0 ? config.port : 3306;
        params.database = config.database;
        params.user     = config.user;
        params.password = config.password;
        params.timeout  = std::chrono::duration_cast<std::chrono::milliseconds>(
            config.connect_timeout);

        params.proxy.host     = config.proxy_host;
        params.proxy.port     = config.proxy_port;
        params.proxy.user     = config.proxy_user;
        params.proxy.password = config.proxy_password;

        // `require` implica exigir: se o servidor nao oferecer TLS, a conexao
        // falha em vez de cair em claro sem avisar.
        params.use_tls     = config.ssl_enabled();
        params.require_tls = config.ssl_enabled();
        params.allow_invalid_certificate = !config.ssl_verifies_certificate();

        OTTER_ASSIGN_OR_RETURN(auto conn, mywire::Connection::connect(params));

        auto holt = std::unique_ptr<MysqlHolt>(new MysqlHolt(std::move(conn)));
        holt->database_ = config.database;

        // Propriedades do driver: no MySQL viram `SET @@nome = valor`.
        //
        // Nao ha' equivalente a' StartupMessage do PostgreSQL -- o handshake
        // do MySQL nao carrega parametros arbitrarios --, entao sao aplicadas
        // logo apos conectar, antes de qualquer consulta do usuario.
        //
        // Um nome invalido FALHA a conexao, em vez de ser ignorado: aplicar
        // metade das propriedades e seguir daria ao usuario uma sessao que
        // ele acha configurada e nao esta'.
        OTTER_RETURN_IF_ERROR(
            holt->apply_driver_properties(config.driver_properties));

        // Nivel de isolamento do perfil, pela mesma razao: falhar nomeando o
        // nivel e' melhor que uma sessao que ignorou o pedido.
        if (config.isolation_level.has_value()) {
            OTTER_RETURN_IF_ERROR(
                holt->set_isolation_level(*config.isolation_level));
        }

        return std::unique_ptr<Holt>(std::move(holt));
    }
};

} // namespace

Driver& mysql_driver() {
    static MysqlDriver driver;
    return driver;
}

std::string mysql_use_target(std::string_view sql) {
    const auto is_space = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    std::size_t i = 0;
    while (i < sql.size() && is_space(sql[i])) ++i;
    if (sql.size() - i < 4) return {};
    const auto lower = [](char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    };
    if (lower(sql[i]) != 'u' || lower(sql[i + 1]) != 's' || lower(sql[i + 2]) != 'e' ||
        !is_space(sql[i + 3])) {
        return {};
    }
    i += 4;
    while (i < sql.size() && is_space(sql[i])) ++i;

    std::string name;
    if (i < sql.size() && sql[i] == '`') {
        // Crase dobrada dentro do nome e' uma crase do nome.
        for (++i; i < sql.size(); ++i) {
            if (sql[i] == '`') {
                if (i + 1 < sql.size() && sql[i + 1] == '`') { name.push_back('`'); ++i; continue; }
                ++i;
                break;
            }
            name.push_back(sql[i]);
        }
    } else {
        while (i < sql.size() && !is_space(sql[i]) && sql[i] != ';') name.push_back(sql[i++]);
    }
    // So' espaco e um ';' depois do nome: "USE a; SELECT 1" nao e' um USE so'.
    while (i < sql.size() && (is_space(sql[i]) || sql[i] == ';')) ++i;
    return i == sql.size() ? name : std::string{};
}

} // namespace otter::db
