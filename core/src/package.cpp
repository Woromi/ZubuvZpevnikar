#include <system_error>
#include <zub/core/package.hpp>

namespace fs = std::filesystem;

namespace zub::package {

bool isPackage(const fs::path& dir) {
    std::error_code ec;
    return fs::is_regular_file(dir / "zubuvzpevnik.sty", ec) &&
           fs::is_regular_file(dir / "scripts" / "build.sh", ec);
}

std::optional<fs::path> ofSongbook(const fs::path& songbookRoot) {
    const fs::path own = songbookRoot / "zubuvzpevnik";
    if (isPackage(own))
        return own;
    return std::nullopt;
}

} // namespace zub::package
