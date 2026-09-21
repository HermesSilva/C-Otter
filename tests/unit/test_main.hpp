// C-Otter -- framework de teste minimo.
//
// Zero dependencias (ADR 0001 #1). Nao compete com Catch2/GoogleTest: cobre o
// que o nucleo precisa -- registrar casos, comparar valores, reportar falha com
// arquivo e linha.
#pragma once

#include <cstdio>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

namespace otter::test {

using TestFn = void (*)();

struct TestCase {
    std::string_view name;
    TestFn           fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failure_count() {
    static int count = 0;
    return count;
}

struct Registrar {
    Registrar(std::string_view name, TestFn fn) { registry().push_back({name, fn}); }
};

// Lancado por OTTER_CHECK para abortar o caso atual sem derrubar a suite.
struct AssertionFailure {};

inline void report(std::string_view file, int line, std::string_view expr,
                   std::string_view detail = {}) {
    std::fprintf(stderr, "  FAIL %s:%d\n    %.*s\n", file.data(), line,
                 static_cast<int>(expr.size()), expr.data());
    if (!detail.empty()) {
        std::fprintf(stderr, "    %.*s\n", static_cast<int>(detail.size()), detail.data());
    }
    ++failure_count();
}

inline int run_all() {
    int failed = 0;
    for (const TestCase& tc : registry()) {
        const int before = failure_count();
        std::fprintf(stderr, "[ RUN  ] %.*s\n",
                     static_cast<int>(tc.name.size()), tc.name.data());
        try {
            tc.fn();
        } catch (const AssertionFailure&) {
            // Ja' reportado por OTTER_CHECK.
        } catch (const std::exception& e) {
            report("<exception>", 0, tc.name, e.what());
        } catch (...) {
            report("<exception>", 0, tc.name, "excecao desconhecida");
        }
        if (failure_count() > before) {
            ++failed;
            std::fprintf(stderr, "[ FAIL ] %.*s\n\n",
                         static_cast<int>(tc.name.size()), tc.name.data());
        } else {
            std::fprintf(stderr, "[  OK  ] %.*s\n",
                         static_cast<int>(tc.name.size()), tc.name.data());
        }
    }

    const std::size_t total = registry().size();
    std::fprintf(stderr, "\n%zu testes, %d falharam\n", total, failed);
    return failed == 0 ? 0 : 1;
}

} // namespace otter::test

#define OTTER_TEST(name)                                                       \
    static void name();                                                        \
    static ::otter::test::Registrar _otter_reg_##name{#name, &name};           \
    static void name()

#define OTTER_CHECK(expr)                                                      \
    do {                                                                       \
        if (!(expr)) {                                                         \
            ::otter::test::report(__FILE__, __LINE__, #expr);                  \
            throw ::otter::test::AssertionFailure{};                           \
        }                                                                      \
    } while (false)

// Copia por valor, deliberadamente.
//
// Ligar a `const auto&` parece mais barato, mas cria dangling reference no caso
// comum `obj_temporario().campo()`: o temporario morre ao fim da inicializacao
// e a extensao de tempo de vida nao cobre subobjetos devolvidos por funcao
// membro. Este bug custou um teste falso-negativo aqui -- copiar e' correto.
#define OTTER_CHECK_EQ(a, b)                                                   \
    do {                                                                       \
        const auto _a = (a);                                                   \
        const auto _b = (b);                                                   \
        if (!(_a == _b)) {                                                     \
            ::otter::test::report(__FILE__, __LINE__, #a " == " #b);           \
            throw ::otter::test::AssertionFailure{};                           \
        }                                                                      \
    } while (false)
