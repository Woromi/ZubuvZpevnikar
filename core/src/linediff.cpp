#include <zub/core/linediff.hpp>

#include <algorithm>
#include <cstddef>

namespace zub::diff {

namespace {

std::vector<std::string_view> splitLines(std::string_view text) {
    std::vector<std::string_view> result;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        if (end == std::string_view::npos) {
            result.push_back(text);
            break;
        }
        result.push_back(text.substr(0, end));
        text.remove_prefix(end + 1);
    }
    return result;
}

} // namespace

std::vector<Line> lines(std::string_view before, std::string_view after) {
    const auto a = splitLines(before);
    const auto b = splitLines(after);
    const std::size_t n = a.size();
    const std::size_t m = b.size();

    // lcs[i][j]: length of the LCS of a[i..] and b[j..]
    std::vector<std::vector<int>> lcs(n + 1, std::vector<int>(m + 1, 0));
    for (std::size_t i = n; i-- > 0;) {
        for (std::size_t j = m; j-- > 0;) {
            lcs[i][j] = a[i] == b[j] ? lcs[i + 1][j + 1] + 1 : std::max(lcs[i + 1][j], lcs[i][j + 1]);
        }
    }

    std::vector<Line> result;
    result.reserve(n + m);
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && a[i] == b[j]) {
            result.push_back({Op::Same, static_cast<int>(i + 1), static_cast<int>(j + 1), a[i]});
            ++i;
            ++j;
        } else if (i < n && (j == m || lcs[i + 1][j] >= lcs[i][j + 1])) {
            result.push_back({Op::Removed, static_cast<int>(i + 1), 0, a[i]});
            ++i;
        } else {
            result.push_back({Op::Added, 0, static_cast<int>(j + 1), b[j]});
            ++j;
        }
    }
    return result;
}

int mapOldLine(const std::vector<Line>& diff, int oldLine) {
    if (oldLine < 1)
        oldLine = 1; // Added lines carry oldLine 0: never match them
    int lastNew = 0;
    bool seen = false;
    for (const Line& line : diff) {
        if (line.newLine > lastNew)
            lastNew = line.newLine;
        if (line.oldLine == oldLine && line.op == Op::Same)
            return line.newLine;
        if (line.oldLine == oldLine)
            seen = true;
        else if (seen && line.newLine > 0)
            return line.newLine; // next kept or added line after the removed one
    }
    return lastNew;
}

} // namespace zub::diff
