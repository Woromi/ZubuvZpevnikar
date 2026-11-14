// Pitch bends for the playback: see zub::midi::smoothBends.
#include "zub/core/midi.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>
#include <optional>
#include <utility>

namespace zub::midi {

namespace {

using Tick = std::int64_t;

constexpr int bendRange = 12; // semitones, set with RPN 0

struct Event {
    Tick tick = 0;
    int band = 1;      // 0: before everything else at the tick (the RPN setup)
    long order = 0;    // within a tick: idx * 2 for the file's events, between them for ours
    std::string bytes; // the event without its delta time; channel events with their status byte
    bool removed = false;
    // Channel events:
    int status = 0; // high nibble, 0 for meta and sysex
    int channel = 0;
    int data1 = 0;
    int data2 = 0;
    // Meta events:
    int metaType = -1;
    std::string metaText;
};

struct Chunk {
    std::string type;
    std::string body;
};

bool isNoteOn(const Event& e) {
    return e.status == 0x90 && e.data2 > 0;
}
bool isNoteOff(const Event& e) {
    return e.status == 0x80 || (e.status == 0x90 && e.data2 == 0);
}

std::expected<std::vector<Event>, std::string> parseTrack(std::string_view body) {
    std::vector<Event> events;
    std::size_t i = 0;
    Tick tick = 0;
    int running = 0;
    const auto byteAt = [&](std::size_t at) { return static_cast<unsigned char>(body[at]); };
    const auto vlq = [&](std::size_t& at) -> std::optional<std::uint32_t> {
        std::uint32_t value = 0;
        for (int n = 0; n < 4; ++n) {
            if (at >= body.size())
                return std::nullopt;
            const unsigned b = byteAt(at++);
            value = (value << 7) | (b & 0x7fu);
            if (!(b & 0x80u))
                return value;
        }
        return std::nullopt;
    };
    while (i < body.size()) {
        const auto delta = vlq(i);
        if (!delta || i >= body.size())
            return std::unexpected("truncated MIDI track");
        tick += *delta;
        Event e;
        e.tick = tick;
        e.order = static_cast<long>(events.size()) * 2;
        int status = byteAt(i);
        if (status == 0xff) {
            if (i + 1 >= body.size())
                return std::unexpected("truncated MIDI meta event");
            e.metaType = byteAt(i + 1);
            std::size_t at = i + 2;
            const auto length = vlq(at);
            if (!length || at + *length > body.size())
                return std::unexpected("truncated MIDI meta event");
            e.metaText = std::string(body.substr(at, *length));
            e.bytes = std::string(body.substr(i, at + *length - i));
            i = at + *length;
        } else if (status == 0xf0 || status == 0xf7) {
            std::size_t at = i + 1;
            const auto length = vlq(at);
            if (!length || at + *length > body.size())
                return std::unexpected("truncated MIDI sysex event");
            e.bytes = std::string(body.substr(i, at + *length - i));
            i = at + *length;
            running = 0;
        } else {
            std::size_t first = i;
            if (status & 0x80) {
                running = status;
                ++first;
            } else if (running) {
                status = running;
            } else {
                return std::unexpected("MIDI data byte without a status");
            }
            const int kind = status >> 4;
            if (kind == 0xf)
                return std::unexpected("unsupported MIDI system message");
            const std::size_t count = (kind == 0xc || kind == 0xd) ? 1 : 2;
            if (first + count > body.size())
                return std::unexpected("truncated MIDI channel event");
            e.status = status & 0xf0;
            e.channel = status & 0x0f;
            e.data1 = byteAt(first);
            e.data2 = count == 2 ? static_cast<int>(byteAt(first + 1)) : 0;
            if (e.data1 > 127 || e.data2 > 127)
                return std::unexpected("MIDI data byte out of range");
            e.bytes.push_back(static_cast<char>(status));
            e.bytes.push_back(static_cast<char>(e.data1));
            if (count == 2)
                e.bytes.push_back(static_cast<char>(e.data2));
            i = first + count;
        }
        events.push_back(std::move(e));
    }
    return events;
}

void appendVlq(std::string& out, std::uint64_t value) {
    std::string bytes;
    bytes.push_back(static_cast<char>(value & 0x7f));
    while (value >>= 7)
        bytes.push_back(static_cast<char>((value & 0x7f) | 0x80));
    out.append(bytes.rbegin(), bytes.rend());
}

std::string serializeTrack(std::vector<Event>& events) {
    std::erase_if(events, [](const Event& e) { return e.removed; });
    std::ranges::stable_sort(events, [](const Event& a, const Event& b) {
        return std::tie(a.tick, a.band, a.order) < std::tie(b.tick, b.band, b.order);
    });
    std::string body;
    Tick last = 0;
    for (const Event& e : events) {
        appendVlq(body, static_cast<std::uint64_t>(e.tick - last));
        last = e.tick;
        body += e.bytes;
    }
    return body;
}

std::uint32_t readBe(std::string_view s, std::size_t at, int bytes) {
    std::uint32_t v = 0;
    for (int n = 0; n < bytes; ++n)
        v = (v << 8) | static_cast<unsigned char>(s[at + static_cast<std::size_t>(n)]);
    return v;
}

void appendBe(std::string& out, std::uint32_t v, int bytes) {
    for (int n = bytes - 1; n >= 0; --n)
        out.push_back(static_cast<char>((v >> (8 * n)) & 0xffu));
}

std::optional<int> markerKind(int program) {
    switch (program) {
    case markerBend:
    case markerBendRelease:
    case markerPreBend:
    case markerPreBendRelease:
    case markerSlide:
        return program;
    default:
        return std::nullopt;
    }
}

bool isMarkerName(const std::string& name) {
    return name == "gunshot" || name == "applause" || name == "helicopter" ||
           name == "telephone ring" || name == "seashore";
}

struct Point {
    Tick tick;
    double semis;
};

struct Note {
    int channel = 0;
    int key = 0;
    Tick on = 0;
    Tick off = 0;
    std::size_t onIdx = 0;
    std::size_t offIdx = 0;
    bool closed = false;
    bool removed = false;     // merged into the note before it
    std::vector<Point> curve; // empty: not bent
};

// Looks for a note of `channel` that is still there and ends within 2 ticks before `at`.
Note* noteEndingAt(std::vector<Note>& notes, int channel, Tick at, const Note& not_) {
    Note* found = nullptr;
    for (Note& n : notes) {
        if (&n == &not_ || n.removed || !n.closed || n.channel != channel || n.on >= at)
            continue;
        if (n.off > at || at - n.off > 2)
            continue;
        if (!found || n.on > found->on)
            found = &n;
    }
    return found;
}

// Looks for the first note of `channel` that starts within 3 ticks after `at`.
Note* noteStartingAfter(std::vector<Note>& notes, int channel, Tick at) {
    Note* found = nullptr;
    for (Note& n : notes) {
        if (n.removed || !n.closed || n.channel != channel || n.on < at || n.on - at > 3)
            continue;
        if (!found || n.on < found->on)
            found = &n;
    }
    return found;
}

// Makes `y` part of `x`: x sounds until y's end.
void merge(std::vector<Event>& events, Note& x, Note& y) {
    y.removed = true;
    events[y.onIdx].removed = true;
    events[x.offIdx].removed = true;
    Event& off = events[y.offIdx];
    off.data1 = x.key;
    off.bytes[1] = static_cast<char>(x.key);
    x.offIdx = y.offIdx;
    x.off = y.off;
}

int bendValue(double semis) {
    const double v = 8192.0 + std::round(semis / bendRange * 8192.0);
    return static_cast<int>(std::clamp(v, 0.0, 16383.0));
}

struct Target {
    std::size_t note;
    int kind;
};

// Returns whether anything changed.
bool processTrack(std::vector<Event>& events, Tick ticksPerStep) {
    // Notes, paired by channel and key in order.
    std::vector<Note> notes;
    std::map<int, std::vector<std::size_t>> open; // channel * 128 + key -> notes waiting for an off
    std::map<std::pair<Tick, int>, std::vector<std::size_t>> startingAt;
    std::vector<Tick> markerTicks;
    for (std::size_t i = 0; i < events.size(); ++i) {
        const Event& e = events[i];
        if (isNoteOn(e)) {
            Note n;
            n.channel = e.channel;
            n.key = e.data1;
            n.on = e.tick;
            n.onIdx = i;
            open[e.channel * 128 + e.data1].push_back(notes.size());
            startingAt[{e.tick, e.channel}].push_back(notes.size());
            notes.push_back(n);
        } else if (isNoteOff(e)) {
            auto& waiting = open[e.channel * 128 + e.data1];
            if (waiting.empty())
                continue;
            Note& n = notes[waiting.front()];
            waiting.erase(waiting.begin());
            n.off = e.tick;
            n.offIdx = i;
            n.closed = n.off > n.on;
        }
    }

    // The markers: a program change at the tick of exactly one note-on of its channel.
    std::vector<Target> targets;
    bool changed = false;
    for (Event& e : events) {
        if (e.metaType == 0x04 && isMarkerName(e.metaText)) {
            e.removed = true;
            changed = true;
        } else if (e.status == 0xc0 && markerKind(e.data1)) {
            e.removed = true;
            changed = true;
            const auto at = startingAt.find({e.tick, e.channel});
            if (at != startingAt.end() && at->second.size() == 1)
                targets.push_back({at->second.front(), e.data1});
        }
    }
    std::ranges::stable_sort(targets, [&](const Target& a, const Target& b) {
        return notes[a.note].on < notes[b.note].on;
    });

    for (const Target& t : targets) {
        Note& y = notes[t.note];
        if (y.removed || !y.closed)
            continue;
        Note* x = noteEndingAt(notes, y.channel, y.on, y);
        if (!x)
            continue;
        const double s = y.key - x->key;
        if (std::abs(s) > bendRange)
            continue;
        const Tick start = x->curve.empty() ? x->on : std::max(x->on, x->curve.back().tick);
        const auto lastValue = [&] { return x->curve.empty() ? 0.0 : x->curve.back().semis; };
        const auto add = [&](Tick tick, double semis) {
            if (!x->curve.empty())
                tick = std::max(tick, x->curve.back().tick);
            x->curve.push_back({tick, semis});
        };
        if (t.kind == markerSlide) {
            if (x->curve.empty())
                add(x->on, 0.0);
            const double from = lastValue();
            add(start + (y.on - start) / 4, from);
            add(y.on, s);
            merge(events, *x, y);
            continue;
        }
        const bool pre = t.kind == markerPreBend || t.kind == markerPreBendRelease;
        const bool release = t.kind == markerBendRelease || t.kind == markerPreBendRelease;
        const Tick span = y.off - start;
        if (pre && x->curve.empty()) {
            add(x->on, s);
        } else {
            if (x->curve.empty())
                add(x->on, 0.0);
            const double from = lastValue();
            add(start + span / 10, from);
            add(start + span * 6 / 10, s);
        }
        merge(events, *x, y);
        if (release) {
            Note* n = noteStartingAfter(notes, x->channel, x->off);
            const double back = n ? n->key - x->key : 0.0;
            if (n && n != x && std::abs(back) <= bendRange) {
                const Tick from = x->off;
                const Tick length = n->off - from;
                add(from, s);
                add(from + length * 4 / 10, back);
                merge(events, *x, *n);
            }
        }
    }

    // The pitch bend events.
    std::set<int> channels;
    std::vector<Event> added;
    const auto pitchBend = [&](Tick tick, int channel, double semis, long order) {
        Event e;
        e.tick = tick;
        e.order = order;
        e.status = 0xe0;
        e.channel = channel;
        const int v = bendValue(semis);
        e.data1 = v & 0x7f;
        e.data2 = v >> 7;
        e.bytes = {static_cast<char>(0xe0 | channel), static_cast<char>(e.data1),
                   static_cast<char>(e.data2)};
        added.push_back(std::move(e));
    };
    for (Note& x : notes) {
        if (x.removed || x.curve.empty())
            continue;
        changed = true;
        channels.insert(x.channel);
        const long beforeOn = static_cast<long>(x.onIdx) * 2 - 1;
        const long during = static_cast<long>(x.onIdx) * 2 + 1;
        double sent = x.curve.front().semis;
        pitchBend(x.on, x.channel, sent, beforeOn);
        for (std::size_t k = 1; k < x.curve.size(); ++k) {
            const Point a = x.curve[k - 1];
            const Point b = x.curve[k];
            if (a.semis == b.semis || b.tick <= a.tick)
                continue;
            const Tick steps = std::clamp<Tick>((b.tick - a.tick) / ticksPerStep, 1, 100);
            for (Tick step = 1; step <= steps; ++step) {
                const Tick at = std::min(a.tick + (b.tick - a.tick) * step / steps, x.off - 1);
                const double value = a.semis + (b.semis - a.semis) * static_cast<double>(step) /
                                                   static_cast<double>(steps);
                if (bendValue(value) == bendValue(sent) || at < x.on)
                    continue;
                sent = value;
                pitchBend(at, x.channel, value, during);
            }
        }
        // The next note of the channel starts unbent, unless it is bent itself.
        const Note* next = nullptr;
        for (const Note& n : notes) {
            if (n.removed || n.channel != x.channel || n.on < x.off || &n == &x)
                continue;
            if (!next || n.on < next->on || (n.on == next->on && n.onIdx < next->onIdx))
                next = &n;
        }
        if (next && next->curve.empty())
            pitchBend(next->on, x.channel, 0.0, static_cast<long>(next->onIdx) * 2 - 1);
    }
    long setup = 0;
    for (const int channel : channels) {
        const auto control = [&](int number, int value) {
            Event e;
            e.tick = 0;
            e.band = 0;
            e.order = setup++;
            e.status = 0xb0;
            e.channel = channel;
            e.data1 = number;
            e.data2 = value;
            e.bytes = {static_cast<char>(0xb0 | channel), static_cast<char>(number),
                       static_cast<char>(value)};
            added.push_back(std::move(e));
        };
        control(101, 0); // RPN 0: pitch bend sensitivity
        control(100, 0);
        control(6, bendRange);
        control(38, 0);
        control(101, 127); // RPN null: later data entry changes nothing
        control(100, 127);
    }
    for (Event& e : added)
        events.push_back(std::move(e));
    return changed;
}

} // namespace

std::expected<std::string, std::string> smoothBends(std::string_view midi) {
    if (midi.size() < 14 || midi.substr(0, 4) != "MThd" || readBe(midi, 4, 4) < 6)
        return std::unexpected("not a Standard MIDI File");
    const std::size_t headerLength = readBe(midi, 4, 4);
    const std::uint32_t division = readBe(midi, 12, 2);
    if (division == 0 || (division & 0x8000u))
        return std::unexpected("unsupported MIDI time division");
    if (8 + headerLength > midi.size())
        return std::unexpected("truncated MIDI header");

    std::vector<Chunk> chunks;
    std::size_t at = 8 + headerLength;
    while (at < midi.size()) {
        if (at + 8 > midi.size())
            return std::unexpected("truncated MIDI chunk");
        const std::size_t length = readBe(midi, at + 4, 4);
        if (at + 8 + length > midi.size())
            return std::unexpected("truncated MIDI chunk");
        chunks.push_back(
            {std::string(midi.substr(at, 4)), std::string(midi.substr(at + 8, length))});
        at += 8 + length;
    }

    std::vector<std::vector<Event>> tracks;
    std::uint32_t microsPerQuarter = 500000;
    bool tempoSeen = false;
    for (const Chunk& chunk : chunks) {
        if (chunk.type != "MTrk")
            continue;
        auto track = parseTrack(chunk.body);
        if (!track)
            return std::unexpected(track.error());
        for (const Event& e : *track)
            if (!tempoSeen && e.metaType == 0x51 && e.metaText.size() == 3) {
                microsPerQuarter = readBe(e.metaText, 0, 3);
                tempoSeen = true;
            }
        tracks.push_back(std::move(*track));
    }
    // One bend step is about 10 ms.
    const double ticksPer10ms = 10000.0 / std::max<std::uint32_t>(microsPerQuarter, 1) * division;
    const Tick ticksPerStep = std::max<Tick>(1, std::llround(ticksPer10ms));

    bool changed = false;
    for (auto& track : tracks)
        changed = processTrack(track, ticksPerStep) || changed;
    if (!changed)
        return std::string(midi);

    std::string out(midi.substr(0, 8 + headerLength));
    std::size_t trackNumber = 0;
    for (const Chunk& chunk : chunks) {
        out += chunk.type;
        if (chunk.type != "MTrk") {
            appendBe(out, static_cast<std::uint32_t>(chunk.body.size()), 4);
            out += chunk.body;
            continue;
        }
        const std::string body = serializeTrack(tracks[trackNumber++]);
        appendBe(out, static_cast<std::uint32_t>(body.size()), 4);
        out += body;
    }
    return out;
}

} // namespace zub::midi
