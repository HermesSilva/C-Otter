cmake_minimum_required(VERSION 3.28)

project(C-Otter
    VERSION 0.1.0
    DESCRIPTION "Universal database manager - the otter that shares DBeaver's river"
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Padrao da linguagem: C++23 estrito, sem extensoes de compilador.
#
# C++23 (e nao 20) porque std::expected -- o mecanismo de erro do nucleo, ver
# ADR 0001 #4 -- so' existe a partir do C++23. A alternativa seria escrever um
# expected proprio; subir o padrao custa menos e ambos os toolchains-alvo ja'
# suportam: MSVC 19.4x (VS 2022 17.10+) e Clang 19+.
#
# Clang 19, e nao 17: ate' o 18 ele anuncia __cpp_concepts = 201907, e o
# <expected> da libstdc++ (a biblioteca padrao do Linux) se desliga abaixo de
# 202002. O sintoma sao milhares de "no template named 'expected'", por isso a
# checagem abaixo recusa o compilador logo no configure, dizendo o motivo.
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# O projeto nao usa modulos C++20. Sem isto o CMake 3.28+ varre cada fonte
# atras de `import` -- com Clang, exige o clang-scan-deps (pacote 'a parte) e o
# build morre em "CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS-NOTFOUND". Vem ANTES da
# checagem abaixo, e vai junto para o projeto de teste dela: la' a varredura
# falharia e a checagem acusaria o compilador errado.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CMAKE_CXX_SCAN_FOR_MODULES)

include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
    #include <expected>
    std::expected<int, int> f() { return 1; }
    int main() { return *f() - 1; }
" OTTER_HAS_STD_EXPECTED)
if(NOT OTTER_HAS_STD_EXPECTED)
    message(FATAL_ERROR
        "std::expected indisponivel com ${CMAKE_CXX_COMPILER_ID} "
        "${CMAKE_CXX_COMPILER_VERSION}. No Linux use Clang 19+ ou GCC 13+ "
        "(o Clang ate' o 18 nao habilita o <expected> da libstdc++).")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# Saida unificada: facilita rodar binarios sem instalar.
foreach(_out RUNTIME LIBRARY ARCHIVE)
    set(CMAKE_${_out}_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endforeach()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# LINKAGEM ESTATICA TOTAL
#
# Requisito do projeto: o binario roda sozinho. Sem VCRUNTIME140.dll, sem
# redistribuivel da MSVC, sem .so de driver ao lado do executavel.
# Isto sustenta a meta de instalador < 25 MB contra os ~200 MB + JRE do DBeaver.
#
# Consequencia juridica a respeitar: qualquer dependencia LGPL (ex.: Qt, certos
# clientes de SGBD) e' incompativel com link estatico em produto fechado. Os
# drivers escolhidos evitam isso -- libpq (PostgreSQL License), SQLite (domino
# publico), ODBC (API do sistema). libmysqlclient e' GPL: usar MariaDB Connector/C
# (LGPL -> exige link dinamico) ou o protocolo wire proprio. Ver docs/adr/0002.
# ---------------------------------------------------------------------------
set(BUILD_SHARED_LIBS OFF CACHE BOOL "C-Otter links everything statically" FORCE)
set(CMAKE_LINK_SEARCH_START_STATIC ON)
set(CMAKE_FIND_LIBRARY_SUFFIXES ".lib;.a" )

if(MSVC)
    # Runtime estatica: /MT (release) e /MTd (debug).
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    add_link_options(-static-libgcc -static-libstdc++)
    # -static completo so' quando nao houver dependencia de glibc dinamica.
endif()

# ---------------------------------------------------------------------------
# Opcoes
# ---------------------------------------------------------------------------
option(OTTER_BUILD_TESTS  "Build unit tests"          ON)
option(OTTER_BUILD_SPIKES "Build Phase-0 spikes"      ON)
option(OTTER_BUILD_UI     "Build the ImGui UI layer"  OFF)
option(OTTER_SANITIZERS   "Enable ASan/UBSan"         OFF)
option(OTTER_WERROR       "Treat warnings as errors"  ON)

# Numero de build: o quarto campo da versao (X.Y.Z.N), que sobe sozinho a cada
# entrega. Quem o da' e' o workflow de Release (github.run_number, pela
# variavel de ambiente), e nao um arquivo no repositorio: contador versionado
# exigiria um commit por entrega. Build local fica em 0 e se apresenta "-dev".
set(OTTER_BUILD_NUMBER "$ENV{OTTER_BUILD_NUMBER}" CACHE STRING
    "Numero de build (run do workflow de Release); 0 = build local")
if(NOT OTTER_BUILD_NUMBER MATCHES "^[0-9]+$")
    set(OTTER_BUILD_NUMBER 0)
endif()

# ---------------------------------------------------------------------------
# Flags comuns: warnings agressivos desde o primeiro dia.
# Ligar -Werror depois do codigo pronto e' caro; ligar agora e' de graca.
# ---------------------------------------------------------------------------
add_library(otter_flags INTERFACE)

if(MSVC)
    target_compile_options(otter_flags INTERFACE
        /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor
        /EHsc /MP
        /wd4251                      # DLL interface em membros nao exportados
        $<$<BOOL:${OTTER_WERROR}>:/WX>
    )
    target_compile_definitions(otter_flags INTERFACE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE _UNICODE
        _CRT_SECURE_NO_WARNINGS
    )
else()
    target_compile_options(otter_flags INTERFACE
        -Wall -Wextra -Wpedantic
        -Wshadow -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual
        -Wnull-dereference -Wdouble-promotion
        # Vem com o -Wextra e acusa `Meta{a, b}` quando a struct tem mais
        # campos. Aqui eles tem inicializador de membro, e omiti-los e' o
        # proposito: o campo novo nao obriga a reescrever cada chamada.
        -Wno-missing-field-initializers
        $<$<BOOL:${OTTER_WERROR}>:-Werror>
    )
endif()

if(OTTER_SANITIZERS)
    if(MSVC)
        target_compile_options(otter_flags INTERFACE /fsanitize=address)
    else()
        target_compile_options(otter_flags INTERFACE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(otter_flags INTERFACE -fsanitize=address,undefined)
    endif()
endif()

# ---------------------------------------------------------------------------
# helper: otter_add_library
#
# Aplica a regra arquitetural herdada do DBeaver -- "o nucleo nao conhece a UI"
# -- de forma verificavel pelo build, nao por convencao verbal.
# ---------------------------------------------------------------------------
function(otter_add_library name)
    cmake_parse_arguments(A "UI_LAYER" "" "SOURCES;DEPS" ${ARGN})

    add_library(${name} STATIC ${A_SOURCES})
    add_library(otter::${name} ALIAS ${name})

    target_include_directories(${name} PUBLIC ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(${name} PUBLIC otter_flags ${A_DEPS})
    set_target_properties(${name} PROPERTIES FOLDER "libs")

    # Marca quem pertence a camada de UI; checado em otter_verify_layering().
    set_property(TARGET ${name} PROPERTY OTTER_IS_UI ${A_UI_LAYER})
endfunction()

add_subdirectory(third_party)
add_subdirectory(lib)      # protocolos de rede (ADR 0009) -- nao depende de src/
add_subdirectory(src)

if(OTTER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(OTTER_BUILD_SPIKES)
    add_subdirectory(spikes)
endif()

# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "  C-Otter ${PROJECT_VERSION} (build ${OTTER_BUILD_NUMBER}) -- every JOIN is an OTTER JOIN")
message(STATUS "  compiler : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  build    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  tests    : ${OTTER_BUILD_TESTS} | spikes: ${OTTER_BUILD_SPIKES} | ui: ${OTTER_BUILD_UI}")
message(STATUS "")
