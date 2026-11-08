#include <zub/core/songsuggest.hpp>

#include <zub/core/json.hpp>

#include <cstddef>
#include <cstdint>

namespace zub::songsuggest {

namespace {

std::string utf8(const std::filesystem::path& p) {
    const auto u8 = p.u8string();
    return {u8.begin(), u8.end()};
}

std::string_view trim(std::string_view text) {
    constexpr std::string_view space = " \t\r\n";
    const std::size_t first = text.find_first_not_of(space);
    if (first == std::string_view::npos)
        return {};
    return text.substr(first, text.find_last_not_of(space) + 1 - first);
}

std::string failure(const char* what, int exitCode, std::string_view stdErr) {
    const auto message = trim(stdErr);
    if (message.empty())
        return std::string("chords_converter ") + what + " exited with code " +
               std::to_string(exitCode);
    return std::string(message);
}

std::unexpected<std::string> badOutput(const std::string& what) {
    return std::unexpected("Unexpected output of chords_converter suggest: " + what);
}

// A required integer member.
std::optional<int> intMember(const json::Value& object, std::string_view key) {
    const json::Value* value = object.find(key);
    if (!value)
        return std::nullopt;
    const auto number = value->asInt();
    if (!number || *number < INT32_MIN || *number > INT32_MAX)
        return std::nullopt;
    return static_cast<int>(*number);
}

std::optional<std::string> stringMember(const json::Value& object, std::string_view key) {
    const json::Value* value = object.find(key);
    if (!value || !value->asString())
        return std::nullopt;
    return *value->asString();
}

std::optional<Measured> readMeasured(const json::Value& object) {
    Measured measured;
    const auto pages = intMember(object, "pages");
    const json::Value* fits = object.find("fits");
    if (!pages || !fits || !fits->asBool())
        return std::nullopt;
    measured.pages = *pages;
    measured.fits = *fits->asBool();
    if (const json::Value* wraps = object.find("wraps"); wraps && !wraps->isNull()) {
        const auto* list = wraps->asArray();
        if (!list)
            return std::nullopt;
        for (const json::Value& wrap : *list) {
            const json::Value* line = wrap.find("line");
            if (!line || line->isNull()) {
                ++measured.chorusWraps;
                continue;
            }
            const auto number = line->asInt();
            if (!number || *number < 0 || *number > INT32_MAX)
                return std::nullopt;
            measured.wrapLines.push_back(static_cast<int>(*number));
        }
    }
    return measured;
}

} // namespace

converter::Command suggestCommand(const std::filesystem::path& converter,
                                  const std::filesystem::path& songFile, std::optional<Paper> fit,
                                  bool measure, const std::filesystem::path& songbookRoot) {
    converter::Command command{utf8(converter), {"suggest", utf8(songFile)}};
    if (fit) {
        command.arguments.push_back("--fit");
        command.arguments.push_back(*fit == Paper::A4 ? "a4" : "a5");
    }
    if (measure)
        command.arguments.push_back("--measure");
    command.arguments.push_back("--songbook");
    command.arguments.push_back(utf8(songbookRoot));
    command.arguments.push_back("--json");
    return command;
}

converter::Command joinCommand(const std::filesystem::path& converter,
                               const std::filesystem::path& songFile,
                               const std::filesystem::path& songbookRoot) {
    return {utf8(converter),
            {"join", utf8(songFile), "--songbook", utf8(songbookRoot), "--json"}};
}

converter::Command applyCommand(const std::filesystem::path& converter,
                                const std::filesystem::path& songFile, std::string_view id) {
    const std::string one(id);
    return applyCommand(converter, songFile, std::span(&one, 1));
}

converter::Command applyCommand(const std::filesystem::path& converter,
                                const std::filesystem::path& songFile,
                                std::span<const std::string> ids) {
    converter::Command command{utf8(converter), {"apply", utf8(songFile)}};
    for (const auto& id : ids) {
        command.arguments.push_back("--id");
        command.arguments.push_back(id);
    }
    command.arguments.push_back("-o");
    command.arguments.push_back("-");
    return command;
}

std::expected<Suggestions, std::string> interpretSuggest(int exitCode, std::string_view stdOut,
                                                         std::string_view stdErr) {
    if (exitCode != 0)
        return std::unexpected(failure("suggest", exitCode, stdErr));
    const auto parsed = json::parse(stdOut);
    if (!parsed)
        return badOutput(parsed.error());
    if (!parsed->asObject())
        return badOutput("not an object");

    Suggestions result;
    if (const json::Value* fits = parsed->find("fits"); fits && !fits->isNull()) {
        if (!fits->asBool())
            return badOutput("\"fits\" is not a boolean");
        result.fits = fits->asBool();
    }
    if (const json::Value* note = parsed->find("note"); note && !note->isNull()) {
        if (!note->asString())
            return badOutput("\"note\" is not a string");
        result.note = *note->asString();
    }
    if (const json::Value* measured = parsed->find("measured"); measured && !measured->isNull()) {
        if (!measured->asObject())
            return badOutput("\"measured\" is not an object");
        for (const auto& [paper, slot] :
             {std::pair{"a4", &result.a4}, std::pair{"a5", &result.a5}}) {
            const json::Value* entry = measured->find(paper);
            if (!entry || entry->isNull())
                continue;
            *slot = readMeasured(*entry);
            if (!*slot)
                return badOutput(std::string("bad \"measured\".\"") + paper + "\"");
        }
    }
    const json::Value* proposals = parsed->find("proposals");
    if (!proposals || !proposals->asArray())
        return badOutput("no \"proposals\" list");
    for (const json::Value& entry : *proposals->asArray()) {
        Proposal proposal;
        const auto id = stringMember(entry, "id");
        const auto description = stringMember(entry, "description");
        const auto stage = intMember(entry, "stage");
        const auto gain = intMember(entry, "gain_lines");
        const auto score = intMember(entry, "score");
        const auto grade = stringMember(entry, "grade");
        if (!id || id->empty() || !description || !stage || !gain || !score || !grade)
            return badOutput("a proposal lacks id, description, stage, gain_lines, score or grade");
        proposal.id = *id;
        proposal.description = *description;
        proposal.stage = *stage;
        proposal.gainLines = *gain;
        proposal.score = *score;
        proposal.grade = *grade;
        proposal.kind = stringMember(entry, "kind").value_or(std::string{});
        proposal.category = stringMember(entry, "category").value_or("fit");
        proposal.line = intMember(entry, "line").value_or(0);
        if (const json::Value* edits = entry.find("edits"); edits && !edits->isNull()) {
            if (!edits->asArray())
                return badOutput("a proposal's \"edits\" is not a list");
            for (const json::Value& edit : *edits->asArray()) {
                const auto start = intMember(edit, "start");
                const auto end = intMember(edit, "end");
                const auto replacement = stringMember(edit, "replacement");
                if (!start || !end || !replacement || *start < 0 || *end < *start)
                    return badOutput("a bad edit in a proposal");
                proposal.edits.push_back({static_cast<std::size_t>(*start),
                                          static_cast<std::size_t>(*end), *replacement});
            }
        }
        if (const json::Value* sources = entry.find("sources"); sources && !sources->isNull()) {
            if (!sources->asArray())
                return badOutput("a proposal's \"sources\" is not a list");
            for (const json::Value& source : *sources->asArray()) {
                const auto line = intMember(source, "line");
                const auto start = intMember(source, "start");
                const auto end = intMember(source, "end");
                if (!line || !start || !end || *start < 0 || *end < *start)
                    return badOutput("a bad source in a proposal");
                proposal.sources.push_back({*line, static_cast<std::size_t>(*start),
                                            static_cast<std::size_t>(*end)});
            }
        }
        result.proposals.push_back(std::move(proposal));
    }
    return result;
}

std::expected<std::string, std::string> interpretApply(int exitCode, std::string_view stdOut,
                                                       std::string_view stdErr) {
    if (exitCode != 0)
        return std::unexpected(failure("apply", exitCode, stdErr));
    if (!stdOut.contains("\\endsong"))
        return std::unexpected("chords_converter apply printed no song");
    return std::string(stdOut);
}

} // namespace zub::songsuggest
