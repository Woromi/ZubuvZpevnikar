#include <cstddef>
#include <zub/core/latex.hpp>
#include <zub/core/songtodo.hpp>

namespace zub::todo {

namespace {

constexpr std::string_view kTodo = "TODO";

std::string_view trim(std::string_view text) {
    constexpr std::string_view space = " \t\r";
    const auto first = text.find_first_not_of(space);
    if (first == std::string_view::npos)
        return {};
    return text.substr(first, text.find_last_not_of(space) + 1 - first);
}

// The comment of a line (after its first unescaped '%'), or nullopt-like npos.
std::size_t commentStart(std::string_view line) {
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\')
            ++i; // "\%" is not a comment
        else if (line[i] == '%')
            return i;
    }
    return std::string_view::npos;
}

// The note of a line's TODO comment; empty when the line has none.
std::string_view todoOf(std::string_view line, bool& found) {
    found = false;
    const auto percent = commentStart(line);
    if (percent == std::string_view::npos)
        return {};
    std::string_view comment = line.substr(percent);
    while (!comment.empty() && comment.front() == '%')
        comment.remove_prefix(1);
    comment = trim(comment);
    if (!comment.starts_with(kTodo))
        return {};
    found = true;
    comment.remove_prefix(kTodo.size());
    if (comment.starts_with(':'))
        comment.remove_prefix(1);
    return trim(comment);
}

template <typename F> void forEachLine(std::string_view text, F f) {
    for (std::size_t from = 0; from < text.size();) {
        const auto newline = text.find('\n', from);
        const std::size_t end = newline == std::string_view::npos ? text.size() : newline + 1;
        f(text.substr(from, end - from), from);
        from = end;
    }
}

std::string markLine() {
    return "% TODO: " + std::string(kNotChecked);
}

bool isMarkLine(std::string_view line) {
    return trim(line.substr(0, line.find('\n'))) == markLine();
}

} // namespace

std::vector<std::string> notes(std::string_view song) {
    std::vector<std::string> result;
    forEachLine(song, [&result](std::string_view line, std::size_t) {
        bool found = false;
        const auto note = todoOf(line.substr(0, line.find('\n')), found);
        if (found)
            result.emplace_back(note.empty() ? kTodo : note);
    });
    return result;
}

bool hasNotCheckedMark(std::string_view song) {
    bool has = false;
    forEachLine(song,
                [&has](std::string_view line, std::size_t) { has = has || isMarkLine(line); });
    return has;
}

std::string addNotCheckedMark(std::string_view song) {
    if (hasNotCheckedMark(song))
        return std::string(song);
    const std::string_view newline = song.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
    const std::string line = markLine() + std::string(newline);

    // After the header: \beginsong{title}[options], to the end of the line it closes on. A
    // \beginsong in a comment doesn't count.
    std::size_t at = 0;
    std::size_t begin = song.find("\\beginsong");
    while (begin != std::string_view::npos) {
        const auto lineStart = song.rfind('\n', begin);
        const auto from = lineStart == std::string_view::npos ? 0 : lineStart + 1;
        if (commentStart(song.substr(from, begin - from)) == std::string_view::npos)
            break;
        begin = song.find("\\beginsong", begin + 1);
    }
    if (begin != std::string_view::npos) {
        std::size_t end = begin + 10;
        while (end < song.size() && (song[end] == ' ' || song[end] == '\t'))
            ++end;
        if (end < song.size() && song[end] == '{') {
            if (const auto close = latex::findMatching(song, end))
                end = *close + 1;
        }
        if (end < song.size() && song[end] == '[') {
            if (const auto close = latex::findMatching(song, end))
                end = *close + 1;
        }
        const auto lineEnd = song.find('\n', end);
        if (lineEnd == std::string_view::npos)
            return std::string(song) + std::string(newline) + line;
        at = lineEnd + 1;
    }
    std::string result(song.substr(0, at));
    result += line;
    result += song.substr(at);
    return result;
}

std::string removeNotCheckedMark(std::string_view song) {
    std::string result;
    result.reserve(song.size());
    forEachLine(song, [&result](std::string_view line, std::size_t) {
        if (!isMarkLine(line))
            result += line;
    });
    return result;
}

} // namespace zub::todo
