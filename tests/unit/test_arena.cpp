#include "test_main.hpp"

#include "base/arena.hpp"

#include <cstdint>
#include <cstring>
#include <string>

using otter::Arena;
using otter::ArenaScope;

namespace {

bool is_aligned(const void* p, std::size_t align) {
    return (reinterpret_cast<std::uintptr_t>(p) % align) == 0;
}

} // namespace

OTTER_TEST(arena_allocates_and_aligns) {
    Arena arena;

    void* a = arena.allocate(1, 1);
    void* b = arena.allocate(8, 8);
    void* c = arena.allocate(64, 64);

    OTTER_CHECK(a != nullptr);
    OTTER_CHECK(b != nullptr);
    OTTER_CHECK(c != nullptr);
    OTTER_CHECK(is_aligned(b, 8));
    OTTER_CHECK(is_aligned(c, 64));
    OTTER_CHECK(arena.bytes_used() >= 73);
}

OTTER_TEST(arena_zero_size_returns_non_null) {
    // Um pedido de 0 bytes nao e' falha; nullptr significa erro de alocacao.
    Arena arena;
    OTTER_CHECK(arena.allocate(0) != nullptr);
}

OTTER_TEST(arena_grows_across_blocks) {
    Arena arena(1024);

    // Excede o bloco inicial vArias vezes.
    for (int i = 0; i < 100; ++i) {
        void* p = arena.allocate(256, 16);
        OTTER_CHECK(p != nullptr);
        std::memset(p, i & 0xFF, 256);
    }

    OTTER_CHECK(arena.block_count() > 1);
    OTTER_CHECK(arena.bytes_reserved() >= 100 * 256);
}

OTTER_TEST(arena_allocation_larger_than_block) {
    // Pedido maior que block_size deve gerar um bloco sob medida.
    Arena arena(1024);
    auto span = arena.allocate_array<std::uint64_t>(10'000);

    OTTER_CHECK(span.size() == 10'000);
    OTTER_CHECK(span.data() != nullptr);
    OTTER_CHECK(is_aligned(span.data(), alignof(std::uint64_t)));

    span[0]     = 42;
    span[9'999] = 43;
    OTTER_CHECK_EQ(span[0], std::uint64_t{42});
    OTTER_CHECK_EQ(span[9'999], std::uint64_t{43});
}

OTTER_TEST(arena_create_object) {
    struct Point { int x; int y; };

    Arena arena;
    Point* p = arena.create<Point>(3, 4);

    OTTER_CHECK(p != nullptr);
    OTTER_CHECK_EQ(p->x, 3);
    OTTER_CHECK_EQ(p->y, 4);
}

OTTER_TEST(arena_copy_string) {
    Arena arena;

    std::string source = "OTTER JOIN";
    std::string_view copy = arena.copy_string(source);

    OTTER_CHECK_EQ(copy, std::string_view{"OTTER JOIN"});
    // Precisa ser copia de verdade: a origem pode morrer antes da arena.
    OTTER_CHECK(copy.data() != source.data());

    source.clear();
    OTTER_CHECK_EQ(copy, std::string_view{"OTTER JOIN"});
}

OTTER_TEST(arena_copy_empty_string) {
    Arena arena;
    OTTER_CHECK(arena.copy_string({}).empty());
}

OTTER_TEST(arena_reset_reuses_memory) {
    Arena arena(4096);

    void* first = arena.allocate(128);
    const std::size_t reserved_before = arena.bytes_reserved();

    arena.reset();
    OTTER_CHECK_EQ(arena.bytes_used(), std::size_t{0});

    // reset() nao devolve memoria ao SO -- esse e' o ponto: reuso sem syscall.
    OTTER_CHECK_EQ(arena.bytes_reserved(), reserved_before);

    void* again = arena.allocate(128);
    OTTER_CHECK_EQ(first, again);
}

OTTER_TEST(arena_scope_rewinds) {
    Arena arena(64 * 1024);

    OTTER_CHECK(arena.allocate(100) != nullptr);
    const std::size_t outer = arena.bytes_used();

    {
        ArenaScope scope(arena);
        OTTER_CHECK(arena.allocate(1000) != nullptr);
        OTTER_CHECK(arena.bytes_used() > outer);
    }

    OTTER_CHECK_EQ(arena.bytes_used(), outer);
}

OTTER_TEST(arena_move_transfers_ownership) {
    Arena source(2048);
    void* p = source.allocate(256);
    std::memset(p, 0xAB, 256);
    const std::size_t used = source.bytes_used();

    Arena dest = std::move(source);

    OTTER_CHECK_EQ(dest.bytes_used(), used);
    OTTER_CHECK_EQ(source.bytes_used(), std::size_t{0});
    OTTER_CHECK_EQ(source.block_count(), std::size_t{0});

    // A memoria movida continua valida.
    OTTER_CHECK_EQ(static_cast<unsigned char*>(p)[255], 0xAB);
}

OTTER_TEST(arena_release_frees_everything) {
    Arena arena(1024);
    for (int i = 0; i < 20; ++i) {
        OTTER_CHECK(arena.allocate(512) != nullptr);
    }
    OTTER_CHECK(arena.block_count() > 1);

    arena.release();

    OTTER_CHECK_EQ(arena.block_count(), std::size_t{0});
    OTTER_CHECK_EQ(arena.bytes_used(), std::size_t{0});
    OTTER_CHECK_EQ(arena.bytes_reserved(), std::size_t{0});

    // Continua utilizavel depois de release().
    OTTER_CHECK(arena.allocate(64) != nullptr);
}
