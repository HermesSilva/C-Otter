// C-Otter -- ui/file_dialog.hpp
//
// Dialogo de arquivo do sistema operacional.
//
// Por que nativo e nao em ImGui: o usuario ja' conhece o dele -- tem os
// favoritos, o historico, a busca e o atalho para OneDrive. Um seletor
// desenhado em modo imediato seria trabalho para entregar algo pior.
//
// O custo e' uma implementacao por plataforma. No Windows, IFileDialog do
// COM; no Linux, ficaria xdg-desktop-portal ou zenity.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace otter::ui {

struct FileFilter {
    std::string_view label;     // "SQL scripts"
    std::string_view pattern;   // "*.sql"
};

// Abre o dialogo de "abrir". Devolve vazio se o usuario cancelar.
//
// Bloqueia a thread ate' o usuario decidir -- e' o comportamento do dialogo
// nativo, e o certo: nada mais faz sentido enquanto ele esta' aberto.
[[nodiscard]] std::optional<std::string> open_file_dialog(
    std::string_view title, const std::vector<FileFilter>& filters,
    std::string_view initial_path = {});

[[nodiscard]] std::optional<std::string> save_file_dialog(
    std::string_view title, const std::vector<FileFilter>& filters,
    std::string_view suggested_name = {},
    std::string_view initial_path = {});

} // namespace otter::ui
