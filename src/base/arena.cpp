#include "base/arena.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace otter {
namespace {

// Alinha `value` para cima ate o proximo multiplo de `align` (potencia de 2).
constexpr std::size_t align_up(std::size_t value, std::size_t align) noexcept {
    return (value + align - 1) & ~(align - 1);
}

// So' e' chamada dentro de assert: em Release (NDEBUG) fica sem uso, e o Clang
// acusa -Wunused-function em namespace anonimo.
[[maybe_unused]] constexpr bool is_power_of_two(std::size_t v) noexcept {
    return v != 0 && (v & (v - 1)) == 0;
}

} // namespace

Arena::Arena(std::size_t block_size)
    : block_size_(std::max(block_size, std::size_t{1024})) {}

Arena::~Arena() {
    release();
}

Arena::Arena(Arena&& other) noexcept
    : blocks_(std::move(other.blocks_)),
      cursor_(other.cursor_),
      block_end_(other.block_end_),
      block_index_(other.block_index_),
      block_size_(other.block_size_),
      used_(other.used_),
      reserved_(other.reserved_) {
    other.blocks_.clear();
    other.cursor_      = nullptr;
    other.block_end_   = nullptr;
    other.block_index_ = 0;
    other.used_        = 0;
    other.reserved_    = 0;
}

Arena& Arena::operator=(Arena&& other) noexcept {
    if (this != &other) {
        release();
        blocks_      = std::move(other.blocks_);
        cursor_      = other.cursor_;
        block_end_   = other.block_end_;
        block_index_ = other.block_index_;
        block_size_  = other.block_size_;
        used_        = other.used_;
        reserved_    = other.reserved_;

        other.blocks_.clear();
        other.cursor_      = nullptr;
        other.block_end_   = nullptr;
        other.block_index_ = 0;
        other.used_        = 0;
        other.reserved_    = 0;
    }
    return *this;
}

void* Arena::allocate(std::size_t bytes, std::size_t align) {
    assert(is_power_of_two(align) && "alinhamento deve ser potencia de 2");

    if (bytes == 0) {
        // Devolve um ponteiro valido e distinto de nullptr (que sinaliza falha).
        return cursor_ != nullptr ? cursor_ : reinterpret_cast<void*>(align);
    }

    std::byte* aligned = reinterpret_cast<std::byte*>(
        align_up(reinterpret_cast<std::uintptr_t>(cursor_), align));

    if (cursor_ == nullptr || aligned + bytes > block_end_) {
        if (!grow(bytes, align)) return nullptr;
        aligned = reinterpret_cast<std::byte*>(
            align_up(reinterpret_cast<std::uintptr_t>(cursor_), align));
    }

    const std::size_t padding = static_cast<std::size_t>(aligned - cursor_);
    cursor_ = aligned + bytes;
    used_ += padding + bytes;
    return aligned;
}

bool Arena::grow(std::size_t min_bytes, std::size_t align) {
    // Se ja' existe um bloco seguinte reservado (apos reset/rewind), reutiliza.
    const std::size_t next_index = blocks_.empty() ? 0 : block_index_ + 1;
    if (next_index < blocks_.size() &&
        blocks_[next_index].capacity >= min_bytes + align) {
        block_index_ = next_index;
        cursor_      = blocks_[next_index].data;
        block_end_   = cursor_ + blocks_[next_index].capacity;
        return true;
    }

    // Bloco novo: grande o bastante para o pedido, incluindo folga de alinhamento.
    const std::size_t capacity = std::max(block_size_, min_bytes + align);

    std::byte* data = static_cast<std::byte*>(::operator new(capacity, std::nothrow));
    if (data == nullptr) return false;

    blocks_.push_back(Block{data, capacity});
    block_index_ = blocks_.size() - 1;
    cursor_      = data;
    block_end_   = data + capacity;
    reserved_ += capacity;

    // Blocos crescem geometricamente ate um teto, para que arenas longevas nao
    // paguem uma alocacao do SO a cada poucos KB.
    block_size_ = std::min(block_size_ * 2, std::size_t{4} * 1024 * 1024);
    return true;
}

std::string_view Arena::copy_string(std::string_view text) {
    if (text.empty()) return {};

    void* mem = allocate(text.size(), alignof(char));
    if (mem == nullptr) return {};

    std::memcpy(mem, text.data(), text.size());
    return std::string_view{static_cast<const char*>(mem), text.size()};
}

Arena::Mark Arena::mark() const noexcept {
    return Mark{block_index_, cursor_, used_};
}

void Arena::rewind(const Mark& mark) noexcept {
    if (blocks_.empty()) return;

    // So' rebobina se ainda estivermos no mesmo bloco da marca. Ter transbordado
    // para outro bloco significa que os blocos intermediarios seguem reservados;
    // eles serao reaproveitados pela proxima alocacao (ver grow()).
    if (mark.block_index == block_index_) {
        cursor_ = mark.cursor;
        used_   = mark.used;
    }
}

void Arena::reset() {
    if (blocks_.empty()) return;

    // Preserva os blocos ja' reservados e volta ao primeiro: o caminho quente
    // entre fetches sucessivos nao deve tocar o alocador do SO.
    block_index_ = 0;
    cursor_      = blocks_[0].data;
    block_end_   = cursor_ + blocks_[0].capacity;
    used_        = 0;
}

void Arena::release() {
    for (const Block& block : blocks_) {
        ::operator delete(block.data, std::nothrow);
    }
    blocks_.clear();
    cursor_      = nullptr;
    block_end_   = nullptr;
    block_index_ = 0;
    used_        = 0;
    reserved_    = 0;
}

} // namespace otter
