// C-Otter -- db/connection_config.hpp
//
// Configuracao completa de uma conexao, equivalente ao que o DBeaver expoe no
// assistente de conexao: Main, PostgreSQL, Driver properties, SSH, Proxy, SSL,
// Initialization, Shell commands.
//
// Separado de ConnConfig (db/holt.hpp), que e' o subconjunto minimo que o
// driver precisa para abrir o socket.
#pragma once

#include "db/holt.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace otter::db {

// Como o usuario se identifica no servidor.
enum class AuthModel : std::uint8_t {
    database_native,   // usuario e senha do proprio SGBD
    no_auth,           // sem autenticacao (SQLite, alguns proxies)
    pg_ident,          // ident/peer do PostgreSQL
    kerberos,
    aws_iam,
};

// Tipo de conexao -- controla a cor do ambiente e as travas de seguranca.
// O DBeaver usa isto para pintar producao de vermelho e exigir confirmacao.
enum class ConnectionType : std::uint8_t {
    development,
    test,
    production,
};

struct ConnectionTypeInfo {
    const char*   name;
    std::uint32_t color;          // faixa de cor na UI
    bool          auto_commit;
    bool          confirm_execute;      // pergunta antes de executar
    bool          confirm_data_change;  // pergunta antes de UPDATE/DELETE
};

[[nodiscard]] const ConnectionTypeInfo& connection_type_info(ConnectionType type);

// --- Rede --------------------------------------------------------------------

enum class SshAuthType : std::uint8_t { password, public_key, agent };

struct SshTunnelConfig {
    bool        enabled = false;
    std::string host;
    std::uint16_t port = 22;
    std::string user;
    SshAuthType auth = SshAuthType::password;
    std::string password;
    std::string private_key_path;
    std::string passphrase;
    bool        save_password = false;
    std::chrono::seconds connect_timeout{10};
    std::chrono::seconds keep_alive{60};
};

struct SslConfig {
    bool        enabled = false;
    SslMode     mode = SslMode::prefer;
    std::string root_cert_path;
    std::string client_cert_path;
    std::string client_key_path;
};

struct ProxyConfig {
    bool          enabled = false;
    std::string   host;
    std::uint16_t port = 1080;
    std::string   user;
    std::string   password;
};

// --- Especificidades do PostgreSQL ------------------------------------------
//
// Extraidas de PostgreConnectionPageAdvanced do DBeaver.

struct PostgresOptions {
    bool show_non_default_databases = false;  // mostrar outros bancos
    bool show_template_databases    = false;  // mostrar templates
    bool show_unavailable_databases = false;  // mostrar bancos sem acesso
    bool show_database_statistics   = false;  // ler tamanho dos objetos
    bool read_all_data_types        = false;  // incluir tipos raros
    bool read_keys_with_columns     = false;  // ler colunas das chaves
    bool replace_legacy_timezone    = false;  // timestamptz legado
    bool use_prepared_statements    = true;
    std::string session_role;                 // SET ROLE ao conectar
};

// --- Configuracao completa ---------------------------------------------------

// Preferencias do editor SQL, POR CONEXAO.
//
// No DBeaver as paginas "SQL Editor" e "Code Completion" do dialogo de
// conexao sobrepoem as preferencias globais para aquela conexao -- e' o que
// permite formatar em maiusculas no banco legado e em minusculas no novo.
//
// Aqui elas nascem com o mesmo padrao do global; mudar uma vale so' para a
// conexao que esta' sendo editada.
struct EditorOptions {
    // --- Formatacao (main.sql.format) ---
    //
    // Espelham sql::FormatOptions. Duplicados como tipos simples de
    // proposito: connection_config nao deve depender de sql/, e sao tres
    // campos -- uma dependencia de cabecalho custaria mais que a copia.
    int  keyword_case      = 1;     // 0=preservar, 1=MAIUSCULAS, 2=minusculas
    int  indent_width      = 4;
    bool river_style       = true;
    std::size_t wrap_select_after = 3;

    // --- Completar codigo (main.sql.completion) ---
    bool complete_on_typing      = true;
    bool complete_in_comments    = false;
    bool complete_in_strings     = false;
    bool auto_insert_single      = false;
    int  complete_delay_ms       = 200;

    // --- Editor de codigo (main.sql.codeeditor) ---
    //
    // Espelham TextEditor::config. Os padroes sao os que o editor ja' usava
    // com os valores fixos em MainShell.
    int  tab_size                = 4;
    bool show_line_numbers       = true;
    bool auto_indent             = true;
    bool show_matching_brackets  = true;
    bool show_whitespace         = false;

    // Quebra de linha e dobra de blocos. O widget suporta as duas; nada as
    // ligava.
    //
    // Word wrap DESLIGADO por padrao, como em todo editor de codigo: com ele
    // ligado, uma linha longa passa a ocupar varias e o numero da linha deixa
    // de corresponder ao que o servidor reporta num erro.
    bool word_wrap               = false;
    bool code_folding            = true;

    // --- Processamento SQL (main.sqlexecute) ---
    //
    // Linhas por pagina. O mesmo `RESULT_SET_MAX_ROWS` do DBeaver: grande o
    // bastante para preencher a tela, pequeno o bastante para voltar rapido.
    //
    // Por CONEXAO porque a resposta certa depende da latencia: 200 e' bom
    // num banco local e caro num servidor do outro lado do Atlantico.
    int  page_size               = 200;

    // --- Transferencia de dados (main.datatransfer) ---
    //
    // Padroes da janela de exportacao para ESTA conexao. Ela sempre pergunta
    // antes de gravar; isto so' decide com que valores a janela abre.
    // Valores de db::ExportFormat: 0=CSV, 1=JSON, 2=Markdown, 3=SQL INSERT.
    int  export_format        = 0;
    bool export_write_header  = true;

    // Texto para NULL no arquivo. Vazio e' o certo para reimportar -- e
    // diferente do null_text da GRADE, que existe para ser visivel.
    std::string export_null_text;

    // --- Editor de dados / Grade (main.resultset.grid) ---
    //
    // Como NULL aparece na grade. Vazio nao e' opcao: string vazia e NULL
    // sao valores diferentes no banco, e exibi-los igual e' o erro classico
    // de cliente SQL -- por isso o padrao e' "[null]", visivelmente
    // diferente de qualquer texto.
    std::string null_text = "[null]";

    // Alinhar numeros a' direita, como em planilha: a virgula decimal fica
    // na mesma coluna e da' para comparar ordens de grandeza de relance.
    bool align_numbers_right = true;

    // Parar o script no primeiro erro, ou seguir para o proximo comando.
    //
    // Parar e' o padrao: num script de migracao, seguir depois de um erro
    // executa os comandos seguintes num estado que o autor nao previu.
    bool stop_script_on_error    = true;
};

struct ConnectionProfile {
    // Identificacao
    std::string    id;                 // gerado, estavel
    std::string    name;               // rotulo do usuario
    std::string    description;
    std::string    folder;             // pasta na arvore
    ConnectionType type = ConnectionType::development;
    std::uint32_t  color = 0;          // 0 = usar a cor do tipo

    // Driver
    std::string driver_id = "postgresql";

    // Servidor
    std::string   host = "localhost";
    std::uint16_t port = 5432;
    std::string   database;
    std::string   url;                 // alternativa ao host/porta
    bool          use_url = false;

    // Autenticacao
    AuthModel   auth_model = AuthModel::database_native;
    std::string user;
    std::string password;
    bool        save_password = false;

    // Rede
    SshTunnelConfig ssh;
    SslConfig       ssl;
    ProxyConfig     proxy;

    // Inicializacao
    bool        auto_commit = true;
    std::string default_schema;         // search_path inicial
    std::string bootstrap_queries;      // executadas ao conectar
    bool        read_only = false;

    // Nivel de isolamento a aplicar AO CONECTAR.
    //
    // -1 = nao mexer, usar o padrao do servidor. Os demais valores sao os de
    // db::IsolationLevel (0..3), gravados como numero para o
    // connection_config nao depender de holt.hpp.
    //
    // A pagina "Transações" do dialogo o edita SEM conexao: o DBeaver so'
    // lista os niveis com a conexao viva, porque le' os suportados do
    // servidor. Aqui os quatro do padrao SQL sao oferecidos sempre, e um que
    // o servidor nao aceite falha ao conectar, nomeando-o -- o que e' melhor
    // que uma lista vazia antes de conectar.
    int         isolation_level = -1;
    std::chrono::seconds connect_timeout{10};
    bool        keep_alive = false;
    std::chrono::seconds keep_alive_interval{60};
    bool        close_idle_connections = false;

    // Propriedades livres do driver
    std::map<std::string, std::string> driver_properties;

    // Especificas do SGBD
    PostgresOptions postgres;

    // Preferencias do editor para ESTA conexao (paginas SQL Editor e
    // Code Completion do dialogo).
    EditorOptions   editor;

    // Nome sugerido quando o usuario nao informa um: "banco@host".
    [[nodiscard]] std::string effective_name() const;

    // Converte para o subconjunto que o driver consome.
    [[nodiscard]] struct ConnConfig to_conn_config() const;
};

} // namespace otter::db
