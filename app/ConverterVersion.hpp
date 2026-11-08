#pragma once

#include <QString>
#include <filesystem>
#include <optional>
#include <zub/core/converter.hpp>

// The version of a chords_converter program, from "chords_converter --version". The program is
// run synchronously with a 2 s timeout (it answers instantly); the answer is cached per
// (path, modification time), so a rebuilt converter is asked again.

struct ConverterVersionResult {
    enum class State {
        Known,      // `version` is set
        Unparseable, // it ran but printed no version (an older converter, or another program)
        NoAnswer,   // it did not finish within 2 s (cached), or could not be started (not cached)
    };
    State state = State::NoAnswer;
    std::optional<zub::converter::Version> version;
    bool failedToStart = false;
};

ConverterVersionResult converterVersion(const std::filesystem::path& converter);

// Empty when the converter is at least zub::converter::kRequired; else a message for the user
// ("too old", "version unknown").
QString converterProblem(const std::filesystem::path& converter);
