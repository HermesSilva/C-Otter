// C-Otter -- base/version.hpp
//
// A versao que o programa diz ter.
//
// X.Y.Z vem do VERSION do CMakeLists.txt; o quarto campo e' o numero de build,
// que o workflow de Release sobe a cada entrega (OTTER_BUILD_NUMBER). E' o
// mesmo texto que vai no nome do pacote (tools/package_version.sh): quem
// relata um defeito le na janela About o numero do arquivo que baixou.
#pragma once

#include <string_view>

namespace otter {

// 0 num build local; o numero do run do workflow num pacote entregue.
[[nodiscard]] int build_number() noexcept;

// "0.1.0.42" num pacote entregue; "0.1.0-dev" num build local.
[[nodiscard]] std::string_view version_string() noexcept;

} // namespace otter
