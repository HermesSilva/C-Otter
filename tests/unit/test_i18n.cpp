#include "test_main.hpp"

#include "base/i18n.hpp"

#include <string>

using namespace otter;

namespace {

// Catalogo de teste, registrado uma vez.
void ensure_test_catalog() {
    static bool registered = false;
    if (registered) return;
    registered = true;

    static const char* const kPairs[] = {
        "Connect",    "Conectar",
        "%zu rows",   "%zu linhas",
        "Save",       "Salvar",
    };
    i18n::register_catalog("xx-TEST", "Test Language", "Idioma de Teste",
                           kPairs, sizeof(kPairs) / sizeof(kPairs[0]));
}

} // namespace

OTTER_TEST(i18n_defaults_to_english) {
    i18n::set_language("en");
    OTTER_CHECK_EQ(i18n::current_language(), std::string_view{"en"});
    // Em ingles, translate devolve o proprio texto.
    OTTER_CHECK_EQ(std::string(TR("Connect")), std::string{"Connect"});
}

OTTER_TEST(i18n_translates_when_catalog_active) {
    ensure_test_catalog();
    OTTER_CHECK(i18n::set_language("xx-TEST"));
    OTTER_CHECK_EQ(std::string(TR("Connect")), std::string{"Conectar"});
    i18n::set_language("en");
}

OTTER_TEST(i18n_falls_back_to_english_when_missing) {
    ensure_test_catalog();
    i18n::set_language("xx-TEST");

    // Sem traducao, devolve o ingles -- nunca uma chave crua. E' a razao de a
    // chave ser a propria frase em ingles.
    OTTER_CHECK_EQ(std::string(TR("Not translated here")),
                   std::string{"Not translated here"});
    i18n::set_language("en");
}

OTTER_TEST(i18n_records_missing_translations) {
    ensure_test_catalog();
    i18n::set_language("xx-TEST");

    (void)TR("A string that has no translation");

    const auto missing = i18n::missing_translations();
    const bool found = std::find(missing.begin(), missing.end(),
                                 "A string that has no translation") !=
                       missing.end();
    OTTER_CHECK(found);
    i18n::set_language("en");
}

OTTER_TEST(i18n_format_uses_translated_pattern) {
    ensure_test_catalog();
    i18n::set_language("xx-TEST");

    const std::string result = TRF("%zu rows", std::size_t{7});
    OTTER_CHECK_EQ(result, std::string{"7 linhas"});
    i18n::set_language("en");
}

OTTER_TEST(i18n_format_rotates_buffers) {
    // Duas chamadas na mesma expressao nao podem sobrescrever uma a outra.
    i18n::set_language("en");
    const char* a = TRF("first %d", 1);
    const char* b = TRF("second %d", 2);

    OTTER_CHECK_EQ(std::string(a), std::string{"first 1"});
    OTTER_CHECK_EQ(std::string(b), std::string{"second 2"});
}

OTTER_TEST(i18n_window_title_keeps_stable_id) {
    // O sufixo ###id preserva a identidade da janela no ImGui; sem ele, a
    // janela perderia a posicao no dock a cada troca de idioma.
    ensure_test_catalog();
    i18n::set_language("xx-TEST");

    OTTER_CHECK_EQ(std::string(TRW("Save", "###SavePanel")),
                   std::string{"Salvar###SavePanel"});
    i18n::set_language("en");
}

OTTER_TEST(i18n_unknown_language_is_rejected) {
    OTTER_CHECK(!i18n::set_language("zz-NOPE"));
    // O idioma anterior permanece ativo.
    OTTER_CHECK_EQ(i18n::current_language(), std::string_view{"en"});
}

OTTER_TEST(i18n_falls_back_to_base_language) {
    ensure_test_catalog();
    // "xx-OTHER" nao existe, mas "xx" casa com xx-TEST pelo idioma base.
    OTTER_CHECK(i18n::set_language("xx-OTHER"));
    OTTER_CHECK_EQ(i18n::current_language(), std::string_view{"xx-TEST"});
    i18n::set_language("en");
}

OTTER_TEST(i18n_lists_available_languages) {
    ensure_test_catalog();
    const auto languages = i18n::available_languages();

    // Ingles sempre presente, mesmo sem catalogo.
    OTTER_CHECK(languages.size() >= 2);
    OTTER_CHECK_EQ(languages.front().code, std::string{"en"});
}

OTTER_TEST(i18n_builtin_pt_br_is_registered) {
    // Verifica que o catalogo pt-BR sobrevive ao /OPT:REF do linker: com
    // registro por objeto global, a unidade de traducao inteira era
    // descartada e o idioma sumia do binario.
    i18n::load_builtin_catalogs();

    OTTER_CHECK(i18n::set_language("pt-BR"));
    OTTER_CHECK_EQ(std::string(TR("Connection")), std::string{"Conexão"});
    i18n::set_language("en");
}
