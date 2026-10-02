# ADR 0006 — Backend de janela e render

**Data:** 2026-09-21
**Status:** Aceito

## Contexto

A primeira janela foi construída com **Win32 + Direct3D 11**, que é exclusivo do Windows.
No Linux, `otter_ui` compilava sem nenhuma implementação de `AppWindow::create` — o link
falharia com símbolo indefinido, e o `message(WARNING)` do CMake deixava isso passar como
se fosse aceitável. Era um furo real, não um detalhe de portabilidade futura.

Duas perguntas surgiram:

1. Se o alvo é Windows, qual backend usar?
2. GTK não seria "mais bonito", já que o Scintilla o usa no Linux?

## Decisão 1 — GLFW + OpenGL 3.3 nas duas plataformas

Uma implementação única substitui Win32+D3D11, em vez de acrescentar um irmão para Linux.

| | D3D11 (antes) | GLFW + OpenGL 3.3 |
|---|---|---|
| Plataformas | Windows | **Windows, Linux, macOS** |
| Implementações a manter | 2 (uma por plataforma) | **1** |
| Licença | API do sistema | **zlib/libpng** — link estático OK (ADR 0002) |
| Sem GPU (VM, RDP, servidor) | WARP | Mesa/llvmpipe |
| Custo para uma UI 2D | — | Nenhuma perda mensurável |

**Economia: ~120 h/h** frente a manter dois backends de render.

A perda de integração nativa com D3D é irrelevante aqui: a UI é 2D, com triângulos
texturizados e um atlas de fonte. OpenGL 3.3 é suportado por qualquer GPU desde 2010 e
funciona por software quando não há GPU.

O usuário do projeto trabalha apenas em Windows. Mesmo assim, o backend portável é a escolha
mais barata **hoje** (uma implementação em vez de duas) e torna o Linux quase gratuito no dia
em que for desejado. Isso não é especular sobre requisito futuro: é escolher a opção que já
custa menos agora.

## Decisão 2 — GTK é rejeitado, e a pergunta por trás dele é legítima

**GTK não é um tema; é um toolkit de widgets concorrente do ImGui.** Adotá-lo no Windows
significaria descartar `main_shell.cpp`, o tema da lontra e o docking, e reescrever a UI
inteira em outro paradigma. Além disso, GTK no Windows tem aparência estrangeira, DPI
problemático e arrasta ~40 MB de DLLs — destruindo o binário de 1,36 MB medido.

A pergunta real por trás disso é pertinente: **o Scintilla é um controle nativo e não vive
dentro do ImGui.** Ele ocupa um retângulo próprio da janela, com scrollbars e menu de
contexto do sistema, sem seguir automaticamente o tema da lontra.

### O que se pode e o que não se pode harmonizar

| Aspecto | Controlável pelo Scintilla |
|---------|---------------------------|
| Cores de fundo, texto, seleção, calha | **Sim** — `SCI_STYLESETFORE/BACK`, por estilo |
| Fonte e tamanho | **Sim** — a mesma do ImGui |
| Cursor, marcadores, indicadores | **Sim** |
| Lista de autocomplete | **Sim** — cores configuráveis |
| Scrollbars | Nativas do SO (ocultáveis com `SCI_SETVSCROLLBAR`) |
| Menu de contexto | Nativo (substituível com `SCI_USEPOPUP(false)`) |
| Cantos arredondados, bordas do tema | **Não** — é um retângulo |

Na prática, com as cores e a fonte alinhadas, a costura fica discreta. O que não se
harmoniza é a geometria: o painel do editor terá cantos retos onde o resto tem 4 px de raio.

### Backend ImGui para o Scintilla: medido e rejeitado

`Platform.h` expõe **83 métodos virtuais** (`Surface`, `Font`, `Window`, `ListBox`,
`Menu`). Implementar um backend ImGui significa escrever todos, incluindo layout de texto,
medição de glifos e gradientes — e mantê-los a cada versão do Scintilla. É a Opção C do
ADR 0003, rejeitada por custo (~450 h/h + manutenção perpétua).

## Decisão 3 — Linux fica fora do escopo de entrega

O usuário não usa Linux. Consequências:

- **Não** construímos nem testamos o backend Linux agora
- A escolha de GLFW+OpenGL mantém a porta aberta **sem custo adicional**
- Scintilla no Linux exigiria GTK (LGPL, link dinâmico) — problema adiado, não resolvido
- Os presets `linux-*` permanecem no `CMakePresets.json`, sem garantia de funcionamento

Se o Linux entrar no escopo depois, o trabalho restante é o backend GTK do Scintilla e a
verificação da cadeia de build — não a reescrita da UI.

### Atualização — 2026-10-02: os presets `linux-*` compilam

A pedido do usuário a cadeia de build foi verificada (Ubuntu 24.04 no WSL, Clang 19,
OpenSSL 3.0). `linux-debug` e `linux-release` compilam do zero com `-Werror`, os 733
testes unitários passam, e o programa abre num display virtual (Xvfb). O Scintilla saiu no
ADR 0007, então o problema do GTK deixou de existir.

O que custou, e fica registrado para não se repetir:

| Achado | Correção |
|---|---|
| `crypto_openssl.cpp` listado no CMake e inexistente | Escrito sobre a API EVP |
| Clang ≤ 18 não habilita o `<expected>` da libstdc++ | Clang 19+; o configure recusa o compilador, com o motivo |
| CMake 3.28+ exige `clang-scan-deps` para varrer módulos | `CMAKE_CXX_SCAN_FOR_MODULES OFF` |
| Busca restrita a `.a` achava a `libm.a` e não achava a `libGL` | Exceção para GLFW e OpenGL: glibc e libGL entram como `.so` |
| O pacote não abria fora da máquina de build: `libOpenGL.so.0: cannot open shared object file`. O `FindOpenGL` prefere GLVND e liga contra a `libOpenGL` (pacote `libopengl0`), que o Ubuntu desktop não traz; o CI a tinha por causa do `libgl1-mesa-dev`, e por isso o passo "Abrir o programa" passava | `OpenGL_GL_PREFERENCE LEGACY`: liga contra a `libGL.so.1` (`libgl1`). O `tools/package.sh` recusa o pacote se o binário exigir `.so` fora da lista |
| GLFW compilado com `gcc` e o resto com Clang quebrava o LTO | Presets fixam `CMAKE_C_COMPILER=clang` |
| Avisos que a MSVC não dá (`-Wformat-security`, `-Wsign-conversion`, `-Wswitch`) | Corrigidos no código; cabeçalhos de terceiros viraram `SYSTEM` |

Continua **sem garantia de entrega**: não há CI Linux, as suítes ao vivo não rodaram lá, e
faltam TLS (`tls_openssl.cpp` é esboço) e a autenticação integrada do SQL Server.

Pacotes para compilar: `clang-19 llvm-19 ninja-build pkg-config libssl-dev libgl1-mesa-dev xorg-dev
libwayland-dev libwayland-bin libxkbcommon-dev wayland-protocols`.

## Consequências

- `app_window_win32.cpp` → `app_window_glfw.cpp`; D3D11 sai
- GLFW entra em `third_party/`, compilado estaticamente
- `imgui_impl_win32` + `imgui_impl_dx11` → `imgui_impl_glfw` + `imgui_impl_opengl3`
- A fronteira `AppWindow` continua existindo, agora com implementação única
- Meta de cold start (< 200 ms) precisa ser re-medida: criar contexto GL tem custo próprio
- O tema do Scintilla deve espelhar `ui/theme.hpp` para que a costura fique discreta

## Referências

- ADR 0002 — link estático e licenças
- ADR 0003 — Scintilla, e a rejeição do backend ImGui (Opção C)
- [GLFW](https://www.glfw.org/) — licença zlib/libpng
