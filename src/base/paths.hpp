// C-Otter -- base/paths.hpp
//
// Onde o programa guarda o que e' dele.
//
// O C-Otter e' PORTATIL (ADR 0020, diretiva 14): tudo o que ele grava --
// preferencias, disposicao das janelas, conexoes e senhas -- fica numa pasta
// `.C-Otter` ao lado do executavel. Copiar a pasta do programa leva tudo
// junto; nada e' escrito no perfil do usuario nem no registro.
//
// Um lugar so' para a regra: quem precisa de um caminho de dados pede aqui.
#pragma once

#include <string>

namespace otter {

// A pasta onde esta' o executavel em execucao. Vazio se o sistema nao disser.
[[nodiscard]] std::string executable_directory();

// `<pasta do executavel>/.C-Otter`. Nao cria a pasta.
[[nodiscard]] std::string data_directory();

} // namespace otter
