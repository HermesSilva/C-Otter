// C-Otter -- db/value_view.hpp
//
// Apresentação de um valor por TIPO, para o painel de valor da grade.
//
// A célula da grade mostra tudo como uma linha de texto -- é o certo para
// caber na tabela. Mas um JSON de 4 KB, um BLOB de 200 KB ou um booleano
// viram, respectivamente: uma linha ilegível, bytes binários derramados na
// tela, e um "1" que não diz se é verdadeiro.
//
// Mapeado dos 15 editores de `org.jkiss.dbeaver.ui.data.editors` do DBeaver.
// Aqui a lógica de FORMATAR; a janela que exibe fica na UI.
//
// Por que aqui e não na UI: formatar JSON e converter bytes em hexadecimal é
// lógica pura, testável sem janela -- e é onde os defeitos moram (um JSON
// malformado não pode travar a indentação, um byte 0x00 não pode truncar).
#pragma once

#include "db/types.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace otter::db {

// Como apresentar o valor. Decidido pelo tipo da coluna, com uma correção
// pelo conteúdo: uma coluna `text` que contém JSON merece o visualizador de
// JSON, e o servidor não diz isso.
enum class ValueView : std::uint8_t {
    plain,        // texto, uma linha
    multiline,    // texto com quebras
    json,
    binary,       // hexadecimal + ASCII
    boolean,
};

[[nodiscard]] std::string_view to_string(ValueView view) noexcept;

// Escolhe a apresentação. `sample` é o valor, usado para a correção pelo
// conteúdo.
[[nodiscard]] ValueView choose_view(DataKind kind, std::string_view sample);

// Indenta um JSON.
//
// Tolerante de propósito: um JSON malformado sai COMO VEIO, sem exceção e sem
// travar. O valor pode ter sido gravado por outro sistema, e recusar-se a
// mostrá-lo seria pior que mostrá-lo feio.
[[nodiscard]] std::string format_json(std::string_view text);

// Despejo hexadecimal com a coluna ASCII ao lado, 16 bytes por linha.
//
// `max_bytes` corta o que passar disso: um BLOB de 200 MB formatado inteiro
// consumiria memória sem que ninguém fosse ler além das primeiras linhas.
// Quando corta, a última linha diz quanto ficou de fora.
[[nodiscard]] std::string format_hex(std::span<const std::byte> data,
                                     std::size_t max_bytes = 64 * 1024);

// O valor é verdadeiro? Cobre as formas que os SGBDs usam.
//
// O MySQL não tem booleano: `BOOLEAN` é apelido de `TINYINT(1)`, e o valor
// chega como "0" ou "1". O PostgreSQL manda "t"/"f". Tratar só um dos dois
// deixaria metade das colunas booleanas sem editor.
[[nodiscard]] bool is_true(std::string_view value) noexcept;

// O texto representa um booleano? Usado para a correção pelo conteúdo, quando
// o tipo declarado é inteiro (o caso do MySQL).
[[nodiscard]] bool looks_boolean(std::string_view value) noexcept;

} // namespace otter::db
