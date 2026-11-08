#include <algorithm>
#include <map>
#include <optional>
#include <utility>
#include <zub/core/latex.hpp>
#include <zub/core/songoutline.hpp>

namespace zub::songoutline {

namespace {

bool isLetter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// Comments become spaces, so offsets stay those of the text. Same rule as
// latex::stripComments: from an unescaped '%' to the end of the line.
std::string blankComments(std::string_view tex) {
    std::string out(tex);
    bool inComment = false;
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (out[i] == '\n') {
            inComment = false;
        } else if (inComment) {
            out[i] = ' ';
        } else if (out[i] == '\\') {
            ++i; // an escaped character, "\%" included
        } else if (out[i] == '%') {
            inComment = true;
            out[i] = ' ';
        }
    }
    return out;
}

struct Token {
    std::size_t begin; // at the backslash
    std::size_t end;   // after the name (and a '*' if starred is asked for by the caller)
    std::string_view name;
};

// The next control word at or after `from`, or nullopt.
std::optional<Token> nextCommand(std::string_view text, std::size_t from) {
    for (std::size_t i = from; i < text.size(); ++i) {
        if (text[i] != '\\')
            continue;
        if (i + 1 >= text.size())
            return std::nullopt;
        if (!isLetter(text[i + 1])) {
            ++i; // control symbol
            continue;
        }
        std::size_t end = i + 1;
        while (end < text.size() && isLetter(text[end]))
            ++end;
        return Token{i, end, text.substr(i + 1, end - i - 1)};
    }
    return std::nullopt;
}

// After optional spaces (not a blank line), a '[' group: its contents and the position after it.
std::optional<std::pair<std::string_view, std::size_t>> optionalArgument(std::string_view text,
                                                                         std::size_t at) {
    std::size_t i = at;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
        ++i;
    if (i >= text.size() || text[i] != '[')
        return std::nullopt;
    const auto close = latex::findMatching(text, i);
    if (!close)
        return std::nullopt;
    return std::pair{text.substr(i + 1, *close - i - 1), *close + 1};
}

struct PartChords {
    Chords chords;
    bool offPartWay;
};

// The chords of a part's text, starting from `state`: commands before the first lyric or chord
// set up the whole part; a \chordsoff or \chordsoffonsmallpage after it stops them part way.
PartChords chordsIn(std::string_view text, Chords state) {
    PartChords result{state, false};
    const auto blank = [](std::string_view between) {
        return between.find_first_not_of(" \t\r\n") == std::string_view::npos;
    };
    bool started = false;
    std::size_t last = 0;
    for (auto token = nextCommand(text, 0); token; token = nextCommand(text, token->end)) {
        if (!blank(text.substr(last, token->begin - last)))
            started = true;
        last = token->end;
        const bool off = token->name == "chordsoff";
        const bool offSmall = token->name == "chordsoffonsmallpage";
        if (token->name == "chordson" && !started)
            result.chords = Chords::On;
        if (!off && !offSmall)
            continue; // other setup; chords (\[Am]) and words count as text above
        if (started) {
            if (result.chords != Chords::Off)
                result.offPartWay = true;
        } else if (off) {
            result.chords = Chords::Off;
        } else if (result.chords == Chords::On) {
            result.chords = Chords::OffOnSmallPage;
        }
    }
    return result;
}

std::optional<int> number(std::string_view text) {
    int value = 0;
    std::size_t i = 0;
    while (i < text.size() && text[i] == ' ')
        ++i;
    const std::size_t first = i;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9' && value < 100000)
        value = value * 10 + (text[i++] - '0');
    while (i < text.size() && text[i] == ' ')
        ++i;
    if (i == first || i != text.size())
        return std::nullopt;
    return value;
}

std::string kindName(std::string_view kind) {
    static const std::map<std::string_view, std::string_view> names = {
        {"verse", "Verse"},   {"verse*", "Unnumbered verse"}, {"chorus", "Chorus"},
        {"bridge", "Bridge"}, {"prechorus", "Pre-chorus"},    {"intro", "Intro"},
        {"outro", "Outro"},   {"interlude", "Interlude"},     {"instrumental", "Instrumental"},
    };
    if (const auto it = names.find(kind); it != names.end())
        return std::string(it->second);
    return std::string(kind);
}

bool isContinuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

std::size_t codePoints(std::string_view s) {
    return static_cast<std::size_t>(std::ranges::count_if(s, [](char c) { return !isContinuation(c); }));
}

// The first `count` code points of `s` (whole UTF-8 sequences).
std::string_view firstCodePoints(std::string_view s, std::size_t count) {
    std::size_t i = 0;
    for (std::size_t n = 0; i < s.size(); ++i) {
        if (!isContinuation(s[i]) && n++ == count)
            break;
    }
    return s.substr(0, i);
}

// Commands whose arguments are not printed text (file names, part kinds, numbers).
bool dropsArguments(std::string_view name) {
    static constexpr std::string_view names[] = {
        "lilypondfile", "narrowlilypondfile", "usechords", "copychords", "setchordwidth",
        "transpose", "capo", "memorize", "replay", "baselineadj", "addtocounter", "mbar", "role"};
    return std::ranges::find(names, name) != std::end(names);
}

// One source line as printed: chords placed over the text, markup removed.
ShownLine shownLine(std::string_view line) {
    ShownLine shown;
    std::string& text = shown.text;
    const auto space = [&text] {
        if (!text.empty() && text.back() != ' ')
            text += ' ';
    };
    for (std::size_t i = 0; i < line.size();) {
        const char c = line[i];
        if (c == '\\' && i + 1 < line.size() && line[i + 1] == '[') {
            const auto close = latex::findMatching(line, i + 1);
            const std::size_t end = close ? *close : line.size();
            const std::string chord = latex::toDisplayText(line.substr(i + 2, end - i - 2));
            const std::size_t column = codePoints(text);
            std::size_t have = codePoints(shown.chords);
            if (have > 0 && have >= column) {
                shown.chords += ' ';
                ++have;
            }
            shown.chords.append(column > have ? column - have : 0, ' ');
            shown.chords += chord;
            i = close ? *close + 1 : line.size();
        } else if (c == '\\' && i + 1 < line.size() && isLetter(line[i + 1])) {
            std::size_t end = i + 1;
            while (end < line.size() && isLetter(line[end]))
                ++end;
            const std::string_view name = line.substr(i + 1, end - i - 1);
            if (name == "lrep") {
                text += "|: ";
            } else if (name == "rrep") {
                space();
                text += ":|";
            } else if (name == "rep") {
                if (end < line.size() && line[end] == '{') {
                    if (const auto close = latex::findMatching(line, end)) {
                        space();
                        text += "(×" + std::string(line.substr(end + 1, *close - end - 1)) + ")";
                        end = *close + 1;
                    }
                }
            } else {
                // options are never printed text, nor the arguments of some commands
                while (end < line.size() && (line[end] == '[' || (dropsArguments(name) && line[end] == '{'))) {
                    const auto close = latex::findMatching(line, end);
                    if (!close)
                        break;
                    end = *close + 1;
                }
            }
            i = end;
        } else if (c == '\\') {
            if (i + 1 < line.size() && std::string_view("&%#$_").find(line[i + 1]) != std::string_view::npos)
                text += line[i + 1];
            else
                space();
            i += 2;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '~') {
            space();
            ++i;
        } else if (c == '{' || c == '}') {
            ++i;
        } else {
            text += c;
            ++i;
        }
    }
    while (!text.empty() && text.back() == ' ')
        text.pop_back();
    while (!shown.chords.empty() && shown.chords.back() == ' ')
        shown.chords.pop_back();
    if (codePoints(text) > kShownColumns) {
        text.resize(firstCodePoints(text, kShownColumns - 1).size());
        while (!text.empty() && text.back() == ' ')
            text.pop_back();
        text += "…";
    }
    if (codePoints(shown.chords) > kShownColumns) {
        // whole chords only, no "…": the dots belong to the lyrics
        const std::size_t cut = firstCodePoints(shown.chords, kShownColumns).size();
        if (shown.chords[cut] != ' ') {
            const auto gap = shown.chords.rfind(' ', cut);
            shown.chords.resize(gap == std::string::npos ? 0 : gap);
        } else {
            shown.chords.resize(cut);
        }
        while (!shown.chords.empty() && shown.chords.back() == ' ')
            shown.chords.pop_back();
    }
    return shown;
}

std::vector<ShownLine> shownLines(std::string_view body) {
    std::vector<ShownLine> lines;
    for (std::size_t from = 0; from < body.size() && lines.size() < kShownLines;) {
        const auto newline = std::min(body.find('\n', from), body.size());
        auto shown = shownLine(body.substr(from, newline - from));
        if (!shown.text.empty() || !shown.chords.empty())
            lines.push_back(std::move(shown));
        from = newline + 1;
    }
    return lines;
}

std::string label(std::string_view kind, int verse, std::optional<int> chorusNumber) {
    if (kind == "verse")
        return std::to_string(verse) + ".";
    if (kind == "chorus")
        return chorusNumber ? "R" + std::to_string(*chorusNumber) + ":" : "R:";
    if (kind == "bridge")
        return "*";
    if (kind == "prechorus")
        return "P:";
    return {};
}

int lineAt(std::string_view text, std::size_t offset) {
    return 1 +
           static_cast<int>(std::count(
               text.begin(),
               text.begin() + static_cast<std::ptrdiff_t>(std::min(offset, text.size())), '\n'));
}

} // namespace

Outline outline(std::string_view songText) {
    const std::string blanked = blankComments(songText);
    const std::string_view text = blanked;
    Outline result;

    std::size_t pos = 0;
    std::size_t stop = text.size();
    if (const auto begin = text.find("\\beginsong"); begin != std::string_view::npos)
        pos = begin + 10;
    if (const auto end = text.find("\\endsong", pos); end != std::string_view::npos)
        stop = end;
    const std::string_view song = text.substr(0, stop);

    Chords songChords = Chords::On;
    int verses = 0;
    std::optional<int> latestChorus;
    std::map<std::string, int> seen; // name key -> how often so far
    std::map<std::optional<int>, std::vector<ShownLine>> choruses; // by number, for repeats

    const auto chorusName = [&](std::optional<int> chorusNumber) {
        const std::string key = "chorus#" + (chorusNumber ? std::to_string(*chorusNumber) : "");
        const int count = ++seen[key];
        if (chorusNumber)
            return "Chorus R" + std::to_string(*chorusNumber) + " (" + std::to_string(count) + ")";
        return "Chorus " + std::to_string(count);
    };

    for (auto token = nextCommand(song, pos); token; token = nextCommand(song, token->end)) {
        const std::string_view name = token->name;
        if (name == "chordsoff") {
            songChords = Chords::Off;
        } else if (name == "chordson") {
            songChords = Chords::On;
        } else if (name == "chordsoffonsmallpage") {
            if (songChords == Chords::On)
                songChords = Chords::OffOnSmallPage;
        } else if (name == "smalleronsmallpage") {
            result.smallerOnSmallPage = true;
        } else if (name == "insertchorus") {
            Part part;
            part.kind = "chorus";
            part.begin = token->begin;
            std::size_t after = token->end;
            const bool star = after < song.size() && song[after] == '*';
            if (star)
                ++after;
            part.repeat = star ? Repeat::Reminder : Repeat::Full;
            std::optional<int> chorusNumber = latestChorus;
            Chords chords = star ? Chords::Off : songChords;
            if (const auto first = optionalArgument(song, after)) {
                if (const auto n = number(first->first))
                    chorusNumber = n;
                after = first->second;
                if (const auto setup = optionalArgument(song, after)) {
                    after = setup->second;
                    if (!star)
                        chords = chordsIn(setup->first, chords).chords;
                }
            }
            part.chords = chords;
            part.name = chorusName(chorusNumber);
            part.label = label("chorus", 0, chorusNumber);
            if (const auto it = choruses.find(chorusNumber); it != choruses.end()) {
                part.shown = it->second;
                if (star && !part.shown.empty()) {
                    part.shown.resize(1);
                    part.shown[0].chords.clear();
                    auto& first = part.shown[0].text;
                    if (first.ends_with("…"))
                        first.resize(first.size() - std::string_view("…").size());
                    first += "...";
                }
            }
            part.end = after;
            part.line = lineAt(text, part.begin);
            token->end = after;
            result.parts.push_back(std::move(part));
        } else if (name.size() > 5 && name.starts_with("begin")) {
            Part part;
            std::string kind(name.substr(5));
            std::size_t after = token->end;
            const bool star = after < song.size() && song[after] == '*';
            if (star)
                ++after;
            const std::string endCommand = "\\end" + kind;
            part.kind = star ? kind + "*" : kind;
            std::optional<int> chorusNumber;
            if (kind == "chorus") {
                if (const auto arg = optionalArgument(song, after)) {
                    if (const auto n = number(arg->first)) {
                        chorusNumber = n;
                        after = arg->second;
                    }
                }
            }
            // \endverse but not \endverseA. Without one, the part ends where the next one
            // starts.
            std::size_t endAt = song.size();
            bool terminated = false;
            for (std::size_t from = after;;) {
                const auto found = song.find(endCommand, from);
                if (found == std::string_view::npos)
                    break;
                const std::size_t next = found + endCommand.size();
                if (next >= song.size() || !isLetter(song[next])) {
                    endAt = found;
                    terminated = true;
                    break;
                }
                from = next;
            }
            if (!terminated) {
                for (auto next = nextCommand(song, after); next; next = nextCommand(song, next->end)) {
                    if ((next->name.size() > 5 && next->name.starts_with("begin")) ||
                        next->name == "insertchorus") {
                        endAt = next->begin;
                        break;
                    }
                }
            }
            const std::string_view body = song.substr(after, endAt - after);
            for (std::size_t from = 0; from < body.size();) {
                const auto newline = std::min(body.find('\n', from), body.size());
                const auto shown = shownLine(body.substr(from, newline - from));
                if (!shown.text.empty() || !shown.chords.empty())
                    ++part.lines;
                from = newline + 1;
            }
            const PartChords chords = chordsIn(body, songChords);
            part.chords = chords.chords;
            part.chordsOffPartWay = chords.offPartWay;
            part.shown = shownLines(body);
            if (kind == "chorus") {
                latestChorus = chorusNumber;
                part.name = chorusName(chorusNumber);
                choruses[chorusNumber] = part.shown;
            } else if (part.kind == "verse") {
                part.name = "Verse " + std::to_string(++verses);
            } else {
                part.name = kindName(part.kind) + " " + std::to_string(++seen[part.kind]);
            }
            part.label = label(part.kind, verses, chorusNumber);
            part.begin = token->begin;
            part.end = terminated ? endAt + endCommand.size() : endAt;
            if (part.end <= part.begin)
                part.end = after;
            part.line = lineAt(text, part.begin);
            token->end = std::max(token->end, part.end); // go on after the part
            result.parts.push_back(std::move(part));
        }
    }
    return result;
}

namespace {

// By position; edits at the same position keep their order.
std::vector<songsuggest::Edit> sortedEdits(std::span<const songsuggest::Edit> edits) {
    std::vector<songsuggest::Edit> sorted(edits.begin(), edits.end());
    std::ranges::stable_sort(sorted, {}, [](const songsuggest::Edit& e) {
        return std::pair{e.start, e.end};
    });
    return sorted;
}

std::expected<std::string, std::string> applySorted(std::string_view text,
                                                    const std::vector<songsuggest::Edit>& sorted) {
    std::string result;
    std::size_t at = 0;
    for (const auto& edit : sorted) {
        if (edit.start < at || edit.end < edit.start || edit.end > text.size())
            return std::unexpected("the proposal's edits do not fit the song");
        result.append(text.substr(at, edit.start - at));
        result += edit.replacement;
        at = edit.end;
    }
    result.append(text.substr(at));
    return result;
}

bool sameLook(const Part& a, const Part& b) {
    return a.chords == b.chords && a.repeat == b.repeat && a.chordsOffPartWay == b.chordsOffPartWay;
}

} // namespace

std::expected<std::string, std::string> applyEdits(std::string_view text,
                                                   std::span<const songsuggest::Edit> edits) {
    return applySorted(text, sortedEdits(edits));
}

std::expected<Preview, std::string> preview(std::string_view before,
                                            std::span<const songsuggest::Edit> edits,
                                            std::span<const songsuggest::Source> sources) {
    const auto sorted = sortedEdits(edits);
    auto afterText = applySorted(before, sorted);
    if (!afterText)
        return std::unexpected(afterText.error());
    const Outline old = outline(before);
    Preview result;
    result.after = outline(*afterText);
    auto& parts = result.after.parts;
    result.changed.assign(parts.size(), false);

    // Where each replacement lands in the new text, and the length change up to its end.
    struct Landed {
        std::size_t start;
        std::size_t end;
        long long shiftAfter;
        const songsuggest::Edit* edit;
    };
    std::vector<Landed> landed;
    long long shift = 0;
    for (const auto& edit : sorted) {
        const auto start = static_cast<std::size_t>(static_cast<long long>(edit.start) + shift);
        shift += static_cast<long long>(edit.replacement.size()) -
                 static_cast<long long>(edit.end - edit.start);
        landed.push_back({start, start + edit.replacement.size(), shift, &edit});
    }

    for (std::size_t i = 0; i < parts.size(); ++i) {
        // The part's start in the editor's text: behind the edits wholly before it, or the start
        // of the edit whose replacement it begins in.
        std::size_t oldBegin = parts[i].begin;
        for (const auto& l : landed) {
            if (l.end <= parts[i].begin) {
                oldBegin = static_cast<std::size_t>(static_cast<long long>(parts[i].begin) -
                                                    l.shiftAfter);
            } else {
                if (l.start <= parts[i].begin)
                    oldBegin = l.edit->start;
                break;
            }
        }
        parts[i].line = lineAt(before, oldBegin);
    }

    for (const auto& l : landed) {
        bool inPart = false;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            const bool touches = l.start < l.end
                                     ? l.start < parts[i].end && l.end > parts[i].begin
                                     : l.start > parts[i].begin && l.start < parts[i].end;
            if (touches) {
                result.changed[i] = true;
                inPart = true;
            }
        }
        if (!inPart)
            result.songChanged = true;
    }
    // A setting between parts changes the parts after it too.
    result.linesBefore.assign(parts.size(), -1);
    if (old.parts.size() == parts.size()) {
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (!sameLook(old.parts[i], parts[i]))
                result.changed[i] = true;
            result.linesBefore[i] = old.parts[i].lines;
        }
    }
    // A source's start in the new text: behind the edits wholly before it.
    result.source.assign(parts.size(), false);
    for (const auto& source : sources) {
        long long moved = 0;
        for (const auto& edit : sorted) {
            if (edit.end > source.start)
                break;
            moved += static_cast<long long>(edit.replacement.size()) -
                     static_cast<long long>(edit.end - edit.start);
        }
        const long long at = static_cast<long long>(source.start) + moved;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            const auto begin = static_cast<long long>(parts[i].begin);
            const auto end = static_cast<long long>(parts[i].end);
            if (at >= begin && at < end)
                result.source[i] = true;
        }
    }
    result.smallerChanged = old.smallerOnSmallPage != result.after.smallerOnSmallPage;
    if (result.smallerChanged)
        result.songChanged = true;
    return result;
}

} // namespace zub::songoutline
