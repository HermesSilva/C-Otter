// C-Otter -- ui/app_window.hpp
//
// Janela da aplicacao e loop de render. Isola Win32/DX11 do resto da UI para
// que o backend de Linux (GLFW/OpenGL) entre depois sem tocar no codigo de
// paineis.
#pragma once

#include "base/error.hpp"

#include <functional>
#include <memory>
#include <string>

struct ImFont;

namespace otter::ui {

// Fonte monoespacada, para o que E' codigo: o editor SQL.
//
// A interface usa a fonte do sistema (ver load_ui_font). Devolve nulo se o
// sistema nao tinha nenhuma mono instalada -- quem chama testa antes de
// PushFont().
[[nodiscard]] ImFont* mono_font();

struct WindowConfig {
    std::string title  = "C-Otter";
    int         width  = 1600;
    int         height = 980;
};

class AppWindow {
public:
    // Chamado uma vez por frame, entre NewFrame() e Render().
    using FrameFn = std::function<void()>;

    static Result<std::unique_ptr<AppWindow>> create(const WindowConfig& config);

    ~AppWindow();

    AppWindow(const AppWindow&)            = delete;
    AppWindow& operator=(const AppWindow&) = delete;

    // Bombeia mensagens e desenha ate a janela fechar.
    void run(const FrameFn& draw_frame);

    // Pede o encerramento do loop.
    void request_close() noexcept { should_close_ = true; }

    // O usuario clicou no "X" desde o quadro anterior?
    //
    // O clique NAO fecha sozinho: vira um pedido que o shell responde --
    // confirmando a saida, ou perguntando o que fazer com o trabalho nao
    // salvo. Quem consome deve chamar clear_close_request() ao tratar.
    [[nodiscard]] bool close_requested() const noexcept {
        return close_requested_;
    }
    void clear_close_request() noexcept { close_requested_ = false; }

    [[nodiscard]] void* native_handle() const noexcept;

private:
    AppWindow() = default;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool should_close_ = false;
    bool close_requested_ = false;
};

} // namespace otter::ui
