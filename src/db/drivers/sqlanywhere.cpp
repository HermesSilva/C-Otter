// Driver do SQL Anywhere sobre lib/tdswire/tds5 (ADR 0026). Adapta o protocolo
// ao contrato Holt/Driver, como os outros tres fazem para os deles.
//
// O que e' diferente dos outros, e por isso nao da' para copiar:
//
//   - o TDS 5.0 do SQL Anywhere nao leva data antes de 1753 nem diz o tipo
//     exato de uma coluna de texto: as datas vem como TEXTO
//     (return_date_time_as_string) e o tipo de cada coluna de um SELECT e'
//     perguntado ao servidor (sa_describe_query), que tambem diz de que tabela
//     ela veio -- mesmo sob apelido
//   - o modo manual e' `chained = On`: o servidor abre a transacao sozinho no
//     primeiro comando. Nesse modo o DONE diz "em transacao" SEMPRE, ate' logo
//     depois de um COMMIT -- o que vale e' a propriedade TransactionStartTime
//     da conexao, vazia enquanto nada foi modificado
//   - DDL confirma a transacao (como no MySQL, ao contrario do PostgreSQL e do
//     SQL Server)
//   - cancelar e' um pacote ATTENTION na MESMA conexao
//   - um banco inexistente no login NAO e' erro para o servidor: ele entra no
//     banco padrao. O driver confere e recusa
//   - a conexao nao e' cifrada: o TDS do SQL Anywhere nao tem TLS
//   - o TDS nao tem texto vazio: o servidor manda '' como UM ESPACO. Aqui um
//     texto que e' so' um espaco volta a ser vazio -- um espaco de verdade
//     fica indistinguivel, mas e' muito mais raro que o vazio
#include "db/drivers/sqlanywhere.hpp"

#include "db/catalog_sqlanywhere.hpp"
#include "tdswire/tds5.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>

namespace otter::db {
namespace {

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// O tipo que o PROTOCOLO informou -- usado quando o servidor nao descreveu a
// consulta (uma chamada de procedure, um lote de varios comandos).
DataKind kind_from_wire(const tdswire::Tds5Column& column) noexcept {
    using tdswire::Tds5Type;
    switch (column.type) {
        case Tds5Type::int1:
        case Tds5Type::sint1:
        case Tds5Type::int2:
        case Tds5Type::int4:
        case Tds5Type::int8:
        case Tds5Type::uint2:
        case Tds5Type::uint4:
        case Tds5Type::uint8:
        case Tds5Type::intn:
        case Tds5Type::uintn:
            return DataKind::integer;
        case Tds5Type::bit:
            return DataKind::boolean;
        case Tds5Type::float4:
        case Tds5Type::float8:
        case Tds5Type::fltn:
            return DataKind::floating;
        case Tds5Type::money:
        case Tds5Type::shortmoney:
        case Tds5Type::moneyn:
        case Tds5Type::decn:
        case Tds5Type::numn:
            return DataKind::numeric;
        case Tds5Type::date:
        case Tds5Type::daten:
            return DataKind::date;
        case Tds5Type::time:
        case Tds5Type::timen:
        case Tds5Type::bigtimen:
            return DataKind::time;
        case Tds5Type::datetime:
        case Tds5Type::shortdate:
        case Tds5Type::datetimn:
        case Tds5Type::bigdatetimen:
            return sqlanywhere_kind(column.type_name);   // date e time viajam aqui
        case Tds5Type::binary:
        case Tds5Type::varbinary:
            return column.type_name == "uniqueidentifier" ? DataKind::uuid
                                                          : DataKind::binary;
        case Tds5Type::image:
            return DataKind::binary;
        case Tds5Type::longbinary:
            return column.type_name.starts_with("n") ? DataKind::string : DataKind::binary;
        case Tds5Type::char_:
        case Tds5Type::varchar:
        case Tds5Type::longchar:
        case Tds5Type::text:
        case Tds5Type::unitext:
        case Tds5Type::xml:
            return DataKind::string;
        case Tds5Type::void_:
            return DataKind::unknown;
    }
    return DataKind::unknown;
}

// O comando e' uma consulta que da' para descrever? SELECT ou WITH.
bool looks_like_select(std::string_view sql) {
    std::size_t i = 0;
    for (;;) {
        while (i < sql.size() && std::isspace(static_cast<unsigned char>(sql[i])) != 0) ++i;
        if (sql.substr(i, 2) == "--" || sql.substr(i, 2) == "//") {
            const std::size_t end = sql.find('\n', i);
            if (end == std::string_view::npos) return false;
            i = end + 1;
        } else if (sql.substr(i, 2) == "/*") {
            const std::size_t end = sql.find("*/", i + 2);
            if (end == std::string_view::npos) return false;
            i = end + 2;
        } else {
            break;
        }
    }
    const auto starts = [&sql, i](std::string_view word) {
        if (sql.size() - i < word.size()) return false;
        for (std::size_t k = 0; k < word.size(); ++k) {
            if (std::toupper(static_cast<unsigned char>(sql[i + k])) != word[k]) return false;
        }
        const std::size_t end = i + word.size();
        return end == sql.size() || std::isalnum(static_cast<unsigned char>(sql[end])) == 0;
    };
    return starts("SELECT") || starts("WITH");
}

// A palavra INTO solta no texto (fora de nome maior). SELECT ... INTO cria uma
// tabela ou preenche variaveis: pedir ao servidor que o "descreva" tem efeito
// -- a tabela temporaria nasce na descricao, e a consulta de verdade falha.
bool mentions_into(std::string_view sql) {
    const auto word_char = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    };
    for (std::size_t i = 0; i + 4 <= sql.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(sql[i])) != 'I' ||
            std::toupper(static_cast<unsigned char>(sql[i + 1])) != 'N' ||
            std::toupper(static_cast<unsigned char>(sql[i + 2])) != 'T' ||
            std::toupper(static_cast<unsigned char>(sql[i + 3])) != 'O') {
            continue;
        }
        const bool starts = i == 0 || !word_char(sql[i - 1]);
        const bool ends   = i + 4 == sql.size() || !word_char(sql[i + 4]);
        if (starts && ends) return true;
    }
    return false;
}

class SqlAnywhereHolt final : public Holt {
public:
    explicit SqlAnywhereHolt(tdswire::Tds5Connection conn) : conn_(std::move(conn)) {}

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

    [[nodiscard]] Result<ResultSet> query_internal(std::string_view sql) override {
        return run(sql, /*internal=*/true);
    }

    Status cancel() override { return conn_.cancel(); }

    // --- Transacoes ---------------------------------------------------------

    [[nodiscard]] bool auto_commit() const noexcept override { return auto_commit_; }

    Status set_auto_commit(bool enabled) override {
        if (enabled == auto_commit_) return {};

        if (enabled) {
            // Saindo do modo manual: confirma o que estava pendente. Commit, e
            // nao rollback -- descartar trabalho do usuario sem pedir seria pior.
            OTTER_RETURN_IF_ERROR(run_silent("COMMIT"));
            OTTER_RETURN_IF_ERROR(run_silent("SET TEMPORARY OPTION chained = 'Off'"));
        } else {
            // "Chained": o servidor abre a transacao sozinho no primeiro
            // comando, e avisa em cada DONE.
            OTTER_RETURN_IF_ERROR(run_silent("SET TEMPORARY OPTION chained = 'On'"));
        }
        auto_commit_ = enabled;
        clear_changes();
        refresh_transaction_state();
        return {};
    }

    [[nodiscard]] TxnState txn_state() const noexcept override {
        return in_transaction_ ? TxnState::active : TxnState::idle;
    }

    Status commit() override {
        OTTER_RETURN_IF_ERROR(run_silent("COMMIT"));
        clear_changes();
        refresh_transaction_state();
        return {};
    }

    Status rollback() override {
        OTTER_RETURN_IF_ERROR(run_silent("ROLLBACK"));
        clear_changes();
        refresh_transaction_state();
        return {};
    }

    Status savepoint(std::string_view name) override {
        return run_silent("SAVEPOINT " + sqlanywhere_quote(name));
    }

    Status rollback_to(std::string_view name) override {
        const Status status =
            run_silent("ROLLBACK TO SAVEPOINT " + sqlanywhere_quote(name));
        refresh_transaction_state();
        return status;
    }

    Status release_savepoint(std::string_view name) override {
        // ROLLBACK TO SAVEPOINT ja' solta o ponto no SQL Anywhere: solta-lo de
        // novo da' "Savepoint not found" (-220), que aqui nao e' erro -- o
        // estado pedido (o ponto nao existe mais) ja' e' o que ha'.
        const Status status = run_silent("RELEASE SAVEPOINT " + sqlanywhere_quote(name));
        if (!status && status.error().message().find("-220") != std::string::npos) return {};
        return status;
    }

    [[nodiscard]] Result<IsolationLevel> isolation_level() override {
        OTTER_ASSIGN_OR_RETURN(
            auto rs, run("SELECT connection_property('isolation_level')", true));
        if (rs.row_count() == 0) return IsolationLevel::read_committed;

        // 0..3; os de snapshot ("snapshot", "statement-snapshot"...) nao estao
        // nos quatro do padrao SQL e saem como o mais proximo.
        const std::string_view level = rs.text(0, 0);
        if (level == "0") return IsolationLevel::read_uncommitted;
        if (level == "2") return IsolationLevel::repeatable_read;
        if (level == "3" || level == "snapshot") return IsolationLevel::serializable;
        return IsolationLevel::read_committed;
    }

    Status set_isolation_level(IsolationLevel level) override {
        const char* value = "1";
        switch (level) {
            case IsolationLevel::read_uncommitted: value = "0"; break;
            case IsolationLevel::read_committed:   value = "1"; break;
            case IsolationLevel::repeatable_read:  value = "2"; break;
            case IsolationLevel::serializable:     value = "3"; break;
        }
        return run_silent(std::string("SET TEMPORARY OPTION isolation_level = ") + value);
    }

    [[nodiscard]] Capabilities capabilities() const noexcept override {
        Capabilities caps;
        caps.transactions       = true;
        caps.savepoints         = true;
        caps.ddl_in_transaction = false;   // CREATE TABLE confirma a transacao
        caps.server_cursors     = false;
        caps.binary_transfer    = true;
        caps.multiple_results   = true;
        caps.arrays             = false;
        caps.cancel_query       = true;
        caps.explain_plan       = false;   // GRAPHICAL_PLAN: ainda nao lido
        return caps;
    }

    [[nodiscard]] std::string server_version() const override { return version_; }
    [[nodiscard]] std::string current_schema() const override { return user_; }
    [[nodiscard]] std::string current_database() const override { return database_; }

    [[nodiscard]] std::vector<std::string> take_server_output() override {
        std::vector<std::string> out = std::move(output_);
        output_.clear();
        return out;
    }
    [[nodiscard]] bool reports_server_output() const noexcept override { return true; }

    // Ajusta as opcoes da sessao e le' quem e' o servidor. `wanted_database`
    // e' o banco do perfil: o servidor NAO recusa um nome que nao conhece.
    Status initialize(std::string_view wanted_database) {
        bool first = true;
        for (const std::string& option : sqlanywhere_session_options()) {
            Status status = run_silent(option);
            // O login TDS 5.0 passa num SAP ASE, que nao conhece SET TEMPORARY
            // OPTION. Dizer o que e', em vez de deixar o erro de sintaxe dele.
            if (!status && first) {
                return std::unexpected(status.error().with_context(
                    "this server does not behave like SQL Anywhere (SAP ASE and other "
                    "TDS 5.0 servers are not supported)"));
            }
            OTTER_RETURN_IF_ERROR(status);
            first = false;
        }

        OTTER_ASSIGN_OR_RETURN(
            auto rs, run("SELECT db_name(), user_name(), @@version", /*internal=*/true));
        if (rs.row_count() > 0) {
            database_ = std::string(rs.text(0, 0));
            user_     = std::string(rs.text(0, 1));
            version_  = std::string(rs.text(0, 2));
        }

        if (!wanted_database.empty() && !iequals(wanted_database, database_)) {
            return fail(Errc::connection_failed,
                        "the server has no running database named '" +
                            std::string(wanted_database) + "' (it would have used '" +
                            database_ + "' instead). Start the database on the server, or "
                            "leave the database field empty to use the default one");
        }
        return {};
    }

    // Propriedades do driver como opcoes TEMPORARIAS da conexao: valem ate'
    // desconectar e nao tocam o banco. Um nome que o servidor nao conhece
    // falha a conexao -- meia configuracao aplicada seria pior.
    Status apply_driver_properties(const std::map<std::string, std::string>& properties) {
        for (const auto& [name, value] : properties) {
            if (name.empty() || name == "loginTimeout") continue;
            OTTER_RETURN_IF_ERROR(run_silent("SET TEMPORARY OPTION " +
                                             sqlanywhere_quote(name) + " = " +
                                             sqlanywhere_literal(value)));
        }
        return {};
    }

private:
    Status run_silent(std::string_view sql) {
        auto result = run(sql, /*internal=*/true);
        if (!result) return std::unexpected(result.error());
        return {};
    }

    struct Described {
        std::string name;
        std::string type;          // "char(20)", "numeric(15,2)", "date"
        std::string domain;        // "char", "numeric", "date"
        int         width = 0;
        int         scale = 0;
        std::string schema;
        std::string table;
        std::string column;
        bool        nullable = true;
    };

    // O que o servidor sabe sobre as colunas de `sql`. Vazio quando ele nao
    // descreve (SQL invalido, comando que nao e' consulta) -- e ai' vale so' o
    // que o protocolo informou.
    std::vector<Described> describe(std::string_view sql) {
        std::vector<Described> out;
        (void)conn_.query(
            sqlanywhere_describe_query(sql), {},
            [&out](std::span<const tdswire::Value> row) {
                if (row.size() < 9) return;
                const auto text = [&row](std::size_t i) {
                    return row[i].null ? std::string{} : std::string(row[i].text);
                };
                Described column;
                column.name     = text(0);
                column.domain   = text(1);
                column.type     = text(2);
                column.width    = std::atoi(text(3).c_str());
                column.scale    = std::atoi(text(4).c_str());
                column.schema   = text(5);
                column.table    = text(6);
                column.column   = text(7);
                column.nullable = text(8) != "0";
                out.push_back(std::move(column));
            });
        (void)conn_.take_messages();
        return out;
    }

    Result<ResultSet> run(std::string_view sql, bool internal) {
        if (!conn_.is_open()) return fail(Errc::closed, "connection is closed");

        const auto started = std::chrono::steady_clock::now();

        std::vector<Described> described;
        if (!internal && looks_like_select(sql) && !mentions_into(sql)) {
            described = describe(sql);
        }

        ResultSetBuilder builder;
        std::size_t rows = 0;
        int result_sets = 0;
        // As colunas de data e hora (descritas): tiram os zeros do fim.
        std::vector<bool> temporal;
        // As de texto: um espaco sozinho e' o '' do servidor.
        std::vector<bool> textual;

        const Status status = conn_.query(
            sql,
            [&](const std::vector<tdswire::Tds5Column>& columns) {
                // Um lote pode devolver varios conjuntos; a grade mostra o
                // PRIMEIRO. Os outros sao lidos e descartados.
                if (++result_sets != 1) return;
                const bool known = described.size() == columns.size();
                temporal.assign(columns.size(), false);
                textual.assign(columns.size(), false);

                for (std::size_t c = 0; c < columns.size(); ++c) {
                    const tdswire::Tds5Column& column = columns[c];
                    ColumnInfo info;
                    info.name      = column.name;
                    info.type_name = column.type_name;
                    info.kind      = kind_from_wire(column);
                    info.type_oid  = static_cast<std::uint32_t>(column.type);
                    info.size      = static_cast<std::int32_t>(column.max_length);
                    info.precision = column.precision;
                    info.scale     = column.scale;
                    info.nullable  = column.nullable;

                    if (known) {
                        const Described& d = described[c];
                        info.type_name = d.type.empty() ? d.domain : d.type;
                        info.kind      = sqlanywhere_kind(d.domain);
                        info.size      = d.width;
                        info.precision = static_cast<std::int16_t>(d.width);
                        info.scale     = static_cast<std::int16_t>(d.scale);
                        info.nullable  = d.nullable;
                        info.source_schema      = d.schema;
                        info.source_table       = d.table;
                        info.source_column_name = d.column;
                        temporal[c] = info.kind == DataKind::time ||
                                      info.kind == DataKind::timestamp;
                    } else if (!column.table.empty()) {
                        // Sem a descricao, o que o protocolo disse. Sob apelido
                        // ele manda o APELIDO como nome da coluna: a gravacao
                        // falharia com "coluna nao encontrada", nao em silencio.
                        info.source_schema      = column.schema;
                        info.source_table       = column.table;
                        info.source_column_name = column.column;
                    }
                    textual[c] = info.kind == DataKind::string;
                    builder.add_column(std::move(info));
                }
            },
            [&](std::span<const tdswire::Value> row) {
                if (result_sets != 1) return;
                for (std::size_t c = 0; c < row.size(); ++c) {
                    if (row[c].null) {
                        builder.append_null(c);
                    } else if (c < temporal.size() && temporal[c]) {
                        builder.append_text(c, sqlanywhere_trim_fraction(row[c].text));
                    } else if (c < textual.size() && textual[c] && row[c].text == " ") {
                        builder.append_text(c, std::string_view{});
                    } else {
                        builder.append_text(c, row[c].text);
                    }
                }
                ++rows;
            });

        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started);

        // MESSAGE ... TO CLIENT, PRINT e avisos: o painel de saida os mostra.
        for (tdswire::Message& message : conn_.take_messages()) {
            if (!internal) output_.push_back(std::move(message.text));
        }

        // Antes de qualquer outra ida ao servidor, que zeraria a contagem.
        const std::int64_t affected = conn_.last_affected_rows();

        // As consultas do PROGRAMA (catalogo) so' leem: nao mudam o estado da
        // transacao, e conferi-lo a cada uma dobraria as idas ao servidor no
        // modo manual. COMMIT, ROLLBACK e a troca de modo conferem por conta.
        if (!internal) refresh_transaction_state();

        if (!status) {
            record(QueryLog{std::string(sql), elapsed, 0, internal, true,
                            status.error().message()});
            return std::unexpected(status.error());
        }

        builder.set_row_count(rows);
        builder.set_affected_rows(affected);

        if (!internal && !auto_commit_ && affected > 0 && rows == 0) {
            note_change();
        }

        record(QueryLog{std::string(sql), elapsed, rows, internal, false, {}});
        return builder.take();
    }

    // Ha' transacao com alteracao pendente?
    //
    // Em auto-commit (chained Off) o DONE do servidor diz a verdade: so' ha'
    // transacao depois de um BEGIN TRANSACTION explicito. No modo manual
    // (chained On) ele diz "em transacao" sempre -- e ai' a resposta vem de
    // TransactionStartTime, que so' tem valor depois da primeira modificacao.
    // Custa uma ida ao servidor por comando, e so' no modo manual.
    void refresh_transaction_state() {
        if (!conn_.in_transaction()) {
            in_transaction_ = false;
            return;
        }
        if (auto_commit_) {
            in_transaction_ = true;
            return;
        }
        bool modified = false;
        (void)conn_.query("SELECT connection_property('TransactionStartTime')", {},
                          [&modified](std::span<const tdswire::Value> row) {
                              // Vazio chega como um espaco: o TDS nao tem texto vazio.
                              if (row.empty() || row[0].null) return;
                              modified = row[0].text.find_first_not_of(' ') !=
                                         std::string_view::npos;
                          });
        (void)conn_.take_messages();
        in_transaction_ = modified;
    }

    tdswire::Tds5Connection  conn_;
    bool                     in_transaction_ = false;
    std::string              database_;
    std::string              user_;
    std::string              version_;
    bool                     auto_commit_ = true;
    std::vector<std::string> output_;
};

class SqlAnywhereDriver final : public Driver {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "sqlanywhere"; }
    [[nodiscard]] std::string_view display_name() const noexcept override {
        return "SQL Anywhere";
    }
    [[nodiscard]] std::uint16_t default_port() const noexcept override { return 2638; }

    [[nodiscard]] Result<std::unique_ptr<Holt>> connect(
        const ConnConfig& config) override {
        // Recusar com o motivo, em vez de conectar em claro quando o perfil
        // pede cifra: o TDS do SQL Anywhere nao tem TLS (a cifra do servidor,
        // `-ec`, e' do protocolo nativo dele).
        if (config.ssl_enabled()) {
            return fail(Errc::not_supported,
                        "SQL Anywhere connections use the TDS protocol, which the server "
                        "does not encrypt. Turn SSL off for this connection and use an SSH "
                        "tunnel if the network is not trusted");
        }
        if (config.integrated_auth) {
            return fail(Errc::not_supported,
                        "integrated login is not available over the TDS protocol: enter a "
                        "database user and password");
        }

        tdswire::Tds5ConnectParams params;
        params.host     = config.host;
        params.port     = config.port != 0 ? config.port : 2638;
        params.database = config.database;
        params.user     = config.user;
        params.password = config.password;
        params.timeout  = std::chrono::duration_cast<std::chrono::milliseconds>(
            config.connect_timeout);

        params.proxy.host     = config.proxy_host;
        params.proxy.port     = config.proxy_port;
        params.proxy.user     = config.proxy_user;
        params.proxy.password = config.proxy_password;

        OTTER_ASSIGN_OR_RETURN(auto conn, tdswire::Tds5Connection::connect(params));
        auto holt = std::unique_ptr<SqlAnywhereHolt>(new SqlAnywhereHolt(std::move(conn)));
        OTTER_RETURN_IF_ERROR(holt->initialize(config.database));
        OTTER_RETURN_IF_ERROR(holt->apply_driver_properties(config.driver_properties));

        if (config.isolation_level.has_value()) {
            OTTER_RETURN_IF_ERROR(holt->set_isolation_level(*config.isolation_level));
        }
        return std::unique_ptr<Holt>(std::move(holt));
    }
};

} // namespace

std::vector<std::string> sqlanywhere_session_options() {
    return {
        // Aspas duplas delimitam NOME, como em toda ferramenta do SQL Anywhere
        // (o TDS as deixa como texto). E' o que o SQL gerado aqui usa.
        "SET TEMPORARY OPTION quoted_identifier = 'On'",
        // Coluna sem NULL/NOT NULL declarado aceita NULL, e `= NULL` segue o
        // padrao: os padroes do banco, que o TDS troca pelos do ASE.
        "SET TEMPORARY OPTION allow_nulls_by_default = 'On'",
        "SET TEMPORARY OPTION ansinull = 'On'",
        // Datas e horas como TEXTO, no formato ISO. O TDS manda `date` como
        // DATETIME, que nao existe antes de 1753: '0001-01-01' chegaria como
        // '1753-01-01', sem aviso.
        "SET TEMPORARY OPTION return_date_time_as_string = 'On'",
        "SET TEMPORARY OPTION date_format = 'YYYY-MM-DD'",
        "SET TEMPORARY OPTION time_format = 'HH:NN:SS.SSSSSS'",
        "SET TEMPORARY OPTION timestamp_format = 'YYYY-MM-DD HH:NN:SS.SSSSSS'",
    };
}

std::string sqlanywhere_trim_fraction(std::string_view text) {
    const std::size_t dot = text.rfind('.');
    if (dot == std::string_view::npos) return std::string(text);

    // So' digitos depois do ponto (um fuso "+03:00" depois dele nao e' fracao).
    std::size_t end = text.size();
    if (!std::all_of(text.begin() + static_cast<std::ptrdiff_t>(dot) + 1, text.end(),
                     [](char c) { return c >= '0' && c <= '9'; })) {
        return std::string(text);
    }
    while (end > dot + 1 && text[end - 1] == '0') --end;
    if (end == dot + 1) end = dot;   // tudo zero: o ponto sai junto
    return std::string(text.substr(0, end));
}

std::string sqlanywhere_describe_query(std::string_view sql) {
    // O ';' final nao cabe dentro do literal: sa_describe_query quer UM
    // comando.
    while (!sql.empty() && (sql.back() == ';' || sql.back() == ' ' || sql.back() == '\n' ||
                            sql.back() == '\r' || sql.back() == '\t')) {
        sql.remove_suffix(1);
    }
    return "SELECT name, domain_name, domain_name_with_size, width, scale,"
           "       base_owner_name, base_table_name, base_column_name, nulls_allowed"
           "  FROM sa_describe_query(" + sqlanywhere_literal(sql) + ", 0)"
           " ORDER BY column_number";
}

Driver& sqlanywhere_driver() {
    static SqlAnywhereDriver driver;
    return driver;
}

} // namespace otter::db
