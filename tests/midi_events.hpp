#pragma once

// A minimal Standard MIDI File reader for the tests (the library's own reader is private).
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace testmidi {

struct Ev {
    long tick = 0;
    int status = 0; // high nibble of a channel event; 0xff for meta, 0xf0 for sysex
    int channel = 0;
    int d1 = 0; // data byte 1; the type of a meta event
    int d2 = 0;
    std::string text; // meta and sysex data

    bool noteOn() const { return status == 0x90 && d2 > 0; }
    bool noteOff() const { return status == 0x80 || (status == 0x90 && d2 == 0); }
};

inline std::vector<std::vector<Ev>> parse(std::string_view d) {
    std::vector<std::vector<Ev>> tracks;
    const auto u = [&](std::size_t i) { return static_cast<unsigned char>(d[i]); };
    std::size_t p = 8 + (static_cast<std::size_t>(u(4)) << 24 | u(5) << 16 | u(6) << 8 | u(7));
    while (p + 8 <= d.size()) {
        const std::size_t length =
            static_cast<std::size_t>(u(p + 4)) << 24 | u(p + 5) << 16 | u(p + 6) << 8 | u(p + 7);
        const std::size_t end = p + 8 + length;
        const bool isTrack = d.substr(p, 4) == "MTrk";
        p += 8;
        if (!isTrack) {
            p = end;
            continue;
        }
        std::vector<Ev> track;
        long tick = 0;
        int running = 0;
        const auto vlq = [&] {
            long v = 0;
            while (true) {
                const unsigned b = u(p++);
                v = (v << 7) | (b & 0x7f);
                if (!(b & 0x80))
                    return v;
            }
        };
        while (p < end) {
            Ev e;
            tick += vlq();
            e.tick = tick;
            int status = u(p);
            if (status == 0xff) {
                e.status = 0xff;
                e.d1 = u(p + 1);
                p += 2;
                const long n = vlq();
                e.text = std::string(d.substr(p, static_cast<std::size_t>(n)));
                p += static_cast<std::size_t>(n);
            } else if (status == 0xf0 || status == 0xf7) {
                e.status = 0xf0;
                ++p;
                const long n = vlq();
                e.text = std::string(d.substr(p, static_cast<std::size_t>(n)));
                p += static_cast<std::size_t>(n);
                running = 0;
            } else {
                if (status & 0x80) {
                    running = status;
                    ++p;
                } else {
                    status = running;
                }
                e.status = status & 0xf0;
                e.channel = status & 0x0f;
                e.d1 = u(p++);
                if (e.status != 0xc0 && e.status != 0xd0)
                    e.d2 = u(p++);
            }
            track.push_back(std::move(e));
        }
        tracks.push_back(std::move(track));
        p = end;
    }
    return tracks;
}

// A one-track format 0 file with 384 ticks per quarter from events (tick, status byte, data...),
// written with running status where the status repeats, then the end-of-track meta event.
struct Raw {
    long tick;
    std::vector<int> bytes;
};

inline std::string build(const std::vector<Raw>& events, bool runningStatus = false) {
    std::string track;
    long last = 0;
    int running = 0;
    const auto vlq = [&](long v) {
        std::string b(1, static_cast<char>(v & 0x7f));
        while (v >>= 7)
            b.insert(b.begin(), static_cast<char>((v & 0x7f) | 0x80));
        track += b;
    };
    for (const Raw& e : events) {
        vlq(e.tick - last);
        last = e.tick;
        std::size_t from = 0;
        if (runningStatus && e.bytes[0] == running && e.bytes[0] < 0xf0)
            from = 1;
        running = e.bytes[0] < 0xf0 ? e.bytes[0] : 0;
        for (std::size_t i = from; i < e.bytes.size(); ++i)
            track.push_back(static_cast<char>(e.bytes[i]));
    }
    vlq(0);
    track += std::string("\xff\x2f\x00", 3);
    std::string out = std::string("MThd\0\0\0\6\0\0\0\1\x01\x80", 14);
    out += "MTrk";
    const auto n = track.size();
    out += {static_cast<char>(n >> 24), static_cast<char>(n >> 16), static_cast<char>(n >> 8),
            static_cast<char>(n)};
    out += track;
    return out;
}

} // namespace testmidi
