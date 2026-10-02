# Dependencias de terceiros, compiladas do fonte e linkadas estaticamente (ADR 0002).
#
# Todo o stack e' permissivo (MIT / zlib) e compativel com link estatico em
# produto fechado -- nenhuma dependencia LGPL. Ver docs/LICENSES.md.
#
# Codigo de terceiros nao passa pelo nosso -Werror: nao controlamos o estilo
# deles, e travar o build por um warning alheio nao ensina nada.

# --- GLFW 3.4 ----------------------------------------------------------------
# Licenca zlib/libpng -- compativel com link estatico (ADR 0002).
# Backend unico para Windows e Linux; ver ADR 0006.
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

# O GLFW procura a libm e a librt com find_library. Com a busca restrita a ".a"
# (raiz) ele acha a libm.a, que nao liga contra a glibc dinamica: o link morre
# em "undefined reference to _dl_x86_cpu_features". A glibc entra como .so.
set(_otter_library_suffixes ${CMAKE_FIND_LIBRARY_SUFFIXES})
if(NOT WIN32)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".so")
endif()
add_subdirectory(glfw EXCLUDE_FROM_ALL)
set(CMAKE_FIND_LIBRARY_SUFFIXES ${_otter_library_suffixes})
set_target_properties(glfw PROPERTIES FOLDER "third_party")

# --- Dear ImGui (branch docking) ---------------------------------------------
# Licenca MIT. Docking e' necessario para o layout de paineis do C-Otter
# (navigator | editor | grid), equivalente as views do Eclipse no DBeaver.
add_library(imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_demo.cpp          # referencia viva de widgets; removivel na v1
    imgui/backends/imgui_impl_glfw.cpp
    imgui/backends/imgui_impl_opengl3.cpp
)

# SYSTEM: os cabecalhos de terceiros sao incluidos pelo NOSSO codigo, e sem
# isto os avisos deles (milhares de -Wold-style-cast) caem no nosso -Werror.
target_include_directories(imgui SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
)

# O loader de OpenGL vem embutido no proprio backend do ImGui: sem GLAD/GLEW.
target_link_libraries(imgui PUBLIC glfw)

if(WIN32)
    target_link_libraries(imgui PUBLIC opengl32)
else()
    # A raiz restringe a busca a ".a" (link estatico total), mas a libGL vem do
    # driver de video da maquina e so' existe como .so: com a glibc, e' a
    # dependencia dinamica legitima, como o opengl32 do Windows.
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".so")
    find_package(OpenGL REQUIRED)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${_otter_library_suffixes})
    target_link_libraries(imgui PUBLIC OpenGL::GL)
endif()

if(MSVC)
    target_compile_options(imgui PRIVATE /W0)
else()
    target_compile_options(imgui PRIVATE -w)
endif()

set_target_properties(imgui PROPERTIES FOLDER "third_party")

# --- ImGuiColorTextEdit -------------------------------------------------------
# Licenca MIT. Widget de edicao que vive DENTRO do ImGui: escrita unica para
# Windows e Linux, sem controle nativo por plataforma (ADR 0007).
add_library(texteditor STATIC
    texteditor/TextEditor.cpp
    texteditor/extras/TrieAutoComplete.cpp
)

target_include_directories(texteditor SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/texteditor
    ${CMAKE_CURRENT_SOURCE_DIR}/texteditor/extras
)

target_link_libraries(texteditor PUBLIC imgui)

if(MSVC)
    target_compile_options(texteditor PRIVATE /W0)
else()
    target_compile_options(texteditor PRIVATE -w)
endif()

set_target_properties(texteditor PROPERTIES FOLDER "third_party")

# --- nanosvg ------------------------------------------------------------------
# Licenca zlib -- compativel com link estatico (ADR 0002). So' cabecalhos;
# nanosvg_impl.cpp e' o ponto de montagem. Rasteriza os icones originais do
# DBeaver (SVG) no tamanho exato do DPI. Fixada no commit
# 239e102ec2c691f2902e20ace2ed36ee4a35cfe6 de github.com/memononen/nanosvg.
add_library(nanosvg STATIC nanosvg/nanosvg_impl.cpp)

target_include_directories(nanosvg SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/nanosvg)

if(MSVC)
    target_compile_options(nanosvg PRIVATE /W0)
else()
    target_compile_options(nanosvg PRIVATE -w)
    target_link_libraries(nanosvg PUBLIC m)
endif()

set_target_properties(nanosvg PROPERTIES FOLDER "third_party")
