// C-Otter -- backend de janela: GLFW + OpenGL 3.3 core.
//
// Implementacao unica para Windows e Linux (ADR 0006). Substituiu o backend
// Win32+D3D11: uma UI 2D nao ganha nada com D3D, e manter dois backends de
// render custaria ~120 h/h sem beneficio mensuravel.
#include "ui/app_window.hpp"
#include "ui/theme.hpp"

// windows.h antes de GLFW: ambos definem APIENTRY, e incluir na ordem inversa
// produz C4005 (macro redefinition), que o nosso /WX transforma em erro.
#ifdef _WIN32
#include <windows.h>
#endif

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <string>

namespace otter::ui {
namespace {

void glfw_error_callback(int code, const char* description) {
    std::fprintf(stderr, "[glfw] erro %d: %s\n", code, description);
}

// Carrega uma fonte monoespacada do sistema com cobertura latina.
// A fonte embutida do ImGui cobre so' ASCII: acentos do portugues e o sigma da
// linha de totais virariam '?'.
void load_ui_font(ImGuiIO& io, float scale) {
    // Latin + Latin-1 + Latin Extended-A cobrem portugues; os demais blocos
    // trazem aspas tipograficas, setas e simbolos usados na grade.
    static const ImWchar ranges[] = {
        0x0020, 0x00FF,   // Basic Latin + Latin-1 Supplement
        0x0100, 0x017F,   // Latin Extended-A
        0x2018, 0x201F,   // aspas tipograficas
        0x2190, 0x2193,   // setas
        0x2211, 0x2211,   // N-ARY SUMMATION
        0x221A, 0x221A,   // raiz
        0x2260, 0x2265,   // comparadores
        0,
    };

    const float size = 16.0f * scale;

#ifdef _WIN32
    wchar_t win_dir[MAX_PATH] = {};
    if (GetWindowsDirectoryW(win_dir, MAX_PATH) != 0) {
        char dir[MAX_PATH * 2] = {};
        WideCharToMultiByte(CP_UTF8, 0, win_dir, -1, dir, sizeof(dir), nullptr, nullptr);

        // SQL e' codigo: fonte monoespacada preserva o alinhamento de colunas.
        const char* candidates[] = {"\\Fonts\\CascadiaMono.ttf",
                                    "\\Fonts\\CascadiaCode.ttf",
                                    "\\Fonts\\consola.ttf"};
        for (const char* candidate : candidates) {
            const std::string path = std::string(dir) + candidate;
            if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;

            ImFontConfig config;
            config.OversampleH = 2;
            config.OversampleV = 1;
            config.PixelSnapH  = true;
            if (io.Fonts->AddFontFromFileTTF(path.c_str(), size, &config, ranges)) {
                return;
            }
        }
    }
#else
    const char* candidates[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
    };
    for (const char* path : candidates) {
        ImFontConfig config;
        config.OversampleH = 2;
        config.OversampleV = 1;
        config.PixelSnapH  = true;
        if (io.Fonts->AddFontFromFileTTF(path, size, &config, ranges)) return;
    }
#endif
    // Nenhuma encontrada: segue com a fonte embutida (so' ASCII).
}

} // namespace

struct AppWindow::Impl {
    GLFWwindow* window = nullptr;
    float       scale  = 1.0f;
};

Result<std::unique_ptr<AppWindow>> AppWindow::create(const WindowConfig& config) {
    glfwSetErrorCallback(glfw_error_callback);

    if (glfwInit() != GLFW_TRUE) {
        return fail(Errc::internal, "glfwInit falhou");
    }

    // OpenGL 3.3 core: suportado por qualquer GPU desde 2010 e por Mesa/llvmpipe
    // em maquina sem GPU (VM, RDP, servidor).
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);   // so' mostra apos posicionar

    auto window = std::unique_ptr<AppWindow>(new AppWindow());
    window->impl_ = std::make_unique<Impl>();
    Impl& impl = *window->impl_;

    // Limita a janela a area de trabalho do monitor: criar maior que a tela
    // deixa o rodape (barra de status) fora da area visivel.
    int width  = config.width;
    int height = config.height;

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    int work_x = 0, work_y = 0, work_w = 0, work_h = 0;
    if (monitor != nullptr) {
        glfwGetMonitorWorkarea(monitor, &work_x, &work_y, &work_w, &work_h);
        if (work_w > 0 && width  > work_w) width  = work_w;
        if (work_h > 0 && height > work_h) height = work_h;
    }

    impl.window = glfwCreateWindow(width, height, config.title.c_str(),
                                   nullptr, nullptr);
    if (impl.window == nullptr) {
        glfwTerminate();
        return fail(Errc::internal, "glfwCreateWindow falhou (OpenGL 3.3 indisponivel?)");
    }

    if (work_w > 0 && work_h > 0) {
        glfwSetWindowPos(impl.window,
                         work_x + (work_w - width)  / 2,
                         work_y + (work_h - height) / 2);
    }
    glfwShowWindow(impl.window);

    glfwMakeContextCurrent(impl.window);
    glfwSwapInterval(1);   // vsync

    // Escala de DPI: sem isto o texto fica minusculo em monitor 4K.
    float scale_x = 1.0f, scale_y = 1.0f;
    if (monitor != nullptr) glfwGetMonitorContentScale(monitor, &scale_x, &scale_y);
    impl.scale = scale_x > 0.0f ? scale_x : 1.0f;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;   // layout persistido por nos, nao pelo imgui.ini

    apply_theme(ImGui::GetStyle());
    if (impl.scale != 1.0f) ImGui::GetStyle().ScaleAllSizes(impl.scale);

    load_ui_font(io, impl.scale);

    ImGui_ImplGlfw_InitForOpenGL(impl.window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    return window;
}

AppWindow::~AppWindow() {
    if (!impl_) return;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (impl_->window != nullptr) glfwDestroyWindow(impl_->window);
    glfwTerminate();
}

void* AppWindow::native_handle() const noexcept {
    return impl_ ? static_cast<void*>(impl_->window) : nullptr;
}

void AppWindow::run(const FrameFn& draw_frame) {
    Impl& impl = *impl_;

    while (!should_close_ && glfwWindowShouldClose(impl.window) == 0) {
        glfwPollEvents();

        // Janela minimizada: dorme em vez de queimar CPU/GPU redesenhando nada.
        if (glfwGetWindowAttrib(impl.window, GLFW_ICONIFIED) != 0) {
            glfwWaitEventsTimeout(0.1);
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        draw_frame();

        ImGui::Render();

        int fb_width = 0, fb_height = 0;
        glfwGetFramebufferSize(impl.window, &fb_width, &fb_height);
        glViewport(0, 0, fb_width, fb_height);

        // Fundo do tema ativo: fixar a cor deixaria o tema claro com uma
        // moldura escura nas bordas.
        const std::uint32_t bg = colors().bg_darkest;
        glClearColor(static_cast<float>((bg >> 0)  & 0xFF) / 255.0f,
                     static_cast<float>((bg >> 8)  & 0xFF) / 255.0f,
                     static_cast<float>((bg >> 16) & 0xFF) / 255.0f,
                     1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(impl.window);
    }
}

} // namespace otter::ui
