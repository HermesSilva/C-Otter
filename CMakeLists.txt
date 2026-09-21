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
# suportam: MSVC 19.4x (VS 2022 17.10+) e Clang 17+.
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
message(STATUS "  C-Otter ${PROJECT_VERSION} -- every JOIN is an OTTER JOIN")
message(STATUS "  compiler : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  build    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  tests    : ${OTTER_BUILD_TESTS} | spikes: ${OTTER_BUILD_SPIKES} | ui: ${OTTER_BUILD_UI}")
message(STATUS "")
