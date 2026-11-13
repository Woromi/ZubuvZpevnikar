#include "scorescan.hpp"

#include <algorithm>
#include <zub/core/scoreregion.hpp>

namespace zub::scoreregion {

namespace {

namespace sd = zub::fretboard::scan_detail;
using sd::Event;

// One change of the text: `erase` bytes at `pos` become `insert`.
struct Edit {
    std::size_t pos;
    std::size_t erase;
    std::string insert;
};

bool unclosed(std::string_view text, const Event& e) {
    return e.chord && e.closeAt >= text.size();
}

// Where the post-events and articulations of a note or chord start.
std::size_t tailBegin(const Event& e) {
    return e.chord ? e.closeAt + 1 : e.pitchEnd;
}

std::string_view tailOf(std::string_view text, const Event& e) {
    const std::size_t from = std::min(tailBegin(e), e.postEnd);
    return text.substr(from, e.postEnd - from);
}

// The first non-space character at or after `i`.
std::size_t skipSpaces(std::string_view text, std::size_t i) {
    while (i < text.size() && sd::isSpace(text[i]))
        ++i;
    return i;
}

bool isWordAt(std::string_view text, std::size_t i, std::string_view word) {
    return text.substr(i, word.size()) == word &&
           (i + word.size() >= text.size() || !sd::isLetter(text[i + word.size()]));
}

// Is the `\deadNote` command right before the pitch at `pitch`?
std::optional<std::size_t> deadNoteBefore(std::string_view text, std::size_t pitch) {
    constexpr std::string_view command = "\\deadNote";
    std::size_t i = pitch;
    while (i > 0 && sd::isSpace(text[i - 1]))
        --i;
    if (i < command.size() || text.substr(i - command.size(), command.size()) != command)
        return std::nullopt;
    return i - command.size();
}

// The edits, front to back and not overlapping; the selection follows them.
std::string applyEdits(std::string_view text, std::vector<Edit>& edits) {
    std::ranges::stable_sort(edits, {}, &Edit::pos);
    std::string out;
    std::size_t at = 0;
    for (const Edit& edit : edits) {
        out.append(text.substr(at, edit.pos - at));
        out += edit.insert;
        at = edit.pos + edit.erase;
    }
    out.append(text.substr(at));
    return out;
}

Range mapRange(const Range& range, const std::vector<Edit>& edits) {
    Range mapped = range;
    const auto delta = [](const Edit& edit) {
        return static_cast<std::ptrdiff_t>(edit.insert.size()) -
               static_cast<std::ptrdiff_t>(edit.erase);
    };
    std::ptrdiff_t begin = static_cast<std::ptrdiff_t>(range.begin);
    std::ptrdiff_t end = static_cast<std::ptrdiff_t>(range.end);
    for (const Edit& edit : edits) {
        // An insertion at the begin is inside the region; changes wholly before it shift it.
        if (edit.pos + edit.erase <= range.begin && (edit.erase > 0 || edit.pos < range.begin))
            begin += delta(edit);
        if (edit.pos <= range.end)
            end += delta(edit);
    }
    mapped.begin = static_cast<std::size_t>(std::max<std::ptrdiff_t>(begin, 0));
    mapped.end = static_cast<std::size_t>(std::max<std::ptrdiff_t>(end, 0));
    return mapped;
}

Result finish(std::string_view text, std::vector<Edit> edits, Range range) {
    const Range selection = mapRange(range, edits);
    return {applyEdits(text, edits), selection};
}

// `^\strokeDown` / `^\strokeUp` in the tail of `e`: the edits that leave exactly `wanted`.
void strokeEdits(std::string_view text, const Event& e, std::string_view wanted,
                 std::vector<Edit>& edits) {
    const std::size_t from = std::min(tailBegin(e), e.postEnd);
    bool placed = false;
    for (std::size_t i = from; i + 1 < e.postEnd; ++i) {
        if (text[i] != '^' || text[i + 1] != '\\')
            continue;
        for (const std::string_view word : {"strokeDown", "strokeUp"}) {
            if (!isWordAt(text, i + 2, word))
                continue;
            const std::size_t length = 2 + word.size();
            if (placed) { // a second one: gone
                edits.push_back({i, length, {}});
            } else if (word != wanted.substr(2)) {
                edits.push_back({i, length, std::string(wanted)});
            }
            placed = true;
            i += length - 1;
            break;
        }
    }
    if (!placed)
        edits.push_back({e.postEnd, 0, std::string(wanted)});
}

} // namespace

std::optional<std::size_t> offsetOf(std::string_view text, int line, int column) {
    if (line < 1 || column < 0)
        return std::nullopt;
    std::size_t i = 0;
    for (int current = 1; current < line; ++current) {
        const auto nl = text.find('\n', i);
        if (nl == std::string_view::npos)
            return std::nullopt;
        i = nl + 1;
    }
    for (int n = 0; n < column && i < text.size() && text[i] != '\n'; ++n) {
        ++i;
        while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80)
            ++i;
    }
    return i;
}

std::optional<Range> expandToEvents(std::string_view text, Range range) {
    if (range.end < range.begin)
        std::swap(range.begin, range.end);
    const auto events = sd::scan(text, sd::Names::Any);
    std::optional<Range> out;
    for (const Event& e : events) {
        const bool overlaps = range.begin == range.end
                                  ? e.begin <= range.begin && range.begin < e.postEnd
                                  : e.begin < range.end && range.begin < e.postEnd;
        if (!overlaps)
            continue;
        if (!out)
            out = Range{e.begin, e.postEnd};
        else
            out->end = std::max(out->end, e.postEnd);
    }
    return out;
}

std::optional<Result> apply(Action action, std::string_view text, Range range) {
    const auto region = expandToEvents(text, range);
    if (!region)
        return std::nullopt;
    std::vector<Event> inside;
    std::vector<const Event*> notes;
    for (Event& e : sd::scan(text, sd::Names::Any)) {
        if (e.begin >= region->begin && e.postEnd <= region->end)
            inside.push_back(std::move(e));
    }
    for (const Event& e : inside) {
        if (!e.rest && !unclosed(text, e))
            notes.push_back(&e);
    }

    std::vector<Edit> edits;
    switch (action) {
    case Action::Tie:
        if (notes.size() < 2)
            return std::nullopt;
        for (std::size_t i = 0; i + 1 < notes.size(); ++i) {
            const Event& e = *notes[i];
            const std::size_t next = skipSpaces(text, e.postEnd);
            const bool tied = tailOf(text, e).find('~') != std::string_view::npos ||
                              (next < text.size() && text[next] == '~');
            if (!tied)
                edits.push_back({e.postEnd, 0, "~"});
        }
        break;
    case Action::Slur: {
        if (notes.size() < 2)
            return std::nullopt;
        const Event& first = *notes.front();
        const Event& last = *notes.back();
        if (tailOf(text, first).find('(') == std::string_view::npos)
            edits.push_back({first.postEnd, 0, "("});
        if (tailOf(text, last).find(')') == std::string_view::npos)
            edits.push_back({last.postEnd, 0, ")"});
        break;
    }
    case Action::Glissando: {
        if (notes.empty())
            return std::nullopt;
        const Event& first = *notes.front();
        const std::size_t next = skipSpaces(text, first.postEnd);
        if (!isWordAt(text, next, "\\glissando"))
            edits.push_back({first.postEnd, 0, " \\glissando"});
        break;
    }
    case Action::Repeat: {
        std::string open = "\\repeat volta 2 { ";
        std::string close = " }";
        if (region->begin > 0 && !sd::isSpace(text[region->begin - 1]))
            open.insert(0, " ");
        if (region->end < text.size() && !sd::isSpace(text[region->end]))
            close += ' ';
        edits.push_back({region->begin, 0, std::move(open)});
        edits.push_back({region->end, 0, std::move(close)});
        break;
    }
    case Action::StrokeDown:
    case Action::StrokeUp:
        if (notes.empty())
            return std::nullopt;
        for (const Event* e : notes)
            strokeEdits(text, *e, action == Action::StrokeDown ? "^\\strokeDown" : "^\\strokeUp",
                        edits);
        break;
    case Action::DeadNotes: {
        std::vector<std::size_t> pitches;
        for (const Event* e : notes)
            pitches.insert(pitches.end(), e->pitches.begin(), e->pitches.end());
        if (pitches.empty())
            return std::nullopt;
        const bool all = std::ranges::all_of(
            pitches, [&](std::size_t pitch) { return deadNoteBefore(text, pitch).has_value(); });
        for (const std::size_t pitch : pitches) {
            const auto dead = deadNoteBefore(text, pitch);
            if (all)
                edits.push_back({*dead, pitch - *dead, {}});
            else if (!dead)
                edits.push_back({pitch, 0, "\\deadNote "});
        }
        break;
    }
    }
    return finish(text, std::move(edits), *region);
}

} // namespace zub::scoreregion
