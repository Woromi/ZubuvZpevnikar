#include <zub/core/song.hpp>
#include <zub/core/wishlist.hpp>

namespace zub::wishlist {

namespace {

using std::string;
using std::string_view;

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

string_view trimmed(string_view s) {
    while (!s.empty() && isSpace(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back()))
        s.remove_suffix(1);
    return s;
}

struct Line {
    string_view text; // without its line break
    std::size_t begin, end; // byte range with the line break
};

std::vector<Line> lines(string_view text) {
    std::vector<Line> out;
    std::size_t begin = 0;
    while (begin < text.size()) {
        std::size_t end = text.find('\n', begin);
        const std::size_t next = end == string_view::npos ? text.size() : end + 1;
        if (end == string_view::npos)
            end = text.size();
        out.push_back({text.substr(begin, end - begin), begin, next});
        begin = next;
    }
    return out;
}

} // namespace

std::vector<bookfile::Wish> parse(string_view text) {
    std::vector<bookfile::Wish> out;
    if (text.starts_with("\xEF\xBB\xBF")) // a byte order mark (some editors write one)
        text.remove_prefix(3);
    const auto all = lines(text);
    for (std::size_t i = 0; i < all.size(); ++i) {
        const string_view line = trimmed(all[i].text);
        if (line.empty() || line.front() == '#')
            continue;
        std::size_t nameEnd = 0;
        while (nameEnd < line.size() && !isSpace(line[nameEnd]))
            ++nameEnd;
        string name(line.substr(0, nameEnd));
        if (name.find('/') == string::npos || invalidInputName(name))
            name.clear();
        out.push_back({string(line), std::move(name), i + 1});
    }
    return out;
}

std::expected<string, string> add(string_view text, string_view wish) {
    const string_view w = trimmed(wish);
    if (w.empty())
        return std::unexpected("an empty wish");
    if (w.find('\n') != string_view::npos || w.find('\r') != string_view::npos)
        return std::unexpected("a wish is one line");
    if (w.front() == '#')
        return std::unexpected("a wish cannot start with #");
    const bool crlf = text.find("\r\n") != string_view::npos;
    const string eol = crlf ? "\r\n" : "\n";
    string out(text);
    if (!out.empty() && out.back() != '\n')
        out += eol;
    out += string(w) + eol;
    return out;
}

std::expected<string, string> remove(string_view text, std::size_t line,
                                     string_view expectedText) {
    const auto all = lines(text);
    if (line == 0 || line > all.size())
        return std::unexpected("no line " + std::to_string(line));
    Line l = all[line - 1];
    if (line == 1 && l.text.starts_with("\xEF\xBB\xBF")) { // the byte order mark stays
        l.text.remove_prefix(3);
        l.begin += 3;
    }
    const string_view content = trimmed(l.text);
    if (content.empty() || content.front() == '#' || content != trimmed(expectedText))
        return std::unexpected("the wish list changed: line " + std::to_string(line) +
                               " is not that wish");
    string out(text);
    out.erase(l.begin, l.end - l.begin);
    return out;
}

} // namespace zub::wishlist
