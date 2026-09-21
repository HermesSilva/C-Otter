// C-Otter -- base/error.hpp
//
// Tratamento de erro do nucleo. Sem excecoes: elas nao cruzam fronteira de
// driver e tornam o custo de caminhos de falha imprevisivel (ADR 0001 #4).
#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace otter {

enum class Errc {
    ok = 0,

    // Genericos
    invalid_argument,
    out_of_range,
    not_found,
    already_exists,
    not_supported,
    internal,

    // Recursos
    out_of_memory,
    io_error,
    permission_denied,

    // Ciclo de vida / concorrencia
    cancelled,
    timed_out,
    closed,

    // Dominio de banco de dados
    connection_failed,
    auth_failed,
    protocol_error,
    query_failed,
    type_mismatch,

    // Texto
    invalid_utf8,
    parse_error,
};

[[nodiscard]] std::string_view to_string(Errc code) noexcept;

// Erro carregado por valor. Guarda a mensagem porque diagnosticar falha de
// banco sem contexto (qual host, qual query) custa mais do que a alocacao.
class Error {
public:
    Error() = default;

    explicit Error(Errc code) : code_(code) {}
    Error(Errc code, std::string message) : code_(code), message_(std::move(message)) {}

    [[nodiscard]] Errc code() const noexcept { return code_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }

    // Encadeia contexto ao propagar: "connect failed: dns lookup failed".
    [[nodiscard]] Error with_context(std::string_view context) const;

    [[nodiscard]] std::string to_string() const;

private:
    Errc        code_ = Errc::internal;
    std::string message_;
};

// Tipo de retorno padrao do nucleo.
template <typename T>
using Result = std::expected<T, Error>;

using Status = std::expected<void, Error>;

// Atalhos de construcao de falha.
[[nodiscard]] inline std::unexpected<Error> fail(Errc code) {
    return std::unexpected(Error{code});
}

[[nodiscard]] inline std::unexpected<Error> fail(Errc code, std::string message) {
    return std::unexpected(Error{code, std::move(message)});
}

// Propagacao de erro. Nao usamos expressoes-statement do GNU ({ ... }) porque
// MSVC nao as suporta e /permissive- e' obrigatorio no projeto.
#define OTTER_DETAIL_CAT2(a, b) a##b
#define OTTER_DETAIL_CAT(a, b) OTTER_DETAIL_CAT2(a, b)

// Declara `decl` a partir de `expr`, retornando o erro se houver.
//   OTTER_ASSIGN_OR_RETURN(auto conn, driver.connect(cfg));
#define OTTER_ASSIGN_OR_RETURN(decl, expr)                                    \
    auto OTTER_DETAIL_CAT(_otter_r_, __LINE__) = (expr);                      \
    if (!OTTER_DETAIL_CAT(_otter_r_, __LINE__))                               \
        return std::unexpected(OTTER_DETAIL_CAT(_otter_r_, __LINE__).error());\
    decl = std::move(*OTTER_DETAIL_CAT(_otter_r_, __LINE__))

// Para Status (expected<void>): propaga falha, descarta sucesso.
#define OTTER_RETURN_IF_ERROR(expr)                                           \
    do {                                                                      \
        auto OTTER_DETAIL_CAT(_otter_s_, __LINE__) = (expr);                  \
        if (!OTTER_DETAIL_CAT(_otter_s_, __LINE__))                           \
            return std::unexpected(                                           \
                OTTER_DETAIL_CAT(_otter_s_, __LINE__).error());               \
    } while (false)

} // namespace otter
