// C-Otter -- catalogo pt-BR embutido.
//
// Serve de prova de que a i18n funciona e de referencia de formato para quem
// for traduzir para outro idioma. Um idioma novo pode vir daqui (embutido) ou
// de um arquivo .lang ao lado do executavel -- nenhum dos dois exige tocar em
// codigo de UI.
#include "base/i18n.hpp"

namespace otter::i18n {
namespace {

// Pares [texto em ingles, traducao]. O ingles e' a chave: sem traducao, a UI
// degrada para ingles legivel, nunca para uma chave crua.
const char* const kPtBr[] = {
    // --- Menus ---
    "File",                       "Arquivo",
    "Edit",                       "Editar",
    "SQL",                        "SQL",
    "Help",                       "Ajuda",
    "New connection...",          "Nova conexão...",
    "Edit connection...",         "Editar conexão...",
    "Disconnect",                 "Desconectar",
    "Exit",                       "Sair",
    "Undo",                       "Desfazer",
    "Redo",                       "Refazer",
    "Select all",                 "Selecionar tudo",
    "Find",                       "Localizar",
    "Execute",                    "Executar",
    "ImGui demo",                 "Demo do ImGui",
    "Icon gallery",               "Galeria de ícones",
    "Scale",                      "Escala",
    "tree size",                  "tamanho da árvore",
    "Every object type needs its own drawing. Two icons that look alike "
    "at tree size are a defect.",
    "Cada tipo de objeto precisa do seu próprio desenho. Dois ícones "
    "parecidos no tamanho da árvore são um defeito.",
    "About C-Otter",              "Sobre o C-Otter",
    "Language",                   "Idioma",

    // --- Paineis ---
    "Raft",                       "Raft",
    "Navigator",                  "Navigator",
    "Result",                     "Resultado",
    "Queries",                    "Queries",
    "New connection",             "Nova conexão",
    "Edit",                       "Editar",
    "no connection",              "nenhuma conexão",
    "connect to browse the schema", "conecte-se para navegar o schema",
    "loading...",                 "carregando...",
    "Copy name",                  "Copiar nome",

    // --- Arvore de objetos ---
    "First page",                 "Primeira página",
    "Previous page",              "Página anterior",
    "Next page",                  "Próxima página",
    "rows %zu-%zu",               "linhas %zu-%zu",
    "no more rows",               "sem mais linhas",
    "column(s)",                  "coluna(s)",
    "  |  your LIMIT",            "  |  seu LIMIT",
    "showing the first %zu of %zu columns",
    "mostrando as %zu primeiras de %zu colunas",
    "The grid cannot draw more than %zu columns.\n"
    "Narrow the SELECT list to see the remaining ones.",
    "A grade não desenha mais de %zu colunas.\n"
    "Reduza a lista do SELECT para ver as demais.",
    "The query was rewritten with LIMIT %zu.\n"
    "See the executed SQL in the Queries tab.",
    "A consulta foi reescrita com LIMIT %zu.\n"
    "Veja o SQL executado na aba Queries.",
    "Tables",                     "Tabelas",
    "Data types",                 "Tipos de dados",
    "Views",                      "Views",
    "Materialized views",         "Views materializadas",
    "Definition",                 "Definição",
    "Copy",                       "Copiar",
    "Copy the definition to the clipboard",
    "Copia a definição para a área de transferência",
    "Open in editor",             "Abrir no editor",
    "Open the definition in a new SQL tab",
    "Abre a definição numa nova aba de SQL",
    "Columns",                    "Colunas",
    "Constraints",                "Constraints",
    "Indexes",                    "Índices",
    "Foreign keys",               "Chaves estrangeiras",
    "References",                 "Referências",
    "Triggers",                   "Triggers",
    "Sequences",                  "Sequences",
    "Functions",                  "Funções",
    "auto-commit",                "auto-commit",
    "manual transaction",         "transação manual",
    "read only",                  "somente leitura",

    // --- Abas de editor ---
    "Script",                     "Script",
    "New SQL tab",                "Nova aba SQL",
    "Close tab",                  "Fechar aba",
    "Close",                      "Fechar",
    "Close others",               "Fechar as outras",
    "Pin tab",                    "Fixar aba",
    "Copy SQL",                   "Copiar SQL",

    // --- Temas ---
    "Theme",                      "Tema",
    "Dark",                       "Escuro",
    "Light",                      "Claro",
    "Amber",                      "Âmbar",

    // --- Barra de ferramentas e transacoes ---
    "Connect",                    "Conectar",
    "Execute (Ctrl+Enter)",       "Executar (Ctrl+Enter)",
    "Cancel query",               "Cancelar query",
    "Auto-commit: on",            "Auto-commit: ligado",
    "Auto-commit: off",           "Auto-commit: desligado",
    "Commit (Ctrl+Shift+C)",      "Confirmar (Ctrl+Shift+C)",
    "Rollback (Ctrl+Shift+R)",    "Desfazer (Ctrl+Shift+R)",
    "New connection (Ctrl+Shift+N)", "Nova conexão (Ctrl+Shift+N)",
    "Commit",                     "Confirmar",
    "Rollback",                   "Desfazer",
    "transaction open",           "transação aberta",
    "uncommitted changes",        "alterações pendentes",
    "transaction aborted",        "transação abortada",
    "transaction committed",      "transação confirmada",
    "transaction rolled back",    "transação desfeita",
    "auto-commit on",             "auto-commit ligado",
    "auto-commit off",            "auto-commit desligado",
    "connected, but the catalog failed: ",
        "conectado, mas o catálogo falhou: ",

    // --- Editor ---
    "Execute  (Ctrl+Enter)",      "Executar  (Ctrl+Enter)",
    " (%lld row(s) affected)",    " (%lld linha(s) afetada(s))",
    "no suggestions",             "sem sugestões",
    "table",                      "tabela",
    "view",                       "view",

    // --- Grade ---
    "run a query to see the result", "execute uma query para ver o resultado",
    "command executed",           "comando executado",
    "no queries yet",             "nenhuma query ainda",
    "time",                       "tempo",
    "rows",                       "linhas",
    "state",                      "estado",
    "ok",                         "ok",
    "error",                      "erro",

    // --- Assistente de conexao ---
    "New connection###ConnDialog",  "Nova conexão###ConnDialog",
    "Edit connection###ConnDialog", "Editar conexão###ConnDialog",
    "Select the database",        "Selecione o banco de dados",
    "Choose the driver for the new connection.",
        "Escolha o driver para a nova conexão.",
    "Filter drivers...",          "Filtrar drivers...",
    "Driver",                     "Driver",
    "Category",                   "Categoria",
    "Status",                     "Estado",
    "available",                  "disponível",
    "Next >",                     "Avançar >",
    "< Back",                     "< Voltar",
    "Cancel",                     "Cancelar",
    "Finish",                     "Concluir",
    "Save",                       "Salvar",
    "Test connection",            "Testar conexão",
    "select an available driver", "selecione um driver disponível",
    "connecting...",              "conectando...",

    // Categorias
    "All",                        "Todos",
    "Popular",                    "Popular",
    "NoSQL",                      "NoSQL",
    "Analytical",                 "Analítico",
    "Files",                      "Arquivos",
    "Embedded",                   "Embarcado",
    "Timeseries",                 "Séries temporais",

    // Abas
    "Main",                       "Principal",
    "PostgreSQL",                 "PostgreSQL",
    "SSH",                        "SSH",
    "SSL",                        "SSL",
    "Proxy",                      "Proxy",
    "Initialization",             "Inicialização",
    "General",                    "Geral",

    // Aba Principal
    "Server",                     "Servidor",
    "Host",                       "Host",
    "Port",                       "Porta",
    "Database",                   "Banco de dados",
    "Authentication",             "Autenticação",
    "Method",                     "Método",
    "User",                       "Usuário",
    "Password",                   "Senha",
    "Save password",              "Salvar senha",
    "Database native",            "Banco de dados nativo",
    "No authentication",          "Sem autenticação",
    "Ident / Peer",               "Ident / Peer",
    "Kerberos",                   "Kerberos",
    "AWS IAM",                    "AWS IAM",

    // Aba PostgreSQL
    "Navigator settings",         "Navegador",
    "Show all databases",         "Mostrar todos os bancos",
    "Show template databases",    "Mostrar bancos template",
    "Show inaccessible databases","Mostrar bancos sem acesso",
    "Performance",                "Desempenho",
    "Read size statistics",       "Ler estatísticas de tamanho",
    "Read all data types",        "Ler todos os tipos de dado",
    "Read key columns",           "Ler colunas das chaves",
    "Use prepared statements",    "Usar prepared statements",
    "Session role",               "Role da sessão",
    "Replace legacy timezone",    "Substituir fuso horário legado",

    // Aba Driver
    "Driver properties",          "Propriedades do driver",
    "Parameters passed directly to the driver on connect.",
        "Parâmetros passados diretamente ao driver na conexão.",
    "Property",                   "Propriedade",
    "Value",                      "Valor",
    "property",                   "propriedade",
    "value",                      "valor",
    "Add",                        "Adicionar",

    // Abas de rede
    "Use SSH tunnel",             "Usar túnel SSH",
    "Use SSL",                    "Usar SSL",
    "Use SOCKS proxy",            "Usar proxy SOCKS",
    "Not implemented.",           "Não implementado.",
    "Not implemented - the configuration is saved, but the tunnel is not established.",
        "Não implementado — a configuração é salva, mas o túnel não é estabelecido.",
    "Not implemented - the protocol does not negotiate TLS yet.",
        "Não implementado — o protocolo ainda não negocia TLS.",
    "SSH host",                   "Host SSH",
    "Authentication##ssh",        "Autenticação##ssh",
    "Public key",                 "Chave pública",
    "SSH agent",                  "Agente SSH",
    "Private key",                "Chave privada",
    "Passphrase",                 "Passphrase",
    "Mode",                       "Modo",
    "CA certificate",             "Certificado da CA",
    "Client certificate",         "Certificado do cliente",
    "Client key",                 "Chave do cliente",

    // Aba Inicializacao
    "Transactions",               "Transações",
    "Auto-commit",                "Auto-commit",
    "Read-only connection",       "Conexão somente leitura",
    "Session",                    "Sessão",
    "Default schema",             "Schema padrão",
    "Initialization queries",     "Consultas de inicialização",
    "Connection",                 "Conexão",
    "Timeout (s)",                "Timeout (s)",
    "Keep-alive",                 "Keep-alive",
    "Interval (s)",               "Intervalo (s)",
    "Close idle connections",     "Fechar conexões ociosas",

    // Aba Geral
    "Identification",             "Identificação",
    "Connection name",            "Nome da conexão",
    "Description",                "Descrição",
    "Folder",                     "Pasta",
    "Connection type",            "Tipo de conexão",
    "Development",                "Desenvolvimento",
    "Test",                       "Teste",
    "Production",                 "Produção",

    // Estados de driver
    "protocol in development",    "protocolo em desenvolvimento",
    "planned for phase 3",        "planejado para a fase 3",
    "TDS planned for phase 3",    "TDS planejado para a fase 3",
    "out of scope for v1",        "fora do escopo da v1",
    "uses the PostgreSQL driver", "usa o driver PostgreSQL",

    // --- Mensagens com formatacao ---
    "%zu row(s), %zu column(s)",  "%zu linha(s), %zu coluna(s)",
    "%zu row(s) x %zu column(s)  |  %zu bytes",
        "%zu linha(s) × %zu coluna(s)  |  %zu bytes",
    "connected | %zu schema(s), %zu table(s), %zu FK(s)",
        "conectado | %zu schema(s), %zu tabela(s), %zu FK(s)",
    "connecting to %s:%u...",     "conectando a %s:%u...",
    "disconnected",               "desconectado",
    "  Ln %zu, Col %zu  |  %zu lines%s", "  Ln %zu, Col %zu  |  %zu linhas%s",
    "%zu query(s)  |  including internal catalog ones",
        "%zu query(s)  |  inclusive as internas de catálogo",
    "%zu rows",                   "%zu linhas",

    // --- Erros ---
    "connection closed",          "conexão fechada",
    "connection failed",          "falha na conexão",
    "authentication failed",      "falha na autenticação",
    "query failed",               "falha na query",
    "cancelled",                  "cancelado",
    "timed out",                  "tempo esgotado",
};

} // namespace

// Registro EXPLICITO, chamado de load_builtin_catalogs().
//
// Nao usamos um objeto global com construtor: nada referenciaria esta unidade
// de traducao, e o linker com /OPT:REF a descartaria inteira -- o catalogo
// simplesmente nao existiria no binario. Foi exatamente o que aconteceu na
// primeira tentativa: o menu de idiomas so' listava English.
void register_pt_br() {
    register_catalog("pt-BR", "Portuguese (Brazil)", "Português (Brasil)",
                     kPtBr, sizeof(kPtBr) / sizeof(kPtBr[0]));
}

} // namespace otter::i18n
