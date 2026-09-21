#include "base/aes.hpp"

#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#endif

namespace otter::crypto {
namespace {

// --- AES-128 ---------------------------------------------------------------
//
// Implementacao de referencia do FIPS-197. Escrita por extenso em vez de
// tabelas T pre-computadas: o volume aqui sao algumas centenas de bytes por
// arquivo de configuracao, e clareza vale mais que ciclos.

// S-box e inversa do padrao.
constexpr std::uint8_t kSbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16,
};

constexpr std::uint8_t kInvSbox[256] = {
    0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
    0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
    0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
    0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
    0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
    0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
    0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
    0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
    0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
    0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
    0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
    0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
    0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
    0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
    0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
    0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d,
};

// Multiplicacao no corpo GF(2^8) do AES, com o polinomio 0x11B.
constexpr std::uint8_t xtime(std::uint8_t value) noexcept {
    return static_cast<std::uint8_t>((value << 1) ^ ((value & 0x80) ? 0x1B : 0x00));
}

std::uint8_t gf_mul(std::uint8_t a, std::uint8_t b) noexcept {
    std::uint8_t result = 0;
    for (int i = 0; i < 8; ++i) {
        if (b & 1) result ^= a;
        a = xtime(a);
        b >>= 1;
    }
    return result;
}

// AES-128: 10 rodadas, 11 subchaves de 16 bytes.
constexpr int kRounds = 10;
using RoundKeys = std::array<std::uint8_t, kAesBlockSize * (kRounds + 1)>;

RoundKeys expand_key(const AesKey& key) {
    RoundKeys keys{};
    std::memcpy(keys.data(), key.data(), kAes128KeySize);

    std::uint8_t rcon = 1;
    for (std::size_t i = kAes128KeySize; i < keys.size(); i += 4) {
        std::uint8_t temp[4] = {
            keys[i - 4], keys[i - 3], keys[i - 2], keys[i - 1],
        };

        if (i % kAes128KeySize == 0) {
            // RotWord + SubWord + Rcon
            const std::uint8_t first = temp[0];
            temp[0] = static_cast<std::uint8_t>(kSbox[temp[1]] ^ rcon);
            temp[1] = kSbox[temp[2]];
            temp[2] = kSbox[temp[3]];
            temp[3] = kSbox[first];
            rcon = xtime(rcon);
        }

        for (std::size_t j = 0; j < 4; ++j) {
            keys[i + j] = static_cast<std::uint8_t>(
                keys[i + j - kAes128KeySize] ^ temp[j]);
        }
    }
    return keys;
}

void add_round_key(std::uint8_t* state, const std::uint8_t* key) noexcept {
    for (std::size_t i = 0; i < kAesBlockSize; ++i) state[i] ^= key[i];
}

void encrypt_block(std::uint8_t* state, const RoundKeys& keys) {
    add_round_key(state, keys.data());

    for (int round = 1; round <= kRounds; ++round) {
        for (std::size_t i = 0; i < kAesBlockSize; ++i) state[i] = kSbox[state[i]];

        // ShiftRows: o estado e' column-major, entao a linha r ocupa os
        // indices r, r+4, r+8, r+12.
        std::uint8_t tmp[kAesBlockSize];
        for (std::size_t c = 0; c < 4; ++c) {
            for (std::size_t r = 0; r < 4; ++r) {
                tmp[c * 4 + r] = state[((c + r) % 4) * 4 + r];
            }
        }
        std::memcpy(state, tmp, kAesBlockSize);

        if (round != kRounds) {
            for (std::size_t c = 0; c < 4; ++c) {
                std::uint8_t* col = state + c * 4;
                const std::uint8_t a0 = col[0], a1 = col[1];
                const std::uint8_t a2 = col[2], a3 = col[3];
                col[0] = static_cast<std::uint8_t>(gf_mul(a0, 2) ^ gf_mul(a1, 3) ^ a2 ^ a3);
                col[1] = static_cast<std::uint8_t>(a0 ^ gf_mul(a1, 2) ^ gf_mul(a2, 3) ^ a3);
                col[2] = static_cast<std::uint8_t>(a0 ^ a1 ^ gf_mul(a2, 2) ^ gf_mul(a3, 3));
                col[3] = static_cast<std::uint8_t>(gf_mul(a0, 3) ^ a1 ^ a2 ^ gf_mul(a3, 2));
            }
        }

        add_round_key(state, keys.data() + round * kAesBlockSize);
    }
}

void decrypt_block(std::uint8_t* state, const RoundKeys& keys) {
    add_round_key(state, keys.data() + kRounds * kAesBlockSize);

    for (int round = kRounds - 1; round >= 0; --round) {
        // InvShiftRows
        std::uint8_t tmp[kAesBlockSize];
        for (std::size_t c = 0; c < 4; ++c) {
            for (std::size_t r = 0; r < 4; ++r) {
                tmp[((c + r) % 4) * 4 + r] = state[c * 4 + r];
            }
        }
        std::memcpy(state, tmp, kAesBlockSize);

        for (std::size_t i = 0; i < kAesBlockSize; ++i) state[i] = kInvSbox[state[i]];

        add_round_key(state, keys.data() + round * kAesBlockSize);

        if (round != 0) {
            for (std::size_t c = 0; c < 4; ++c) {
                std::uint8_t* col = state + c * 4;
                const std::uint8_t a0 = col[0], a1 = col[1];
                const std::uint8_t a2 = col[2], a3 = col[3];
                col[0] = static_cast<std::uint8_t>(
                    gf_mul(a0, 14) ^ gf_mul(a1, 11) ^ gf_mul(a2, 13) ^ gf_mul(a3, 9));
                col[1] = static_cast<std::uint8_t>(
                    gf_mul(a0, 9) ^ gf_mul(a1, 14) ^ gf_mul(a2, 11) ^ gf_mul(a3, 13));
                col[2] = static_cast<std::uint8_t>(
                    gf_mul(a0, 13) ^ gf_mul(a1, 9) ^ gf_mul(a2, 14) ^ gf_mul(a3, 11));
                col[3] = static_cast<std::uint8_t>(
                    gf_mul(a0, 11) ^ gf_mul(a1, 13) ^ gf_mul(a2, 9) ^ gf_mul(a3, 14));
            }
        }
    }
}

} // namespace

std::vector<std::uint8_t> aes128_cbc_encrypt(
    std::span<const std::uint8_t> plaintext, const AesKey& key, const AesIv& iv) {
    const RoundKeys keys = expand_key(key);

    // PKCS#7: sempre acrescenta padding, mesmo quando o tamanho ja' e'
    // multiplo do bloco -- senao nao ha' como saber se o ultimo byte e'
    // padding ou dado.
    const std::size_t pad =
        kAesBlockSize - (plaintext.size() % kAesBlockSize);

    std::vector<std::uint8_t> out;
    out.reserve(kAesBlockSize + plaintext.size() + pad);
    out.insert(out.end(), iv.begin(), iv.end());

    std::array<std::uint8_t, kAesBlockSize> chain = iv;
    std::size_t offset = 0;

    while (offset < plaintext.size() + pad) {
        std::uint8_t block[kAesBlockSize];
        for (std::size_t i = 0; i < kAesBlockSize; ++i) {
            const std::size_t index = offset + i;
            const std::uint8_t byte =
                index < plaintext.size() ? plaintext[index]
                                         : static_cast<std::uint8_t>(pad);
            block[i] = static_cast<std::uint8_t>(byte ^ chain[i]);
        }

        encrypt_block(block, keys);
        out.insert(out.end(), block, block + kAesBlockSize);
        std::memcpy(chain.data(), block, kAesBlockSize);
        offset += kAesBlockSize;
    }
    return out;
}

Result<std::vector<std::uint8_t>> aes128_cbc_decrypt(
    std::span<const std::uint8_t> input, const AesKey& key) {
    if (input.size() <= kAesBlockSize) {
        return fail(Errc::invalid_argument, "ciphertext shorter than the IV");
    }
    if ((input.size() - kAesBlockSize) % kAesBlockSize != 0) {
        return fail(Errc::invalid_argument,
                    "ciphertext is not a multiple of the block size");
    }

    const RoundKeys keys = expand_key(key);

    std::array<std::uint8_t, kAesBlockSize> chain{};
    std::memcpy(chain.data(), input.data(), kAesBlockSize);

    std::vector<std::uint8_t> out;
    out.reserve(input.size() - kAesBlockSize);

    for (std::size_t offset = kAesBlockSize; offset < input.size();
         offset += kAesBlockSize) {
        std::uint8_t block[kAesBlockSize];
        std::memcpy(block, input.data() + offset, kAesBlockSize);

        std::array<std::uint8_t, kAesBlockSize> next{};
        std::memcpy(next.data(), block, kAesBlockSize);

        decrypt_block(block, keys);
        for (std::size_t i = 0; i < kAesBlockSize; ++i) block[i] ^= chain[i];

        out.insert(out.end(), block, block + kAesBlockSize);
        chain = next;
    }

    // Padding invalido quase sempre significa chave errada -- vale uma
    // mensagem propria, porque o sintoma seria "JSON malformado" mais adiante.
    const std::uint8_t pad = out.back();
    if (pad == 0 || pad > kAesBlockSize || pad > out.size()) {
        return fail(Errc::invalid_argument,
                    "invalid PKCS#7 padding (wrong key?)");
    }
    for (std::size_t i = out.size() - pad; i < out.size(); ++i) {
        if (out[i] != pad) {
            return fail(Errc::invalid_argument,
                        "invalid PKCS#7 padding (wrong key?)");
        }
    }
    out.resize(out.size() - pad);
    return out;
}

Result<AesIv> random_iv() {
    AesIv iv{};
#ifdef _WIN32
    const NTSTATUS status = BCryptGenRandom(
        nullptr, iv.data(), static_cast<ULONG>(iv.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status != 0) {
        return fail(Errc::internal, "BCryptGenRandom failed");
    }
#else
    // Em Linux, /dev/urandom. getrandom() seria melhor, mas exige glibc 2.25+
    // e o ganho nao justifica a restricao.
    std::FILE* source = std::fopen("/dev/urandom", "rb");
    if (source == nullptr) {
        return fail(Errc::io_error, "cannot open /dev/urandom");
    }
    const std::size_t read = std::fread(iv.data(), 1, iv.size(), source);
    std::fclose(source);
    if (read != iv.size()) {
        return fail(Errc::io_error, "short read from /dev/urandom");
    }
#endif
    return iv;
}

} // namespace otter::crypto
