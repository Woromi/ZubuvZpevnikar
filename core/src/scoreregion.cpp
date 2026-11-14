#include "scorescan.hpp"

#include <algorithm>
#include <map>
#include <zub/core/fretboard.hpp>
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
    if (e.rest)
        return e.begin + 1; // `r`, `R`, `s`; pitchEnd is not set
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
void sortEdits(std::vector<Edit>& edits) {
    // Front to back; an insertion before an erase at the same place.
    std::ranges::stable_sort(edits, [](const Edit& a, const Edit& b) {
        return a.pos != b.pos ? a.pos < b.pos : a.erase == 0 && b.erase > 0;
    });
}

std::string applyEdits(std::string_view text, std::vector<Edit>& edits) {
    sortEdits(edits);
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

// The `(` and `)` in the tail of `e` (not those in strings): opening - closing, and how many.
int slurBalance(std::string_view text, const Event& e, int& marks) {
    const std::string_view tail = tailOf(text, e);
    int balance = 0;
    for (std::size_t i = 0; i < tail.size(); ++i) {
        if (tail[i] == '"') {
            for (++i; i < tail.size() && tail[i] != '"'; ++i)
                if (tail[i] == '\\')
                    ++i;
        } else if (tail[i] == '(' || tail[i] == ')') {
            balance += tail[i] == '(' ? 1 : -1;
            ++marks;
        }
    }
    return balance;
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

bool isBlank(char c) {
    return c == ' ' || c == '\t';
}

// One `( ) \( \) [ ]` written against an event: [pos, pos + length) with its direction mark.
struct Mark {
    std::size_t event;
    int kind; // 0 slur, 1 phrasing slur, 2 beam
    bool open;
    std::size_t pos, length;
};

// The mark at `i` (after an optional direction character `-^_`): its kind, which end, its length
// without the direction character.
bool markAt(std::string_view text, std::size_t i, int& kind, bool& open, std::size_t& length) {
    const char c = text[i];
    length = 1;
    if (c == '\\' && i + 1 < text.size() && (text[i + 1] == '(' || text[i + 1] == ')')) {
        kind = 1;
        open = text[i + 1] == '(';
        length = 2;
    } else if (c == '(' || c == ')') {
        kind = 0;
        open = c == '(';
    } else if (c == '[' || c == ']') {
        kind = 2;
        open = c == '[';
    } else {
        return false;
    }
    return true;
}

// The marks of `e`: those in its tail, and those that the scan leaves after it (`\(`, `-[`: only
// blanks between them and the event). `reach` is the end of the last one.
void marksOf(std::string_view text, const Event& e, std::size_t index, std::vector<Mark>& out,
             std::size_t& reach) {
    const std::size_t from = std::min(tailBegin(e), e.postEnd);
    const auto add = [&](std::size_t i, std::size_t floor) {
        int kind = 0;
        bool open = false;
        std::size_t length = 0;
        if (!markAt(text, i, kind, open, length))
            return false;
        std::size_t begin = i;
        if (begin > floor && (text[begin - 1] == '-' || text[begin - 1] == '^' ||
                              text[begin - 1] == '_')) {
            --begin;
            ++length;
        }
        out.push_back({index, kind, open, begin, length});
        return true;
    };
    for (std::size_t i = from; i < e.postEnd; ++i) {
        if (text[i] == '"') {
            for (++i; i < e.postEnd && text[i] != '"'; ++i)
                if (text[i] == '\\')
                    ++i;
        } else if (add(i, from) && text[i] == '\\') {
            ++i;
        }
    }
    reach = e.postEnd;
    for (std::size_t i = e.postEnd; i < text.size();) {
        std::size_t j = i;
        while (j < text.size() && isBlank(text[j]))
            ++j;
        const std::size_t mark = j + ((j < text.size() && (text[j] == '-' || text[j] == '^' ||
                                                           text[j] == '_'))
                                          ? 1
                                          : 0);
        if (mark >= text.size() || !add(mark, j))
            break;
        reach = i = out.back().pos + out.back().length;
    }
}

// The `~` in the tail of `e`, or written after blanks right behind it.
std::optional<Edit> tieAfter(std::string_view text, const Event& e, std::size_t limit) {
    const std::string_view tail = tailOf(text, e);
    const std::size_t from = std::min(tailBegin(e), e.postEnd);
    for (std::size_t i = 0; i < tail.size(); ++i) {
        if (tail[i] == '"') {
            for (++i; i < tail.size() && tail[i] != '"'; ++i)
                if (tail[i] == '\\')
                    ++i;
        } else if (tail[i] == '~') {
            return Edit{from + i, 1, {}};
        }
    }
    const std::size_t next = skipSpaces(text, e.postEnd);
    if (next < limit && text[next] == '~')
        return Edit{e.postEnd, next + 1 - e.postEnd, {}};
    return std::nullopt;
}

// The erase of the blanks around the removed [begin, end) (see removeEvents).
Edit eraseWithBlanks(std::string_view text, std::size_t begin, std::size_t end) {
    std::size_t before = 0;
    while (before < begin && isBlank(text[begin - before - 1]))
        ++before;
    std::size_t after = 0;
    while (end + after < text.size() && isBlank(text[end + after]))
        ++after;
    const bool leftEmpty = begin == before || text[begin - before - 1] == '\n';
    const std::size_t rest = end + after;
    const bool rightEmpty = rest >= text.size() || text[rest] == '\n' || text[rest] == '\r';
    std::size_t from = begin;
    std::size_t to = end;
    std::string insert;
    if (leftEmpty && rightEmpty) {
        from = begin - before;
        to = rest;
    } else if (leftEmpty) {
        to = rest;
    } else if (rightEmpty) {
        from = begin - before;
        to = rest;
    } else if (before > 0) {
        if (after == 0 && text[end] == '}')
            from = begin - before;
        else
            to = rest;
    } else if (std::string_view("{<([").find(text[begin - 1]) != std::string_view::npos) {
        to = rest;
    } else if (after == 0 && std::string_view("}>)]").find(text[end]) == std::string_view::npos) {
        insert = " ";
    }
    return {from, to - from, std::move(insert)};
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

std::vector<Range> musicEvents(std::string_view text) {
    std::vector<Range> out;
    for (const Event& e : sd::scan(text, sd::Names::Any)) {
        if (!e.definition)
            out.push_back({e.prefixBegin, e.postEnd});
    }
    return out;
}

std::optional<std::size_t> eventAt(const std::vector<Range>& events, std::size_t offset) {
    // The first event that ends after the offset; it contains it when it has begun.
    const auto it = std::ranges::upper_bound(events, offset, {}, &Range::end);
    if (it == events.end() || it->begin > offset)
        return std::nullopt;
    return static_cast<std::size_t>(it - events.begin());
}

std::size_t caretStop(const std::vector<Range>& events, std::size_t offset) {
    // The first event that ends after the offset.
    return static_cast<std::size_t>(std::ranges::upper_bound(events, offset, {}, &Range::end) -
                                    events.begin());
}

std::size_t stopOffset(const std::vector<Range>& events, std::size_t stop) {
    if (events.empty())
        return 0;
    return stop < events.size() ? events[stop].begin : events.back().end;
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
        if (!overlaps || e.definition)
            continue;
        if (!out)
            out = Range{e.prefixBegin, e.postEnd};
        else
            out->end = std::max(out->end, e.postEnd);
    }
    return out;
}

std::optional<Result> apply(Action action, std::string_view text, Range range, Refusal* why) {
    const auto refuse = [&](Refusal reason) -> std::optional<Result> {
        if (why)
            *why = reason;
        return std::nullopt;
    };
    if (why)
        *why = Refusal::NothingToApply;
    const auto region = expandToEvents(text, range);
    if (!region)
        return std::nullopt;
    std::vector<Event> inside;
    std::vector<const Event*> notes;
    // The slurs left open before the region, from the start of its block.
    int openSlurs = 0;
    const std::size_t blockStart = sd::enclosingBlockStart(text, region->begin);
    for (Event& e : sd::scan(text, sd::Names::Any)) {
        if (e.definition)
            continue;
        if (e.begin >= region->begin && e.postEnd <= region->end) {
            inside.push_back(std::move(e));
        } else if (e.begin >= blockStart && e.postEnd <= region->begin && !e.rest) {
            int marks = 0;
            openSlurs += slurBalance(text, e, marks);
        }
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
        // `~` after a note whose next event (in the region) is a note: never before a rest, and
        // never on the last one.
        for (std::size_t i = 0; i + 1 < inside.size(); ++i) {
            const Event& e = inside[i];
            const Event& following = inside[i + 1];
            if (e.rest || unclosed(text, e) || following.rest || unclosed(text, following))
                continue;
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
        int marks = 0;
        for (const Event* e : notes)
            slurBalance(text, *e, marks);
        const bool done = marks == 2 && tailOf(text, first).find('(') != std::string_view::npos &&
                          tailOf(text, last).find(')') != std::string_view::npos;
        if (!done && (marks > 0 || openSlurs > 0))
            return refuse(Refusal::SlurPresent);
        if (!done) {
            edits.push_back({first.postEnd, 0, "("});
            edits.push_back({last.postEnd, 0, ")"});
        }
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
        if (!sd::bracesBalanced(text.substr(region->begin, region->end - region->begin)))
            return refuse(Refusal::CrossesBlock);
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

std::optional<Result> removeEvents(std::string_view text, Range range, RemoveRefusal* why) {
    const auto refuse = [&](RemoveRefusal reason) -> std::optional<Result> {
        if (why)
            *why = reason;
        return std::nullopt;
    };
    const auto region = expandToEvents(text, range);
    if (!region)
        return refuse(RemoveRefusal::NothingToRemove);
    std::vector<Event> events;
    for (Event& e : sd::scan(text, sd::Names::Any)) {
        if (!e.definition)
            events.push_back(std::move(e));
    }
    std::vector<bool> removed(events.size(), false);
    std::vector<Range> gone;
    std::vector<std::size_t> goneIndex;
    for (std::size_t i = 0; i < events.size(); ++i) {
        const Event& e = events[i];
        if (e.begin < region->begin || e.postEnd > region->end)
            continue;
        if (unclosed(text, e))
            return refuse(RemoveRefusal::UnclosedChord);
        removed[i] = true;
        gone.push_back({e.prefixBegin, e.postEnd}); // widened below
        goneIndex.push_back(i);
    }
    if (gone.empty())
        return refuse(RemoveRefusal::NothingToRemove);

    std::vector<Edit> edits;
    // The partners of slurs and beams that lose one end.
    std::vector<Mark> marks;
    std::vector<std::size_t> reach(events.size());
    for (std::size_t i = 0; i < events.size(); ++i)
        marksOf(text, events[i], i, marks, reach[i]);
    for (std::size_t g = 0; g < gone.size(); ++g)
        gone[g].end = reach[goneIndex[g]]; // with the marks that follow the event
    std::map<int, std::vector<const Mark*>> open;
    for (const Mark& mark : marks) {
        auto& stack = open[mark.kind];
        if (mark.open) {
            stack.push_back(&mark);
        } else if (!stack.empty()) {
            const Mark& partner = *stack.back();
            stack.pop_back();
            if (removed[partner.event] != removed[mark.event]) {
                const Mark& left = removed[mark.event] ? partner : mark;
                edits.push_back({left.pos, left.length, {}});
            }
        }
    }
    // A tie from the event before the range.
    std::size_t before = events.size();
    std::size_t after = events.size();
    for (std::size_t i = 0; i < events.size(); ++i) {
        if (!removed[i] && events[i].postEnd <= region->begin)
            before = i;
        if (!removed[i] && events[i].begin >= region->end && after == events.size())
            after = i;
    }
    if (before < events.size()) {
        if (const auto tie = tieAfter(text, events[before], region->begin))
            edits.push_back(*tie);
    }
    // The blanks around the removed events (a run of events separated by blanks goes together).
    std::size_t first = 0;
    while (first < gone.size()) {
        std::size_t last = first;
        while (last + 1 < gone.size() &&
               std::ranges::all_of(text.substr(gone[last].end, gone[last + 1].begin - gone[last].end),
                                   isBlank))
            ++last;
        edits.push_back(eraseWithBlanks(text, gone[first].begin, gone[last].end));
        first = last + 1;
    }
    // The next event keeps its length.
    if (after < events.size()) {
        const Event& next = events[after];
        const std::string inForce = zub::fretboard::effectiveDuration(text, region->end);
        if (next.durBegin == next.durEnd && !unclosed(text, next) &&
            inForce != zub::fretboard::effectiveDuration(text, region->begin))
            edits.push_back({next.durBegin, 0, inForce});
    }

    sortEdits(edits);
    // Where the removed text began, in the result.
    std::ptrdiff_t cursor = static_cast<std::ptrdiff_t>(region->begin);
    std::ptrdiff_t shift = 0;
    for (const Edit& edit : edits) {
        if (edit.pos >= region->begin)
            break;
        if (edit.pos + edit.erase <= region->begin) {
            shift += static_cast<std::ptrdiff_t>(edit.insert.size()) -
                     static_cast<std::ptrdiff_t>(edit.erase);
        } else {
            cursor = static_cast<std::ptrdiff_t>(edit.pos);
            break;
        }
    }
    const auto at = static_cast<std::size_t>(std::max<std::ptrdiff_t>(cursor + shift, 0));
    return Result{applyEdits(text, edits), {at, at}};
}

} // namespace zub::scoreregion
