// C-Otter -- ui/ddl_dialog.hpp
//
// Mostra o DDL gerado e pede confirmacao ANTES de executar.
//
// Existe porque DDL nao tem desfazer. A grade editavel (ADR 0014) ja' mostra o
// que vai gravar antes de gravar; aqui o risco e' um grau acima -- la' o pior
// caso e' uma linha errada, aqui e' uma tabela inteira.
//
// O DBeaver faz o mesmo: a aba "Persist" lista os comandos pendentes e o
// usuario confirma. Executar direto do formulario seria mais rapido e
// indefensavel.
#pragma once

#include "db/alter.hpp"

#include <functional>
#include <string>

namespace otter::ui {

class DdlDialog {
public:
    // Chamado quando o usuario confirma. Recebe os comandos na ordem.
    using Confirm = std::function<void(const std::vector<std::string>&)>;

    // Abre com um script ja' gerado. `title` diz o que vai acontecer
    // ("Alterar tabela cliente"), porque o SQL sozinho exige lê-lo para saber.
    void open(std::string title, db::AlterScript script, Confirm on_confirm);

    void close() noexcept { visible_ = false; }
    [[nodiscard]] bool visible() const noexcept { return visible_; }

    // Desenha. Devolve verdadeiro se executou neste quadro.
    bool draw(bool can_execute, bool ddl_in_transaction);

private:
    bool            visible_ = false;
    std::string     title_;
    db::AlterScript script_;
    Confirm         on_confirm_;

    // Confirmacao extra quando ha' comando destrutivo. Comeca sempre falsa:
    // reaproveitar o "sim" da janela anterior seria o caminho para um DROP
    // acidental.
    bool            acknowledged_ = false;

    // Edicao manual do script antes de executar. O DBeaver permite, e e' o
    // que salva quando o gerador nao cobre um caso -- melhor que forcar o
    // usuario a montar tudo a mao noutra aba.
    std::string     edited_;
    bool            editing_ = false;
};

} // namespace otter::ui
