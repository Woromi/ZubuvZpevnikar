#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// What every chords_converter invocation has in common: the command to run (the app starts the
// process) and the program's version.
namespace zub::converter {

struct Command {
    std::string program;
    std::vector<std::string> arguments;
};

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    auto operator<=>(const Version&) const = default;
};

// The oldest converter whose tidy/suggest/apply interface this app uses (0.10.0: shrink, join and
// check; proposals carry score, grade, sources and category; check findings carry a dismiss edit).
inline constexpr Version kRequired{0, 10, 0};

// Reads the output of "chords_converter --version": "chords_converter 0.2.0" (first line; an
// optional "-suffix" after the patch number is ignored). Nullopt for anything else, which is
// what an older converter prints (it has no such option).
std::optional<Version> parseVersion(std::string_view stdOut);

std::string toString(const Version& version); // "0.2.0"

} // namespace zub::converter
