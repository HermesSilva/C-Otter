// C-Otter -- base/arena.hpp
//
// Alocador de arena (bump allocator) com blocos encadeados.
//
// Razao de existir: o DBeaver materializa cada valor de cada celula como um
// objeto no heap da JVM. Um SELECT de 1M x 10 colunas produz 10 milhoes de
// objetos e a pressao de GC correspondente -- e' a causa raiz do consumo de
// 1,5-3 GB medido em docs/ANALYSIS.md.
//
// Aqui, uma unidade de trabalho (um fetch, uma analise de query) recebe uma
// arena. Alocar e' incrementar um ponteiro; liberar e' descartar a arena
// inteira de uma vez. Nenhuma chamada a free() por celula.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace otter {

class Arena {
public:
    static constexpr std::size_t kDefaultBlockSize = 64 * 1024;
    static constexpr std::size_t kMaxAlign = alignof(std::max_align_t);

    explicit Arena(std::size_t block_size = kDefaultBlockSize);
    ~Arena();

    // Move-only: copiar uma arena quase sempre e' um bug de design.
    Arena(const Arena&)            = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&& other) noexcept;
    Arena& operator=(Arena&& other) noexcept;

    // Bloco de bytes cru. Retorna nullptr apenas se a alocacao do SO falhar.
    [[nodiscard]] void* allocate(std::size_t bytes, std::size_t align = kMaxAlign);

    // Objeto trivialmente destrutivel. A restricao e' deliberada: a arena nao
    // roda destrutores, entao tipos com cleanup nao pertencem a ela.
    template <typename T, typename... Args>
    [[nodiscard]] T* create(Args&&... args) {
        static_assert(std::is_trivially_destructible_v<T>,
                      "Arena nao executa destrutores; use um tipo trivial ou "
                      "gerencie o objeto fora da arena");
        void* mem = allocate(sizeof(T), alignof(T));
        if (mem == nullptr) return nullptr;
        return new (mem) T(std::forward<Args>(args)...);
    }

    // Array nao inicializado -- o caminho usado pelos buffers colunares.
    template <typename T>
    [[nodiscard]] std::span<T> allocate_array(std::size_t count) {
        static_assert(std::is_trivially_destructible_v<T>,
                      "Arena nao executa destrutores");
        if (count == 0) return {};
        void* mem = allocate(sizeof(T) * count, alignof(T));
        if (mem == nullptr) return {};
        return std::span<T>{static_cast<T*>(mem), count};
    }

    // Copia texto para dentro da arena. O string_view resultante vive enquanto
    // a arena viver -- nunca o devolva alem do escopo dela.
    [[nodiscard]] std::string_view copy_string(std::string_view text);

    // Posicao da arena, para rebobinar trabalho temporario (ver ArenaScope).
    struct Mark {
        std::size_t block_index;
        std::byte*  cursor;
        std::size_t used;
    };

    [[nodiscard]] Mark mark() const noexcept;
    void rewind(const Mark& mark) noexcept;

    // Devolve toda a memoria ao estado inicial, preservando o primeiro bloco
    // para reuso. E' o caminho quente entre fetches sucessivos.
    void reset();

    // Libera tudo, inclusive o bloco inicial.
    void release();

    [[nodiscard]] std::size_t bytes_used() const noexcept { return used_; }
    [[nodiscard]] std::size_t bytes_reserved() const noexcept { return reserved_; }
    [[nodiscard]] std::size_t block_count() const noexcept { return blocks_.size(); }

private:
    struct Block {
        std::byte*  data;
        std::size_t capacity;
    };

    bool grow(std::size_t min_bytes, std::size_t align);

    std::vector<Block> blocks_;
    std::byte*  cursor_      = nullptr;  // proximo byte livre no bloco atual
    std::byte*  block_end_   = nullptr;  // fim do bloco atual
    std::size_t block_index_ = 0;        // indice do bloco atual em blocks_
    std::size_t block_size_  = kDefaultBlockSize;
    std::size_t used_        = 0;
    std::size_t reserved_    = 0;
};

// Marca uma posicao da arena e a restaura na destruicao. Util para trabalho
// temporario dentro de um escopo maior (ex.: avaliar uma subexpressao).
//
// Restaura apenas dentro do bloco corrente: se o escopo tiver transbordado para
// um novo bloco, os blocos extras permanecem reservados (serao reaproveitados
// por alocacoes seguintes). Rebobinar atraves de blocos exigiria rastrear a
// cadeia inteira, o que encareceria o caminho quente sem beneficio real.
class ArenaScope {
public:
    explicit ArenaScope(Arena& arena) : arena_(arena), mark_(arena.mark()) {}

    ~ArenaScope() { arena_.rewind(mark_); }

    ArenaScope(const ArenaScope&)            = delete;
    ArenaScope& operator=(const ArenaScope&) = delete;

private:
    Arena&       arena_;
    Arena::Mark  mark_;
};

} // namespace otter
