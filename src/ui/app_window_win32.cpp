// C-Otter -- backend Win32 + Direct3D 11.
#include "ui/app_window.hpp"
#include "ui/theme.hpp"

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <d3d11.h>
#include <windows.h>

#include <string>

// Declarado pelo backend do ImGui; nao vem no header.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace otter::ui {
namespace {

constexpr const wchar_t* kWindowClass = L"COtterMainWindow";

// Carrega uma fonte monoespacada do sistema com cobertura latina.
// SQL e' codigo: fonte proporcional prejudica alinhamento de colunas.
void load_ui_font(ImGuiIO& io) {
    wchar_t win_dir[MAX_PATH] = {};
    if (GetWindowsDirectoryW(win_dir, MAX_PATH) == 0) return;

    char fonts_dir[MAX_PATH * 2] = {};
    WideCharToMultiByte(CP_UTF8, 0, win_dir, -1, fonts_dir, sizeof(fonts_dir),
                        nullptr, nullptr);

    // Ordem de preferencia: Cascadia (moderna), Consolas (universal no Windows).
    const char* candidates[] = {"\\Fonts\\CascadiaMono.ttf",
                                "\\Fonts\\CascadiaCode.ttf",
                                "\\Fonts\\consola.ttf"};

    // Latin + Latin-1 Supplement + Latin Extended-A cobrem portugues; o bloco
    // de simbolos matematicos traz o sigma da linha de totais.
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

    for (const char* candidate : candidates) {
        std::string path = std::string(fonts_dir) + candidate;
        if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;

        ImFontConfig config;
        config.OversampleH = 2;
        config.OversampleV = 1;
        config.PixelSnapH  = true;

        if (io.Fonts->AddFontFromFileTTF(path.c_str(), 16.0f, &config, ranges)) {
            return;
        }
    }
    // Nenhuma encontrada: segue com a fonte embutida (so' ASCII).
}

std::wstring to_wide(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int needed = MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        wide.data(), needed);
    return wide;
}

} // namespace

struct AppWindow::Impl {
    HWND                    hwnd          = nullptr;
    WNDCLASSEXW             wc            = {};
    ID3D11Device*           device        = nullptr;
    ID3D11DeviceContext*    context       = nullptr;
    IDXGISwapChain*         swap_chain    = nullptr;
    ID3D11RenderTargetView* render_target = nullptr;
    bool                    occluded      = false;
    bool                    resize_pending = false;
    UINT                    resize_width  = 0;
    UINT                    resize_height = 0;

    bool create_device();
    void destroy_device();
    void create_render_target();
    void destroy_render_target();

    static LRESULT WINAPI wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};

bool AppWindow::Impl::create_device() {
    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferCount                        = 2;
    desc.BufferDesc.Width                   = 0;   // herda da janela
    desc.BufferDesc.Height                  = 0;
    desc.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.RefreshRate.Numerator   = 60;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    desc.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow                       = hwnd;
    desc.SampleDesc.Count                   = 1;
    desc.Windowed                           = TRUE;
    desc.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = 0;
#ifndef NDEBUG
    // Camada de debug do D3D: ausente em maquinas sem SDK, tratada abaixo.
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL obtained = {};

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
        static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
        &desc, &swap_chain, &device, &obtained, &context);

#ifndef NDEBUG
    if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING) {
        // Sem as ferramentas de debug do D3D instaladas: segue sem elas.
        flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
            static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
            &desc, &swap_chain, &device, &obtained, &context);
    }
#endif

    if (hr == DXGI_ERROR_UNSUPPORTED) {
        // Sem GPU compativel (VM, RDP): cai para rasterizacao por software.
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels,
            static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
            &desc, &swap_chain, &device, &obtained, &context);
    }

    if (FAILED(hr)) return false;

    create_render_target();
    return true;
}

void AppWindow::Impl::create_render_target() {
    ID3D11Texture2D* back_buffer = nullptr;
    if (SUCCEEDED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer))) && back_buffer) {
        device->CreateRenderTargetView(back_buffer, nullptr, &render_target);
        back_buffer->Release();
    }
}

void AppWindow::Impl::destroy_render_target() {
    if (render_target) { render_target->Release(); render_target = nullptr; }
}

void AppWindow::Impl::destroy_device() {
    destroy_render_target();
    if (swap_chain) { swap_chain->Release(); swap_chain = nullptr; }
    if (context)    { context->Release();    context    = nullptr; }
    if (device)     { device->Release();     device     = nullptr; }
}

LRESULT WINAPI AppWindow::Impl::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 1;

    auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_SIZE:
            if (self && wp != SIZE_MINIMIZED) {
                self->resize_pending = true;
                self->resize_width   = LOWORD(lp);
                self->resize_height  = HIWORD(lp);
            }
            return 0;

        case WM_SYSCOMMAND:
            // Bloqueia o menu de sistema via ALT: o ImGui usa ALT para navegacao.
            if ((wp & 0xFFF0) == SC_KEYMENU) return 0;
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// -----------------------------------------------------------------------------

Result<std::unique_ptr<AppWindow>> AppWindow::create(const WindowConfig& config) {
    auto window = std::unique_ptr<AppWindow>(new AppWindow());
    window->impl_ = std::make_unique<Impl>();
    Impl& impl = *window->impl_;

    // DPI-aware antes de criar a janela: senao o Windows escala por bitmap e
    // o texto fica borrado em monitor 4K.
    ImGui_ImplWin32_EnableDpiAwareness();

    impl.wc = {sizeof(impl.wc),
               CS_CLASSDC,
               Impl::wnd_proc,
               0L, 0L,
               GetModuleHandleW(nullptr),
               nullptr, nullptr, nullptr, nullptr,
               kWindowClass,
               nullptr};
    ::RegisterClassExW(&impl.wc);

    const std::wstring title = to_wide(config.title);

    // Limita ao retangulo de trabalho do monitor (exclui a barra de tarefas),
    // senao o rodape da janela fica fora da area visivel.
    int width  = config.width;
    int height = config.height;
    int pos_x  = CW_USEDEFAULT;
    int pos_y  = CW_USEDEFAULT;

    RECT work = {};
    if (::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        const int work_w = work.right - work.left;
        const int work_h = work.bottom - work.top;

        if (width  > work_w) width  = work_w;
        if (height > work_h) height = work_h;

        pos_x = work.left + (work_w - width)  / 2;
        pos_y = work.top  + (work_h - height) / 2;
    }

    impl.hwnd = ::CreateWindowExW(
        0, kWindowClass, title.c_str(), WS_OVERLAPPEDWINDOW,
        pos_x, pos_y, width, height,
        nullptr, nullptr, impl.wc.hInstance, nullptr);

    if (impl.hwnd == nullptr) {
        ::UnregisterClassW(impl.wc.lpszClassName, impl.wc.hInstance);
        return fail(Errc::internal, "CreateWindowExW falhou");
    }

    SetWindowLongPtrW(impl.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&impl));

    if (!impl.create_device()) {
        impl.destroy_device();
        ::DestroyWindow(impl.hwnd);
        ::UnregisterClassW(impl.wc.lpszClassName, impl.wc.hInstance);
        return fail(Errc::internal, "criacao do dispositivo D3D11 falhou");
    }

    ::ShowWindow(impl.hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(impl.hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;   // layout persistido por nos, nao pelo imgui.ini

    apply_otter_theme(ImGui::GetStyle());

    // A fonte embutida do ImGui cobre so' ASCII: acentos do portugues e simbolos
    // como sigma viram '?'. Carregamos uma fonte do sistema com o intervalo
    // latino completo.
    load_ui_font(io);

    ImGui_ImplWin32_Init(impl.hwnd);
    ImGui_ImplDX11_Init(impl.device, impl.context);

    return window;
}

AppWindow::~AppWindow() {
    if (!impl_) return;

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    impl_->destroy_device();
    if (impl_->hwnd) ::DestroyWindow(impl_->hwnd);
    ::UnregisterClassW(impl_->wc.lpszClassName, impl_->wc.hInstance);
}

void* AppWindow::native_handle() const noexcept {
    return impl_ ? impl_->hwnd : nullptr;
}

void AppWindow::run(const FrameFn& draw_frame) {
    Impl& impl = *impl_;

    while (!should_close_) {
        MSG msg;
        bool quit = false;
        while (::PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) quit = true;
        }
        if (quit) break;

        // Janela oculta: dorme em vez de queimar GPU.
        if (impl.occluded &&
            impl.swap_chain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            ::Sleep(10);
            continue;
        }
        impl.occluded = false;

        if (impl.resize_pending && impl.resize_width != 0 && impl.resize_height != 0) {
            impl.destroy_render_target();
            impl.swap_chain->ResizeBuffers(0, impl.resize_width, impl.resize_height,
                                           DXGI_FORMAT_UNKNOWN, 0);
            impl.resize_pending = false;
            impl.create_render_target();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        draw_frame();

        ImGui::Render();

        constexpr float clear[4] = {0.102f, 0.086f, 0.075f, 1.0f};   // bg_darkest
        impl.context->OMSetRenderTargets(1, &impl.render_target, nullptr);
        impl.context->ClearRenderTargetView(impl.render_target, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        const HRESULT hr = impl.swap_chain->Present(1, 0);   // vsync
        impl.occluded = (hr == DXGI_STATUS_OCCLUDED);
    }
}

} // namespace otter::ui
