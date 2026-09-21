#include "db/result_set.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace otter::db {
namespace {

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = std::tolower(static_cast<unsigned char>(a[i]));
        const auto cb = std::tolower(static_cast<unsigned char>(b[i]));
        if (ca != cb) return false;
    }
    return true;
}

} // namespace

std::optional<std::size_t> ResultSet::find_column(std::string_view name) const {
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        if (iequals(columns_[i].info().name, name)) return i;
    }
    return std::nullopt;
}

std::size_t ResultSet::bytes_used() const noexcept {
    std::size_t total = 0;
    for (const Column& column : columns_) total += column.bytes_used();
    return total;
}

void ResultSetBuilder::add_column(ColumnInfo info) {
    result_.columns_.emplace_back(std::move(info));
}

void ResultSetBuilder::append(std::size_t column, std::span<const std::byte> value) {
    Column& c = result_.columns_[column];
    c.data_.insert(c.data_.end(), value.begin(), value.end());
    c.offsets_.push_back(c.data_.size());
    c.nulls_.push_back(false);
}

void ResultSetBuilder::append_text(std::size_t column, std::string_view value) {
    append(column, std::as_bytes(std::span<const char>(value.data(), value.size())));
}

void ResultSetBuilder::append_null(std::size_t column) {
    Column& c = result_.columns_[column];
    // Nulo nao ocupa bytes; o offset repetido marca extensao zero, e o bitmap
    // distingue nulo de valor vazio.
    c.offsets_.push_back(c.data_.size());
    c.nulls_.push_back(true);
}

} // namespace otter::db
