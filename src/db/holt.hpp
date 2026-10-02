// C-Otter -- db/holt.hpp
//
// Holt e' uma conexao com um banco (a toca da lontra). Substitui o papel do
// java.sql.Connection do JDBC, com contrato definido pelo que o nucleo precisa.
#pragma once

#include "base/error.hpp"
#include "db/result_set.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace otter::db {

// Modo TLS, com os nomes do `sslmode` do PostgreSQL -- que o DBeaver tambem
// usa. Fica aqui, e nao em `connection_config.hpp`, porque o `ConnConfig` que
// chega ao driver precisa dele e e' o mais basico dos dois cabecalhos.
enum class SslMode : std::uint8_t {
    disable, allow, prefer, require, verify_ca, verify_full,
};

[[nodiscard]] const char* to_string(SslMode mode) noexcept;
[[nodiscard]] SslMode ssl_mode_from_string(std::string_view text) noexcept;

// Nivel de isolamento, na ordem do padrao SQL.
enum class IsolationLevel : std::uint8_t {
    read_uncommitted,
    read_committed,
    repeatable_read,
    serializable,
};

[[nodiscard]] std::string_view to_string(IsolationLevel level) noexcept;

// Tunel SSH (aba "SSH"), como chega ao ponto de conexao -- ja' sem as senhas,
// que o tunel nao usa: ver db/ssh_tunnel.hpp.
struct SshEndpoint {
    std::string   host;          // vazio = sem tunel
    std::uint16_t port = 22;
    std::string   user;
    std::string   key_path;      // vazio = as chaves padrao e o agente

    // O perfil pede o que o tunel nao faz; guardado para a recusa dizer qual.
    bool password_auth = false;
    bool key_has_passphrase = false;

    std::chrono::seconds connect_timeout{10};
    std::chrono::seconds keep_alive{60};

    [[nodiscard]] bool enabled() const noexcept { return !host.empty(); }
};

struct ConnConfig {
    // Qual driver falar: "postgresql", "mysql". Viaja junto com o host e a
    // senha porque e' o que decide o PROTOCOLO -- separa-lo do resto faria
    // cada ponto de conexao ter de reencontra-lo, e um ponto esquecido
    // conectaria ao MySQL falando o protocolo do PostgreSQL.
    std::string driver_id = "postgresql";

    std::string host     = "localhost";
    std::uint16_t port   = 5432;
    std::string database;
    std::string user;
    std::string password;

    // Autenticacao do Windows (SQL Server): a conta que roda o programa, por
    // SSPI. Usuario e senha nao sao usados.
    bool integrated_auth = false;

    // Parametros extras do driver, da aba "Parâmetros internos" do dialogo.
    //
    // Era uma string solta `options` que NINGUEM preenchia e NINGUEM lia: o
    // usuario digitava as propriedades, elas iam para disco, e a conexao
    // ignorava todas. Um campo que parece funcionar e nao funciona e' o que
    // a diretriz 6 proibe.
    std::map<std::string, std::string> driver_properties;

    // Tunel SSH: quem conecta abre o tunel e troca host/porta pela ponta
    // local dele (ui/session.cpp).
    SshEndpoint ssh;

    // Proxy SOCKS5 (aba "Proxy"). `proxy_host` vazio = conexao direta.
    std::string   proxy_host;
    std::uint16_t proxy_port = 1080;
    std::string   proxy_user;
    std::string   proxy_password;

    // Nivel de isolamento a aplicar ao conectar; vazio = padrao do servidor.
    std::optional<IsolationLevel> isolation_level;

    // O que roda logo depois de conectar, na ordem: SET ROLE, schema padrao,
    // as consultas de inicializacao do perfil e, por ultimo, o modo somente
    // leitura. Montado por ConnectionProfile::to_conn_config() -- o perfil
    // gravava estes campos e NADA os aplicava (diretiva 6).
    std::vector<std::string> init_statements;
    // "Ignore errors": uma instrucao de inicializacao que falha nao derruba
    // a conexao.
    bool ignore_init_errors = false;
    bool auto_commit = true;
    std::chrono::seconds connect_timeout{10};

    // Modo TLS. O enum vem de `connection_config.hpp`, que ja' o define para
    // o perfil salvo -- um segundo enum com os mesmos nomes so' criaria a
    // chance de traduzir um para o outro errado.
    SslMode ssl_mode = SslMode::disable;

    // `allow` e `prefer` do libpq significam "tenta cifrar, aceita em claro":
    // protegem contra um escuta passivo e contra mais ninguem. O C-Otter os
    // aceita no perfil, por vir do DBeaver, mas NAO exige TLS neles.
    [[nodiscard]] bool ssl_enabled() const noexcept {
        return ssl_mode == SslMode::require ||
               ssl_mode == SslMode::verify_ca ||
               ssl_mode == SslMode::verify_full;
    }
    // `require` nao verifica nada: quem escolhe esse modo quer o canal
    // cifrado num servidor de desenvolvimento, com certificado autoassinado.
    [[nodiscard]] bool ssl_verifies_certificate() const noexcept {
        return ssl_mode == SslMode::verify_ca || ssl_mode == SslMode::verify_full;
    }
};

// O que o driver suporta. A UI consulta isto para habilitar ou esconder acoes,
// em vez de tentar e falhar.
struct Capabilities {
    bool transactions      = false;
    bool savepoints        = false;
    bool ddl_in_transaction = false;
    bool server_cursors    = false;
    bool binary_transfer   = false;
    bool multiple_results  = false;
    bool arrays            = false;
    bool cancel_query      = false;
    bool explain_plan      = false;
};

// Estado da transacao na conexao. Vem do servidor, nao de um palpite do
// cliente: o PostgreSQL informa em cada ReadyForQuery.
enum class TxnState : std::uint8_t {
    idle,        // fora de transacao
    active,      // transacao aberta, com alteracoes pendentes
    failed,      // transacao abortada; so' ROLLBACK e' aceito
};

[[nodiscard]] std::string_view to_string(TxnState state) noexcept;

// Registro de uma query executada -- alimenta o inspetor de queries (ADR 0008).
// Toda query e' registrada, inclusive as internas de metadados: ferramenta que
// esconde o que faz e' dificil de confiar.
struct QueryLog {
    std::string               sql;
    std::chrono::microseconds duration{0};
    std::size_t               rows = 0;
    bool                      internal = false;   // consulta de catalogo
    bool                      failed = false;
    std::string               error;
};

class Holt {
public:
    virtual ~Holt() = default;

    Holt(const Holt&)            = delete;
    Holt& operator=(const Holt&) = delete;

    [[nodiscard]] virtual bool is_open() const noexcept = 0;
    virtual void close() = 0;

    // Executa e devolve o resultado completo.
    [[nodiscard]] virtual Result<ResultSet> query(std::string_view sql) = 0;

    // Executa sem produzir resultado (DDL, DML).
    [[nodiscard]] virtual Status execute(std::string_view sql) = 0;

    // Consulta do PROGRAMA, nao do usuario: o ping do keep-alive e as
    // amostras do dashboard. Registrada no log como interna, e nao conta como
    // alteracao pendente da transacao do usuario.
    [[nodiscard]] virtual Result<ResultSet> query_internal(std::string_view sql) {
        return query(sql);
    }

    // Cancela a query em andamento, de outra thread.
    virtual Status cancel() = 0;

    // --- Transacoes ---------------------------------------------------------

    [[nodiscard]] virtual bool auto_commit() const noexcept = 0;
    virtual Status set_auto_commit(bool enabled) = 0;

    // Estado reportado pelo servidor apos a ultima query.
    [[nodiscard]] virtual TxnState txn_state() const noexcept = 0;

    virtual Status commit() = 0;
    virtual Status rollback() = 0;

    virtual Status savepoint(std::string_view name) = 0;
    virtual Status rollback_to(std::string_view name) = 0;
    virtual Status release_savepoint(std::string_view name) = 0;

    [[nodiscard]] virtual Result<IsolationLevel> isolation_level() = 0;
    virtual Status set_isolation_level(IsolationLevel level) = 0;

    // Quantas instrucoes de alteracao rodaram desde o ultimo commit. A UI usa
    // para avisar antes de fechar com trabalho pendente.
    [[nodiscard]] std::size_t uncommitted_changes() const noexcept {
        return uncommitted_changes_;
    }

    [[nodiscard]] virtual Capabilities capabilities() const noexcept = 0;
    [[nodiscard]] virtual std::string server_version() const = 0;
    [[nodiscard]] virtual std::string current_schema() const = 0;

    // O banco em que a sessao esta', dito pelo SERVIDOR. Vazio = o driver nao
    // sabe (vale o do perfil). Existe porque um perfil do SQL Server pode vir
    // sem banco -- o servidor escolhe o padrao do login -- e a arvore
    // desenhava o banco da conexao sem nome.
    [[nodiscard]] virtual std::string current_database() const { return {}; }

    // Descricao do canal: "TLS 1.3, AES_256_GCM" quando cifrado, vazio quando
    // em claro. Texto em vez de booleano porque a barra de status mostra QUAL
    // protocolo e cifra foram negociados -- "cifrado" sozinho nao distingue
    // um TLS 1.3 de um TLS 1.0 com cifra obsoleta.
    //
    // Nao e' puro: um driver que nao negocia TLS nao deve ser obrigado a
    // declarar que nao negocia.
    [[nodiscard]] virtual std::string secure_channel() const { return {}; }

    // O que o servidor DISSE alem do resultado, desde a ultima chamada: os
    // NOTICE do PostgreSQL. Nao e' puro -- um driver sem esse canal devolve
    // vazio, e o painel de saida diz que o SGBD nao a reporta.
    [[nodiscard]] virtual std::vector<std::string> take_server_output() {
        return {};
    }
    [[nodiscard]] virtual bool reports_server_output() const noexcept {
        return false;
    }

    // Historico desta conexao, para o inspetor de queries.
    [[nodiscard]] const std::vector<QueryLog>& query_log() const noexcept {
        return query_log_;
    }
    void clear_query_log() { query_log_.clear(); }

protected:
    Holt() = default;

    void record(QueryLog entry) {
        // Limite defensivo: uma sessao longa nao pode crescer sem fim.
        constexpr std::size_t kMaxEntries = 2000;
        if (query_log_.size() >= kMaxEntries) {
            query_log_.erase(query_log_.begin(),
                             query_log_.begin() + kMaxEntries / 4);
        }
        query_log_.push_back(std::move(entry));
    }

    // Contabiliza alteracoes pendentes. Chamado pelo driver ao ver um comando
    // que modifica dados fora de auto-commit.
    void note_change() noexcept { ++uncommitted_changes_; }
    void clear_changes() noexcept { uncommitted_changes_ = 0; }

private:
    std::vector<QueryLog> query_log_;
    std::size_t           uncommitted_changes_ = 0;
};

class Holt;

// Aplica `init_statements` e o auto-commit do perfil a uma conexao recem
// aberta. Uma instrucao que falha devolve erro nomeando-a, salvo com
// `ignore_init_errors`.
[[nodiscard]] Status apply_session_setup(Holt& holt, const ConnConfig& config);

// As instrucoes de inicializacao de um perfil, para o SGBD `driver_id`.
// Separada para o teste: e' regra pura.
struct SessionSetup {
    std::string session_role;
    std::string default_schema;
    std::string bootstrap_queries;   // uma por linha
    bool        read_only = false;
};
[[nodiscard]] std::vector<std::string> session_setup_statements(
    std::string_view driver_id, const SessionSetup& setup);

// Driver de um SGBD. Interface virtual pura, registrada estaticamente
// (sem OSGi, ver ADR 0001 #5).
class Driver {
public:
    virtual ~Driver() = default;

    [[nodiscard]] virtual std::string_view id() const noexcept = 0;
    [[nodiscard]] virtual std::string_view display_name() const noexcept = 0;
    [[nodiscard]] virtual std::uint16_t default_port() const noexcept = 0;

    [[nodiscard]] virtual Result<std::unique_ptr<Holt>> connect(
        const ConnConfig& config) = 0;
};

} // namespace otter::db
