#include <algorithm>
#include <array>
#include <string_view>
#include <system_error>
#include <vector>
#include <zub/core/package.hpp>
#include <zub/core/textfile.hpp>

namespace fs = std::filesystem;

namespace zub::package {

bool isPackage(const fs::path& dir) {
    std::error_code ec;
    return fs::is_regular_file(dir / "zubuvzpevnik.sty", ec) &&
           fs::is_regular_file(dir / "scripts" / "build.sh", ec);
}

namespace {

bool definesIn(const std::string& text) {
    constexpr std::array<std::string_view, 6> definers = {
        "\\newcommand", "\\renewcommand", "\\providecommand", "\\def",
        "\\NewDocumentCommand", "\\DeclareDocumentCommand"};
    std::string_view rest = text;
    while (!rest.empty()) {
        const auto eol = rest.find('\n');
        const std::string_view line = rest.substr(0, eol);
        rest = eol == std::string_view::npos ? std::string_view{} : rest.substr(eol + 1);
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string_view::npos || line[first] == '%')
            continue;
        if (!line.contains("\\transposesong"))
            continue;
        if (std::ranges::any_of(definers, [&](std::string_view d) { return line.contains(d); }))
            return true;
    }
    return false;
}

} // namespace

bool definesTransposesong(const fs::path& dir) {
    std::vector<fs::path> files = {dir / "zubuvzpevnik.sty"};
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir / "feature", ec))
        if (entry.path().extension() == ".cfg")
            files.push_back(entry.path());
    for (const auto& file : files) {
        const auto text = readTextFile(file);
        if (text && definesIn(*text))
            return true;
    }
    return false;
}

} // namespace zub::package
