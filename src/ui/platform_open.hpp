// C-Otter -- ui/platform_open.hpp
//
// Abrir um endereco no navegador padrao do sistema ("Search in web").
// Isolado num arquivo proprio porque no Windows exige <windows.h>, cujas
// macros (min, max) quebram o resto da UI.
#pragma once

#include <string>
#include <string_view>

namespace otter::ui {

// Codifica para uso num parametro de URL: tudo que nao e' letra, digito ou
// um dos quatro sinais seguros vira %XX. Alem de produzir uma URL valida, e'
// o que impede que o texto selecionado carregue aspas ou `&` ate' o shell.
[[nodiscard]] std::string url_encode(std::string_view text);

// Falso se o sistema recusou. So' aceita http:// e https://.
bool open_url(const std::string& url);

// Abre um ARQUIVO com o aplicativo que o sistema associa a ele ("Open with"
// da grade). So' aceita um caminho que existe e e' arquivo comum: o que chega
// aqui e' um temporario que o proprio programa acabou de gravar.
bool open_path(const std::string& path);

// "Show resource in explorer": abre o gerenciador de arquivos com o arquivo
// selecionado.
bool reveal_in_file_manager(const std::string& path);

} // namespace otter::ui
