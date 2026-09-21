#include "base/error.hpp"

namespace otter {

std::string_view to_string(Errc code) noexcept {
    switch (code) {
        case Errc::ok:                return "ok";
        case Errc::invalid_argument:  return "invalid argument";
        case Errc::out_of_range:      return "out of range";
        case Errc::not_found:         return "not found";
        case Errc::already_exists:    return "already exists";
        case Errc::not_supported:     return "not supported";
        case Errc::internal:          return "internal error";
        case Errc::out_of_memory:     return "out of memory";
        case Errc::io_error:          return "I/O error";
        case Errc::permission_denied: return "permission denied";
        case Errc::cancelled:         return "cancelled";
        case Errc::timed_out:         return "timed out";
        case Errc::closed:            return "closed";
        case Errc::connection_failed: return "connection failed";
        case Errc::auth_failed:       return "authentication failed";
        case Errc::protocol_error:    return "protocol error";
        case Errc::query_failed:      return "query failed";
        case Errc::type_mismatch:     return "type mismatch";
        case Errc::invalid_utf8:      return "invalid UTF-8";
        case Errc::parse_error:       return "parse error";
    }
    return "unknown error";
}

Error Error::with_context(std::string_view context) const {
    std::string combined;
    combined.reserve(context.size() + message_.size() + 2);
    combined.append(context);
    if (!message_.empty()) {
        combined.append(": ");
        combined.append(message_);
    }
    return Error{code_, std::move(combined)};
}

std::string Error::to_string() const {
    const std::string_view name = otter::to_string(code_);
    if (message_.empty()) {
        return std::string{name};
    }
    std::string out;
    out.reserve(name.size() + message_.size() + 3);
    out.append(message_);
    out.append(" [");
    out.append(name);
    out.append("]");
    return out;
}

} // namespace otter
