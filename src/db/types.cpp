#include "db/types.hpp"

namespace otter::db {

std::string_view to_string(DataKind kind) noexcept {
    switch (kind) {
        case DataKind::unknown:   return "unknown";
        case DataKind::boolean:   return "boolean";
        case DataKind::integer:   return "integer";
        case DataKind::floating:  return "float";
        case DataKind::numeric:   return "numeric";
        case DataKind::string:    return "string";
        case DataKind::binary:    return "binary";
        case DataKind::date:      return "date";
        case DataKind::time:      return "time";
        case DataKind::timestamp: return "timestamp";
        case DataKind::interval:  return "interval";
        case DataKind::uuid:      return "uuid";
        case DataKind::json:      return "json";
        case DataKind::array:     return "array";
        case DataKind::geometry:  return "geometry";
        case DataKind::lob:       return "lob";
    }
    return "unknown";
}

} // namespace otter::db
