#include "ui/icons.hpp"
#include "ui/hint.hpp"

#include "ui/icon_images.hpp"
#include "ui/theme.hpp"

#include "imgui.h"

#include <cmath>

namespace otter::ui {
namespace {

// Os icones sao desenhados numa caixa normalizada de -0.5..0.5 e escalados
// para `size`. Assim um mesmo desenho serve para a barra e para a arvore.
struct Canvas {
    ImDrawList*   dl;
    ImVec2        center;
    float         size;
    std::uint32_t color;
    float         thickness;

    [[nodiscard]] ImVec2 at(float x, float y) const {
        return ImVec2(center.x + x * size, center.y + y * size);
    }

    void line(float x1, float y1, float x2, float y2) const {
        dl->AddLine(at(x1, y1), at(x2, y2), color, thickness);
    }

    void rect(float x1, float y1, float x2, float y2, float rounding = 0.0f) const {
        dl->AddRect(at(x1, y1), at(x2, y2), color, rounding * size,
                    ImDrawFlags_None, thickness);
    }

    void rect_filled(float x1, float y1, float x2, float y2,
                     float rounding = 0.0f) const {
        dl->AddRectFilled(at(x1, y1), at(x2, y2), color, rounding * size);
    }

    void circle(float x, float y, float radius, int segments = 0) const {
        dl->AddCircle(at(x, y), radius * size, color, segments, thickness);
    }

    void circle_filled(float x, float y, float radius) const {
        dl->AddCircleFilled(at(x, y), radius * size, color, 0);
    }

    // Elipse achatada: base e topo do cilindro de banco de dados.
    void ellipse(float x, float y, float rx, float ry, bool filled = false) const {
        constexpr int kSegments = 24;
        ImVec2 points[kSegments];
        for (int i = 0; i < kSegments; ++i) {
            const float a = 2.0f * 3.14159265f * static_cast<float>(i) / kSegments;
            points[i] = at(x + rx * std::cos(a), y + ry * std::sin(a));
        }
        if (filled) {
            dl->AddConvexPolyFilled(points, kSegments, color);
        } else {
            dl->AddPolyline(points, kSegments, color, ImDrawFlags_Closed, thickness);
        }
    }

    // Arco por angulos em graus, sentido horario a partir do leste.
    void arc(float x, float y, float radius, float from_deg, float to_deg) const {
        dl->PathArcTo(at(x, y), radius * size,
                      from_deg * 3.14159265f / 180.0f,
                      to_deg * 3.14159265f / 180.0f, 20);
        dl->PathStroke(color, ImDrawFlags_None, thickness);
    }

    void triangle_filled(float x1, float y1, float x2, float y2,
                         float x3, float y3) const {
        dl->AddTriangleFilled(at(x1, y1), at(x2, y2), at(x3, y3), color);
    }
};

void draw_connect(const Canvas& c) {
    // Plugue: dois pinos e um corpo.
    c.line(-0.18f, -0.42f, -0.18f, -0.18f);
    c.line( 0.18f, -0.42f,  0.18f, -0.18f);
    c.rect(-0.32f, -0.18f, 0.32f, 0.10f, 0.12f);
    c.line(0.0f, 0.10f, 0.0f, 0.42f);
}

void draw_disconnect(const Canvas& c) {
    draw_connect(c);
    // Barra diagonal de "desligado".
    c.line(-0.40f, 0.40f, 0.40f, -0.40f);
}

void draw_play(const Canvas& c) {
    c.triangle_filled(-0.22f, -0.34f, -0.22f, 0.34f, 0.34f, 0.0f);
}

void draw_stop(const Canvas& c) {
    c.rect_filled(-0.28f, -0.28f, 0.28f, 0.28f, 0.10f);
}

void draw_commit(const Canvas& c) {
    // Check.
    c.dl->PathLineTo(c.at(-0.32f, 0.02f));
    c.dl->PathLineTo(c.at(-0.10f, 0.26f));
    c.dl->PathLineTo(c.at( 0.34f, -0.24f));
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness * 1.3f);
}

void draw_rollback(const Canvas& c) {
    // Seta circular anti-horaria.
    c.arc(0.0f, 0.02f, 0.30f, 150.0f, 400.0f);
    c.triangle_filled(-0.34f, -0.06f, -0.14f, -0.06f, -0.24f, -0.30f);
}

void draw_database(const Canvas& c) {
    // Cilindro: topo elipse, laterais, base em arco.
    c.ellipse(0.0f, -0.26f, 0.32f, 0.13f);
    c.line(-0.32f, -0.26f, -0.32f, 0.22f);
    c.line( 0.32f, -0.26f,  0.32f, 0.22f);
    c.arc(0.0f, 0.22f, 0.32f, 0.0f, 180.0f);
    c.arc(0.0f, -0.02f, 0.32f, 10.0f, 170.0f);
}

void draw_table(const Canvas& c) {
    c.rect(-0.34f, -0.30f, 0.34f, 0.30f, 0.08f);
    c.line(-0.34f, -0.12f, 0.34f, -0.12f);   // cabeçalho
    c.line(-0.34f,  0.08f, 0.34f,  0.08f);
    c.line(-0.06f, -0.12f, -0.06f, 0.30f);
}

void draw_view(const Canvas& c) {
    // Olho.
    c.dl->PathLineTo(c.at(-0.38f, 0.0f));
    c.dl->PathBezierCubicCurveTo(c.at(-0.18f, -0.28f), c.at(0.18f, -0.28f),
                                 c.at(0.38f, 0.0f), 16);
    c.dl->PathBezierCubicCurveTo(c.at(0.18f, 0.28f), c.at(-0.18f, 0.28f),
                                 c.at(-0.38f, 0.0f), 16);
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
    c.circle_filled(0.0f, 0.0f, 0.11f);
}

void draw_column(const Canvas& c) {
    c.rect(-0.16f, -0.34f, 0.16f, 0.34f, 0.08f);
    c.line(-0.16f, -0.14f, 0.16f, -0.14f);
    c.line(-0.16f,  0.06f, 0.16f,  0.06f);
}

void draw_key(const Canvas& c) {
    c.circle(-0.16f, -0.02f, 0.17f);
    c.line(0.0f, 0.02f, 0.36f, 0.02f);
    c.line(0.24f, 0.02f, 0.24f, 0.18f);
    c.line(0.36f, 0.02f, 0.36f, 0.22f);
}

void draw_folder(const Canvas& c) {
    c.dl->PathLineTo(c.at(-0.36f,  0.28f));
    c.dl->PathLineTo(c.at(-0.36f, -0.22f));
    c.dl->PathLineTo(c.at(-0.06f, -0.22f));
    c.dl->PathLineTo(c.at( 0.02f, -0.10f));
    c.dl->PathLineTo(c.at( 0.36f, -0.10f));
    c.dl->PathLineTo(c.at( 0.36f,  0.28f));
    c.dl->PathStroke(c.color, ImDrawFlags_Closed, c.thickness);
}

void draw_refresh(const Canvas& c) {
    c.arc(0.0f, 0.0f, 0.30f, 40.0f, 300.0f);
    c.triangle_filled(0.16f, -0.30f, 0.36f, -0.30f, 0.26f, -0.08f);
}

void draw_search(const Canvas& c) {
    c.circle(-0.08f, -0.08f, 0.22f);
    c.line(0.09f, 0.09f, 0.34f, 0.34f);
}

void draw_settings(const Canvas& c) {
    // Engrenagem: círculo com dentes radiais.
    c.circle(0.0f, 0.0f, 0.16f);
    for (int i = 0; i < 8; ++i) {
        const float a = 2.0f * 3.14159265f * static_cast<float>(i) / 8.0f;
        const float cx = std::cos(a);
        const float sy = std::sin(a);
        c.line(cx * 0.22f, sy * 0.22f, cx * 0.36f, sy * 0.36f);
    }
}

void draw_plus(const Canvas& c) {
    c.line(0.0f, -0.30f, 0.0f, 0.30f);
    c.line(-0.30f, 0.0f, 0.30f, 0.0f);
}

void draw_close(const Canvas& c) {
    c.line(-0.24f, -0.24f, 0.24f, 0.24f);
    c.line( 0.24f, -0.24f, -0.24f, 0.24f);
}

void draw_pin(const Canvas& c) {
    c.line(0.0f, 0.06f, 0.0f, 0.38f);
    c.rect_filled(-0.20f, -0.34f, 0.20f, 0.06f, 0.06f);
}

void draw_save(const Canvas& c) {
    c.rect(-0.32f, -0.32f, 0.32f, 0.32f, 0.08f);
    c.rect_filled(-0.18f, -0.32f, 0.18f, -0.08f);   // obturador
    c.rect(-0.20f, 0.06f, 0.20f, 0.32f);            // etiqueta
}

void draw_open(const Canvas& c) {
    draw_folder(c);
    c.line(0.0f, 0.22f, 0.0f, -0.02f);
    c.triangle_filled(-0.10f, 0.02f, 0.10f, 0.02f, 0.0f, 0.22f);
}

void draw_copy(const Canvas& c) {
    c.rect(-0.34f, -0.34f, 0.10f, 0.16f, 0.08f);
    c.rect(-0.10f, -0.16f, 0.34f, 0.34f, 0.08f);
}

void draw_chevron_left(const Canvas& c) {
    c.dl->PathLineTo(c.at( 0.12f, -0.26f));
    c.dl->PathLineTo(c.at(-0.16f,  0.0f));
    c.dl->PathLineTo(c.at( 0.12f,  0.26f));
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
}

void draw_chevron_right(const Canvas& c) {
    c.dl->PathLineTo(c.at(-0.12f, -0.26f));
    c.dl->PathLineTo(c.at( 0.16f,  0.0f));
    c.dl->PathLineTo(c.at(-0.12f,  0.26f));
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
}

// Chevron encostado numa barra: ir ao extremo, nao um passo. A barra e' o que
// distingue "primeira pagina" de "pagina anterior" -- dois botoes vizinhos com
// o mesmo desenho seriam indistinguiveis na pressa.
void draw_first_page(const Canvas& c) {
    c.line(-0.26f, -0.26f, -0.26f, 0.26f);
    c.dl->PathLineTo(c.at( 0.22f, -0.26f));
    c.dl->PathLineTo(c.at(-0.06f,  0.0f));
    c.dl->PathLineTo(c.at( 0.22f,  0.26f));
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
}

void draw_last_page(const Canvas& c) {
    c.line(0.26f, -0.26f, 0.26f, 0.26f);
    c.dl->PathLineTo(c.at(-0.22f, -0.26f));
    c.dl->PathLineTo(c.at( 0.06f,  0.0f));
    c.dl->PathLineTo(c.at(-0.22f,  0.26f));
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
}

void draw_chevron_down(const Canvas& c) {
    c.dl->PathLineTo(c.at(-0.26f, -0.12f));
    c.dl->PathLineTo(c.at( 0.0f,   0.16f));
    c.dl->PathLineTo(c.at( 0.26f, -0.12f));
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
}

void draw_warning(const Canvas& c) {
    c.dl->PathLineTo(c.at(0.0f, -0.34f));
    c.dl->PathLineTo(c.at(0.36f, 0.30f));
    c.dl->PathLineTo(c.at(-0.36f, 0.30f));
    c.dl->PathStroke(c.color, ImDrawFlags_Closed, c.thickness);
    c.line(0.0f, -0.12f, 0.0f, 0.08f);
    c.circle_filled(0.0f, 0.20f, 0.04f);
}

void draw_error(const Canvas& c) {
    c.circle(0.0f, 0.0f, 0.32f);
    c.line(-0.14f, -0.14f, 0.14f, 0.14f);
    c.line( 0.14f, -0.14f, -0.14f, 0.14f);
}

void draw_info(const Canvas& c) {
    c.circle(0.0f, 0.0f, 0.32f);
    c.circle_filled(0.0f, -0.16f, 0.045f);
    c.line(0.0f, -0.02f, 0.0f, 0.18f);
}

void draw_clock(const Canvas& c) {
    c.circle(0.0f, 0.0f, 0.32f);
    c.line(0.0f, 0.0f, 0.0f, -0.18f);
    c.line(0.0f, 0.0f, 0.14f, 0.08f);
}

// Cadeado: arco por cima, corpo retangular por baixo. O arco NAO fecha no
// corpo -- deixar a fresta e' o que distingue o cadeado de uma bolsa a 14 px.
void draw_lock(const Canvas& c) {
    // O arco fica INTEIRAMENTE acima do corpo. Duas versões anteriores o
    // centraram dentro da caixa e o resultado lia como envelope: a alça
    // aparecia como o vinco do papel, não como argola de cadeado. O que
    // distingue os dois à distância é o vão entre a alça e a caixa.
    // π a 2π, e não π a 0: os dois cobrem a mesma meia-volta, mas o ImGui
    // percorre o intervalo NA ORDEM dada, e π→0 desenha a metade de BAIXO.
    // Com ela, a alça caía dentro da caixa e o ícone lia como envelope.
    c.dl->PathArcTo(c.at(0.0f, -0.06f), 0.17f * c.size, 3.1416f,
                    2.0f * 3.1416f, 14);
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);

    // O corpo é mais LARGO que o arco: é esse degrau nos ombros que faz a
    // silhueta ler como cadeado. Com as duas larguras iguais, o contorno vira
    // um retângulo de topo abaulado.
    c.rect(-0.28f, -0.06f, 0.28f, 0.32f, 0.06f);
    c.line(0.0f, 0.06f, 0.0f, 0.19f);   // o segredo
}

// Ficha de cadastro: moldura com pares rotulo/valor empilhados. O contraste
// entre o traco CURTO (rotulo) e o LONGO (valor) e' o que distingue esta
// ficha de uma tabela generica a 14 px.
void draw_record(const Canvas& c) {
    c.rect(-0.34f, -0.34f, 0.34f, 0.34f, 0.06f);

    for (const float y : {-0.18f, -0.02f, 0.14f}) {
        c.line(-0.24f, y, -0.10f, y);   // rotulo
        c.line( 0.02f, y,  0.24f, y);   // valor
    }
}

void draw_filter(const Canvas& c) {
    c.dl->PathLineTo(c.at(-0.34f, -0.26f));
    c.dl->PathLineTo(c.at( 0.34f, -0.26f));
    c.dl->PathLineTo(c.at( 0.10f,  0.02f));
    c.dl->PathLineTo(c.at( 0.10f,  0.30f));
    c.dl->PathLineTo(c.at(-0.10f,  0.20f));
    c.dl->PathLineTo(c.at(-0.10f,  0.02f));
    c.dl->PathStroke(c.color, ImDrawFlags_Closed, c.thickness);
}

// --- Tipos de objeto do banco -------------------------------------------
//
// Cada desenho precisa ser reconhecivel a ~14 px, que e' o tamanho na arvore.
// Isso limita o detalhe: tres tracos distintos valem mais que oito tracos
// que viram borrao. Todos usam o mesmo peso de linha dos icones de barra.

void draw_materialized_view(const Canvas& c) {
    // View com dados gravados: o olho da view, e abaixo dele o disco que
    // guarda o resultado materializado.
    //
    // A primeira versao empilhou os dois colados e o conjunto virou uma forma
    // unica ilegivel. Aqui o olho e' largo e fino no topo, o disco e' uma
    // elipse dupla embaixo, e ha' folga entre eles.
    c.dl->PathLineTo(c.at(-0.36f, -0.20f));
    c.dl->PathBezierCubicCurveTo(c.at(-0.16f, -0.44f), c.at(0.16f, -0.44f),
                                 c.at(0.36f, -0.20f), 16);
    c.dl->PathBezierCubicCurveTo(c.at(0.16f, 0.04f), c.at(-0.16f, 0.04f),
                                 c.at(-0.36f, -0.20f), 16);
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
    c.circle_filled(0.0f, -0.20f, 0.09f);

    // Disco: topo e base, ligados nas laterais.
    c.ellipse(0.0f, 0.18f, 0.26f, 0.09f);
    c.line(-0.26f, 0.18f, -0.26f, 0.32f);
    c.line( 0.26f, 0.18f,  0.26f, 0.32f);
    c.arc(0.0f, 0.32f, 0.26f, 0.0f, 180.0f);
}

void draw_index(const Canvas& c) {
    // Paginas com marcador lateral: o indice que aponta para a linha.
    c.rect(-0.34f, -0.32f, 0.20f, 0.32f, 0.06f);
    c.line(-0.22f, -0.16f, 0.08f, -0.16f);
    c.line(-0.22f,  0.00f, 0.08f,  0.00f);
    c.line(-0.22f,  0.16f, -0.04f, 0.16f);
    // Marcador saliente, a "aba" do indice.
    c.dl->PathLineTo(c.at(0.20f, -0.24f));
    c.dl->PathLineTo(c.at(0.38f, -0.24f));
    c.dl->PathLineTo(c.at(0.38f,  0.14f));
    c.dl->PathLineTo(c.at(0.29f,  0.04f));
    c.dl->PathLineTo(c.at(0.20f,  0.14f));
    c.dl->PathStroke(c.color, ImDrawFlags_Closed, c.thickness);
}

void draw_constraint(const Canvas& c) {
    // Escudo: a regra que protege a integridade dos dados.
    c.dl->PathLineTo(c.at(0.0f, -0.36f));
    c.dl->PathLineTo(c.at(0.30f, -0.22f));
    c.dl->PathLineTo(c.at(0.30f,  0.06f));
    c.dl->PathBezierCubicCurveTo(c.at(0.28f, 0.24f), c.at(0.14f, 0.32f),
                                 c.at(0.0f, 0.38f), 12);
    c.dl->PathBezierCubicCurveTo(c.at(-0.14f, 0.32f), c.at(-0.28f, 0.24f),
                                 c.at(-0.30f, 0.06f), 12);
    c.dl->PathLineTo(c.at(-0.30f, -0.22f));
    c.dl->PathStroke(c.color, ImDrawFlags_Closed, c.thickness);
}

void draw_foreign_key(const Canvas& c) {
    // Dois elos SEPARADOS e sobrepostos na diagonal. A primeira tentativa
    // desenhou dois arcos unidos por retas horizontais: as bordas se fundiram
    // num oval unico e o simbolo perdeu o sentido de "ligacao".
    //
    // Aqui cada elo e' um retangulo arredondado inteiro; a sobreposicao entre
    // eles e' o que comunica o vinculo, e sobrevive ao tamanho da arvore.
    c.dl->AddRect(c.at(-0.38f, -0.26f), c.at(0.06f, 0.02f), c.color,
                  0.14f * c.size, ImDrawFlags_None, c.thickness);
    c.dl->AddRect(c.at(-0.06f, -0.02f), c.at(0.38f, 0.26f), c.color,
                  0.14f * c.size, ImDrawFlags_None, c.thickness);
}

void draw_references(const Canvas& c) {
    // Alvo a direita, tres origens convergindo para ele: quem aponta para
    // esta tabela. E' o inverso do foreign_key e precisa ler como tal.
    //
    // O alvo e' preenchido, as origens sao vazias -- a assimetria diz o
    // sentido sem depender de pontas de seta, que somem no tamanho da arvore.
    c.circle_filled(0.28f, 0.0f, 0.13f);

    for (int i = 0; i < 3; ++i) {
        const float y = -0.28f + static_cast<float>(i) * 0.28f;
        c.circle(-0.30f, y, 0.075f);
        // Para no raio do alvo, sem invadi-lo.
        c.line(-0.22f, y * 0.88f, 0.14f, y * 0.24f);
    }
}

void draw_sequence(const Canvas& c) {
    // Degraus subindo: o valor que sempre avanca.
    c.dl->PathLineTo(c.at(-0.36f,  0.30f));
    c.dl->PathLineTo(c.at(-0.12f,  0.30f));
    c.dl->PathLineTo(c.at(-0.12f,  0.06f));
    c.dl->PathLineTo(c.at( 0.10f,  0.06f));
    c.dl->PathLineTo(c.at( 0.10f, -0.18f));
    c.dl->PathLineTo(c.at( 0.32f, -0.18f));
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
    // Seta no topo, marcando o sentido.
    c.triangle_filled(0.22f, -0.28f, 0.22f, -0.08f, 0.38f, -0.18f);
}

void draw_function(const Canvas& c) {
    // Caixa com entrada e saida: recebe argumentos, devolve um valor. E' o
    // que distingue a function da procedure, que so' executa.
    //
    // A primeira versao desenhou "(f)" em tracos: no tamanho da arvore o f
    // some e sobram dois riscos curvos. Formas geometricas sobrevivem melhor
    // a 14 px que letras.
    c.rect(-0.14f, -0.22f, 0.14f, 0.22f, 0.10f);

    // Duas entradas a esquerda.
    c.line(-0.38f, -0.12f, -0.14f, -0.12f);
    c.line(-0.38f,  0.12f, -0.14f,  0.12f);
    c.circle_filled(-0.38f, -0.12f, 0.055f);
    c.circle_filled(-0.38f,  0.12f, 0.055f);

    // Uma saida a direita, com ponta de seta.
    c.line(0.14f, 0.0f, 0.30f, 0.0f);
    c.triangle_filled(0.28f, -0.09f, 0.28f, 0.09f, 0.40f, 0.0f);
}

void draw_procedure(const Canvas& c) {
    // Bloco de execucao: retangulo com um play dentro. Distingue-se da
    // function por nao ter retorno -- e' um comando, nao uma expressao.
    c.rect(-0.34f, -0.28f, 0.34f, 0.28f, 0.10f);
    c.line(-0.34f, -0.12f, 0.34f, -0.12f);
    c.triangle_filled(-0.10f, -0.02f, -0.10f, 0.18f, 0.10f, 0.08f);
}

void draw_trigger(const Canvas& c) {
    // Raio: dispara sozinho quando o evento acontece.
    c.dl->PathLineTo(c.at( 0.10f, -0.38f));
    c.dl->PathLineTo(c.at(-0.22f,  0.04f));
    c.dl->PathLineTo(c.at(-0.02f,  0.04f));
    c.dl->PathLineTo(c.at(-0.10f,  0.38f));
    c.dl->PathLineTo(c.at( 0.22f, -0.04f));
    c.dl->PathLineTo(c.at( 0.02f, -0.04f));
    c.dl->PathStroke(c.color, ImDrawFlags_Closed, c.thickness);
}

void draw_data_type(const Canvas& c) {
    // Chaves { } com um nucleo: a forma que envolve o valor.
    c.dl->PathLineTo(c.at(-0.14f, -0.32f));
    c.dl->PathBezierCubicCurveTo(c.at(-0.26f, -0.30f), c.at(-0.24f, -0.10f),
                                 c.at(-0.32f, 0.0f), 10);
    c.dl->PathBezierCubicCurveTo(c.at(-0.24f, 0.10f), c.at(-0.26f, 0.30f),
                                 c.at(-0.14f, 0.32f), 10);
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);

    c.dl->PathLineTo(c.at(0.14f, -0.32f));
    c.dl->PathBezierCubicCurveTo(c.at(0.26f, -0.30f), c.at(0.24f, -0.10f),
                                 c.at(0.32f, 0.0f), 10);
    c.dl->PathBezierCubicCurveTo(c.at(0.24f, 0.10f), c.at(0.26f, 0.30f),
                                 c.at(0.14f, 0.32f), 10);
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);

    // Tres barras dentro das chaves, sugerindo os campos do tipo. Um ponto
    // sozinho, como na primeira versao, desaparecia no tamanho da arvore.
    c.line(-0.07f, -0.13f, 0.07f, -0.13f);
    c.line(-0.07f,  0.00f, 0.07f,  0.00f);
    c.line(-0.07f,  0.13f, 0.07f,  0.13f);
}

void draw_extension(const Canvas& c) {
    // Peca de quebra-cabeca: o modulo que encaixa no servidor.
    c.dl->PathLineTo(c.at(-0.32f, -0.30f));
    c.dl->PathLineTo(c.at(-0.06f, -0.30f));
    c.dl->PathBezierCubicCurveTo(c.at(-0.06f, -0.46f), c.at(0.16f, -0.46f),
                                 c.at(0.16f, -0.30f), 10);
    c.dl->PathLineTo(c.at(0.32f, -0.30f));
    c.dl->PathLineTo(c.at(0.32f, -0.04f));
    c.dl->PathBezierCubicCurveTo(c.at(0.48f, -0.04f), c.at(0.48f, 0.18f),
                                 c.at(0.32f, 0.18f), 10);
    c.dl->PathLineTo(c.at(0.32f, 0.32f));
    c.dl->PathLineTo(c.at(-0.32f, 0.32f));
    c.dl->PathStroke(c.color, ImDrawFlags_Closed, c.thickness);
}

void draw_role(const Canvas& c) {
    // Silhueta com chave ao lado: usuario e suas permissoes.
    c.circle(-0.10f, -0.18f, 0.15f);
    c.dl->PathArcTo(c.at(-0.10f, 0.34f), 0.26f * c.size,
                    3.14159265f, 2.0f * 3.14159265f, 16);
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
    // A chave, menor, no canto.
    c.circle(0.26f, 0.06f, 0.09f);
    c.line(0.26f, 0.15f, 0.26f, 0.36f);
    c.line(0.26f, 0.28f, 0.37f, 0.28f);
}

void draw_tablespace(const Canvas& c) {
    // Gaveta/prateleira: o lugar fisico onde os arquivos ficam.
    //
    // A primeira versao usou tres elipses empilhadas, mas no tamanho da arvore
    // ficava igual ao materialized_view, que tambem tem disco. Formas
    // retangulares resolvem a colisao -- nenhum outro icone de objeto as usa
    // nesta proporcao.
    // Mais estreito que a tabela e com perspectiva no topo: e' um movel, nao
    // uma grade. A tabela e' larga e tem divisao vertical; esta nao tem.
    c.line(-0.26f, -0.30f, 0.26f, -0.30f);
    c.rect(-0.30f, -0.22f, 0.30f, 0.32f, 0.06f);
    c.line(-0.30f, 0.05f, 0.30f, 0.05f);
    // Puxador redondo por gaveta -- assinatura que nenhum outro icone repete.
    c.circle_filled(0.0f, -0.09f, 0.055f);
    c.circle_filled(0.0f,  0.18f, 0.055f);
}

void draw_schema(const Canvas& c) {
    // Grade ramificada: o agrupamento que contem os objetos.
    c.rect(-0.34f, -0.34f, -0.06f, -0.06f, 0.06f);
    c.rect( 0.06f, -0.34f,  0.34f, -0.06f, 0.06f);
    c.rect(-0.34f,  0.06f, -0.06f,  0.34f, 0.06f);
    c.rect( 0.06f,  0.06f,  0.34f,  0.34f, 0.06f);
    c.line(-0.06f, -0.20f, 0.06f, -0.20f);
    c.line(-0.20f, -0.06f, -0.20f, 0.06f);
}

void draw_pivot(const Canvas& c) {
    // Uma COLUNA de celulas virando uma LINHA: a transposicao, que e' o que o
    // pivot faz. A seta em curva no meio e' o que separa este icone do de
    // tabela -- sem ela, dois retangulos com divisorias seriam a mesma coisa.
    c.rect(-0.36f, -0.32f, -0.16f, 0.32f, 0.05f);   // coluna, em pe'
    c.line(-0.36f, -0.11f, -0.16f, -0.11f);
    c.line(-0.36f,  0.11f, -0.16f,  0.11f);

    c.rect(-0.02f, 0.12f, 0.36f, 0.32f, 0.05f);     // linha, deitada
    c.line(0.11f, 0.12f, 0.11f, 0.32f);
    c.line(0.24f, 0.12f, 0.24f, 0.32f);

    // Seta da coluna para a linha, passando por cima.
    c.dl->PathLineTo(c.at(-0.05f, -0.22f));
    c.dl->PathBezierCubicCurveTo(c.at(0.20f, -0.22f), c.at(0.30f, -0.12f),
                                 c.at(0.30f, 0.04f), 12);
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
    c.line(0.30f, 0.04f, 0.23f, -0.05f);
    c.line(0.30f, 0.04f, 0.37f, -0.05f);
}

void draw_partition(const Canvas& c) {
    // Cilindro FATIADO na horizontal: a tabela dividida em pedacos. As linhas
    // internas sao o que separa este do icone de database, que e' o mesmo
    // cilindro inteiro.
    c.ellipse(0.0f, -0.26f, 0.30f, 0.10f);
    c.line(-0.30f, -0.26f, -0.30f, 0.26f);
    c.line( 0.30f, -0.26f,  0.30f, 0.26f);

    // As duas divisorias -- e' delas que vem a ideia de "particao".
    c.ellipse(0.0f, -0.02f, 0.30f, 0.09f);
    c.ellipse(0.0f,  0.14f, 0.30f, 0.09f);

    c.dl->PathLineTo(c.at(-0.30f, 0.26f));
    c.dl->PathBezierCubicCurveTo(c.at(-0.30f, 0.36f), c.at(0.30f, 0.36f),
                                 c.at(0.30f, 0.26f), 12);
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
}

void draw_event(const Canvas& c) {
    // Relogio com uma seta de repeticao em volta: o agendamento recorrente.
    // O relogio sozinho ja' e' o icone de `clock`; a seta e' o que distingue.
    c.circle(0.0f, 0.02f, 0.24f);
    c.line(0.0f, 0.02f, 0.0f, -0.12f);    // ponteiro das horas
    c.line(0.0f, 0.02f, 0.13f, 0.08f);    // ponteiro dos minutos

    // Arco externo com ponta de seta: repete.
    c.dl->PathArcTo(c.at(0.0f, 0.02f), 0.36f * c.size, -2.6f, 0.6f, 20);
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
    c.line(0.30f, 0.22f, 0.36f, 0.14f);
    c.line(0.30f, 0.22f, 0.22f, 0.18f);
}

void draw_user(const Canvas& c) {
    // Silhueta simples: cabeca e ombros. O icone de `role` ja' e' a silhueta
    // COM chave -- aqui, sem chave, porque usuario e papel sao conceitos
    // diferentes e a arvore mostra os dois em SGBDs diferentes.
    c.circle(0.0f, -0.16f, 0.15f);
    c.dl->PathLineTo(c.at(-0.28f, 0.32f));
    c.dl->PathBezierCubicCurveTo(c.at(-0.28f, 0.02f), c.at(0.28f, 0.02f),
                                 c.at(0.28f, 0.32f), 16);
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
}

void draw_grant(const Canvas& c) {
    // Pergaminho com selo: a concessao formal. O retangulo com a ponta
    // enrolada e' o que separa do icone de `constraint`, que e' um escudo.
    c.line(-0.26f, -0.30f, 0.20f, -0.30f);
    c.line(-0.26f, -0.30f, -0.26f, 0.30f);
    c.line(-0.26f,  0.30f, 0.20f, 0.30f);
    c.line( 0.20f, -0.30f, 0.20f, 0.30f);

    // As linhas de texto.
    c.line(-0.16f, -0.14f, 0.10f, -0.14f);
    c.line(-0.16f, -0.01f, 0.10f, -0.01f);

    // O selo, no canto inferior direito, transbordando a borda.
    c.circle_filled(0.22f, 0.22f, 0.10f);
}

// --- Nos da arvore unica (ADR 0018) -------------------------------------------
//
// Mesma regra dos de cima: formas geometricas, nao letras -- a 14 px uma
// letra vira dois riscos. Onde dois tipos sao parentes (role e role_group,
// trigger e event_trigger, extension e extension_available), a diferenca
// esta' na SILHUETA, nao num detalhe interno.

void draw_foreign_table(const Canvas& c) {
    // A grade da tabela, menor e no canto, com uma seta saindo dela: os
    // dados estao fora.
    c.rect(-0.38f, -0.10f, 0.14f, 0.36f, 0.06f);
    c.line(-0.38f, 0.06f, 0.14f, 0.06f);
    c.line(-0.14f, 0.06f, -0.14f, 0.36f);
    c.line(0.02f, -0.02f, 0.30f, -0.30f);
    c.triangle_filled(0.38f, -0.38f, 0.16f, -0.32f, 0.32f, -0.16f);
}

void draw_aggregate(const Canvas& c) {
    c.dl->PathLineTo(c.at( 0.26f, -0.24f));
    c.dl->PathLineTo(c.at( 0.26f, -0.34f));
    c.dl->PathLineTo(c.at(-0.26f, -0.34f));
    c.dl->PathLineTo(c.at( 0.04f,  0.00f));
    c.dl->PathLineTo(c.at(-0.26f,  0.34f));
    c.dl->PathLineTo(c.at( 0.26f,  0.34f));
    c.dl->PathLineTo(c.at( 0.26f,  0.24f));
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
}

void draw_dependency(const Canvas& c) {
    // O objeto em cima, cheio; os que dependem dele embaixo, vazios. E' o
    // `references` de pe': la' as origens convergem para a direita.
    c.circle_filled(0.0f, -0.26f, 0.12f);
    c.line(-0.06f, -0.16f, -0.22f, 0.14f);
    c.line( 0.06f, -0.16f,  0.22f, 0.14f);
    c.circle(-0.24f, 0.24f, 0.10f);
    c.circle( 0.24f, 0.24f, 0.10f);
}

void draw_rule(const Canvas& c) {
    c.line(-0.34f, -0.14f, 0.22f, -0.14f);
    c.triangle_filled(0.18f, -0.26f, 0.18f, -0.02f, 0.38f, -0.14f);
    c.line(-0.22f, 0.14f, 0.34f, 0.14f);
    c.triangle_filled(-0.18f, 0.02f, -0.18f, 0.26f, -0.38f, 0.14f);
}

void draw_policy(const Canvas& c) {
    // Tres linhas de dados, e so' a do meio passa.
    c.line(-0.34f, -0.26f, 0.34f, -0.26f);
    c.rect_filled(-0.34f, -0.09f, 0.34f, 0.09f, 0.04f);
    c.line(-0.34f, 0.26f, 0.34f, 0.26f);
}

void draw_inheritance(const Canvas& c) {
    c.rect(-0.20f, -0.38f, 0.20f, -0.18f, 0.04f);
    c.dl->AddTriangle(c.at(0.0f, -0.18f), c.at(-0.11f, 0.0f), c.at(0.11f, 0.0f),
                      c.color, c.thickness);
    c.line(0.0f, 0.0f, 0.0f, 0.18f);
    c.rect(-0.20f, 0.18f, 0.20f, 0.38f, 0.04f);
}

void draw_parameter(const Canvas& c) {
    c.arc(-0.02f, 0.0f, 0.34f, 135.0f, 225.0f);
    c.arc( 0.02f, 0.0f, 0.34f, -45.0f, 45.0f);
    c.circle_filled(0.0f, 0.0f, 0.09f);
}

void draw_event_trigger(const Canvas& c) {
    // O raio do trigger, menor, dentro de um anel: o gatilho do BANCO, nao
    // de uma tabela.
    c.circle(0.0f, 0.0f, 0.37f);
    c.dl->PathLineTo(c.at( 0.06f, -0.24f));
    c.dl->PathLineTo(c.at(-0.14f,  0.03f));
    c.dl->PathLineTo(c.at( 0.00f,  0.03f));
    c.dl->PathLineTo(c.at(-0.06f,  0.24f));
    c.dl->PathLineTo(c.at( 0.14f, -0.03f));
    c.dl->PathLineTo(c.at( 0.00f, -0.03f));
    c.dl->PathStroke(c.color, ImDrawFlags_Closed, c.thickness);
}

void draw_storage(const Canvas& c) {
    // Disco rigido visto de cima. Nao as tres barras empilhadas, que sao a
    // torre de `generic_server`.
    c.rect(-0.34f, -0.32f, 0.34f, 0.32f, 0.07f);
    c.circle(-0.05f, -0.04f, 0.19f);
    c.circle_filled(-0.05f, -0.04f, 0.045f);
    c.line(0.24f, 0.24f, 0.07f, 0.06f);
}

void draw_foreign_wrapper(const Canvas& c) {
    c.rect(-0.38f, -0.18f, -0.08f, 0.18f, 0.05f);
    c.rect( 0.08f, -0.18f,  0.38f, 0.18f, 0.05f);
    c.line(-0.08f, -0.08f, 0.08f, -0.08f);
    c.line(-0.08f,  0.08f, 0.08f,  0.08f);
}

void draw_foreign_server(const Canvas& c) {
    c.circle(0.0f, 0.0f, 0.34f);
    c.ellipse(0.0f, 0.0f, 0.14f, 0.34f);
    c.line(-0.34f, 0.0f, 0.34f, 0.0f);
}

void draw_user_mapping(const Canvas& c) {
    c.circle(-0.26f, -0.12f, 0.10f);
    c.arc(-0.26f, 0.26f, 0.16f, 180.0f, 360.0f);
    c.line(-0.04f, 0.04f, 0.12f, 0.04f);
    c.triangle_filled(0.10f, -0.06f, 0.10f, 0.14f, 0.22f, 0.04f);
    c.rect(0.24f, -0.10f, 0.40f, 0.18f, 0.03f);
}

void draw_setting(const Canvas& c) {
    // Controles deslizantes, e nao a engrenagem: aquela e' a ACAO de abrir
    // as preferencias; este e' um valor do servidor.
    c.line(-0.36f, -0.15f, 0.36f, -0.15f);
    c.circle_filled(-0.12f, -0.15f, 0.085f);
    c.line(-0.36f, 0.15f, 0.36f, 0.15f);
    c.circle_filled(0.16f, 0.15f, 0.085f);
}

void draw_role_group(const Canvas& c) {
    // Duas silhuetas, a de tras deslocada. Sem a chave do `role`: o grupo
    // nao entra no servidor, so' reune permissoes.
    c.circle(0.14f, -0.22f, 0.11f);
    c.arc(0.14f, 0.20f, 0.20f, 200.0f, 360.0f);
    c.circle(-0.12f, -0.10f, 0.13f);
    c.arc(-0.12f, 0.36f, 0.24f, 180.0f, 360.0f);
}

void draw_access_method(const Canvas& c) {
    c.rect_filled(-0.09f, -0.36f, 0.09f, -0.20f, 0.03f);
    for (int i = -1; i <= 1; ++i) {
        const float x = static_cast<float>(i) * 0.27f;
        c.line(0.0f, -0.20f, x, 0.18f);
        c.rect(x - 0.09f, 0.18f, x + 0.09f, 0.34f, 0.03f);
    }
}

void draw_operator_class(const Canvas& c) {
    c.dl->PathLineTo(c.at( 0.22f, -0.34f));
    c.dl->PathLineTo(c.at(-0.24f, -0.10f));
    c.dl->PathLineTo(c.at( 0.22f,  0.14f));
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
    c.line(-0.24f, 0.32f, 0.22f, 0.32f);
}

void draw_operator_family(const Canvas& c) {
    c.circle(-0.13f, 0.0f, 0.23f);
    c.circle( 0.13f, 0.0f, 0.23f);
}

void draw_encoding(const Canvas& c) {
    // Tres bits por linha; os acesos em diagonal para nao ler como grade de
    // tabela.
    constexpr bool kLit[2][3] = {{true, false, true}, {false, true, false}};
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 3; ++col) {
            const float x = -0.34f + static_cast<float>(col) * 0.25f;
            const float y = -0.22f + static_cast<float>(row) * 0.26f;
            if (kLit[row][col]) {
                c.rect_filled(x, y, x + 0.18f, y + 0.18f, 0.03f);
            } else {
                c.rect(x, y, x + 0.18f, y + 0.18f, 0.03f);
            }
        }
    }
}

void draw_collation(const Canvas& c) {
    c.line(-0.30f, -0.32f, -0.30f, 0.20f);
    c.triangle_filled(-0.40f, 0.16f, -0.20f, 0.16f, -0.30f, 0.36f);
    c.line(-0.08f, -0.24f, 0.08f, -0.24f);
    c.line(-0.08f,  0.00f, 0.22f,  0.00f);
    c.line(-0.08f,  0.24f, 0.38f,  0.24f);
}

void draw_language(const Canvas& c) {
    c.dl->PathLineTo(c.at(-0.18f, -0.22f));
    c.dl->PathLineTo(c.at(-0.38f,  0.00f));
    c.dl->PathLineTo(c.at(-0.18f,  0.22f));
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
    c.line(0.08f, -0.32f, -0.08f, 0.32f);
    c.dl->PathLineTo(c.at(0.18f, -0.22f));
    c.dl->PathLineTo(c.at(0.38f,  0.00f));
    c.dl->PathLineTo(c.at(0.18f,  0.22f));
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
}

void draw_extension_available(const Canvas& c) {
    // A extensao instalada e' a peca de quebra-cabeca. Esta ainda esta' na
    // caixa: aberta em cima, com a seta entrando.
    c.dl->PathLineTo(c.at(-0.32f, 0.02f));
    c.dl->PathLineTo(c.at(-0.32f, 0.36f));
    c.dl->PathLineTo(c.at( 0.32f, 0.36f));
    c.dl->PathLineTo(c.at( 0.32f, 0.02f));
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
    c.line(0.0f, -0.38f, 0.0f, 0.04f);
    c.triangle_filled(-0.13f, 0.00f, 0.13f, 0.00f, 0.0f, 0.20f);
}

void draw_administer(const Canvas& c) {
    c.rect(-0.36f, -0.12f, 0.36f, 0.32f, 0.06f);
    c.dl->PathLineTo(c.at(-0.14f, -0.12f));
    c.dl->PathLineTo(c.at(-0.14f, -0.30f));
    c.dl->PathLineTo(c.at( 0.14f, -0.30f));
    c.dl->PathLineTo(c.at( 0.14f, -0.12f));
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
    c.line(-0.36f, 0.06f, 0.36f, 0.06f);
    c.rect_filled(-0.07f, 0.00f, 0.07f, 0.14f, 0.02f);
}

void draw_system_info(const Canvas& c) {
    // O `info` e' o circulo com i, usado em mensagens. Aqui o i esta' numa
    // tela: informacao DO SERVIDOR.
    c.rect(-0.36f, -0.34f, 0.36f, 0.16f, 0.06f);
    c.line(0.0f, 0.16f, 0.0f, 0.32f);
    c.line(-0.18f, 0.34f, 0.18f, 0.34f);
    c.circle_filled(0.0f, -0.22f, 0.045f);
    c.line(0.0f, -0.12f, 0.0f, 0.04f);
}

void draw_sessions(const Canvas& c) {
    c.rect(-0.36f, -0.28f, 0.36f, 0.28f, 0.07f);
    c.dl->PathLineTo(c.at(-0.22f, -0.12f));
    c.dl->PathLineTo(c.at(-0.07f,  0.01f));
    c.dl->PathLineTo(c.at(-0.22f,  0.14f));
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
    c.line(0.02f, 0.14f, 0.22f, 0.14f);
}

void draw_locks(const Canvas& c) {
    // O cadeado de `lock` (canal cifrado) ocupa a caixa inteira. Este e'
    // pequeno e cheio, com a LISTA ao lado: os bloqueios em curso.
    c.arc(-0.22f, -0.04f, 0.11f, 180.0f, 360.0f);
    c.rect_filled(-0.38f, -0.04f, -0.06f, 0.24f, 0.04f);
    c.line(0.06f, -0.16f, 0.38f, -0.16f);
    c.line(0.06f,  0.04f, 0.38f,  0.04f);
    c.line(0.06f,  0.24f, 0.38f,  0.24f);
}

void draw_synonym(const Canvas& c) {
    // Dois retangulos (o nome e o objeto) e a seta de um para o outro.
    c.rect(-0.40f, -0.34f, -0.04f, -0.10f, 0.04f);
    c.rect( 0.04f,  0.10f,  0.40f,  0.34f, 0.04f);
    c.line(-0.22f, -0.10f, -0.22f, 0.22f);
    c.line(-0.22f,  0.22f,  0.04f, 0.22f);
    c.line(-0.08f,  0.12f,  0.04f, 0.22f);
    c.line(-0.08f,  0.32f,  0.04f, 0.22f);
}

void draw_job(const Canvas& c) {
    c.rect(-0.28f, -0.28f, 0.28f, 0.36f, 0.06f);
    c.rect_filled(-0.12f, -0.38f, 0.12f, -0.22f, 0.03f);
    c.dl->PathLineTo(c.at(-0.14f, 0.06f));
    c.dl->PathLineTo(c.at(-0.03f, 0.18f));
    c.dl->PathLineTo(c.at( 0.16f, -0.06f));
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
}

void draw_job_step(const Canvas& c) {
    c.line(-0.24f, -0.26f, -0.24f, 0.26f);
    c.circle(-0.24f, -0.26f, 0.085f);
    c.circle_filled(-0.24f, 0.0f, 0.085f);
    c.circle(-0.24f, 0.26f, 0.085f);
    c.line(-0.04f, -0.26f, 0.36f, -0.26f);
    c.line(-0.04f,  0.00f, 0.36f,  0.00f);
    c.line(-0.04f,  0.26f, 0.36f,  0.26f);
}

void draw_job_schedule(const Canvas& c) {
    c.rect(-0.32f, -0.26f, 0.32f, 0.34f, 0.06f);
    c.line(-0.32f, -0.08f, 0.32f, -0.08f);
    c.line(-0.16f, -0.38f, -0.16f, -0.18f);
    c.line( 0.16f, -0.38f,  0.16f, -0.18f);
    c.circle_filled(-0.14f, 0.06f, 0.04f);
    c.circle_filled( 0.02f, 0.06f, 0.04f);
    c.circle_filled( 0.18f, 0.06f, 0.04f);
    c.circle_filled(-0.14f, 0.22f, 0.04f);
}

// --- Barra lateral do editor SQL ------------------------------------------------

void draw_play_new(const Canvas& c) {
    // O play, deslocado, e um + no canto: "executar em OUTRA aba".
    c.triangle_filled(-0.32f, -0.30f, -0.32f, 0.30f, 0.12f, 0.0f);
    c.line(0.26f, 0.06f, 0.26f, 0.34f);
    c.line(0.12f, 0.20f, 0.40f, 0.20f);
}

void draw_play_script(const Canvas& c) {
    // A folha do script, com as linhas, e o play sobre o canto.
    c.rect(-0.34f, -0.36f, 0.16f, 0.30f, 0.05f);
    c.line(-0.24f, -0.20f, 0.06f, -0.20f);
    c.line(-0.24f, -0.04f, 0.06f, -0.04f);
    c.line(-0.24f,  0.12f, -0.06f, 0.12f);
    c.triangle_filled(0.10f, 0.06f, 0.10f, 0.40f, 0.40f, 0.23f);
}

void draw_plan(const Canvas& c) {
    // Um no em cima e dois embaixo, ligados em angulo reto: a arvore do
    // plano, lida de cima para baixo. Caixas, nao os circulos do
    // `dependency`.
    c.rect(-0.14f, -0.36f, 0.14f, -0.16f, 0.04f);
    c.line(0.0f, -0.16f, 0.0f, 0.0f);
    c.line(-0.24f, 0.0f, 0.24f, 0.0f);
    c.line(-0.24f, 0.0f, -0.24f, 0.14f);
    c.line( 0.24f, 0.0f,  0.24f, 0.14f);
    c.rect_filled(-0.38f, 0.14f, -0.10f, 0.34f, 0.04f);
    c.rect(0.10f, 0.14f, 0.38f, 0.34f, 0.04f);
}

void draw_ai(const Canvas& c) {
    // Faisca de quatro pontas, e uma menor ao lado.
    c.dl->PathLineTo(c.at(-0.06f, -0.38f));
    c.dl->PathLineTo(c.at( 0.02f, -0.10f));
    c.dl->PathLineTo(c.at( 0.30f, -0.02f));
    c.dl->PathLineTo(c.at( 0.02f,  0.06f));
    c.dl->PathLineTo(c.at(-0.06f,  0.34f));
    c.dl->PathLineTo(c.at(-0.14f,  0.06f));
    c.dl->PathLineTo(c.at(-0.42f, -0.02f));
    c.dl->PathLineTo(c.at(-0.14f, -0.10f));
    c.dl->PathStroke(c.color, ImDrawFlags_Closed, c.thickness);
    c.line(0.30f, 0.16f, 0.30f, 0.38f);
    c.line(0.19f, 0.27f, 0.41f, 0.27f);
}

void draw_terminal(const Canvas& c) {
    // O `sessions` e' a janela com o prompt. Aqui nao ha' moldura: so' o
    // prompt e o cursor em BLOCO cheio -- o lugar onde se digita.
    c.dl->PathLineTo(c.at(-0.38f, -0.24f));
    c.dl->PathLineTo(c.at(-0.12f,  0.00f));
    c.dl->PathLineTo(c.at(-0.38f,  0.24f));
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness * 1.2f);
    c.rect_filled(0.04f, 0.10f, 0.38f, 0.28f, 0.02f);
}

void draw_server_output(const Canvas& c) {
    // Balao de fala, com a ponta embaixo a' esquerda.
    c.dl->PathLineTo(c.at(-0.36f, -0.32f));
    c.dl->PathLineTo(c.at( 0.36f, -0.32f));
    c.dl->PathLineTo(c.at( 0.36f,  0.14f));
    c.dl->PathLineTo(c.at(-0.10f,  0.14f));
    c.dl->PathLineTo(c.at(-0.28f,  0.36f));
    c.dl->PathLineTo(c.at(-0.24f,  0.14f));
    c.dl->PathLineTo(c.at(-0.36f,  0.14f));
    c.dl->PathStroke(c.color, ImDrawFlags_Closed, c.thickness);
    c.line(-0.20f, -0.16f, 0.20f, -0.16f);
    c.line(-0.20f, -0.02f, 0.08f, -0.02f);
}

void draw_exec_log(const Canvas& c) {
    // Tres linhas de historico e o relogio no canto.
    c.line(-0.38f, -0.28f, 0.10f, -0.28f);
    c.line(-0.38f, -0.08f, -0.06f, -0.08f);
    c.line(-0.38f,  0.12f, -0.10f, 0.12f);
    c.circle(0.18f, 0.16f, 0.20f);
    c.line(0.18f, 0.16f, 0.18f, 0.04f);
    c.line(0.18f, 0.16f, 0.28f, 0.20f);
}

void draw_variables(const Canvas& c) {
    // Um x entre chaves -- ${x}.
    c.dl->PathLineTo(c.at(-0.22f, -0.34f));
    c.dl->PathBezierCubicCurveTo(c.at(-0.34f, -0.32f), c.at(-0.30f, -0.08f),
                                 c.at(-0.40f, 0.0f), 10);
    c.dl->PathBezierCubicCurveTo(c.at(-0.30f, 0.08f), c.at(-0.34f, 0.32f),
                                 c.at(-0.22f, 0.34f), 10);
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
    c.dl->PathLineTo(c.at(0.22f, -0.34f));
    c.dl->PathBezierCubicCurveTo(c.at(0.34f, -0.32f), c.at(0.30f, -0.08f),
                                 c.at(0.40f, 0.0f), 10);
    c.dl->PathBezierCubicCurveTo(c.at(0.30f, 0.08f), c.at(0.34f, 0.32f),
                                 c.at(0.22f, 0.34f), 10);
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
    c.line(-0.12f, -0.14f, 0.12f, 0.14f);
    c.line(-0.12f,  0.14f, 0.12f, -0.14f);
}

void draw_outline(const Canvas& c) {
    // Um titulo e dois itens recuados, cada um com seu marcador.
    c.circle_filled(-0.32f, -0.26f, 0.055f);
    c.line(-0.18f, -0.26f, 0.38f, -0.26f);
    c.circle_filled(-0.14f, 0.0f, 0.055f);
    c.line(0.0f, 0.0f, 0.38f, 0.0f);
    c.circle_filled(-0.14f, 0.26f, 0.055f);
    c.line(0.0f, 0.26f, 0.30f, 0.26f);
}

// --- Pastas tipadas ---------------------------------------------------------------
//
// No conjunto do DBeaver cada uma e' um SVG proprio. Aqui, a pasta do C-Otter
// com `marks` pontos embaixo: o bastante para serem desenhos DIFERENTES entre
// si (o teste compara a geometria) sem inventar onze pictogramas que so'
// aparecem quando o usuario troca de conjunto.
void draw_folder_marked(const Canvas& c, int marks) {
    draw_folder(c);
    for (int i = 0; i < marks; ++i) {
        const int   row = i / 4;
        const float x   = -0.24f + static_cast<float>(i % 4) * 0.16f;
        const float y   = 0.10f + static_cast<float>(row) * 0.12f;
        c.circle_filled(x, y, 0.035f);
    }
}

void draw_object_page(const Canvas& c) {
    // Folha com o canto dobrado.
    c.dl->PathLineTo(c.at(-0.26f, -0.36f));
    c.dl->PathLineTo(c.at( 0.10f, -0.36f));
    c.dl->PathLineTo(c.at( 0.28f, -0.18f));
    c.dl->PathLineTo(c.at( 0.28f,  0.36f));
    c.dl->PathLineTo(c.at(-0.26f,  0.36f));
    c.dl->PathStroke(c.color, ImDrawFlags_Closed, c.thickness);
    c.line(0.10f, -0.36f, 0.10f, -0.18f);
    c.line(0.10f, -0.18f, 0.28f, -0.18f);
}

// --- A grade de resultado ----------------------------------------------------

void draw_accept(const Canvas& c) {
    c.circle(0.0f, 0.0f, 0.36f);
    c.dl->PathLineTo(c.at(-0.18f, 0.00f));
    c.dl->PathLineTo(c.at(-0.04f, 0.15f));
    c.dl->PathLineTo(c.at( 0.20f, -0.14f));
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness * 1.2f);
}

void draw_reject(const Canvas& c) {
    c.circle(0.0f, 0.0f, 0.36f);
    c.line(-0.15f, -0.15f, 0.15f, 0.15f);
    c.line( 0.15f, -0.15f, -0.15f, 0.15f);
}

// A "linha" das quatro acoes de linha: uma faixa com duas divisorias.
void draw_row_strip(const Canvas& c, float top) {
    c.rect(-0.36f, top, 0.36f, top + 0.22f, 0.04f);
    c.line(-0.12f, top, -0.12f, top + 0.22f);
    c.line( 0.12f, top, 0.12f, top + 0.22f);
}

void draw_row_add(const Canvas& c) {
    draw_row_strip(c, -0.32f);
    c.line(0.0f, 0.04f, 0.0f, 0.36f);
    c.line(-0.16f, 0.20f, 0.16f, 0.20f);
}

void draw_row_copy(const Canvas& c) {
    draw_row_strip(c, -0.32f);
    draw_row_strip(c, 0.10f);
}

void draw_row_edit(const Canvas& c) {
    draw_row_strip(c, 0.10f);
    // Lapis: o corpo inclinado e a ponta.
    c.line(-0.22f, -0.04f, 0.14f, -0.36f);
    c.line(-0.10f, 0.04f, 0.24f, -0.26f);
    c.line(0.14f, -0.36f, 0.24f, -0.26f);
    c.triangle_filled(-0.22f, -0.04f, -0.10f, 0.04f, -0.28f, 0.08f);
}

void draw_row_delete(const Canvas& c) {
    draw_row_strip(c, -0.32f);
    c.line(-0.13f, 0.07f, 0.13f, 0.33f);
    c.line( 0.13f, 0.07f, -0.13f, 0.33f);
}

void draw_panels(const Canvas& c) {
    c.rect(-0.36f, -0.30f, 0.36f, 0.30f, 0.06f);
    c.line(0.06f, -0.30f, 0.06f, 0.30f);
    c.line(0.06f, 0.00f, 0.36f, 0.00f);
}

void draw_panel_calc(const Canvas& c) {
    c.rect(-0.36f, -0.36f, 0.36f, 0.36f, 0.06f);
    // Sigma.
    c.dl->PathLineTo(c.at( 0.16f, -0.20f));
    c.dl->PathLineTo(c.at(-0.16f, -0.20f));
    c.dl->PathLineTo(c.at( 0.02f,  0.00f));
    c.dl->PathLineTo(c.at(-0.16f,  0.20f));
    c.dl->PathLineTo(c.at( 0.16f,  0.20f));
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
}

void draw_panel_grouping(const Canvas& c) {
    c.rect_filled(-0.36f, -0.32f, 0.36f, -0.18f, 0.03f);
    c.line(-0.20f, -0.02f, 0.36f, -0.02f);
    c.line(-0.20f, 0.14f, 0.36f, 0.14f);
    c.line(-0.20f, 0.30f, 0.20f, 0.30f);
    c.line(-0.30f, -0.18f, -0.30f, 0.30f);
}

void draw_panel_metadata(const Canvas& c) {
    c.rect(-0.36f, -0.32f, 0.36f, 0.32f, 0.05f);
    c.rect_filled(-0.36f, -0.32f, -0.12f, 0.32f);
    c.line(-0.12f, -0.10f, 0.36f, -0.10f);
    c.line(-0.12f, 0.11f, 0.36f, 0.11f);
}

void draw_panel_references(const Canvas& c) {
    c.rect(-0.38f, -0.34f, -0.06f, -0.06f, 0.04f);
    c.rect(0.06f, 0.06f, 0.38f, 0.34f, 0.04f);
    c.line(-0.22f, -0.06f, -0.22f, 0.20f);
    c.line(-0.22f, 0.20f, 0.06f, 0.20f);
    c.triangle_filled(0.06f, 0.20f, -0.04f, 0.13f, -0.04f, 0.27f);
}

// O funil menor, no canto de cima, para os quatro botoes de filtro: sobra o
// canto de baixo para a marca que os distingue.
void draw_small_funnel(const Canvas& c) {
    c.dl->PathLineTo(c.at(-0.38f, -0.36f));
    c.dl->PathLineTo(c.at( 0.20f, -0.36f));
    c.dl->PathLineTo(c.at(-0.02f, -0.10f));
    c.dl->PathLineTo(c.at(-0.02f,  0.18f));
    c.dl->PathLineTo(c.at(-0.16f,  0.10f));
    c.dl->PathLineTo(c.at(-0.16f, -0.10f));
    c.dl->PathStroke(c.color, ImDrawFlags_Closed, c.thickness);
}

void draw_filter_apply(const Canvas& c) {
    draw_small_funnel(c);
    c.dl->PathLineTo(c.at(0.08f, 0.20f));
    c.dl->PathLineTo(c.at(0.18f, 0.32f));
    c.dl->PathLineTo(c.at(0.38f, 0.06f));
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness * 1.2f);
}

void draw_filter_reset(const Canvas& c) {
    draw_small_funnel(c);
    c.line(0.10f, 0.08f, 0.36f, 0.34f);
    c.line(0.36f, 0.08f, 0.10f, 0.34f);
}

void draw_filter_config(const Canvas& c) {
    draw_small_funnel(c);
    c.line(0.06f, 0.14f, 0.38f, 0.14f);
    c.circle_filled(0.16f, 0.14f, 0.05f);
    c.line(0.06f, 0.30f, 0.38f, 0.30f);
    c.circle_filled(0.30f, 0.30f, 0.05f);
}

void draw_filter_value(const Canvas& c) {
    draw_small_funnel(c);
    c.line(0.10f, 0.14f, 0.38f, 0.14f);
    c.line(0.10f, 0.28f, 0.38f, 0.28f);
}

void draw_grid_mode(const Canvas& c) {
    c.rect(-0.36f, -0.36f, 0.36f, 0.36f, 0.05f);
    c.line(-0.12f, -0.36f, -0.12f, 0.36f);
    c.line( 0.12f, -0.36f,  0.12f, 0.36f);
    c.line(-0.36f, -0.12f, 0.36f, -0.12f);
    c.line(-0.36f,  0.12f, 0.36f,  0.12f);
}

// --- Um icone por SGBD -----------------------------------------------------
//
// O que precisa ser lido em 16 px nao e' a especie do animal, e' a SILHUETA:
// redonda para PostgreSQL, angulosa para MySQL. Desenhar um elefante
// reconhecivel neste tamanho produziria uma mancha; desenhar a cabeca com a
// tromba descendo produz uma forma distinguivel de relance, que e' o
// criterio da diretriz 5.

void draw_pg_server(const Canvas& c) {
    // Cabeca arredondada -- o contorno que da' o "elefante" a' distancia.
    c.dl->PathLineTo(c.at(-0.30f, 0.10f));
    c.dl->PathBezierCubicCurveTo(c.at(-0.34f, -0.22f), c.at(-0.12f, -0.34f),
                                 c.at(0.04f, -0.30f), 14);
    c.dl->PathBezierCubicCurveTo(c.at(0.24f, -0.26f), c.at(0.30f, -0.06f),
                                 c.at(0.26f, 0.10f), 14);
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);

    // A tromba, descendo e curvando: e' o traço que ninguem confunde.
    c.dl->PathLineTo(c.at(0.02f, -0.02f));
    c.dl->PathBezierCubicCurveTo(c.at(0.02f, 0.18f), c.at(-0.06f, 0.26f),
                                 c.at(-0.16f, 0.30f), 12);
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);

    // As duas orelhas, como arcos laterais.
    c.arc(-0.24f, 0.02f, 0.13f, 40.0f, 200.0f);
    c.arc( 0.20f, 0.02f, 0.13f, -20.0f, 140.0f);

    // O olho.
    c.circle_filled(-0.10f, -0.10f, 0.035f);
}

void draw_my_server(const Canvas& c) {
    // Golfinho mergulhando, focinho a' ESQUERDA e cauda a' direita.
    //
    // A primeira versao saiu de cabeca para baixo: a nadadeira apontava para
    // fora do dorso errado e a cauda ficava sob o corpo. So' apareceu ao
    // ampliar a captura -- no tamanho da arvore era uma mancha alongada, que
    // e' exatamente o que a diretriz 5 chama de icone indistinguivel.
    //
    // A convencao aqui: y NEGATIVO e' para CIMA. O dorso e' a curva de cima,
    // o ventre a de baixo.

    // Dorso: sobe do focinho, passa pelo alto do corpo e desce para a cauda.
    c.dl->PathLineTo(c.at(-0.36f, 0.02f));
    c.dl->PathBezierCubicCurveTo(c.at(-0.16f, -0.20f), c.at(0.08f, -0.22f),
                                 c.at(0.26f, -0.06f), 16);
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);

    // Ventre: volta do focinho por baixo, fechando a forma.
    c.dl->PathLineTo(c.at(-0.36f, 0.02f));
    c.dl->PathBezierCubicCurveTo(c.at(-0.16f, 0.18f), c.at(0.08f, 0.20f),
                                 c.at(0.26f, -0.06f), 16);
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);

    // Nadadeira dorsal: triangulo para CIMA, saindo do dorso.
    c.line(-0.04f, -0.19f, 0.06f, -0.36f);
    c.line( 0.06f, -0.36f, 0.12f, -0.17f);

    // Cauda bifurcada na ponta direita, abrindo em V.
    c.line(0.26f, -0.06f, 0.38f, -0.20f);
    c.line(0.26f, -0.06f, 0.38f,  0.04f);
    c.line(0.38f, -0.20f, 0.38f,  0.04f);

    // Olho, perto do focinho.
    c.circle_filled(-0.22f, -0.02f, 0.030f);
}

void draw_ms_server(const Canvas& c) {
    // Cilindro de banco com duas "velas" curvas ao lado -- o que sobra do
    // logotipo do SQL Server quando se tira a cor: base cilindrica e as duas
    // laminas inclinadas. Distingue-se do elefante (redondo) e do golfinho
    // (horizontal) por ser vertical e ter linhas retas.
    c.arc(-0.10f, -0.22f, 0.20f, 180.0f, 360.0f);
    c.arc(-0.10f, -0.22f, 0.20f, 0.0f, 180.0f);
    c.line(-0.30f, -0.22f, -0.30f, 0.26f);
    c.line( 0.10f, -0.22f,  0.10f, 0.26f);
    c.arc(-0.10f, 0.26f, 0.20f, 0.0f, 180.0f);
    c.arc(-0.10f, 0.02f, 0.20f, 0.0f, 180.0f);

    // As duas laminas.
    c.line(0.20f, 0.30f, 0.40f, -0.30f);
    c.line(0.30f, 0.30f, 0.44f, -0.12f);
}

void draw_sa_server(const Canvas& c) {
    // O simbolo da Sybase: um retangulo em pe' com a espiral aurea dentro.
    // Aqui, a moldura e tres quartos de circulo que encolhem -- o bastante
    // para ler "espiral" no tamanho da arvore, onde o logotipo e' uma mancha.
    c.rect(-0.28f, -0.38f, 0.28f, 0.38f, 0.03f);
    c.arc( 0.28f, -0.04f, 0.56f, 180.0f, 270.0f);
    c.arc( 0.00f, -0.04f, 0.28f,  90.0f, 180.0f);
    c.arc( 0.00f,  0.10f, 0.14f,   0.0f,  90.0f);
    c.line(-0.28f, -0.04f, 0.28f, -0.04f);
}

void draw_generic_server(const Canvas& c) {
    // Torre de servidor: tres modulos empilhados, cada um com seu LED.
    //
    // Deliberadamente SEM animal: e' o desenho de "driver que ainda nao tem
    // icone proprio", e precisa parecer uma categoria, nao um SGBD.
    for (int i = 0; i < 3; ++i) {
        const float y = -0.30f + static_cast<float>(i) * 0.21f;
        c.rect(-0.26f, y, 0.26f, y + 0.16f, 0.03f);
        c.circle_filled(-0.17f, y + 0.08f, 0.035f);
        c.line(-0.04f, y + 0.08f, 0.18f, y + 0.08f);
    }
}

} // namespace

Icon driver_icon(std::string_view driver_id) noexcept {
    if (driver_id == "postgresql") return Icon::pg_server;
    if (driver_id == "mysql" || driver_id == "mariadb") return Icon::my_server;
    if (driver_id == "sqlserver" || driver_id == "mssql") return Icon::ms_server;
    if (driver_id == "sqlanywhere") return Icon::sa_server;
    return Icon::generic_server;
}

namespace {

// Halo suave atras do icone. Varias circunferencias concentricas com alfa
// decrescente aproximam um blur gaussiano sem shader nem textura.
void draw_glow(ImDrawList* dl, const ImVec2& center, float radius,
               std::uint32_t color, float strength) {
    if (strength <= 0.0f) return;

    constexpr int kLayers = 5;
    for (int i = kLayers; i > 0; --i) {
        const float t = static_cast<float>(i) / kLayers;
        const float alpha = strength * 0.12f * (1.0f - t) * (1.0f - t);
        dl->AddCircleFilled(center, radius * (0.6f + t * 0.9f),
                            with_alpha(color, alpha), 20);
    }
}

} // namespace

float toolbar_button_size() {
    return ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f + 6.0f;
}

void draw_icon(Icon icon, const ImVec2& center, float size, std::uint32_t color,
               float thickness) {
    draw_icon_to(ImGui::GetWindowDrawList(), icon, center, size, color, thickness);
}

void draw_icon_to(ImDrawList* dl, Icon icon, const ImVec2& center, float size,
                  std::uint32_t color, float thickness) {
    // O original do DBeaver, quando o conjunto ativo e' o dele e o icone tem
    // equivalente. A imagem ocupa um pouco mais que `size`: os vetoriais
    // deixam margem dentro da caixa, e os SVG de 16 px do DBeaver a ocupam
    // quase inteira -- no mesmo `size` eles pareceriam menores.
    //
    // O lado e a posicao sao arredondados para pixel inteiro: uma imagem de
    // 16 px desenhada em 15,6 borra.
    const int pixels = static_cast<int>(size * 1.12f + 0.5f);
    if (const ImTextureID texture = icon_texture(icon, pixels)) {
        const ImVec2 min(static_cast<float>(static_cast<int>(center.x - pixels * 0.5f + 0.5f)),
                         static_cast<float>(static_cast<int>(center.y - pixels * 0.5f + 0.5f)));
        const ImVec2 max(min.x + static_cast<float>(pixels),
                         min.y + static_cast<float>(pixels));

        // A cor do icone e' a dele. Da cor pedida so' o ALFA e' aproveitado:
        // e' como o botao desabilitado fica esmaecido.
        const ImU32 alpha = (static_cast<ImU32>(color) >> IM_COL32_A_SHIFT) & 0xFFu;

        // O logotipo da Sybase e' azul-marinho sobre transparente: foi feito
        // para fundo claro, e num tema escuro vira uma mancha que nao se le'
        // (visto na captura ampliada). O arquivo continua o original do
        // DBeaver; num fundo escuro ele ganha uma placa clara por tras, que
        // e' o fundo para o qual foi desenhado.
        if (icon == Icon::sa_server) {
            const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
            if (0.2126f * bg.x + 0.7152f * bg.y + 0.0722f * bg.z < 0.5f) {
                dl->AddRectFilled(ImVec2(min.x - 1.0f, min.y - 1.0f),
                                  ImVec2(max.x + 1.0f, max.y + 1.0f),
                                  IM_COL32(232, 236, 244, alpha), 3.0f);
            }
        }
        dl->AddImage(texture, min, max, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                     IM_COL32(255, 255, 255, alpha));
        return;
    }

    const Canvas c{dl, center, size, color, thickness};

    switch (icon) {
        case Icon::connect:       draw_connect(c);       break;
        case Icon::disconnect:    draw_disconnect(c);    break;
        case Icon::play:          draw_play(c);          break;
        case Icon::stop:          draw_stop(c);          break;
        case Icon::commit:        draw_commit(c);        break;
        case Icon::rollback:      draw_rollback(c);      break;
        case Icon::pg_server:      draw_pg_server(c);      break;
        case Icon::my_server:      draw_my_server(c);      break;
        case Icon::ms_server:      draw_ms_server(c);      break;
        case Icon::sa_server:      draw_sa_server(c);      break;
        case Icon::generic_server: draw_generic_server(c); break;
        case Icon::database:      draw_database(c);      break;
        case Icon::table:         draw_table(c);         break;
        case Icon::view:          draw_view(c);          break;
        case Icon::column:        draw_column(c);        break;
        case Icon::key:           draw_key(c);           break;
        case Icon::folder:        draw_folder(c);        break;
        case Icon::refresh:       draw_refresh(c);       break;
        case Icon::search:        draw_search(c);        break;
        case Icon::settings:      draw_settings(c);      break;
        case Icon::plus:          draw_plus(c);          break;
        case Icon::close:         draw_close(c);         break;
        case Icon::pin:           draw_pin(c);           break;
        case Icon::save:          draw_save(c);          break;
        case Icon::open:          draw_open(c);          break;
        case Icon::copy:          draw_copy(c);          break;
        case Icon::chevron_left:  draw_chevron_left(c);  break;
        case Icon::chevron_right: draw_chevron_right(c); break;
        case Icon::first_page:    draw_first_page(c);    break;
        case Icon::last_page:     draw_last_page(c);     break;
        case Icon::chevron_down:  draw_chevron_down(c);  break;
        case Icon::warning:       draw_warning(c);       break;
        case Icon::error:         draw_error(c);         break;
        case Icon::info:          draw_info(c);          break;
        case Icon::clock:         draw_clock(c);         break;
        case Icon::lock:          draw_lock(c);          break;
        case Icon::record:        draw_record(c);        break;
        case Icon::filter:        draw_filter(c);        break;

        case Icon::materialized_view: draw_materialized_view(c); break;
        case Icon::index:             draw_index(c);             break;
        case Icon::constraint:        draw_constraint(c);        break;
        case Icon::foreign_key:       draw_foreign_key(c);       break;
        case Icon::references:        draw_references(c);        break;
        case Icon::sequence:          draw_sequence(c);          break;
        case Icon::function:          draw_function(c);          break;
        case Icon::procedure:         draw_procedure(c);         break;
        case Icon::trigger:           draw_trigger(c);           break;
        case Icon::data_type:         draw_data_type(c);         break;
        case Icon::extension:         draw_extension(c);         break;
        case Icon::role:              draw_role(c);              break;
        case Icon::tablespace:        draw_tablespace(c);        break;
        case Icon::schema:            draw_schema(c);            break;
        case Icon::pivot:             draw_pivot(c);             break;
        case Icon::partition:         draw_partition(c);         break;
        case Icon::event:             draw_event(c);             break;
        case Icon::user:              draw_user(c);              break;
        case Icon::grant:             draw_grant(c);             break;

        case Icon::foreign_table: draw_foreign_table(c); break;
        case Icon::aggregate: draw_aggregate(c); break;
        case Icon::dependency: draw_dependency(c); break;
        case Icon::rule: draw_rule(c); break;
        case Icon::policy: draw_policy(c); break;
        case Icon::inheritance: draw_inheritance(c); break;
        case Icon::parameter: draw_parameter(c); break;
        case Icon::event_trigger: draw_event_trigger(c); break;
        case Icon::storage: draw_storage(c); break;
        case Icon::foreign_wrapper: draw_foreign_wrapper(c); break;
        case Icon::foreign_server: draw_foreign_server(c); break;
        case Icon::user_mapping: draw_user_mapping(c); break;
        case Icon::setting: draw_setting(c); break;
        case Icon::role_group: draw_role_group(c); break;
        case Icon::access_method: draw_access_method(c); break;
        case Icon::operator_class: draw_operator_class(c); break;
        case Icon::operator_family: draw_operator_family(c); break;
        case Icon::encoding: draw_encoding(c); break;
        case Icon::collation: draw_collation(c); break;
        case Icon::language: draw_language(c); break;
        case Icon::extension_available: draw_extension_available(c); break;
        case Icon::administer: draw_administer(c); break;
        case Icon::system_info: draw_system_info(c); break;
        case Icon::sessions: draw_sessions(c); break;
        case Icon::locks: draw_locks(c); break;
        case Icon::synonym: draw_synonym(c); break;
        case Icon::job: draw_job(c); break;
        case Icon::job_step: draw_job_step(c); break;
        case Icon::job_schedule: draw_job_schedule(c); break;
        case Icon::play_new: draw_play_new(c); break;
        case Icon::play_script: draw_play_script(c); break;
        case Icon::plan: draw_plan(c); break;
        case Icon::ai: draw_ai(c); break;
        case Icon::terminal: draw_terminal(c); break;
        case Icon::server_output: draw_server_output(c); break;
        case Icon::exec_log: draw_exec_log(c); break;
        case Icon::variables: draw_variables(c); break;
        case Icon::outline: draw_outline(c); break;

        case Icon::folder_database: draw_folder_marked(c, 1); break;
        case Icon::folder_schema: draw_folder_marked(c, 2); break;
        case Icon::folder_table: draw_folder_marked(c, 3); break;
        case Icon::folder_view: draw_folder_marked(c, 4); break;
        case Icon::folder_link: draw_folder_marked(c, 5); break;
        case Icon::folder_user: draw_folder_marked(c, 6); break;
        case Icon::folder_constraint: draw_folder_marked(c, 7); break;
        case Icon::folder_columns: draw_folder_marked(c, 8); break;
        case Icon::folder_admin: draw_folder_marked(c, 9); break;
        case Icon::folder_info: draw_folder_marked(c, 10); break;
        case Icon::object_page: draw_object_page(c); break;
        case Icon::accept: draw_accept(c); break;
        case Icon::reject: draw_reject(c); break;
        case Icon::row_add: draw_row_add(c); break;
        case Icon::row_copy: draw_row_copy(c); break;
        case Icon::row_edit: draw_row_edit(c); break;
        case Icon::row_delete: draw_row_delete(c); break;
        case Icon::panels: draw_panels(c); break;
        case Icon::panel_calc: draw_panel_calc(c); break;
        case Icon::panel_grouping: draw_panel_grouping(c); break;
        case Icon::panel_metadata: draw_panel_metadata(c); break;
        case Icon::panel_references: draw_panel_references(c); break;
        case Icon::filter_apply: draw_filter_apply(c); break;
        case Icon::filter_reset: draw_filter_reset(c); break;
        case Icon::filter_config: draw_filter_config(c); break;
        case Icon::filter_value: draw_filter_value(c); break;
        case Icon::grid_mode: draw_grid_mode(c); break;
    }
}

float tree_arrow_gap() {
    // O TreeNode reserva FontSize + 2*FramePadding.x; o triangulo acaba a
    // ~0,56 FontSize depois da primeira margem. O vao visivel ate' o icone
    // e' o resto: 0,44 FontSize + FramePadding.x (15,5 px medidos na tela
    // com fonte 15 e margem 9). Tira-se METADE dele.
    //
    // O primeiro pedido foi 75%; visto na tela, o usuario corrigiu para
    // 50% -- a 4 px o icone parecia colado na seta.
    const float gap =
        ImGui::GetFontSize() * 0.44f + ImGui::GetStyle().FramePadding.x;
    return -std::floor(gap * 0.50f + 0.5f);
}

void same_line_after_arrow() {
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + tree_arrow_gap());
}

float tree_label_gap() {
    // Era 6. O desenho do icone ja' deixa ~1,5 px de margem dentro da propria
    // caixa: com 2 aqui, o vao VISIVEL cai de 7 para 3,5 -- a metade pedida.
    return 2.0f;
}

bool icon_button(const char* id, Icon icon, const char* tooltip, bool enabled,
                 std::uint32_t tint, float box) {
    const Palette& p = colors();
    if (box <= 0.0f) box = toolbar_button_size();

    ImGui::BeginDisabled(!enabled);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(box, box));
    const bool hovered = ImGui::IsItemHovered();
    const bool active  = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 center(origin.x + box * 0.5f, origin.y + box * 0.5f);

    // Glow no hover, antes do fundo: fica por trás de tudo.
    if (hovered && enabled) {
        draw_glow(dl, center, box * 0.5f,
                  tint != 0 ? tint : p.glow, p.glow_strength);
    }

    if (enabled && (hovered || active)) {
        dl->AddRectFilled(origin, ImVec2(origin.x + box, origin.y + box),
                          with_alpha(p.accent, active ? 0.45f : 0.24f),
                          ImGui::GetStyle().FrameRounding);
    }

    const std::uint32_t color =
        !enabled ? with_alpha(p.text_dim, 0.45f)
                 : (tint != 0 ? tint : (hovered ? p.text_bright : p.text));

    draw_icon(icon, center, box * 0.52f, color);

    ImGui::EndDisabled();

    // AllowWhenDisabled: dentro de BeginDisabled o item nunca conta como
    // "hovered", e o botao desabilitado ficava sem tooltip -- justamente o
    // que precisa dizer por que nao funciona.
    if (tooltip != nullptr && tooltip[0] != '\0' &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        hint(tooltip);
    }
    return pressed && enabled;
}

bool icon_text_button(const char* id, Icon icon, const char* label,
                      const char* tooltip, bool enabled) {
    const Palette& p = colors();
    const ImGuiStyle& style = ImGui::GetStyle();

    const float box = toolbar_button_size();
    const float text_width = ImGui::CalcTextSize(label).x;
    const float width = box + text_width + style.FramePadding.x;

    ImGui::BeginDisabled(!enabled);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(width, box));
    const bool hovered = ImGui::IsItemHovered();
    const bool active  = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 icon_center(origin.x + box * 0.5f, origin.y + box * 0.5f);

    if (hovered && enabled) {
        draw_glow(dl, icon_center, box * 0.5f, p.glow, p.glow_strength);
    }
    if (enabled && (hovered || active)) {
        dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + box),
                          with_alpha(p.accent, active ? 0.45f : 0.24f),
                          style.FrameRounding);
    }

    const std::uint32_t color =
        !enabled ? with_alpha(p.text_dim, 0.45f)
                 : (hovered ? p.text_bright : p.text);

    draw_icon(icon, icon_center, box * 0.52f, color);
    dl->AddText(ImVec2(origin.x + box,
                       origin.y + (box - ImGui::GetFontSize()) * 0.5f),
                color, label);

    ImGui::EndDisabled();

    if (hovered && tooltip != nullptr && tooltip[0] != '\0') {
        hint(tooltip);
    }
    return pressed && enabled;
}

void icon_inline(Icon icon, std::uint32_t color, float scale) {
    const float box = ImGui::GetFontSize() * scale;

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 center(origin.x + box * 0.5f,
                        origin.y + ImGui::GetFontSize() * 0.5f);

    draw_icon(icon, center, box * 0.9f, color, 1.4f);
    ImGui::Dummy(ImVec2(box, ImGui::GetFontSize()));
}

} // namespace otter::ui
