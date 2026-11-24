#pragma once

#include <cstdint>
#include <random>
#include <string>

namespace zubtest {

// A suffix for temporary folder names, different in every test process (instead of the POSIX
// getpid, which Windows does not have).
inline const std::string& uniqueSuffix() {
    static const std::string suffix = [] {
        std::random_device device;
        const auto value = (static_cast<std::uint64_t>(device()) << 32) | device();
        return std::to_string(value);
    }();
    return suffix;
}

} // namespace zubtest
