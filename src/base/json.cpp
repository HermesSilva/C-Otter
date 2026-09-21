#include "base/json.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>

namespace otter::json {
namespace {

// Valor nulo compartilhado, devolvido por operator[] quando a chave nao
// existe. Permite encadear acessos sem checar cada nivel.
const Value& null_value() {
    static const Value kNull;
    return kNull;
}

const Object& empty_object() {
    static const Object kEmpty;
    return kEmpty;
}

const Array& empty_array() {
    static const Array kEmpty;
    return kEmpty;
}

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    Result<Value> parse_document() {
        skip_whitespace();
        auto value = parse_value(0);
        if (!value) return value;

        skip_whitespace();
        if (position_ != text_.size()) {
            return error("trailing content after the top-level value");
        }
        return value;
    }

private:
    // Limite de aninhamento. Um arquivo malformado com milhares de '['
    // estouraria a pilha antes de qualquer verificacao de tamanho.
    static constexpr int kMaxDepth = 64;

    std::unexpected<Error> error(std::string_view what) const {
        // Linha e coluna a partir do inicio: sem isso, "parse error" num
        // arquivo de 4 KB obriga a procurar a olho.
        std::size_t line = 1, column = 1;
        for (std::size_t i = 0; i < position_ && i < text_.size(); ++i) {
            if (text_[i] == '\n') { ++line; column = 1; } else { ++column; }
        }
        char buffer[256];
        std::snprintf(buffer, sizeof buffer, "line %zu, column %zu: %.*s",
                      line, column, static_cast<int>(what.size()), what.data());
        return std::unexpected(Error{Errc::parse_error, buffer});
    }

    [[nodiscard]] char peek() const noexcept {
        return position_ < text_.size() ? text_[position_] : '\0';
    }

    void skip_whitespace() noexcept {
        while (position_ < text_.size()) {
            const char c = text_[position_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++position_;
            } else {
                break;
            }
        }
    }

    bool consume(char expected) noexcept {
        if (peek() != expected) return false;
        ++position_;
        return true;
    }

    Result<Value> parse_value(int depth) {
        if (depth > kMaxDepth) return error("nesting too deep");

        skip_whitespace();
        switch (peek()) {
            case '{':  return parse_object(depth);
            case '[':  return parse_array(depth);
            case '"':  {
                auto text = parse_string();
                if (!text) return std::unexpected(text.error());
                return Value(std::move(*text));
            }
            case 't':  return parse_literal("true", Value(true));
            case 'f':  return parse_literal("false", Value(false));
            case 'n':  return parse_literal("null", Value());
            default:   return parse_number();
        }
    }

    Result<Value> parse_literal(std::string_view word, Value value) {
        if (text_.compare(position_, word.size(), word) != 0) {
            return error("invalid literal");
        }
        position_ += word.size();
        return value;
    }

    Result<Value> parse_object(int depth) {
        ++position_;   // '{'
        Object object;

        skip_whitespace();
        if (consume('}')) return Value(std::move(object));

        for (;;) {
            skip_whitespace();
            if (peek() != '"') return error("expected a key string");

            auto key = parse_string();
            if (!key) return std::unexpected(key.error());

            skip_whitespace();
            if (!consume(':')) return error("expected ':' after the key");

            auto value = parse_value(depth + 1);
            if (!value) return value;

            object.insert_or_assign(std::move(*key), std::move(*value));

            skip_whitespace();
            if (consume(',')) continue;
            if (consume('}')) break;
            return error("expected ',' or '}'");
        }
        return Value(std::move(object));
    }

    Result<Value> parse_array(int depth) {
        ++position_;   // '['
        Array array;

        skip_whitespace();
        if (consume(']')) return Value(std::move(array));

        for (;;) {
            auto value = parse_value(depth + 1);
            if (!value) return value;
            array.push_back(std::move(*value));

            skip_whitespace();
            if (consume(',')) continue;
            if (consume(']')) break;
            return error("expected ',' or ']'");
        }
        return Value(std::move(array));
    }

    Result<std::string> parse_string() {
        ++position_;   // '"'
        std::string out;

        while (position_ < text_.size()) {
            const char c = text_[position_++];

            if (c == '"') return out;

            if (c != '\\') {
                out.push_back(c);
                continue;
            }

            if (position_ >= text_.size()) break;
            const char escape = text_[position_++];
            switch (escape) {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u': {
                    auto decoded = parse_unicode_escape();
                    if (!decoded) return std::unexpected(decoded.error());
                    out += *decoded;
                    break;
                }
                default: return error("unknown escape sequence");
            }
        }
        return error("unterminated string");
    }

    // \uXXXX para UTF-8, tratando o par substituto de UTF-16.
    //
    // O DBeaver grava acentos escapados; sem isto, um nome de conexao com
    // cedilha voltaria corrompido.
    Result<std::string> parse_unicode_escape() {
        auto read_hex4 = [this]() -> Result<std::uint32_t> {
            if (position_ + 4 > text_.size()) {
                return error("truncated \\u escape");
            }
            std::uint32_t value = 0;
            for (int i = 0; i < 4; ++i) {
                const char c = text_[position_++];
                value <<= 4;
                if (c >= '0' && c <= '9')      value |= static_cast<std::uint32_t>(c - '0');
                else if (c >= 'a' && c <= 'f') value |= static_cast<std::uint32_t>(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') value |= static_cast<std::uint32_t>(c - 'A' + 10);
                else return error("invalid hex digit in \\u escape");
            }
            return value;
        };

        auto first = read_hex4();
        if (!first) return std::unexpected(first.error());

        std::uint32_t code = *first;

        // Substituto alto: o ponto de codigo real vem do par.
        if (code >= 0xD800 && code <= 0xDBFF) {
            if (position_ + 2 <= text_.size() && text_[position_] == '\\' &&
                text_[position_ + 1] == 'u') {
                position_ += 2;
                auto second = read_hex4();
                if (!second) return std::unexpected(second.error());
                if (*second >= 0xDC00 && *second <= 0xDFFF) {
                    code = 0x10000 + ((code - 0xD800) << 10) + (*second - 0xDC00);
                }
            }
        }

        std::string out;
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
        return out;
    }

    Result<Value> parse_number() {
        const std::size_t start = position_;
        if (peek() == '-' || peek() == '+') ++position_;

        while (position_ < text_.size()) {
            const char c = text_[position_];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' ||
                c == '+' || c == '-') {
                ++position_;
            } else {
                break;
            }
        }

        if (position_ == start) return error("unexpected character");

        const std::string_view digits = text_.substr(start, position_ - start);
        double value = 0.0;
        const auto result = std::from_chars(
            digits.data(), digits.data() + digits.size(), value);
        if (result.ec != std::errc{}) return error("invalid number");

        return Value(value);
    }

    std::string_view text_;
    std::size_t      position_ = 0;
};

void write_escaped(std::string& out, std::string_view text) {
    out.push_back('"');
    for (const char c : text) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                // Bytes UTF-8 passam intactos; so' os controles viram \u.
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof buffer, "\\u%04x",
                                  static_cast<unsigned>(c));
                    out += buffer;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

void write_number(std::string& out, double value) {
    // Inteiro sai sem ".0": a porta de uma conexao gravada como 5432.0 e'
    // tecnicamente valida e visivelmente errada para quem le' o arquivo.
    if (value == std::floor(value) && std::abs(value) < 1e15) {
        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "%lld",
                      static_cast<long long>(value));
        out += buffer;
        return;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.17g", value);
    out += buffer;
}

void write_value(std::string& out, const Value& value, int indent, int level) {
    const auto newline = [&](int at_level) {
        if (indent <= 0) return;
        out.push_back('\n');
        out.append(static_cast<std::size_t>(indent * at_level), ' ');
    };

    switch (value.kind()) {
        case Kind::null:    out += "null"; break;
        case Kind::boolean: out += value.as_bool() ? "true" : "false"; break;
        case Kind::number:  write_number(out, value.as_number()); break;
        case Kind::string:  write_escaped(out, value.as_string()); break;

        case Kind::array: {
            const Array& array = value.as_array();
            if (array.empty()) { out += "[]"; break; }

            out.push_back('[');
            bool first = true;
            for (const Value& item : array) {
                if (!first) out.push_back(',');
                first = false;
                newline(level + 1);
                write_value(out, item, indent, level + 1);
            }
            newline(level);
            out.push_back(']');
            break;
        }

        case Kind::object: {
            const Object& object = value.as_object();
            if (object.empty()) { out += "{}"; break; }

            out.push_back('{');
            bool first = true;
            for (const auto& [key, item] : object) {
                if (!first) out.push_back(',');
                first = false;
                newline(level + 1);
                write_escaped(out, key);
                out.push_back(':');
                if (indent > 0) out.push_back(' ');
                write_value(out, item, indent, level + 1);
            }
            newline(level);
            out.push_back('}');
            break;
        }
    }
}

} // namespace

Value::Value(bool value) : kind_(Kind::boolean), boolean_(value) {}
Value::Value(double value) : kind_(Kind::number), number_(value) {}
Value::Value(std::int64_t value)
    : kind_(Kind::number), number_(static_cast<double>(value)) {}
Value::Value(std::string value) : kind_(Kind::string), string_(std::move(value)) {}
Value::Value(Array value)
    : kind_(Kind::array), array_(std::make_shared<Array>(std::move(value))) {}
Value::Value(Object value)
    : kind_(Kind::object), object_(std::make_shared<Object>(std::move(value))) {}

bool Value::as_bool(bool fallback) const noexcept {
    return kind_ == Kind::boolean ? boolean_ : fallback;
}

double Value::as_number(double fallback) const noexcept {
    return kind_ == Kind::number ? number_ : fallback;
}

std::string_view Value::as_string(std::string_view fallback) const noexcept {
    return kind_ == Kind::string ? std::string_view(string_) : fallback;
}

const Object& Value::as_object() const noexcept {
    return kind_ == Kind::object && object_ ? *object_ : empty_object();
}

const Array& Value::as_array() const noexcept {
    return kind_ == Kind::array && array_ ? *array_ : empty_array();
}

const Value& Value::operator[](std::string_view key) const noexcept {
    if (kind_ != Kind::object || !object_) return null_value();

    const auto it = object_->find(std::string(key));
    return it != object_->end() ? it->second : null_value();
}

const Value& Value::at(std::size_t index) const noexcept {
    if (kind_ != Kind::array || !array_ || index >= array_->size()) {
        return null_value();
    }
    return (*array_)[index];
}

std::int64_t Value::as_int(std::int64_t fallback) const noexcept {
    if (kind_ == Kind::number) return static_cast<std::int64_t>(number_);

    // O DBeaver grava a porta como string ("5432"). Aceitar as duas formas
    // evita espalhar a conversao por cada ponto de leitura.
    if (kind_ == Kind::string) {
        std::int64_t value = 0;
        const auto result = std::from_chars(
            string_.data(), string_.data() + string_.size(), value);
        if (result.ec == std::errc{}) return value;
    }
    return fallback;
}

Result<Value> parse(std::string_view text) {
    Parser parser(text);
    return parser.parse_document();
}

std::string serialize(const Value& value, int indent) {
    std::string out;
    out.reserve(1024);
    write_value(out, value, indent, 0);
    if (indent > 0) out.push_back('\n');
    return out;
}

} // namespace otter::json
