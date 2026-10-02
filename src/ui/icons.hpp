// C-Otter -- ui/icons.hpp
//
// Icones desenhados vetorialmente com o DrawList do ImGui.
//
// Sem fonte de icones nem atlas de imagem: os simbolos sao primitivas
// (linhas, arcos, poligonos) que escalam com o DPI sem borrar e herdam a cor
// do tema automaticamente. Tambem evita mais um arquivo na distribuicao.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

struct ImVec2;
struct ImDrawList;

namespace otter::ui {

enum class Icon : std::uint8_t {
    connect,        // plugue
    disconnect,
    play,           // executar
    stop,
    commit,         // check
    rollback,       // seta em curva
    database,       // cilindro
    table,
    view,
    column,
    key,            // chave primaria
    folder,
    refresh,
    search,
    settings,       // engrenagem
    plus,
    close,
    pin,
    save,
    open,
    copy,
    chevron_left,
    chevron_right,
    first_page,         // chevron com barra
    last_page,
    chevron_down,
    warning,
    error,
    info,
    clock,
    lock,          // canal cifrado (TLS)
    record,        // visao de registro unico (uma linha por vez)
    filter,

    // Tipos de objeto do banco. Cada um tem desenho proprio: reaproveitar um
    // simbolo generico para dois tipos diferentes torna a arvore ilegivel de
    // relance (diretiva 5 do CLAUDE.md).
    materialized_view,  // olho sobre disco: view com dados persistidos
    index,              // paginas com marcador
    constraint,         // escudo
    foreign_key,        // elo de corrente
    references,         // setas convergindo
    sequence,           // degraus ascendentes
    function,           // f(x)
    procedure,          // bloco com engrenagem
    trigger,            // raio
    data_type,          // chaves {} com nucleo
    extension,          // peca de quebra-cabeca
    role,               // silhueta com chave
    tablespace,         // discos empilhados
    schema,             // grade ramificada
    pivot,              // coluna virando linha, com seta
    partition,          // cilindro fatiado
    event,              // relogio com marca de repeticao
    user,               // silhueta
    grant,              // pergaminho com selo

    // Os nos da arvore unica (ADR 0018). Um por tipo, pelo mesmo motivo dos
    // de cima: o DBeaver usa a mesma pasta laranja para Indexes, Functions,
    // Sequences e Data types, e so' o rotulo os separa.
    foreign_table,       // tabela com seta para fora: os dados moram noutro servidor
    aggregate,           // sigma: a funcao que resume varias linhas numa
    dependency,          // um no cheio e dois vazios pendurados: quem depende deste
    rule,                // duas setas opostas: a consulta reescrita
    policy,              // linhas com uma faixa cheia: so algumas linhas passam
    inheritance,         // caixa-mae, triangulo vazado, caixa-filha
    parameter,           // parenteses com um ponto: o argumento
    event_trigger,       // raio dentro de um anel: dispara por evento do banco
    storage,             // disco rigido: prato e braco
    foreign_wrapper,     // dois blocos ligados por pinos: o adaptador
    foreign_server,      // globo: o servidor remoto
    user_mapping,        // silhueta apontando para uma caixa
    setting,             // dois controles deslizantes
    role_group,          // duas silhuetas: o papel que e grupo, sem login
    access_method,       // arvore de blocos: raiz e tres folhas
    operator_class,      // menor-ou-igual
    operator_family,     // dois circulos sobrepostos: o conjunto de classes
    encoding,            // grade de bits, alguns acesos
    collation,           // seta para baixo e barras crescentes: a ordem
    language,            // colchetes angulares com barra
    extension_available, // caixa aberta recebendo uma seta: ainda por instalar
    administer,          // caixa de ferramentas
    system_info,         // monitor com um i
    sessions,            // janela de terminal com o prompt
    locks,               // cadeado pequeno com a lista ao lado
    synonym,             // dois nomes ligados por uma seta: um aponta para o outro
    job,                 // prancheta com check
    job_step,            // tres marcos ligados, o do meio cheio
    job_schedule,        // folha de calendario

    // A barra lateral do editor SQL -- os botoes do `sqlEditor.side.top` e
    // `side.bottom` do DBeaver.
    play_new,            // play com um +: executa abrindo outra aba de resultado
    play_script,         // folha com play: executa o script inteiro
    plan,                // arvore de nos de custo: o plano de execucao
    ai,                  // faisca de quatro pontas
    terminal,            // prompt com o cursor em bloco
    server_output,       // balao de fala com linhas: o que o servidor disse
    exec_log,            // lista com relogio: o que ja rodou
    variables,           // x entre chaves: variaveis do script
    outline,             // linhas recuadas com marcadores: a estrutura

    // As pastas da arvore do DBeaver. La' cada tipo de pasta tem o seu
    // desenho (tree/folder_*.svg), e as sem tipo usam `folder`. No conjunto
    // vetorial do C-Otter sao a pasta com uma marca que as distingue.
    folder_database,     // pasta de bancos
    folder_schema,       // pasta de schemas
    folder_table,        // pasta de tabelas
    folder_view,         // pasta de views
    folder_link,         // pasta de objetos externos
    folder_user,         // pasta de usuarios e roles
    folder_constraint,   // pasta de constraints
    folder_columns,      // pasta de colunas
    folder_admin,        // pasta de administracao
    folder_info,         // pasta de informacao
    object_page,         // objeto sem icone proprio no DBeaver: a pagina generica

    // A grade de resultado: a barra de baixo, a de filtro e os paineis
    // (`sql/row_*.svg`, `misc/filter_*.svg` e `panel_*.svg` do DBeaver).
    accept,              // check num circulo: gravar as alteracoes
    reject,              // x num circulo: descarta-las
    row_add,             // linha com +
    row_copy,            // duas linhas, uma sobre a outra
    row_edit,            // linha com lapis
    row_delete,          // linha com x
    panels,              // janela dividida: os paineis do resultado
    panel_calc,          // sigma numa moldura: soma da selecao
    panel_grouping,      // tres barras alinhadas a' esquerda, a de cima maior
    panel_metadata,      // tabela com a primeira coluna cheia: nome e tipo
    panel_references,    // duas caixas ligadas por seta
    filter_apply,        // funil com check
    filter_reset,        // funil com x
    filter_config,       // funil com controle deslizante
    filter_value,        // funil com sinal de igual
    grid_mode,           // grade 3x3: a apresentacao em tabela

    // Um por SGBD. O DBeaver mostra o logo de cada banco na lista de
    // conexoes; aqui sao desenhos proprios, pelo mesmo motivo do resto
    // (diretriz 5): vetorial escala com DPI e herda a cor do tema.
    //
    // Distinguiveis no tamanho da arvore, que e' o criterio: o do PostgreSQL
    // e' redondo com a gota do elefante, o do MySQL e' anguloso com o
    // perfil do golfinho.
    pg_server,          // elefante estilizado -- PostgreSQL
    my_server,          // golfinho estilizado -- MySQL/MariaDB
    ms_server,          // cilindro com duas velas -- SQL Server
    sa_server,          // espiral aurea no retangulo -- SQL Anywhere (Sybase)
    generic_server,     // torre -- driver sem desenho proprio
};

// Numero de icones; serve para iterar sobre todos (galeria, testes).
// Ancorado no ULTIMO enumerador, que e' o que ele precisa contar.
//
// Ficou preso em `Icon::grant` quando os icones de SGBD entraram depois
// dele: a galeria passou a desenhar 47 de 50, e os tres novos nao apareciam
// em lugar nenhum -- nem no teste que exige desenho distinto por tipo.
// Apontar para generic_server so' adia o mesmo defeito; o comentario abaixo
// e' o aviso para quem acrescentar o proximo.
//
// AO ACRESCENTAR UM ICONE: poe antes desta linha e atualiza a ancora.
inline constexpr std::size_t kIconCount =
    static_cast<std::size_t>(Icon::generic_server) + 1;

// Icone do SGBD a partir do id do driver.
//
// Fica aqui, e nao no MainShell, porque o catalogo do dialogo de conexao
// tambem precisa dele -- e duas copias divergiriam ao acrescentar um driver.
//
// Driver sem desenho proprio cai na torre generica, em vez de reusar o de
// outro banco: dois SGBDs com o mesmo icone e' a divida que a diretriz 5
// nomeia.
[[nodiscard]] Icon driver_icon(std::string_view driver_id) noexcept;

// Desenha o icone centrado em `center`, com `size` de lado.
void draw_icon(Icon icon, const ImVec2& center, float size, std::uint32_t color,
               float thickness = 1.6f);

// Variante que desenha num DrawList explicito, em vez do da janela corrente.
//
// Existe para o teste: comparar os vertices gerados por cada icone e' o unico
// jeito de provar que dois tipos de objeto nao compartilham desenho. Fora do
// teste, prefira a sobrecarga acima.
void draw_icon_to(ImDrawList* dl, Icon icon, const ImVec2& center, float size,
                  std::uint32_t color, float thickness = 1.6f);

// Botao com icone e tooltip. `id` precisa ser unico no escopo do ImGui.
//
// O glow do tema e' aplicado no hover: um halo discreto atras do icone, que
// some por completo quando glow_strength e' 0 (tema claro, por exemplo).
//
// `box` e' o lado do botao; 0 usa o da barra de ferramentas. A barra lateral
// do editor passa um menor: dez botoes em coluna nao cabem no tamanho cheio.
//
// O tooltip aparece tambem no botao DESABILITADO: e' nele que vai o motivo
// de o comando nao estar disponivel (diretiva 6).
bool icon_button(const char* id, Icon icon, const char* tooltip,
                 bool enabled = true, std::uint32_t tint = 0, float box = 0.0f);

// --- Distancias de uma linha da arvore ---------------------------------------
//
// Pedido do usuario (2026-09-30): metade do espaco entre a seta de expandir e
// o icone, metade entre o icone e o titulo, e metade entre a borda do painel
// e a seta dos itens raiz (esta em ui/navigator.cpp). A linha ficava
// "esticada": borda, um vao, seta, um vao, icone, outro vao, nome.
//
// Um lugar so' para os dois numeros -- eram literais repetidos em cada no'.

// Quanto o icone recua em direcao a' seta. NEGATIVO: o TreeNode reserva para
// a seta bem mais do que o triangulo ocupa.
[[nodiscard]] float tree_arrow_gap();

// Continua na linha do TreeNode, com o icone ja' encostado na seta. Chamar
// logo depois de TreeNodeEx("##id"), antes de icon_inline().
//
// Nao e' SameLine(0, tree_arrow_gap()): para o ImGui, espacamento negativo
// quer dizer "use o padrao" -- o icone se AFASTAVA em vez de se aproximar.
void same_line_after_arrow();

// Espacamento entre o icone e o titulo.
[[nodiscard]] float tree_label_gap();

// Variante com rotulo a direita do icone.
bool icon_text_button(const char* id, Icon icon, const char* label,
                      const char* tooltip, bool enabled = true);

// Icone sem interacao, para arvores e tabelas.
void icon_inline(Icon icon, std::uint32_t color, float scale = 1.0f);

// Altura padrao de um botao da barra de ferramentas.
[[nodiscard]] float toolbar_button_size();

} // namespace otter::ui
