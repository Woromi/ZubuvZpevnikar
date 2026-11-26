#include <algorithm>
#include <cstddef>
#include <variant>
#include <zub/core/json.hpp>
#include <zub/core/latex.hpp>
#include <zub/core/songcheck.hpp>

#include <ranges>

namespace fs = std::filesystem;

namespace zub::songcheck {

namespace {

std::string utf8(const fs::path& p) {
    const auto u8 = p.generic_u8string(); // '/' on every system
    return {u8.begin(), u8.end()};
}

std::string stringOf(const json::Value& object, std::string_view key) {
    const json::Value* value = object.find(key);
    const std::string* text = value ? value->asString() : nullptr;
    return text ? *text : std::string();
}

double numberOf(const json::Value& object, std::string_view key) {
    const json::Value* value = object.find(key);
    if (!value)
        return 0;
    if (const auto* d = std::get_if<double>(&value->data))
        return *d;
    if (const auto i = value->asInt())
        return static_cast<double>(*i);
    return 0;
}

// The tool's object with its "songs" array; an error naming what is wrong.
std::expected<const json::Value::Array*, std::string> songsOf(const json::Value& root,
                                                              std::string_view tool) {
    if (stringOf(root, "tool") != tool)
        return std::unexpected("not the output of " + std::string(tool) + " --json");
    const json::Value* songs = root.find("songs");
    if (!songs || !songs->asArray())
        return std::unexpected(std::string(tool) + " --json: no songs list");
    return songs->asArray();
}

// The lines of `text` with their start offsets.
struct Line {
    std::size_t begin;
    std::string_view text; // without the line break
};
std::vector<Line> linesOf(std::string_view text) {
    std::vector<Line> lines;
    std::size_t at = 0;
    while (at <= text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos)
            end = text.size();
        std::string_view line = text.substr(at, end - at);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        lines.push_back({at, line});
        if (end == text.size())
            break;
        at = end + 1;
    }
    return lines;
}

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    return s;
}

// The kinds a `% nocheck` line names (empty: all); nullopt for another line.
std::optional<std::vector<std::string>> nocheckKinds(std::string_view line) {
    line = trimmed(line);
    if (line.empty() || line.front() != '%')
        return std::nullopt;
    line.remove_prefix(1);
    line = trimmed(line);
    if (!line.starts_with("nocheck"))
        return std::nullopt;
    line.remove_prefix(7);
    if (!line.empty() && line.front() != ' ' && line.front() != '\t')
        return std::nullopt;
    std::vector<std::string> kinds;
    while (!(line = trimmed(line)).empty()) {
        const std::size_t end = line.find_first_of(" \t");
        kinds.emplace_back(line.substr(0, end));
        line = end == std::string_view::npos ? std::string_view() : line.substr(end);
    }
    return kinds;
}

// The index of the header's `key=` line: the first line, between \beginsong and the end of its
// `[...]` options, whose code (before a comment) starts with "key=" or "key =".
std::optional<std::size_t> keyLine(const std::vector<Line>& lines) {
    bool inHeader = false;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string code = latex::stripComments(lines[i].text);
        if (!inHeader) {
            if (code.find("\\beginsong") != std::string::npos)
                inHeader = true;
            else
                continue;
        }
        std::string_view rest = trimmed(code);
        // A key on the \beginsong line itself (a one-line header) has no line of its own.
        if (rest.starts_with("key")) {
            rest.remove_prefix(3);
            if (trimmed(rest).starts_with("="))
                return i;
        }
        if (code.find(']') != std::string::npos && code.find("\\beginsong") == std::string::npos)
            return std::nullopt; // the header ended without a key line
    }
    return std::nullopt;
}

} // namespace

converter::Command scriptCommand(const fs::path& packageDir, std::string_view script,
                                 const fs::path& songbookRoot,
                                 const std::vector<std::string>& extra) {
    converter::Command command{"python3",
                               {utf8(packageDir / "scripts" / std::string(script)), "--songbook",
                                utf8(songbookRoot), "--json"}};
    command.arguments.insert(command.arguments.end(), extra.begin(), extra.end());
    return command;
}

std::expected<std::vector<KeyFinding>, std::string> parseCheckKeys(std::string_view text) {
    const auto root = json::parse(text);
    if (!root)
        return std::unexpected("checkkeys --json: " + root.error());
    const auto songs = songsOf(*root, "checkkeys");
    if (!songs)
        return std::unexpected(songs.error());
    std::vector<KeyFinding> findings;
    for (const json::Value& song : **songs) {
        KeyFinding f;
        f.file = stringOf(song, "file");
        f.stated = stringOf(song, "stated");
        f.detected = stringOf(song, "detected");
        f.kind = stringOf(song, "class");
        f.margin = numberOf(song, "margin");
        if (const json::Value* split = song.find("split"); split && split->asObject())
            f.suggest = stringOf(*split, "suggest");
        if (!f.file.empty())
            findings.push_back(std::move(f));
    }
    return findings;
}

std::vector<std::string> missingFields(const SongIssue& issue) {
    std::string_view list;
    if (issue.code == "bars-without-meter")
        return {"met"};
    if (issue.code == "missing-recommended") {
        constexpr std::string_view prefix = "missing recommended:";
        if (!issue.message.starts_with(prefix))
            return {};
        list = std::string_view(issue.message).substr(prefix.size());
    } else if (issue.code == "missing-required") {
        constexpr std::string_view prefix = "missing required key";
        if (!issue.message.starts_with(prefix))
            return {};
        list = std::string_view(issue.message).substr(prefix.size());
    } else {
        return {};
    }
    std::vector<std::string> fields;
    for (const auto part : std::views::split(list, ',')) {
        std::string field;
        for (const char c : part)
            if (c != ' ' && c != '\t' && c != '\'')
                field += c;
        if (!field.empty())
            fields.push_back(std::move(field));
    }
    return fields;
}

std::expected<std::map<std::string, std::vector<SongIssue>>, std::string>
parseCheckSongs(std::string_view text) {
    const auto root = json::parse(text);
    if (!root)
        return std::unexpected("checksongs --json: " + root.error());
    const auto songs = songsOf(*root, "checksongs");
    if (!songs)
        return std::unexpected(songs.error());
    std::map<std::string, std::vector<SongIssue>> byFile;
    for (const json::Value& song : **songs) {
        const json::Value* issues = song.find("issues");
        if (!issues || !issues->asArray())
            continue;
        auto& list = byFile[stringOf(song, "file")];
        for (const json::Value& issue : *issues->asArray()) {
            SongIssue i;
            i.severity = stringOf(issue, "severity");
            i.code = stringOf(issue, "code");
            i.message = stringOf(issue, "message");
            if (const json::Value* line = issue.find("line"); line && line->asInt())
                i.line = static_cast<int>(*line->asInt());
            list.push_back(std::move(i));
        }
    }
    std::erase_if(byFile,
                  [](const auto& entry) { return entry.first.empty() || entry.second.empty(); });
    return byFile;
}

std::string keyValue(std::string_view detected, bool germanNames) {
    if (detected.empty() || detected[0] < 'A' || detected[0] > 'G')
        return {};
    const char letter = detected[0];
    std::string_view rest = detected.substr(1);
    const bool sharp = rest.starts_with('#');
    if (sharp)
        rest.remove_prefix(1);
    const bool minor = rest == "m";
    if (!rest.empty() && !minor)
        return {};
    std::string note;
    if (sharp && !minor && (letter == 'A' || letter == 'D' || letter == 'G')) {
        // A#, D#, G# majors are written as flats (B♭, E♭, A♭).
        const char flatOf = letter == 'G' ? 'A' : static_cast<char>(letter + 1);
        note = (germanNames && flatOf == 'B') ? std::string("B") : std::string(1, flatOf) + "\\flt";
    } else if (sharp && minor && letter == 'A' && germanNames) {
        note = "B"; // B♭ minor, German naming
    } else if (sharp) {
        note = std::string(1, letter) + "\\shrp";
    } else {
        note = (germanNames && letter == 'B') ? std::string("H") : std::string(1, letter);
    }
    if (!minor)
        return note;
    // "F\shrp m": a blank after a macro, none after a letter ("Hm").
    return note.back() == 't' || note.back() == 'p' ? note + " m" : note + "m";
}

bool keyConfirmed(std::string_view songText) {
    const auto lines = linesOf(songText);
    const auto key = keyLine(lines);
    if (!key)
        return false;
    for (std::size_t i = *key; i > 0; --i) {
        const auto kinds = nocheckKinds(lines[i - 1].text);
        if (!kinds)
            return false;
        if (kinds->empty() || std::ranges::find(*kinds, "key") != kinds->end())
            return true;
    }
    return false;
}

std::optional<std::string> confirmKey(std::string_view songText) {
    const auto lines = linesOf(songText);
    const auto key = keyLine(lines);
    if (!key || keyConfirmed(songText))
        return std::nullopt;
    const Line& line = lines[*key];
    const std::size_t first = line.text.find_first_not_of(" \t");
    const std::size_t indent = first == std::string_view::npos ? line.text.size() : first;
    const bool crlf = songText.find("\r\n") != std::string_view::npos;
    std::string out(songText.substr(0, line.begin));
    out += std::string(line.text.substr(0, indent)) + "% nocheck key" + (crlf ? "\r\n" : "\n");
    out += songText.substr(line.begin);
    return out;
}

} // namespace zub::songcheck
