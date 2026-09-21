// C-Otter -- catalogo pt-BR embutido.
//
// Serve de prova de que a i18n funciona e de referencia de formato para quem
// for traduzir para outro idioma. Um idioma novo pode vir daqui (embutido) ou
// de um arquivo .lang ao lado do executavel -- nenhum dos dois exige tocar em
// codigo de UI.
#include "base/i18n.hpp"

#include <span>

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
    "Execute script",             "Executar script",
    "Format SQL",                 "Formatar SQL",
    "Explain plan",               "Explicar plano",
    "Execution plan",             "Plano de execução",
    "Run the query and measure (ANALYZE)",
    "Executar a consulta e medir (ANALYZE)",
    "ANALYZE executes the query. Writes are rolled back, but the work is "
    "done and the time is real.",
    "ANALYZE executa a consulta. Escritas são desfeitas, mas o trabalho "
    "é feito e o tempo é real.",
    "Explain again",              "Explicar de novo",
    "  explaining...",            "  explicando...",
    "planning %.2f ms  |  execution %.2f ms",
    "planejamento %.2f ms  |  execução %.2f ms",
    "estimated cost %.2f  |  query not executed",
    "custo estimado %.2f  |  consulta não executada",
    "estimate off by %.0fx",      "estimativa errada em %.0fx",
    "Copy JSON",                  "Copiar JSON",
    "Copy the raw EXPLAIN output","Copia a saída crua do EXPLAIN",
    "plan analyzed in %.2f ms (rolled back)",
    "plano analisado em %.2f ms (desfeito)",
    "plan estimated (query not executed)",
    "plano estimado (consulta não executada)",
    "running statement %zu of %zu", "executando comando %zu de %zu",
    "statement %zu failed: %s",   "comando %zu falhou: %s",
    "%zu statement(s) executed",  "%zu comando(s) executado(s)",
    "%zu statement(s), %zu row(s) affected",
    "%zu comando(s), %zu linha(s) afetada(s)",
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
    "no connection",              "nenhuma conexão",
    "connect to browse the schema", "conecte-se para navegar o schema",
    "loading...",                 "carregando...",
    "Copy name",                  "Copiar nome",
    "Close connection",           "Fechar conexão",

    // --- Grade de resultado e paginacao ---
    "First page",                 "Primeira página",
    "Set NULL",                   "Definir NULL",
    "Delete row",                 "Excluir linha",
    "Undo delete",                "Desfazer exclusão",
    "Duplicate row",             "Duplicar linha",
    "New row",                    "Nova linha",
    "Remove row",                 "Remover linha",
    "(default)",                  "(padrão)",
    "Revert cell",                "Desfazer célula",
    "View value...",              "Ver valor...",
    "Value",                      "Valor",
    "multiline text",             "texto com várias linhas",
    "binary",                     "binário",
    "boolean",                    "booleano",
    "Copy value",                 "Copiar valor",
    "was: %s",                    "antes: %s",
    "Save changes",               "Salvar alterações",
    "Discard",                    "Descartar",
    "Run the UPDATEs in a transaction",
    "Executa os UPDATE numa transação",
    "Throw the pending changes away",
    "Descarta as alterações pendentes",
    "%zu change(s) in %zu row(s), not saved",
    "%zu alteração(oes) em %zu linha(s), não salvas",
    "read-only: %s",              "somente leitura: %s",
    "the result joins more than one table",
    "o resultado junta mais de uma tabela",
    "the result has no source table",
    "o resultado não vem de uma tabela",
    "the table has no primary key",
    "a tabela não tem chave primária",
    "the key columns are not in the result",
    "as colunas da chave não estão no resultado",
    "no result to edit",          "sem resultado para editar",
    "loading the table keys...",  "lendo as chaves da tabela...",
    "Apply filter",               "Aplicar filtro",
    "Filter objects...",          "Filtrar objetos...",
    "Group by this column",       "Agrupar por esta coluna",
    "Ungroup",                    "Desagrupar",
    "Aggregate",                  "Agregar",
    "Clear grouping",             "Limpar agrupamento",
    "Grouped by",                 "Agrupado por",
    "Total",                      "Total",
    "over this page only (%zu rows)",
    "apenas sobre esta página (%zu linhas)",
    "Compute on the server",      "Calcular no servidor",
    "count",                      "contagem",
    "count non-null",             "contagem não nula",
    "distinct",                   "distintos",
    "sum",                        "soma",
    "average",                    "média",
    "minimum",                    "mínimo",
    "maximum",                    "máximo",
    "Clear filter",               "Limpar filtro",
    "Sort ascending",             "Ordenar crescente",
    "Sort descending",            "Ordenar decrescente",
    "Copy column name",           "Copiar nome da coluna",
    "WHERE %s ...",               "WHERE %s ...",
    "> 100   |   LIKE '%lontra%'   |   IS NULL",
    "> 100   |   LIKE '%lontra%'   |   IS NULL",
    "filtering needs a paged result",
    "o filtro exige um resultado paginado",
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

    // --- Conexoes salvas e importacao (ADR 0012) ---
    "this database is not supported yet",
    "este banco ainda não é suportado",
    "saved",                      "salvas",

    // --- DDL de escrita (docs/DDL-WRITE.md) ---
    "Review SQL",                 "Conferir SQL",

    // --- Cor condicional (ADR 0005) ---
    "Color",                      "Cor",

    // --- Particoes e eventos (arvore MySQL) ---
    "Partitions",                 "Partições",
    "System info",                "Informação do servidor",
    "Users",                      "Usuários",
    "no access to the user list", "sem acesso à lista de usuários",
    "no grants",                  "nenhum privilégio",
    "[locked]",                   "[bloqueada]",
    "[expired]",                  "[expirada]",
    "Session status",             "Status da sessão",
    "Global status",              "Status global",
    "Session variables",          "Variáveis da sessão",
    "Global variables",           "Variáveis globais",
    "Engines",                    "Engines",
    "Charsets",                   "Charsets",
    "... and more; use the filter", "... e mais; use o filtro",
    "not partitioned",            "não particionada",
    "View data",                  "Ver dados",
    "Events",                     "Eventos",
    "no events (or the scheduler is off)",
    "nenhum evento (ou o agendador está desligado)",
    "never executed",             "nunca executado",

    // --- Pivot (ADR 0005) ---
    "Pivot by this column",       "Pivotar por esta coluna",
    "Clear pivot",                "Limpar pivot",
    "nothing to pivot",           "nada a pivotar",
    "Pivot: %s by %s, %s of %s",  "Pivot: %s por %s, %s de %s",
    "showing %zu of %zu distinct values; group the data before pivoting",
    "mostrando %zu de %zu valores distintos; agrupe os dados antes de pivotar",
    "Heat map",                   "Mapa de calor",
    "Mark negatives",             "Marcar negativos",
    "Mark zeros",                 "Marcar zeros",
    "Mark nulls",                 "Marcar nulos",
    "(numeric columns only)",     "(só em colunas numéricas)",
    "Highlight rows where",       "Destacar linhas onde",
    "too many distinct values",   "valores distintos demais",
    "Clear color rules of this column",
    "Limpar regras de cor desta coluna",
    "Add index...",               "Acrescentar índice...",
    "New table...",               "Nova tabela...",
    "New table",                  "Nova tabela",
    "New view...",                "Nova view...",
    "New view",                   "Nova view",
    "Add column",                 "Acrescentar coluna",
    "Null",                       "Nulo",
    "Key",                        "Chave",
    "Query",                      "Consulta",
    "Replace if it exists",       "Substituir se já existir",
    "CREATE OR REPLACE preserves the grants on the view; dropping and recreating loses them",
    "CREATE OR REPLACE preserva os privilégios da view; remover e recriar os perde",
    "(every column needs a name and a type)",
    "(toda coluna precisa de nome e tipo)",
    "Create table %s",            "Criar tabela %s",
    "Create view %s",             "Criar view %s",
    "Add index",                  "Acrescentar índice",
    "Drop index",                 "Remover índice",
    "Drop constraint",            "Remover constraint",
    "Drop foreign key",           "Remover chave estrangeira",
    "expand Indexes first",       "expanda Índices antes",
    "expand Constraints first",   "expanda Constraints antes",
    "expand Foreign keys first",  "expanda Chaves estrangeiras antes",
    "belongs to a constraint; drop the constraint instead",
    "pertence a uma constraint; remova a constraint",
    "Unique",                     "Único",
    "Concurrently",               "Concorrente",
    "does not block writes, but cannot run inside a transaction",
    "não bloqueia escrita, mas não roda dentro de transação",
    "Columns",                    "Colunas",
    "(name and one column)",      "(nome e uma coluna)",
    "Add index %s to %s",         "Acrescentar índice %s em %s",
    "Drop index %s",              "Remover índice %s",
    "Drop constraint %s",         "Remover constraint %s",
    "Drop foreign key %s",        "Remover chave estrangeira %s",
    "an index needs a name and at least one column",
    "um índice precisa de nome e ao menos uma coluna",
    "index name is required",     "o nome do índice é obrigatório",
    "this index belongs to a constraint; drop the constraint instead",
    "este índice pertence a uma constraint; remova a constraint",
    "this constraint needs at least one column",
    "esta constraint precisa de ao menos uma coluna",
    "a CHECK constraint needs an expression",
    "uma constraint CHECK precisa de uma expressão",
    "constraint name is required", "o nome da constraint é obrigatório",
    "foreign key name is required", "o nome da chave estrangeira é obrigatório",
    "a foreign key needs source columns, a target table and target columns",
    "uma chave estrangeira precisa de colunas de origem, tabela e colunas de destino",
    "the number of source and target columns must match",
    "o número de colunas de origem e destino precisa ser o mesmo",
    "Type",                       "Tipo",
    "Default",                    "Padrão",
    "Comment",                    "Comentário",
    "Alter",                      "Alterar",
    "Add column...",              "Acrescentar coluna...",
    "Drop column",                "Remover coluna",
    "Rename table...",            "Renomear tabela...",
    "Rename table",               "Renomear tabela",
    "Drop table...",              "Remover tabela...",
    "New name",                   "Nome novo",
    "Nullable",                   "Aceita nulo",
    "Position",                   "Posição",
    "last",                       "no fim",
    "first",                      "no início",
    "after",                      "depois de",
    "Copy",                       "Copiar",
    "Stop editing",               "Parar de editar",
    "I understand",               "Eu entendo",
    "This drops data or structure permanently.",
    "Isto apaga dados ou estrutura permanentemente.",
    "(cannot be undone)",         "(não tem desfazer)",
    "(not connected, or busy)",   "(sem conexão, ou ocupado)",
    "(name and type)",            "(nome e tipo)",
    "this database commits each DDL statement on its own: if one fails, the previous ones stay applied",
    "este banco confirma cada comando DDL sozinho: se um falhar, os anteriores ficam aplicados",
    "Add column %s to %s",        "Acrescentar coluna %s em %s",
    "Drop column %s from %s",     "Remover coluna %s de %s",
    "Rename %s to %s",            "Renomear %s para %s",
    "Drop %s",                    "Remover %s",
    "nothing to change",          "nada a alterar",
    "table name is required",     "o nome da tabela é obrigatório",
    "table columns are not loaded yet",
    "as colunas da tabela ainda não foram carregadas",
    "new column needs a name and a type",
    "a coluna nova precisa de nome e tipo",
    "a table needs at least one column",
    "uma tabela precisa de ao menos uma coluna",
    "object name is required",    "o nome do objeto é obrigatório",
    "dropping this object type is not supported",
    "remover este tipo de objeto não é suportado",
    "native PostgreSQL and MySQL protocols",
    "protocolos PostgreSQL e MySQL nativos",
    "double-click to connect",    "duplo clique para conectar",
    "no driver for '%s'",
    "não há driver para '%s'",
    "connected, but there is no catalog reader for '%s'",
    "conectado, mas não há leitor de catálogo para '%s'",
    "SQLite driver is not implemented yet",
    "o driver SQLite ainda não foi implementado",
    "Oracle driver is planned for a later phase",
    "o driver Oracle está previsto para uma fase posterior",
    "SQL Server driver is planned for a later phase",
    "o driver SQL Server está previsto para uma fase posterior",
    "generic JDBC has no equivalent without a JVM",
    "JDBC genérico não tem equivalente sem uma JVM",
    "Import from DBeaver...",     "Importar do DBeaver...",
    "Import from DBeaver",        "Importar do DBeaver",
    "Import selected",            "Importar selecionadas",
    "Rescan",                     "Procurar de novo",
    "Name",                       "Nome",
    "Driver",                     "Driver",
    "Server",                     "Servidor",
    "Status",                     "Estado",
    "with password",              "com senha",
    "no password",                "sem senha",
    "%zu connection(s) imported",  "%zu conexão(oes) importada(s)",
    "weak encryption, for DBeaver compatibility",
    "criptografia fraca, por compatibilidade com o DBeaver",
    "No DBeaver workspace found on this machine.",
    "Nenhum workspace do DBeaver encontrado nesta máquina.",
    "%zu connection(s) found. Nothing is written back to DBeaver.",
    "%zu conexão(oes) encontrada(s). Nada é gravado de volta no DBeaver.",
    "Copy the selected connections into C-Otter",
    "Copia as conexões selecionadas para o C-Otter",
    "Look for DBeaver workspaces again",
    "Procura workspaces do DBeaver de novo",
    "The password is stored in credentials-config.json, encrypted the "
    "same way DBeaver does it.\n\n"
    "That encryption uses a fixed key published in DBeaver's source "
    "code: it protects against a casual look at the file, and against "
    "nothing more. Leave it off for credentials that matter.",
    "A senha fica em credentials-config.json, cifrada do mesmo jeito que o "
    "DBeaver faz.\n\n"
    "Essa cifragem usa uma chave fixa publicada no código-fonte do "
    "DBeaver: protege contra uma olhada casual no arquivo, e contra mais "
    "nada. Deixe desligado para credenciais que importam.",

    // --- Arvore de objetos ---
    "Export result...",           "Exportar resultado...",
    "Export result",              "Exportar resultado",
    "Header row",                 "Linha de cabeçalho",
    "Delimiter",                  "Separador",
    "Neutralize spreadsheet formulas", "Neutralizar fórmulas de planilha",
    "Target table",               "Tabela de destino",
    "One INSERT per row",         "Um INSERT por linha",
    "no options",                 "sem opções",
    "Preview",                    "Prévia",
    "Copy to clipboard",          "Copiar",
    "Save to file",               "Salvar em arquivo",
    "Copy the whole result, not just the preview",
    "Copia o resultado inteiro, não só a prévia",
    "Write the result to the file above",
    "Grava o resultado no arquivo acima",
    "exports the current page only (%zu rows)",
    "exporta apenas a página atual (%zu linhas)",
    "... and %zu more row(s)",    "... e mais %zu linha(s)",
    "%zu row(s) copied",          "%zu linha(s) copiada(s)",
    "%zu row(s) written to %s",   "%zu linha(s) gravada(s) em %s",
    "%zu row(s), %zu column(s)",  "%zu linha(s), %zu coluna(s)",
    "Count rows",                 "Contar linhas",
    "Generate SQL",               "Gerar SQL",
    "expand the table first",     "expanda a tabela antes",
    "Copy qualified name",        "Copiar nome qualificado",
    "Refresh",                    "Atualizar",
    "Tables",                     "Tabelas",
    "Data types",                 "Tipos de dados",
    "Views",                      "Views",
    "Materialized views",         "Views materializadas",
    "Definition",                 "Definição",
    "Copy the definition to the clipboard",
    "Copia a definição para a área de transferência",
    "Open in editor",             "Abrir no editor",
    "Open the definition in a new SQL tab",
    "Abre a definição numa nova aba de SQL",
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
    "Open script...",             "Abrir script...",
    "Save script",                "Salvar script",
    "Save script as...",          "Salvar script como...",
    "Open script",                "Abrir script",
    "SQL scripts",                "Scripts SQL",
    "Text files",                 "Arquivos de texto",
    "cannot open %s",             "não foi possível abrir %s",
    "cannot write %s",            "não foi possível gravar %s",
    "write failed: %s",           "falha ao gravar: %s",
    "saved to %s",                "salvo em %s",
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
    "Category",                   "Categoria",
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
    // A chave antiga ficou: um catalogo de traducao e' consultado por TEXTO,
    // e remover a chave de uma versao anterior nao quebra nada -- mas manter
    // custa uma linha e cobre um binario antigo lendo catalogo novo.
    "Not implemented - the protocol does not negotiate TLS yet.",
        "Não implementado — o protocolo ainda não negocia TLS.",
    "TLS is not available in this build of C-Otter.",
        "O TLS não está disponível nesta compilação do C-Otter.",
    "This mode accepts an unencrypted connection. Use 'require' or stronger to actually require TLS.",
        "Este modo aceita conexão sem criptografia. Use 'require' ou mais forte "
        "para realmente exigir TLS.",
    "Certificate files are saved but not used yet; validation uses the system certificate store.",
        "Os arquivos de certificado são salvos, mas ainda não são usados; a "
        "validação usa o repositório de certificados do sistema.",
    "Encrypted connection",       "Conexão criptografada",

    // Comandos de linha da grade. "de cima"/"de baixo" e nao "acima"/"abaixo":
    // o comando copia DAQUELA linha, nao para uma direcao.
    "Copy from row above",        "Copiar da linha de cima",
    "Copy from row below",        "Copiar da linha de baixo",

    // Visao de registro unico. "Registro" e nao "linha": na visao o que se ve'
    // e' um cadastro inteiro, e chama-lo de linha contradiz o que esta' na
    // tela -- os campos estao empilhados, nao em linha.
    "Single record view (Tab)",   "Visão de registro único (Tab)",
    "Back to the grid (Tab)",     "Voltar para a grade (Tab)",
    "record %zu of %zu",          "registro %zu de %zu",
    "Previous record",            "Registro anterior",
    "Next record",                "Próximo registro",
    // "Value" e "Type" ja' estao no catalogo (painel de valor, diagrama) --
    // repeti-los daria chave duplicada, que o teste do catalogo recusa.
    "Column",                     "Coluna",
    "no rows",                    "sem linhas",

    // Barra na celula (ADR 0005). "Barra" e nao "sparkline": o termo ingles
    // e' de quem le' Tufte, nao de quem usa um cliente de banco.
    "Connection name. Empty uses \"database@host\".",
        "Nome da conexão. Vazio usa \"banco@host\".",

    "Bar",                        "Barra",
    "Bar from zero",              "Barra a partir do zero",
    "Bar over the column range",  "Barra na faixa da coluna",
    "Bar centered on zero",       "Barra centrada no zero",
    "Remove the bar of this column", "Remover a barra desta coluna",
    "for quantities: revenue, count, total",
        "para quantidades: receita, contagem, total",
    "for narrow ranges far from zero, like 36.1..36.9, where anchoring at zero makes every bar look the same",
        "para faixas estreitas e longe do zero, como 36,1..36,9, onde ancorar "
        "no zero deixa todas as barras iguais",
    "for variation and balance, where the sign is the point",
        "para variação e saldo, onde o sinal é o que importa",
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

    // --- Arvore de paginas do dialogo de conexao (docs/DIALOG-PARITY.md) ---
    //
    // Rotulos das categorias: sao os do DBeaver, e a traducao segue a do
    // DBeaver em pt-BR -- quem usa as duas ferramentas ve a mesma palavra.
    "Connection settings",        "Configurações de conexão",
    "Internal parameters",        "Parâmetros internos",
    "Metadata",                   "Metadados",
    "Errors and timeouts",        "Erros e tempos limite",
    "Data Transfer",              "Transferência de dados",
    "Data Editor",                "Editor de dados",
    "Binary Editor",              "Editor binário",
    "Data Formats",               "Formatos de dados",
    "Grid",                       "Grade",
    "SQL Editor",                 "Editor SQL",
    "Code Completion",            "Completar código",
    "Code Editor",                "Editor de código",
    "Formatting",                 "Formatação",
    "SQL Processing",             "Processamento SQL",

    // Rodape, na ordem do DBeaver.
    "Test Connection ...",        "Testar conexão ...",
    "OK",                         "OK",

    // Paginas que a arvore lista mas o C-Otter ainda nao implementou.
    "Not implemented yet.",       "Ainda não implementado.",
    "The DBeaver dialog has this page. It is listed here so the structure "
    "matches; the options are not available in this version.",
        "O diálogo do DBeaver tem esta página. Ela aparece aqui para a "
        "estrutura corresponder; as opções não existem nesta versão.",
    "Error handling and query timeouts",
        "Tratamento de erros e tempo limite de consulta",
    "Import and export defaults for this connection",
        "Padrões de importação e exportação desta conexão",
    "Data editor defaults for this connection",
        "Padrões do editor de dados desta conexão",
    "Grid appearance for this connection",
        "Aparência da grade desta conexão",
    "Binary and BLOB display format",
        "Formato de exibição de binários e BLOB",
    "Number, date and time formats",
        "Formatos de número, data e hora",
    "SQL editor defaults for this connection",
        "Padrões do editor SQL desta conexão",
    "Code completion behaviour for this connection",
        "Comportamento do completar código desta conexão",
    "Code editor behaviour for this connection",
        "Comportamento do editor de código desta conexão",
    "SQL formatting style for this connection",
        "Estilo de formatação SQL desta conexão",
    "Statement delimiters and execution options",
        "Delimitadores de comando e opções de execução",
    "Metadata reading options for this driver",
        "Opções de leitura de metadados deste driver",

    // --- Modelos de autenticacao (estavam como literais em portugues) ---
    "Database Native",            "Banco de dados nativo",
    "No Authentication",          "Sem autenticação",

    // --- Ajuda dos campos do dialogo de conexao ---
    "Lists every database on the server, not only the connected one.",
        "Lista todos os bancos do servidor, não apenas o conectado.",
    "Includes template0 and template1.",
        "Inclui template0 e template1.",
    "Includes databases the user has no permission to connect to.",
        "Inclui bancos aos quais o usuário não tem permissão de conectar.",
    "Computes the on-disk size of tables and indexes. On very large "
    "databases it makes expanding the tree slower.",
        "Calcula o tamanho em disco de tabelas e índices. Em bancos muito "
        "grandes, torna a expansão da árvore mais lenta.",
    "Loads the columns of each key along with the key. Useful for "
    "JOIN inference; costs one extra query.",
        "Carrega as colunas de cada chave junto com a chave. Útil para "
        "inferência de JOIN; custa uma consulta a mais.",
    "Runs SET ROLE when opening the connection.",
        "Executa SET ROLE ao abrir a conexão.",
    "Not implemented - the settings are saved, but the tunnel is "
    "not established.",
        "Não implementado — a configuração é salva, mas o túnel não é "
        "estabelecido.",
    "When off, every change needs an explicit commit. Production "
    "connections start with auto-commit off.",
        "Desligado, cada alteração exige commit explícito. Conexões de "
        "produção começam com auto-commit desligado.",
    "Run in order, right after the connection is established.",
        "Executadas na ordem, logo após a conexão ser estabelecida.",
    "Groups the connection in the tree. Use / for subfolders.",
        "Agrupa a conexão na árvore. Use / para subpastas.",
    "auto-commit: %s | confirm execute: %s | confirm data change: %s",
        "auto-commit: %s | confirmar execução: %s | "
        "confirmar alteração de dados: %s",
    "on",                         "ligado",
    "off",                        "desligado",
    "yes",                        "sim",
    "no",                         "não",
    "Blocks INSERT, UPDATE, DELETE and DDL on the client.",
        "Bloqueia INSERT, UPDATE, DELETE e DDL no cliente.",
    "Sets search_path when connecting.",
        "Define o search_path ao conectar.",
    "Isolation level is not available in this version.",
        "O nível de isolamento não existe nesta versão.",

    // --- Estado da transacao na barra de status ---
    //
    // "Auto"/"None" sao os rotulos do TransactionMonitorToolbar do DBeaver.
    // Mantidos curtos: a barra e' estreita e o numero e' o que importa.
    "Auto",                       "Auto",
    "None",                       "Nenhuma",
    "Failed",                     "Falhou",
    "Auto-commit: each statement commits on its own.",
        "Auto-commit: cada comando confirma sozinho.",
    "Transaction aborted; only rollback is accepted.",
        "Transação abortada; só rollback é aceito.",
    "%zu modifying statement(s) pending",
        "%zu comando(s) de alteração pendente(s)",
    "Current schema",             "Schema corrente",

    // --- Confirmacao de saida ---
    "Exit C-Otter",               "Sair do C-Otter",
    "There is work that was not saved.",
        "Há trabalho que não foi salvo.",
    "%zu script(s) with unsaved text",
        "%zu script(s) com texto não salvo",
    "%zu cell edit(s) not written to the database",
        "%zu alteração(oes) de célula não gravada(s) no banco",
    "Exit and discard",           "Sair e descartar",

    // --- Inspetor de queries ---
    "Show:",                      "Mostrar:",
    "failed only",                "só as que falharam",
    "catalog queries",            "queries de catálogo",
    "The queries C-Otter runs on its own to read the catalog. DBeaver hides "
    "these.",
        "As queries que o C-Otter executa por conta própria para ler o "
        "catálogo. O DBeaver as esconde.",
    "Clear log",                  "Limpar log",
    "%zu of %zu query(s)",        "%zu de %zu query(s)",
    "Open in SQL editor",         "Abrir no editor SQL",
    "Copy error",                 "Copiar erro",

    // --- Autocomplete ---
    "foreign key",                "chave estrangeira",

    // --- Renomear aba ---
    "Rename tab...",              "Renomear aba...",
    "Rename tab",                 "Renomear aba",
    "Empty restores the default name.",
        "Vazio volta ao nome padrão.",

    // --- Contagem sob demanda ---
    "of %zu",                     "de %zu",
    "Count the whole result (one more full scan)",
        "Contar o resultado inteiro (mais uma varredura completa)",

    // --- Pagina Formatação (main.sql.format) ---
    "Keywords",                   "Palavras-chave",
    "As typed",                   "Como digitado",
    "UPPERCASE",                  "MAIÚSCULAS",
    "lowercase",                  "minúsculas",
    "Case",                       "Caixa",
    "Applies when formatting (Ctrl+Shift+F). It does not change what you type.",
        "Vale ao formatar (Ctrl+Shift+F). Não muda o que você digita.",
    "Layout",                     "Disposição",
    "Indent width",               "Largura da indentação",
    "River style",                "Estilo rio",
    "Aligns the main clauses to the right, like psql:\n"
    "  SELECT a, b\n"
    "    FROM t\n"
    "   WHERE x\n\n"
    "Off produces the style more common in source code, with every clause at "
    "the left margin.",
        "Alinha as cláusulas principais à direita, como o psql:\n"
        "  SELECT a, b\n"
        "    FROM t\n"
        "   WHERE x\n\n"
        "Desligado produz o estilo mais comum em código, com cada cláusula na "
        "margem esquerda.",
    "Wrap the SELECT list after", "Quebrar a lista do SELECT após",
    "One column per line when the list has more items than this. Short lists "
    "fit on one line and read better that way.",
        "Uma coluna por linha quando a lista passa disso. Listas curtas cabem "
        "numa linha e ficam melhores assim.",

    // --- Pagina Completar código (main.sql.completion) ---
    "When to suggest",            "Quando sugerir",
    "Suggest while typing",       "Sugerir enquanto digita",
    "Off, completion only opens with Ctrl+Space.",
        "Desligado, o completar só abre com Ctrl+Espaço.",
    "Delay (ms)",                 "Atraso (ms)",
    "How long to wait after a keystroke before opening the list. Zero opens "
    "immediately, which gets in the way when typing fast.",
        "Quanto esperar depois de uma tecla antes de abrir a lista. Zero abre "
        "na hora, o que atrapalha quem digita rápido.",
    "Where to suggest",           "Onde sugerir",
    "Inside comments",            "Dentro de comentários",
    "Inside strings",             "Dentro de strings",
    "Off by default: a table name suggested inside a string literal would be "
    "inserted as text, not as an identifier.",
        "Desligado por padrão: um nome de tabela sugerido dentro de uma "
        "string entraria como texto, não como identificador.",
    "Behaviour",                  "Comportamento",
    "Insert a single match automatically",
        "Inserir sozinho quando houver um só",
    "With one candidate only, insert it without showing the list. Saves a "
    "keystroke, but surprises when the single match is not what you meant.",
        "Com um candidato só, insere sem mostrar a lista. Economiza uma "
        "tecla, mas surpreende quando o único achado não era o pretendido.",

    // --- Pagina Editor de código (main.sql.codeeditor) ---
    "Indentation",                "Indentação",
    "Tab size",                   "Tamanho da tabulação",
    "How many columns a tab takes ON SCREEN. It does not change what is "
    "written to the file.",
        "Quantas colunas uma tabulação ocupa NA TELA. Não muda o que é "
        "gravado no arquivo.",
    "Auto-indent",                "Indentar sozinho",
    "A new line starts at the same indentation as the previous one.",
        "A linha nova começa na mesma indentação da anterior.",
    "Display",                    "Exibição",
    "Line numbers",               "Números de linha",
    "Matching brackets",          "Parênteses correspondentes",
    "Highlights the pair of the bracket under the cursor. Turning it off "
    "also turns off block folding, which depends on it.",
        "Realça o par do parêntese sob o cursor. Desligar também desliga a "
        "dobra de blocos, que depende dele.",
    "Show whitespace",            "Mostrar espaços",
    "(not working yet)",          "(ainda não funciona)",
    "Draws spaces and tabs. The option reaches the editor but nothing is "
    "drawn -- a defect in the text widget, not in the setting.",
        "Desenha espaços e tabulações. A opção chega ao editor mas nada é "
        "desenhado — defeito no widget de texto, não na configuração.",
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

// Exposto para o teste conferir que nao ha' chave repetida. Uma repeticao com
// traducoes DIFERENTES e' um defeito de verdade: qual vence depende da ordem
// de insercao, e o texto da tela muda sem ninguem ter mexido nele.
std::span<const char* const> pt_br_entries() {
    return {kPtBr, sizeof(kPtBr) / sizeof(kPtBr[0])};
}

} // namespace otter::i18n
