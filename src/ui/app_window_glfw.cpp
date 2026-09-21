// C-Otter -- backend de janela: GLFW + OpenGL 3.3 core.
//
// Implementacao unica para Windows e Linux (ADR 0006). Substituiu o backend
// Win32+D3D11: uma UI 2D nao ganha nada com D3D, e manter dois backends de
// render custaria ~120 h/h sem beneficio mensuravel.
#include "ui/app_window.hpp"

#include "db/connection_store.hpp"
#include "ui/theme.hpp"

#include <filesystem>
#include <string>

// windows.h antes de GLFW: ambos definem APIENTRY, e incluir na ordem inversa
// produz C4005 (macro redefinition), que o nosso /WX transforma em erro.
#ifdef _WIN32
#include <windows.h>
#endif

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <cmath>     // sqrt, para o antialias do icone da janela
#include <cstdio>
#include <string>

namespace otter::ui {
namespace {

void glfw_error_callback(int code, const char* description) {
    std::fprintf(stderr, "[glfw] erro %d: %s\n", code, description);
}

// Latin + Latin-1 + Latin Extended-A cobrem portugues; os demais blocos
// trazem aspas tipograficas, setas e simbolos usados na grade.
//
// A fonte embutida do ImGui cobre so' ASCII: acentos do portugues e o sigma da
// linha de totais virariam '?'.
const ImWchar* latin_ranges() {
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
    return ranges;
}

ImFont* load_first_available(ImGuiIO& io, const char* const* paths,
                             std::size_t count, float size) {
    for (std::size_t i = 0; i < count; ++i) {
        ImFontConfig config;
        config.OversampleH = 2;
        config.OversampleV = 1;
        config.PixelSnapH  = true;

        ImFont* font =
            io.Fonts->AddFontFromFileTTF(paths[i], size, &config, latin_ranges());
        if (font != nullptr) return font;
    }
    return nullptr;
}

// Carrega DUAS fontes: a da interface e a do editor.
//
// Ate' 2026-09-21 a UI inteira usava monoespacada, com a justificativa de que
// "SQL e' codigo". A justificativa vale para o EDITOR; menus, rotulos, botoes
// e a arvore de objetos nao sao codigo, e o DBeaver os desenha com a fonte do
// sistema. Monoespacar tudo deixava a interface com cara de terminal e larga
// demais -- um rotulo em Consolas ocupa ~20% mais que em Segoe UI, e era por
// isso que "Configuracoes de conexao" nao cabia na arvore do dialogo.
//
// A mono fica em io.Fonts->Fonts[1], para o editor SQL empurrar com
// PushFont(). A grade usa a da interface: alinhamento de coluna ela ja'
// resolve com ImGuiTable, que mede cada celula.
void load_ui_font(ImGuiIO& io, float scale) {
    const float size = 16.0f * scale;

#ifdef _WIN32
    wchar_t win_dir[MAX_PATH] = {};
    if (GetWindowsDirectoryW(win_dir, MAX_PATH) == 0) return;

    char dir[MAX_PATH * 2] = {};
    WideCharToMultiByte(CP_UTF8, 0, win_dir, -1, dir, sizeof(dir), nullptr, nullptr);

    const std::string ui_paths[] = {
        std::string(dir) + "\\Fonts\\segoeui.ttf",
        std::string(dir) + "\\Fonts\\tahoma.ttf",
        std::string(dir) + "\\Fonts\\arial.ttf",
    };
    const std::string mono_paths[] = {
        std::string(dir) + "\\Fonts\\CascadiaMono.ttf",
        std::string(dir) + "\\Fonts\\CascadiaCode.ttf",
        std::string(dir) + "\\Fonts\\consola.ttf",
    };
#else
    const std::string ui_paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
    };
    const std::string mono_paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
    };
#endif

    const char* ui_c[3] = {ui_paths[0].c_str(), ui_paths[1].c_str(),
                           ui_paths[2].c_str()};
    const char* mono_c[3] = {mono_paths[0].c_str(), mono_paths[1].c_str(),
                             mono_paths[2].c_str()};

    // A PRIMEIRA carregada vira a padrao do ImGui -- por isso a da interface
    // vem antes. Se nenhuma existir, a mono assume o posto: uma UI
    // monoespacada ainda e' melhor que a embutida, que perde os acentos.
    ImFont* ui = load_first_available(io, ui_c, 3, size);
    ImFont* mono = load_first_available(io, mono_c, 3, size);

    if (ui == nullptr && mono == nullptr) return;  // sobra a embutida, so' ASCII
}

// Icone da janela, gerado em memoria.
//
// Midia/Logo.png existe, mas decodificar PNG exigiria trazer um stb_image
// so' para isto -- o projeto so' tem o stb_image_WRITE, que o ImGui usa. Um
// decodificador inteiro por um icone de 32 px nao se paga.
//
// O desenho e' a silhueta da lontra na paleta do produto: cabeca redonda,
// duas orelhas e o focinho. No tamanho da barra de tarefas o que se le' e' a
// silhueta, nao o detalhe -- o mesmo criterio dos icones da arvore.
void set_window_icon(GLFWwindow* window) {
    constexpr int kSize = 32;
    static unsigned char pixels[kSize * kSize * 4];

    // Ambar do tema, que e' a cor da marca (Midia/Logo.png).
    constexpr unsigned char kR = 0xE8, kG = 0x9C, kB = 0x3E;

    auto put = [&](int x, int y, unsigned char alpha) {
        if (x < 0 || y < 0 || x >= kSize || y >= kSize) return;
        unsigned char* px = &pixels[(y * kSize + x) * 4];
        px[0] = kR; px[1] = kG; px[2] = kB; px[3] = alpha;
    };

    // Comeca transparente: o fundo da barra de tarefas aparece em volta.
    for (int i = 0; i < kSize * kSize * 4; ++i) pixels[i] = 0;

    // Disco preenchido com borda suave -- a cabeca.
    const float cx = 15.5f, cy = 17.5f, r = 11.0f;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const float dx = static_cast<float>(x) - cx;
            const float dy = static_cast<float>(y) - cy;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d <= r - 1.0f) {
                put(x, y, 255);
            } else if (d <= r) {
                // Antialias de uma amostra: suficiente a 32 px, e evita a
                // serrilha que um disco duro mostra na barra de tarefas.
                put(x, y, static_cast<unsigned char>(255.0f * (r - d)));
            }
        }
    }

    // As duas orelhas, discos menores no alto.
    for (const float ex : {8.5f, 22.5f}) {
        for (int y = 0; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                const float dx = static_cast<float>(x) - ex;
                const float dy = static_cast<float>(y) - 8.0f;
                if (std::sqrt(dx * dx + dy * dy) <= 4.5f) put(x, y, 255);
            }
        }
    }

    // Focinho e olhos, furados no disco: alfa zero deixa ver o fundo, o que
    // da' contraste sem precisar de uma segunda cor.
    auto punch = [&](float px_, float py, float pr) {
        for (int y = 0; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                const float dx = static_cast<float>(x) - px_;
                const float dy = static_cast<float>(y) - py;
                if (std::sqrt(dx * dx + dy * dy) <= pr) {
                    pixels[(y * kSize + x) * 4 + 3] = 0;
                }
            }
        }
    };
    punch(11.5f, 15.0f, 1.6f);   // olho esquerdo
    punch(19.5f, 15.0f, 1.6f);   // olho direito
    punch(15.5f, 21.0f, 2.6f);   // focinho

    GLFWimage image{};
    image.width  = kSize;
    image.height = kSize;
    image.pixels = pixels;
    glfwSetWindowIcon(window, 1, &image);
}

} // namespace

// Fonte monoespacada para o editor SQL. Nula se o sistema nao tinha nenhuma:
// quem chama deve testar antes de PushFont().
ImFont* mono_font() {
    ImGuiIO& io = ImGui::GetIO();
    // [0] e' a da interface, [1] a mono -- na ordem em que load_ui_font as
    // registrou.
    return io.Fonts->Fonts.Size > 1 ? io.Fonts->Fonts[1] : nullptr;
}

namespace {

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
    set_window_icon(impl.window);
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
    // Layout das janelas ao lado das conexoes salvas, nao no diretorio de
    // trabalho. O padrao do ImGui grava "imgui.ini" onde o programa foi
    // iniciado -- um arquivo que aparece no repositorio, na area de trabalho
    // ou onde quer que o usuario esteja.
    //
    // O caminho fica estatico: o ImGui guarda o ponteiro, nao a string.
    static std::string ini_path = [] {
        const std::filesystem::path directory =
            std::filesystem::path(db::otter_store_location().directory);
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        return (directory / "layout.ini").string();
    }();
    io.IniFilename = ini_path.c_str();

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

    while (!should_close_) {
        glfwPollEvents();

        // O "X" da janela nao fecha sozinho: vira um PEDIDO, que o shell pode
        // recusar para confirmar alteracoes nao salvas.
        //
        // Sem isto, fechar pelo X descartava o trabalho pendente sem
        // perguntar -- e' o unico caminho de saida que nao passava por
        // MainShell::draw().
        if (glfwWindowShouldClose(impl.window) != 0) {
            glfwSetWindowShouldClose(impl.window, GLFW_FALSE);
            close_requested_ = true;
        }

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
