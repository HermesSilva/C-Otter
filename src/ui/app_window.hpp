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

namespace otter::ui {

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

    [[nodiscard]] void* native_handle() const noexcept;

private:
    AppWindow() = default;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool should_close_ = false;
};

} // namespace otter::ui
