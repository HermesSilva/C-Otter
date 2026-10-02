// C-Otter -- base/i18n.hpp
//
// Internacionalizacao nativa. Ingles e' o idioma padrao e a fonte da verdade:
// a chave de traducao E' o texto em ingles, entao o produto funciona mesmo sem
// nenhum catalogo carregado.
//
// Uso:
//     ImGui::Button(TR("Connect"));
//     ImGui::Text(TRF("%zu rows", count));
//
// Acrescentar um idioma e' escrever um arquivo .lang com pares
// original=traducao -- sem tocar em codigo.
#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace otter::i18n {

struct Language {
    std::string code;        // "en", "pt-BR"
    std::string name;        // "English", "Português (Brasil)"
    std::string native_name; // como o falante escreveria
};

// Idiomas disponiveis, incluindo o ingles embutido.
[[nodiscard]] const std::vector<Language>& available_languages();

// Codigo do idioma ativo. "en" quando nenhum catalogo esta' carregado.
[[nodiscard]] std::string_view current_language();

// Troca o idioma. Devolve false se o codigo nao existir.
bool set_language(std::string_view code);

// Detecta o idioma do ambiente. Chamado na inicializacao.
//
// No Windows, o idioma de EXIBICAO do usuario (GetUserDefaultUILanguage) --
// nao o formato regional. Os dois divergem com frequencia: um Windows em
// ingles com datas e numeros no padrao brasileiro e' comum, e ler o formato
// abria o C-Otter em portugues num sistema cujos menus estao em ingles.
//
// Fora do Windows, LC_ALL > LC_MESSAGES > LANG, a precedencia do POSIX.
[[nodiscard]] std::string detect_system_language();

// "pt_BR.UTF-8" -> "pt-BR"; "C" e "POSIX" -> "en"; vazio -> vazio.
// Separada de detect_system_language para ser testavel sem mexer no ambiente.
[[nodiscard]] std::string normalize_locale(std::string_view locale);

// Traduz. Devolve `text` inalterado quando nao ha' traducao -- e' por isso que
// a chave ser o proprio ingles importa: falta de traducao degrada para ingles
// legivel, nunca para uma chave crua como "dialog.connection.host".
[[nodiscard]] const char* translate(const char* text);

// Versao com formatacao. O buffer e' rotativo e thread-local; o resultado vale
// ate' a proxima chamada na mesma thread.
[[nodiscard]] const char* translate_format(const char* format, ...);

// Carrega catalogos de um diretorio (arquivos *.lang).
void load_catalogs(std::string_view directory);

// Registra um catalogo embutido no binario.
void register_catalog(std::string_view code, std::string_view name,
                      std::string_view native_name,
                      const char* const* pairs, std::size_t count);

// Registra os catalogos compilados junto do binario. Chamada explicita, e nao
// construtor de objeto global, porque o linker com /OPT:REF descarta unidades
// de traducao nao referenciadas -- e o catalogo sumiria do executavel.
void load_builtin_catalogs();

// As entradas do catalogo pt-BR, em pares (chave, traducao). Exposto para o
// teste que impede chave repetida.
[[nodiscard]] std::span<const char* const> pt_br_entries();

// Textos vistos mas nunca traduzidos -- alimenta o relatorio de cobertura.
[[nodiscard]] std::vector<std::string> missing_translations();

// Grava um .lang com todos os textos vistos, para servir de ponto de partida
// a um tradutor.
bool export_template(std::string_view path);

// Titulo de janela traduzido com identidade ESTAVEL.
//
// O ImGui usa o nome da janela como identidade; traduzir o titulo faria a
// janela perder a posicao no dock a cada troca de idioma. O sufixo "###id"
// fixa a identidade, e so' a parte antes dele e' exibida.
//
//     ImGui::Begin(TRW("Result", "###ResultPanel"))
[[nodiscard]] const char* translate_window(const char* text, const char* id);

} // namespace otter::i18n

// Macros curtas: aparecem em cada rotulo da UI, entao precisam ser discretas.
#define TR(text)         ::otter::i18n::translate(text)
#define TRF(format, ...) ::otter::i18n::translate_format(format, __VA_ARGS__)
#define TRW(text, id)    ::otter::i18n::translate_window(text, id)
