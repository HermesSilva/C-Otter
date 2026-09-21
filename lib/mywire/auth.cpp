#include "mywire/auth.hpp"

#include "net/crypto.hpp"

#include <algorithm>

namespace otter::mywire {
namespace {

std::span<const std::byte> as_bytes(std::string_view text) {
    return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
}

// XOR posicional. Os dois lados tem sempre o mesmo tamanho aqui -- sao dois
// digests do mesmo algoritmo --, mas o min() evita que uma mudanca futura
// vire leitura fora dos limites.
std::vector<std::byte> xor_bytes(std::span<const std::byte> a,
                                 std::span<const std::byte> b) {
    const std::size_t size = std::min(a.size(), b.size());

    std::vector<std::byte> out(size);
    for (std::size_t i = 0; i < size; ++i) out[i] = a[i] ^ b[i];
    return out;
}

} // namespace

Result<std::vector<std::byte>> native_password_response(
    std::string_view password, std::span<const std::byte> challenge) {

    // Conta sem senha responde vazio. O servidor trata os dois casos de forma
    // diferente: 20 bytes de hash de "" seriam recusados.
    if (password.empty()) return std::vector<std::byte>{};

    const Result<crypto::Sha1Digest> stage1 = crypto::sha1(as_bytes(password));
    if (!stage1) return std::unexpected(stage1.error());

    const Result<crypto::Sha1Digest> stage2 = crypto::sha1(*stage1);
    if (!stage2) return std::unexpected(stage2.error());

    // SHA1( desafio ‖ SHA1(SHA1(senha)) )
    std::vector<std::byte> combined;
    combined.reserve(challenge.size() + stage2->size());
    combined.insert(combined.end(), challenge.begin(), challenge.end());
    combined.insert(combined.end(), stage2->begin(), stage2->end());

    const Result<crypto::Sha1Digest> scrambled = crypto::sha1(combined);
    if (!scrambled) return std::unexpected(scrambled.error());

    return xor_bytes(*stage1, *scrambled);
}

Result<std::vector<std::byte>> caching_sha2_response(
    std::string_view password, std::span<const std::byte> challenge) {

    if (password.empty()) return std::vector<std::byte>{};

    const Result<crypto::Sha256Digest> stage1 = crypto::sha256(as_bytes(password));
    if (!stage1) return std::unexpected(stage1.error());

    const Result<crypto::Sha256Digest> stage2 = crypto::sha256(*stage1);
    if (!stage2) return std::unexpected(stage2.error());

    // SHA256( SHA256(SHA256(senha)) ‖ desafio ) -- o desafio vem DEPOIS aqui,
    // ao contrario do native_password, onde vem antes.
    std::vector<std::byte> combined;
    combined.reserve(stage2->size() + challenge.size());
    combined.insert(combined.end(), stage2->begin(), stage2->end());
    combined.insert(combined.end(), challenge.begin(), challenge.end());

    const Result<crypto::Sha256Digest> scrambled = crypto::sha256(combined);
    if (!scrambled) return std::unexpected(scrambled.error());

    return xor_bytes(*stage1, *scrambled);
}

} // namespace otter::mywire
