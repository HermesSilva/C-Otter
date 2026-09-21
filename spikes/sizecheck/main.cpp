// Spike: custo real do Scintilla no binario final.
//
// Um .lib estatico carrega IR de LTO e todos os simbolos; o numero que importa
// e' o executavel depois do descarte do linker. Este spike instancia Scintilla
// de verdade (uma janela real) para que nada seja eliminado como codigo morto.
#include <windows.h>

#include "ILexer.h"
#include "Scintilla.h"
#include "ScintillaTypes.h"

#include <cstdio>

// Registro do controle, exportado pelo ScintillaWin em build estatico.
extern "C" int Scintilla_RegisterClasses(void* hInstance);
extern "C" int Scintilla_ReleaseResources();

int main() {
    HINSTANCE inst = GetModuleHandleW(nullptr);

    if (!Scintilla_RegisterClasses(inst)) {
        std::fprintf(stderr, "Scintilla_RegisterClasses falhou\n");
        return 1;
    }

    // Janela mensagem-apenas: sem UI, mas o controle e' criado de fato.
    HWND host = CreateWindowExW(0, L"STATIC", L"host", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, inst, nullptr);
    if (host == nullptr) {
        std::fprintf(stderr, "janela host falhou\n");
        return 1;
    }

    HWND sci = CreateWindowExW(0, L"Scintilla", L"", WS_CHILD,
                               0, 0, 100, 100, host, nullptr, inst, nullptr);
    if (sci == nullptr) {
        std::fprintf(stderr, "CreateWindow(Scintilla) falhou: %lu\n", GetLastError());
        return 1;
    }

    auto send = reinterpret_cast<SciFnDirect>(
        SendMessageW(sci, SCI_GETDIRECTFUNCTION, 0, 0));
    auto ptr = static_cast<sptr_t>(SendMessageW(sci, SCI_GETDIRECTPOINTER, 0, 0));

    const char* sql = "SELECT o.name FROM otter o OTTER JOIN raft r ON r.id = o.raft_id;";
    send(ptr, SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(sql));

    const sptr_t length = send(ptr, SCI_GETLENGTH, 0, 0);
    const sptr_t lines  = send(ptr, SCI_GETLINECOUNT, 0, 0);

    std::printf("Scintilla ativo: %lld bytes, %lld linha(s)\n",
                static_cast<long long>(length), static_cast<long long>(lines));

    DestroyWindow(sci);
    DestroyWindow(host);
    Scintilla_ReleaseResources();
    return 0;
}
