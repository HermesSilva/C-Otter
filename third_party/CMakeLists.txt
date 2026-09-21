# Dependencias de terceiros, compiladas do fonte e linkadas estaticamente (ADR 0002).
#
# Scintilla 5.6.6 e Lexilla 5.5.3 -- licenca HPND (permissiva). A unica obrigacao
# e' preservar o aviso de copyright na distribuicao; ver LICENSES.md.
#
# Nao compilamos os ~100 lexers do Lexilla: apenas `lexlib`, a infraestrutura
# necessaria para registrar um ILexer5 proprio. O realce vem do lexer do
# otter_sql, para que editor e executor enxerguem a mesma arvore (ADR 0003).

# --- Scintilla ---------------------------------------------------------------
file(GLOB SCINTILLA_CORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/scintilla/src/*.cxx)

add_library(scintilla STATIC ${SCINTILLA_CORE_SOURCES})

target_include_directories(scintilla PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/scintilla/include
    ${CMAKE_CURRENT_SOURCE_DIR}/scintilla/src
)

if(WIN32)
    target_sources(scintilla PRIVATE
        scintilla/win32/PlatWin.cxx
        scintilla/win32/SurfaceGDI.cxx
        scintilla/win32/SurfaceD2D.cxx
        scintilla/win32/ScintillaWin.cxx
        scintilla/win32/ListBox.cxx
        scintilla/win32/HanjaDic.cxx
    )
    target_include_directories(scintilla PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/scintilla/win32)
    target_compile_definitions(scintilla PRIVATE
        STATIC_BUILD          # sem DllMain: Scintilla vive dentro do nosso binario
        NO_CXX11_REGEX        # usa o mecanismo interno, menor e sem <regex>
    )
    target_link_libraries(scintilla PUBLIC imm32 ole32 oleaut32 uuid msimg32 d2d1 dwrite)
endif()

# Codigo de terceiros nao passa pelo nosso -Werror: nao controlamos o estilo dele
# e travar o build por um warning do Scintilla nao nos ensina nada.
if(MSVC)
    target_compile_options(scintilla PRIVATE /W0)
else()
    target_compile_options(scintilla PRIVATE -w)
endif()

set_target_properties(scintilla PROPERTIES FOLDER "third_party")

# --- Lexilla (apenas lexlib) -------------------------------------------------
file(GLOB LEXILLA_LEXLIB_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/lexilla/lexlib/*.cxx)

add_library(lexilla STATIC ${LEXILLA_LEXLIB_SOURCES})

target_include_directories(lexilla PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/lexilla/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lexilla/lexlib
    ${CMAKE_CURRENT_SOURCE_DIR}/scintilla/include
)

if(MSVC)
    target_compile_options(lexilla PRIVATE /W0)
else()
    target_compile_options(lexilla PRIVATE -w)
endif()

set_target_properties(lexilla PROPERTIES FOLDER "third_party")
