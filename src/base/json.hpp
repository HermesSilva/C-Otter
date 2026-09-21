// C-Otter -- base/json.hpp
//
// Leitor e escritor de JSON, o minimo necessario para os arquivos de
// configuracao (ADR 0012).
//
// Por que proprio: as bibliotecas boas de JSON em C++ sao header-only e
// grandes (nlohmann passa de 20 mil linhas), e o que precisamos e' ler e
// escrever objetos, arrays, strings, numeros e booleanos. Nao ha' streaming,
// nao ha' SAX, nao ha' alocador customizado -- um arquivo de conexoes tem
// alguns KB.
#pragma once

#include "base/error.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace otter::json {

class Value;

// Objeto ordenado por chave. std::map, nao unordered: a saida precisa ser
// estavel entre gravacoes, senao o diff do arquivo vira ruido.
using Object = std::map<std::string, Value>;
using Array  = std::vector<Value>;

enum class Kind : std::uint8_t { null, boolean, number, string, array, object };

class Value {
public:
    Value() = default;
    Value(bool value);
    Value(double value);
    Value(std::int64_t value);
    Value(std::string value);
    Value(const char* value) : Value(std::string(value)) {}
    Value(Array value);
    Value(Object value);

    [[nodiscard]] Kind kind() const noexcept { return kind_; }
    [[nodiscard]] bool is_null() const noexcept { return kind_ == Kind::null; }
    [[nodiscard]] bool is_object() const noexcept { return kind_ == Kind::object; }
    [[nodiscard]] bool is_array() const noexcept { return kind_ == Kind::array; }

    // Acessos tolerantes: tipo errado devolve o padrao em vez de lancar.
    //
    // E' o comportamento certo para configuracao escrita por outro programa:
    // um campo com o tipo inesperado nao deve derrubar a leitura do arquivo
    // inteiro. Quem precisa distinguir ausente de invalido olha kind().
    [[nodiscard]] bool as_bool(bool fallback = false) const noexcept;
    [[nodiscard]] double as_number(double fallback = 0.0) const noexcept;
    [[nodiscard]] std::string_view as_string(
        std::string_view fallback = {}) const noexcept;

    [[nodiscard]] const Object& as_object() const noexcept;
    [[nodiscard]] const Array& as_array() const noexcept;

    // Busca por chave. Devolve um Value nulo quando ausente, o que permite
    // encadear: root["connections"]["id"]["configuration"]["host"].
    [[nodiscard]] const Value& operator[](std::string_view key) const noexcept;
    [[nodiscard]] const Value& at(std::size_t index) const noexcept;

    // Numeros vindos de JSON de outro programa podem ser string ("5432") ou
    // numero (5432). O DBeaver grava a porta como string.
    [[nodiscard]] std::int64_t as_int(std::int64_t fallback = 0) const noexcept;

private:
    Kind        kind_ = Kind::null;
    bool        boolean_ = false;
    double      number_ = 0.0;
    std::string string_;
    std::shared_ptr<Array>  array_;
    std::shared_ptr<Object> object_;
};

// Analisa um documento JSON. O erro traz linha e coluna.
[[nodiscard]] Result<Value> parse(std::string_view text);

// Serializa. `indent` > 0 produz saida legivel por humanos -- estes arquivos
// sao editados a mao com alguma frequencia.
[[nodiscard]] std::string serialize(const Value& value, int indent = 2);

} // namespace otter::json
