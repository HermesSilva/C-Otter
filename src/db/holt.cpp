#include "db/holt.hpp"

namespace otter::db {

const char* to_string(SslMode mode) noexcept {
    switch (mode) {
        case SslMode::disable:     return "disable";
        case SslMode::allow:       return "allow";
        case SslMode::prefer:      return "prefer";
        case SslMode::require:     return "require";
        case SslMode::verify_ca:   return "verify-ca";
        case SslMode::verify_full: return "verify-full";
    }
    return "disable";
}

// Modo desconhecido vira `disable`, nao `verify_full`: um perfil salvo por
// uma versao futura com um modo novo deve falhar ao CONECTAR com mensagem
// clara, nao ser promovido em silencio a uma exigencia que o servidor local
// nao atende.
SslMode ssl_mode_from_string(std::string_view text) noexcept {
    if (text == "allow")       return SslMode::allow;
    if (text == "prefer")      return SslMode::prefer;
    if (text == "require")     return SslMode::require;
    if (text == "verify-ca")   return SslMode::verify_ca;
    if (text == "verify-full") return SslMode::verify_full;
    return SslMode::disable;
}

std::string_view to_string(TxnState state) noexcept {
    switch (state) {
        case TxnState::idle:   return "idle";
        case TxnState::active: return "active";
        case TxnState::failed: return "failed";
    }
    return "idle";
}

std::string_view to_string(IsolationLevel level) noexcept {
    // Exatamente como o SQL espera em SET TRANSACTION ISOLATION LEVEL.
    switch (level) {
        case IsolationLevel::read_uncommitted: return "READ UNCOMMITTED";
        case IsolationLevel::read_committed:   return "READ COMMITTED";
        case IsolationLevel::repeatable_read:  return "REPEATABLE READ";
        case IsolationLevel::serializable:     return "SERIALIZABLE";
    }
    return "READ COMMITTED";
}

} // namespace otter::db
