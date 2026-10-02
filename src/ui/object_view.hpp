// C-Otter -- ui/object_view.hpp
//
// O estado de UM editor de objeto aberto: qual objeto, em que aba esta', o
// que o usuario mudou e ainda nao gravou.
//
// O editor de objeto do DBeaver e' uma aba da area de edicao, ao lado dos
// scripts: "Properties" (com as secoes Columns, Constraints, ..., DDL numa
// lista a' esquerda) e "Data". Aqui ele e' um SqlDocument com este estado --
// a aba, o resultado e a grade ja' existem la'.
#pragma once

#include "db/object_info.hpp"

#include <map>
#include <string>

namespace otter::ui {

struct ObjectView {
    db::ObjectRef ref;

    enum class Page : std::uint8_t { properties, data };
    Page page = Page::properties;
    // Pedido de troca de aba vindo de fora ("View data" na arvore): o ImGui
    // so' aceita selecionar uma aba no quadro em que ela e' desenhada.
    bool select_page = false;

    // A secao escolhida na lista da esquerda, pelo ROTULO em ingles: o
    // conjunto de secoes muda com o tipo, e um indice apontaria para outra.
    std::string section;

    // Propriedades alteradas e nao gravadas: rotulo -> valor novo.
    std::map<std::string, std::string> edits;

    // A aba Data ja' disparou o SELECT.
    bool data_started = false;

    // O DDL (ou o fonte) ja' foi posto no editor de texto desta aba, e de
    // qual leitura veio: uma releitura do objeto precisa trocar o texto, mas
    // redesenhar nao pode apagar o que o usuario esta' digitando.
    bool        source_loaded = false;
    std::string source_original;   // como o editor de texto o devolve
    std::string source_server;     // como o servidor o entregou

    // Aba Permissions: o papel escolhido na lista da esquerda.
    std::string grantee = "PUBLIC";

    // Pedidos de "Next tab" / "Previous tab" / "Open source tab": a lista de
    // secoes so' e' conhecida onde ela e' desenhada.
    int  step_section = 0;
    bool goto_source  = false;
};

} // namespace otter::ui
