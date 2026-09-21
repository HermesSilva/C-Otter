#include "db/holt.hpp"

namespace otter::db {

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
