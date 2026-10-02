#include "base/version.hpp"

// OTTER_VERSION e OTTER_BUILD_NUMBER chegam por definicao de compilacao, so'
// para este arquivo (src/base/CMakeLists.txt).
#define OTTER_STRINGIFY_(x) #x
#define OTTER_STRINGIFY(x) OTTER_STRINGIFY_(x)

namespace otter {

int build_number() noexcept { return OTTER_BUILD_NUMBER; }

std::string_view version_string() noexcept {
#if OTTER_BUILD_NUMBER > 0
    return OTTER_VERSION "." OTTER_STRINGIFY(OTTER_BUILD_NUMBER);
#else
    return OTTER_VERSION "-dev";
#endif
}

} // namespace otter
