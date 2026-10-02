// C-Otter -- ui/app_settings.hpp
//
// Preferencias do PROGRAMA, nao de uma conexao: perfil de atalhos, tema e
// idioma. Ficam em `.C-Otter/settings.json`, na pasta do EXECUTAVEL, junto
// das conexoes e da disposicao das janelas (ADR 0020 -- produto portatil).
//
// Pedido do usuario (2026-09-30): "O config deve ficar numa pasta (...) na
// mesma pasta do executavel; se nao existir, deve criar e salvar o config com
// as opcoes default; o tema default e' ambar."
//
// Nao existia arquivo nenhum para isto. Tema e idioma eram escolhidos no menu
// e ESQUECIDOS ao fechar -- cada execucao voltava ao padrao. O perfil de
// atalhos e' o tipo de escolha que so' faz sentido se durar.
#pragma once

#include "ui/window_placement.hpp"

#include <map>
#include <string>
#include <vector>

namespace otter::ui {

struct AppSettings {
    // "dbeaver" ou "otter" (ui/commands.hpp). O padrao e' o do DBeaver: o
    // publico do C-Otter vem de la', e a mao ja' sabe aquelas teclas.
    std::string keymap = "dbeaver";

    // "dbeaver" (os originais de la') ou "otter" (os vetoriais, na cor do
    // tema). Ver ui/icon_images.hpp.
    std::string icons = "dbeaver";

    // "dark", "light" ou "amber" (ui/theme.hpp). O padrao e' o ambar.
    std::string theme = "amber";

    // Vazio = nao escolhido: vale o idioma do ambiente.
    std::string language;

    // Disposicao do editor: resultado embaixo (falso) ou ao lado.
    bool side_by_side = false;

    // "Show confirmation before save" da grade: mostrar o SQL das alteracoes
    // antes de grava-las. Desligado por padrao, como no DBeaver.
    bool confirm_data_save = false;

    // "Show multiple results in a single tab": os resultados de um script
    // empilhados na mesma aba, em vez de so' o ultimo.
    bool multiple_results = false;

    // --- Navegador ------------------------------------------------------------
    //
    // "Link with editor": a arvore acompanha a aba ativa.
    bool link_with_editor = false;
    // "Show all connections" desmarcado: so' as conectadas aparecem.
    bool connected_only = false;

    // Pastas de conexao SEM conexao dentro. As outras existem pelo campo
    // `folder` de cada perfil; uma pasta recem-criada ainda nao tem nenhum.
    std::vector<std::string> folders;

    // Os grupos de conexao que o usuario deixou FECHADOS. Guardar os fechados
    // (e nao os abertos) faz um grupo novo nascer aberto, que e' o padrao.
    // Pedido do usuario (2026-10-01): "Faca com que as pastas da arvore seja
    // salvo o ultimo estado aberto/fechado."
    std::vector<std::string> closed_folders;

    // Filtro de objetos por conexao (core.object.filter.*), pela chave do
    // nome da conexao. Mascaras separadas por virgula.
    struct Filter {
        bool        enabled = true;
        std::string include;
        std::string exclude;
    };
    std::map<std::string, Filter> filters;

    // Favoritos da arvore (core.navigator.bookmark.*).
    struct Bookmark {
        std::string title;
        std::string connection;   // nome da conexao
        std::string database;     // banco, quando nao e' o da conexao
        std::string type;         // db::to_string(ObjectType)
        std::string schema;
        std::string name;
        std::string parent;
        std::string signature;
    };
    std::vector<Bookmark> bookmarks;

    // --- "Advanced copy": as ultimas opcoes usadas -------------------------------
    std::string copy_column_delimiter = "\t";
    std::string copy_row_delimiter    = "\n";
    std::string copy_quote            = "\"";
    bool        copy_quote_always     = false;
    bool        copy_header           = false;
    bool        copy_row_numbers      = false;
    std::string copy_null_text;

    // --- Dashboard -----------------------------------------------------------------
    int                      dashboard_interval = 5;     // segundos entre leituras
    int                      dashboard_points   = 60;    // pontos guardados por serie
    // Vazio = os graficos padrao do SGBD.
    std::vector<std::string> dashboard_charts;

    // --- Janela principal ------------------------------------------------------
    //
    // Como estava ao fechar (ui/window_placement.hpp). Largura 0 = nada
    // gravado ainda: abre no tamanho padrao, centrada.
    WindowPlacement window;
};

// `<pasta do executavel>/.C-Otter/settings.json` (base/paths.hpp).
[[nodiscard]] std::string app_settings_path();

// Arquivo ausente ou ilegivel devolve os padroes: uma preferencia corrompida
// nao pode impedir o programa de abrir.
[[nodiscard]] AppSettings load_app_settings(const std::string& path);

// Falso se nao conseguiu gravar. Grava TODAS as opcoes, mesmo as que estao no
// padrao: o arquivo serve tambem de lista do que pode ser configurado.
bool save_app_settings(const std::string& path, const AppSettings& settings);

// Le o arquivo; se ele NAO existe, cria a pasta e o grava com os padroes.
// Um arquivo que existe mas nao pode ser lido nao e' sobrescrito -- pode ser
// uma configuracao com um erro de digitacao, e apaga-la seria perder o resto.
[[nodiscard]] AppSettings ensure_app_settings(const std::string& path);

} // namespace otter::ui
