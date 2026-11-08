#pragma once

#include <string_view>
#include <vector>

// A line diff of two texts (used to show what a tidy or the editor changed).
namespace zub::diff {

enum class Op { Same, Removed, Added };

struct Line {
    Op op = Op::Same;
    int oldLine = 0; // 1-based; 0 = not in the old text (Added)
    int newLine = 0; // 1-based; 0 = not in the new text (Removed)
    std::string_view text;
};

// Line diff (LCS; songs are small, O(n*m) is fine). Splits on '\n' and keeps a trailing '\r'
// in `text`; the text views point into the arguments. A final newline does not start an empty
// last line. Removed lines come before the Added lines that replace them.
std::vector<Line> lines(std::string_view before, std::string_view after);

// Where old line `oldLine` is in the new text: its newLine if kept, else the next kept or
// added line after it (e.g. the \insertchorus that replaced a chorus), else the last line
// (0 if the new text is empty).
int mapOldLine(const std::vector<Line>& diff, int oldLine);

} // namespace zub::diff
