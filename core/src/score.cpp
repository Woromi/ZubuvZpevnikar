#include "zub/core/score.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <system_error>
#include <zub/core/midi.hpp>
#include <zub/core/safety.hpp>
#include <zub/core/song.hpp>
#include <zub/core/textfile.hpp>

namespace fs = std::filesystem;

namespace zub::score {

namespace {

// UTF-8 text of a path (see utf8() in library.cpp).
std::string utf8(const fs::path& p) {
    const auto u8 = p.generic_u8string();
    return {u8.begin(), u8.end()};
}

std::optional<std::string> invalidScoreName(const std::string& name) {
    if (const auto reason = invalidInputName(name))
        return reason;
    if (!name.ends_with(".ly") || name.size() == 3 || name[name.size() - 4] == '/')
        return std::string("must be a .ly file");
    return std::nullopt;
}

std::optional<int> toInt(std::string_view digits) {
    int value = 0;
    const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (ec != std::errc{} || end != digits.data() + digits.size())
        return std::nullopt;
    return value;
}

} // namespace

namespace {

// The approval (zub::safety::lilypondRisks) is given for the score text alone. Appending the
// playback score must not change how anything is read: after a text that ends inside a string,
// a %{ comment or a #'( list, the appended copy of the music would be read differently (text that
// was harmless string or comment content could become Scheme). So the text must end in a neutral
// state (a Scheme form appended after it is seen as Scheme), and the result must have exactly the
// risks of the text alone.
bool appendIsNeutral(const std::string& content, const std::string& withPlayback) {
    const auto risks = safety::lilypondRisks(content);
    auto probe = risks;
    probe.emplace_back("#(zub-sentinel");
    return safety::lilypondRisks(content + "\n#(zub-sentinel)\n") == probe &&
           safety::lilypondRisks(withPlayback) == risks;
}

// The Scheme of the playback score's note list: fixed text, only the path of the output file
// varies. A \\layout of the playback score `\\consists` it in every Voice, TabVoice, ChordNames,
// Devnull (a melody without sound) and Lyrics; for each note event (a lyric event in Lyrics) it
// appends a line "<kind> <moment main num> <den> <moment grace num> <den> <line> <char> <length
// num> <den> <file>" (the origin as in LilyPond's point-and-click links; kind n: a staff's voice,
// c: chord names, m: Devnull, l: lyrics; the length is the event's duration) to `notes` and
// flushes it.
std::string loggerScheme(const fs::path& notes) {
    std::string path;
    for (const char c : utf8(notes)) {
        if (c == '"' || c == '\\')
            path += '\\';
        path += c;
    }
    return "zubNoteLogger = #(lambda (context)\n"
           "  (let* ((port #f)\n"
           "         (kind (case (ly:context-name context)\n"
           "                 ((ChordNames) \"c\") ((Devnull) \"m\") ((Lyrics) \"l\") (else "
           "\"n\")))\n"
           "         (record (lambda (event)\n"
           "           (let* ((origin (ly:event-property event 'origin))\n"
           "                  (where (and (ly:input-location? origin)\n"
           "                              (ly:input-file-line-char-column origin)))\n"
           "                  (now (ly:context-current-moment context))\n"
           "                  (duration (ly:event-property event 'duration))\n"
           "                  (length (if (ly:duration? duration)\n"
           "                              (ly:duration-length duration)\n"
           "                              (ly:make-moment 0))))\n"
           "             (if where\n"
           "               (begin\n"
           "                 (if (not port) (set! port (open-file \"" +
           path +
           "\" \"a\")))\n"
           "                 (format port \"~a ~a ~a ~a ~a ~a ~a ~a ~a ~a\\n\"\n"
           "                   kind\n"
           "                   (ly:moment-main-numerator now) (ly:moment-main-denominator now)\n"
           "                   (ly:moment-grace-numerator now) (ly:moment-grace-denominator now)\n"
           "                   (cadr where) (caddr where)\n"
           "                   (ly:moment-main-numerator length)\n"
           "                   (ly:moment-main-denominator length) (car where))\n"
           "                 (force-output port)))))))\n"
           "    (make-engraver\n"
           "      (listeners\n"
           "        ((note-event engraver event) (record event))\n"
           "        ((lyric-event engraver event) (record event)))\n"
           "      ((finalize engraver) (if port (close-port port))))))\n";
}

} // namespace

std::expected<Command, std::string> prepare(const Request& request) {
    if (const auto reason = invalidScoreName(request.inputName))
        return std::unexpected("invalid score name: " + *reason);
    const auto& work = request.workDir;
    if (!work.is_absolute())
        return std::unexpected("work directory must be an absolute path");
    // The package's lilypond/ (zubuvzpevnik.ily): relative, lilypond would look in its cwd.
    if (!request.includeDir.is_absolute())
        return std::unexpected("no package to engrave the score with (zubuvzpevnik.ily)");
    std::error_code ec;
    fs::create_directories(work, ec);
    if (ec)
        return std::unexpected(utf8(work) + ": " + ec.message());

    const auto scoreDir = work / "score";
    fs::remove_all(scoreDir, ec);
    if (ec)
        return std::unexpected(utf8(scoreDir) + ": " + ec.message());
    // The score that makes LilyPond play the music too goes last: line numbers stay valid.
    std::string content = request.content;
    bool withMidi = false;
    int generatedFromLine = 0;
    std::filesystem::path ampOutput;
    bool allAmp = false;
    if (request.midi) {
        MidiOptions options = *request.midi;
        options.germanNames = request.germanNames;
        // Staves that chose the amp sound in a book of their own (its audio goes through the amp
        // effect); the other staves are muted there, and the amp staves in the main book.
        // Sounding chord names count as one more staff.
        std::size_t staffCount = staves(content).size();
        std::size_t ampStaves = 0;
        for (std::size_t i = 0; i < options.staffInstruments.size() && i < staffCount; ++i)
            if (const auto* instrument = midi::findInstrument(options.staffInstruments[i]))
                ampStaves += instrument->amp ? 1 : 0;
        if (const auto* chords = midi::findInstrument(options.chordInstrument);
            chords && hasChordNames(content)) {
            ++staffCount;
            ampStaves += chords->amp ? 1 : 0;
        }
        if (const auto* melody = midi::findInstrument(options.melodyInstrument);
            melody && hasHiddenMelody(content)) {
            ++staffCount;
            ampStaves += melody->amp ? 1 : 0;
        }
        const bool mixed = ampStaves > 0 && ampStaves < staffCount;
        allAmp = ampStaves > 0 && ampStaves == staffCount;
        options.sounding = mixed ? MidiOptions::Sounding::WithoutAmp : MidiOptions::Sounding::All;
        if (const auto played = midiScore(content, options)) {
            // In a book of its own, so its file name is ours even when the score has its own
            // \midi (that one writes score.midi).
            std::string appended = content;
            if (!appended.empty() && appended.back() != '\n')
                appended += '\n';
            const int firstGenerated = static_cast<int>(std::ranges::count(appended, '\n')) + 1;
            appended += "\\book {\n\\bookOutputName \"" + std::string(playbackOutputName) + "\"\n" +
                        *played + "\n}\n";
            if (mixed) {
                options.sounding = MidiOptions::Sounding::AmpOnly;
                if (const auto amp = midiScore(content, options)) {
                    appended += "\\book {\n\\bookOutputName \"" +
                                std::string(playbackAmpOutputName) + "\"\n" + *amp + "\n}\n";
                    ampOutput = std::string(playbackAmpOutputName) + ".midi";
                }
            }
            if (appendIsNeutral(content, appended)) {
                content = std::move(appended);
                withMidi = true;
                generatedFromLine = firstGenerated;
            } else {
                ampOutput.clear();
                allAmp = false;
            }
        }
    }
    const fs::path copy = scoreDir / fs::path(request.inputName);
    fs::create_directories(copy.parent_path(), ec);
    if (ec)
        return std::unexpected(utf8(copy.parent_path()) + ": " + ec.message());
    if (auto w = writeTextFileAtomic(copy, content); !w)
        return std::unexpected(w.error());

    // LilyPond fails with "cannot change directory" if the output directory is missing.
    const auto outDir = work / "out";
    fs::remove_all(outDir, ec);
    fs::create_directories(outDir, ec);
    if (ec)
        return std::unexpected(utf8(outDir) + ": " + ec.message());

    // zubuvzpevnik.sty loads lyluatex with language=english, and deutsch for Czech and Slovak
    // songs; LilyPond itself would default to Dutch note names.
    std::string wrapper =
        request.germanNames ? "\\language \"deutsch\"\n" : "\\language \"english\"\n";
    if (withMidi) {
        wrapper += "\\include \"" + std::string(playbackLoggerName) + "\"\n";
        if (auto w = writeTextFileAtomic(work / std::string(playbackLoggerName),
                                         loggerScheme(outDir / std::string(playbackNotesName)));
            !w)
            return std::unexpected(w.error());
    }
    wrapper += "\\include \"" + request.inputName + "\"\n";
    if (auto w = writeTextFileAtomic(work / "score.ly", wrapper); !w)
        return std::unexpected(w.error());

    Command cmd;
    cmd.program = "lilypond";
    cmd.arguments = {"-dbackend=svg",
                     "-dcrop",
                     "-I",
                     utf8(request.includeDir),
                     "-I",
                     utf8(scoreDir),
                     "-o",
                     utf8(outDir / "score"),
                     "score.ly"};
    cmd.workingDirectory = work;
    cmd.environment = {{"LC_ALL", "C"}};
    cmd.svg = outDir / "score.cropped.svg";
    if (withMidi) {
        cmd.midi = outDir / (std::string(playbackOutputName) + ".midi");
        if (!ampOutput.empty())
            cmd.ampMidi = outDir / ampOutput;
        cmd.midiIsAmp = allAmp;
        cmd.notes = outDir / std::string(playbackNotesName);
    }
    cmd.scoreCopy = copy;
    cmd.generatedFromLine = generatedFromLine;
    return cmd;
}

bool germanNames(std::string_view lang) {
    // The song's language is the first of a list (lang={czech, hebrew}), as in lokalizace.cfg.
    std::string lower = mainLanguage(lang);
    std::ranges::transform(lower, lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower == "czech" || lower == "slovak";
}

std::vector<Error> withoutGeneratedErrors(std::vector<Error> found, const Command& command) {
    if (command.generatedFromLine <= 0)
        return found;
    const auto copy = command.scoreCopy.lexically_normal();
    std::erase_if(found, [&](const Error& error) {
        return error.line >= command.generatedFromLine && error.file.lexically_normal() == copy;
    });
    return found;
}

std::vector<Error> errors(std::string_view log) {
    constexpr std::string_view marker = ": error: ";
    std::vector<Error> found;
    std::size_t start = 0;
    while (start <= log.size()) {
        auto end = log.find('\n', start);
        if (end == std::string_view::npos)
            end = log.size();
        auto line = log.substr(start, end - start);
        start = end + 1;
        if (line.ends_with('\r'))
            line.remove_suffix(1);
        const auto at = line.find(marker);
        if (at == std::string_view::npos)
            continue;
        // "<file>:<line>:<col>" before the marker, parsed from the right: files may contain ':'.
        const auto head = line.substr(0, at);
        const auto colonCol = head.rfind(':');
        if (colonCol == std::string_view::npos || colonCol == 0)
            continue;
        const auto colonLine = head.rfind(':', colonCol - 1);
        if (colonLine == std::string_view::npos || colonLine == 0)
            continue;
        const auto col = toInt(head.substr(colonCol + 1));
        const auto lineNo = toInt(head.substr(colonLine + 1, colonCol - colonLine - 1));
        if (!col || !lineNo)
            continue;
        Error e;
        e.message = std::string(line.substr(at + marker.size()));
        e.file = fs::path(std::u8string(head.begin(), head.begin() + static_cast<long>(colonLine)));
        e.line = *lineNo;
        e.column = *col;
        found.push_back(std::move(e));
    }
    return found;
}

std::optional<Error> firstError(std::string_view log) {
    auto all = errors(log);
    if (all.empty())
        return std::nullopt;
    return std::move(all.front());
}

NewScore newScore() {
    const std::string before = "\\include \"zubuvzpevnik.ily\"\n"
                               "\n"
                               "\\score {\n"
                               "  <<\n"
                               "  \\new ChordNames {\n"
                               "    \\chordmode {\n"
                               "      c1 |\n"
                               "    }\n"
                               "  }\n"
                               "  \\new TabStaff {\n"
                               "    {\n"
                               "      \\tabFullNotation\n"
                               "      ";
    const std::string after = "\n"
                              "    }\n"
                              "  }\n"
                              ">>\n"
                              "}\n";
    return {before + "<e,\\6 e\\4>1" + after, before.size()};
}

std::optional<std::string> partName(std::string_view typed) {
    std::string part;
    bool wordStart = true;
    for (const char c : typed) {
        if (c == ' ' || c == '\t' || c == '-' || c == '_') {
            wordStart = true;
            continue;
        }
        const bool lower = c >= 'a' && c <= 'z';
        const bool upper = c >= 'A' && c <= 'Z';
        const bool digit = c >= '0' && c <= '9';
        if (!lower && !upper && !digit)
            return std::nullopt;
        part += wordStart && lower ? static_cast<char>(c - 'a' + 'A') : c;
        wordStart = false;
    }
    return part;
}

std::string scoreFileName(std::string_view songStem, std::string_view part) {
    std::string name(songStem);
    name += part;
    name += ".ly";
    return name;
}

} // namespace zub::score
