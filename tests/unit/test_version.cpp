#include "test_main.hpp"

#include "base/version.hpp"

#include <string>

// O nome do pacote e a janela About tem de dizer o mesmo numero: o quarto
// campo da versao e' o numero de build, e so' um build local fica sem ele.
OTTER_TEST(version_string_carries_the_build_number) {
    const std::string version(otter::version_string());
    OTTER_CHECK(!version.empty());
    OTTER_CHECK(version.front() >= '0' && version.front() <= '9');

    const int build = otter::build_number();
    OTTER_CHECK(build >= 0);
    const std::string tail = build > 0 ? "." + std::to_string(build) : std::string("-dev");
    OTTER_CHECK(version.ends_with(tail));
}
