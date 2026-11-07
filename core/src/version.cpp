#include "zub/core/version.hpp"

#include "build_info.hpp"

namespace zub {

std::string_view version() noexcept {
    return ZUB_VERSION;
}

} // namespace zub
