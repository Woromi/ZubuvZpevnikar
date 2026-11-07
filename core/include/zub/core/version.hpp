#pragma once

#include <string_view>

namespace zub {

// The application version, as set by project(VERSION) in the top-level CMakeLists.txt.
std::string_view version() noexcept;

} // namespace zub
