// C-Otter -- ui/session.hpp
//
// Estado da conexao ativa, visto pela UI. Isola a camada de dados dos paineis:
// main_shell nao fala com otter_db diretamente.
//
// Toda operacao de banco roda num worker; o thread de render nunca bloqueia.
#pragma once

#include "db/catalog_reader.hpp"
#include "db/plan.hpp"
#include "db/holt.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <array>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace otter::ui {

// Estado da conexao, consultado pela UI a cada frame.
enum class SessionState {
    disconnected,
    connecting,
    connected,
    failed,
};

class Session {
public:
    Session();
    ~Session();

    Session(const Session&)            = delete;
    Session& operator=(const Session&) = delete;

    // Dispara a conexao em background e retorna imediatamente.
    void connect_async(const db::ConnConfig& config);

    // Executa a query em background; o resultado aparece em last_result().
    void execute_async(std::string sql);

    // Executa varios comandos em sequencia, parando no primeiro erro.
    //
    // Parar e' o padrao certo para script de migracao: continuar depois de um
    // CREATE TABLE que falhou executaria os INSERTs seguintes contra uma
    // tabela que nao existe, multiplicando o estrago.
    //
    // O resultado exposto e' o do ULTIMO comando que produziu linhas -- e' o
    // que o usuario quer ver depois de um script que termina num SELECT.
    void execute_script_async(std::vector<std::string> statements,
                              bool stop_on_error = true);

    // Roda EXPLAIN e guarda o plano. Com `analyze`, envolve em transacao e
    // da' ROLLBACK -- EXPLAIN ANALYZE executa a consulta de verdade
    // (ADR 0013).
    void explain_async(std::string sql, bool analyze);

    // Plano da ultima explicacao, se houve.
    [[nodiscard]] std::optional<db::QueryPlan> take_plan();

    // Quantos comandos do script ja' rodaram, para a barra de progresso.
    [[nodiscard]] std::size_t script_progress() const noexcept {
        return script_done_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::size_t script_total() const noexcept {
        return script_total_.load(std::memory_order_acquire);
    }

    // Verdadeiro quando o ultimo script teve pelo menos um comando com erro.
    //
    // Existe porque procurar "erro" na mensagem seria fragil: a mensagem vem
    // traduzida do servidor e muda de idioma com o `lc_messages`.
    [[nodiscard]] bool last_script_failed() const noexcept {
        return script_failed_.load(std::memory_order_acquire);
    }

    void disconnect();

    [[nodiscard]] SessionState state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool busy() const noexcept {
        return busy_.load(std::memory_order_acquire);
    }

    // Os acessos abaixo copiam sob lock: sao chamados pelo thread de UI
    // enquanto o worker pode estar escrevendo.
    [[nodiscard]] std::string status_message() const;
    [[nodiscard]] std::string server_version() const;
    [[nodiscard]] std::string database_name() const;

    // Pastas que o SGBD conectado oferece. Uma pasta que ele NAO tem nao deve
    // aparecer vazia: "Sequences (0)" num MySQL sugere que ele poderia ter
    // uma, e manda o usuario procurar o que nao existe.
    [[nodiscard]] bool has_sequences() const noexcept { return has_sequences_; }
    [[nodiscard]] bool has_user_types() const noexcept { return has_user_types_; }
    [[nodiscard]] bool has_events() const noexcept { return has_events_; }

    // --- Informacao do servidor (System Info) --------------------------------
    //
    // Seis conjuntos com a MESMA forma -- pares nome/valor --, mas de origens
    // diferentes. Um enum em vez de seis metodos: o laco da arvore fica com
    // um chamador so', e acrescentar o setimo nao muda a assinatura de nada.
    enum class ServerInfo {
        session_status,
        global_status,
        session_variables,
        global_variables,
        engines,
        charsets,
    };

    // O SGBD tem essas informacoes? So' o MySQL, por enquanto -- no
    // PostgreSQL o equivalente sao as views pg_stat_*, com outra forma.
    [[nodiscard]] bool has_server_info() const noexcept { return has_server_info_; }

    // --- Usuarios ------------------------------------------------------------
    [[nodiscard]] bool has_users() const noexcept { return has_users_; }
    [[nodiscard]] bool users_loaded() const noexcept { return users_loaded_; }
    [[nodiscard]] std::vector<db::UserMeta> users() const;

    void load_users_async();
    void load_grants_async(std::string user, std::string host);

    void load_server_info_async(ServerInfo what);

    [[nodiscard]] std::vector<db::ServerVariable> session_status() const;
    [[nodiscard]] std::vector<db::ServerVariable> global_status() const;
    [[nodiscard]] std::vector<db::ServerVariable> session_variables() const;
    [[nodiscard]] std::vector<db::ServerVariable> global_variables() const;
    [[nodiscard]] std::vector<db::ServerVariable> engines() const;
    [[nodiscard]] std::vector<db::ServerVariable> charsets() const;

    [[nodiscard]] bool session_status_loaded() const noexcept;
    [[nodiscard]] bool global_status_loaded() const noexcept;
    [[nodiscard]] bool session_variables_loaded() const noexcept;
    [[nodiscard]] bool global_variables_loaded() const noexcept;
    [[nodiscard]] bool engines_loaded() const noexcept;
    [[nodiscard]] bool charsets_loaded() const noexcept;

    // O que o driver suporta. Vazio enquanto nao ha' conexao.
    //
    // Guardado em vez de perguntado ao Holt a cada quadro: o Holt vive atras
    // do mutex, e a UI consulta isto em varios pontos por quadro.
    [[nodiscard]] const std::optional<db::Capabilities>& capabilities() const noexcept {
        return capabilities_;
    }
    [[nodiscard]] std::vector<db::SchemaMeta> schemas() const;
    [[nodiscard]] std::vector<db::ForeignKeyMeta> foreign_keys() const;
    [[nodiscard]] std::optional<db::ResultSet> take_result();
    [[nodiscard]] std::vector<db::QueryLog> query_log() const;

    // Carregamento tardio por pasta da arvore. Cada uma consulta o catalogo
    // apenas quando o no e' expandido -- expandir "Colunas" nao deve custar
    // uma leitura de indices.
    void load_columns_async(std::string schema, std::string table);
    void load_constraints_async(std::string schema, std::string table);
    void load_indexes_async(std::string schema, std::string table);
    void load_keys_async(std::string schema, std::string table);
    void load_triggers_async(std::string schema, std::string table);
    void load_partitions_async(std::string schema, std::string table);
    void load_events_async(std::string schema);

    // Corpo da view (`pg_get_viewdef`). Carregado so' quando o no "Definicao"
    // e' expandido: uma view de relatorio pode ter varios KB de SQL.
    void load_view_definition_async(std::string schema, std::string view);
    void load_sequences_async(std::string schema);
    void load_routines_async(std::string schema);
    void load_types_async(std::string schema);

    // Descarta o cache de uma tabela, para que a proxima expansao releia o
    // catalogo. Sem isto, um ALTER TABLE feito fora do C-Otter ficaria
    // invisivel ate' reconectar.
    void invalidate_table(std::string_view schema, std::string_view table);

    // Corpo de uma funcao ou procedure. Precisa da assinatura alem do nome:
    // sobrecargas compartilham o nome, e pg_get_functiondef identifica a
    // rotina por regprocedure.
    void load_routine_definition_async(std::string schema, std::string name,
                                       std::string arguments);

    // --- Transacoes ---------------------------------------------------------
    //
    // Consultas baratas e sincronas: leem estado ja' conhecido pela conexao,
    // sem ida ao servidor. Chamadas a cada frame pela barra de ferramentas.
    [[nodiscard]] bool auto_commit() const;
    [[nodiscard]] db::TxnState txn_state() const;
    [[nodiscard]] std::size_t uncommitted_changes() const;

    // Executadas no worker: emitem SQL de verdade.
    void set_auto_commit_async(bool enabled);
    void commit_async();
    void rollback_async();

private:
    void join_worker();

    // Fator comum de commit/rollback/auto-commit: roda no worker e reflete o
    // resultado na mensagem de estado.
    void run_txn_async(std::function<Status(db::Holt&)> operation,
                       std::string success_message);

    // Fator comum dos carregadores de catalogo: abre um worker que recebe o
    // catalogo pronto e escreve no modelo sob lock.
    void run_catalog_async(std::function<void(db::CatalogReader&)> loader);

    // Localiza uma tabela no modelo. O chamador deve ja' segurar o mutex.
    [[nodiscard]] db::TableMeta* find_table(std::string_view schema,
                                            std::string_view table);
    [[nodiscard]] db::SchemaMeta* find_schema(std::string_view schema);

    mutable std::mutex mutex_;
    std::unique_ptr<db::Holt> holt_;

    std::atomic<SessionState> state_{SessionState::disconnected};
    std::atomic<bool>         busy_{false};

    // Progresso do script. Atomicos porque a UI le' a cada quadro enquanto o
    // worker escreve; um mutex aqui seria contencao por nada.
    std::atomic<std::size_t>  script_done_{0};
    std::atomic<std::size_t>  script_total_{0};
    std::atomic<bool>         script_failed_{false};

    std::string status_message_;
    std::string database_name_;

    // Qual SGBD esta' do outro lado. Guardado porque o leitor de catalogo e'
    // criado a cada expansao da arvore, e cada uma precisa do leitor certo.
    std::string driver_id_ = "postgresql";

    // Quais pastas este SGBD oferece, decidido na conexao. Guardado em vez de
    // consultado a cada quadro: o Navigator desenha 60 vezes por segundo, e
    // criar um leitor de catalogo em cada um seria desperdicio.
    bool has_sequences_  = true;
    bool has_user_types_ = true;
    bool has_events_     = false;
    bool has_server_info_ = false;
    bool has_users_       = false;
    bool users_loaded_    = false;
    std::vector<db::UserMeta> users_;

    // Os seis conjuntos, indexados pelo enum. Array em vez de seis membros:
    // o codigo que carrega e o que le' ficam com um indice, nao com um
    // switch de seis casos em cada ponto.
    static constexpr std::size_t kServerInfoCount = 6;
    std::array<std::vector<db::ServerVariable>, kServerInfoCount> server_info_;
    std::array<bool, kServerInfoCount> server_info_loaded_{};

    std::optional<db::Capabilities> capabilities_;

    std::vector<db::SchemaMeta>     schemas_;
    std::vector<db::ForeignKeyMeta> foreign_keys_;
    std::optional<db::ResultSet>    result_;
    std::optional<db::QueryPlan>    plan_;

    std::thread worker_;
};

} // namespace otter::ui

