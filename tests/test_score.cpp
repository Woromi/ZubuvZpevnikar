#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>
#include <zub/core/score.hpp>
#include <zub/core/song.hpp>
#include <zub/core/songbook.hpp>
#include <zub/core/textfile.hpp>

namespace fs = std::filesystem;
namespace sc = zub::score;

namespace {

struct TempDir {
    fs::path path;

    TempDir() {
        std::random_device device;
        path = fs::temp_directory_path() /
               ("zub-score-test-" + std::to_string(device()) + "-" + std::to_string(device()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

sc::Request requestIn(const TempDir& dir) {
    sc::Request r;
    r.songbookRoot = "/songbook";
    r.workDir = dir.path / "work";
    r.inputName = "Artist/Title.ly";
    r.content = "{ c4 d e f }\n";
    r.includeDir = "/songbook/zubuvzpevnik/lilypond";
    return r;
}

} // namespace

TEST_CASE("score prepare writes the copy, the wrapper and the command", "[score]") {
    TempDir dir;
    auto request = requestIn(dir);
    const auto work = request.workDir;
    {
        std::error_code ec;
        fs::create_directories(work / "score/Old", ec);
        std::ofstream(work / "score/Old/Stale.ly") << "x";
    }

    const auto cmd = sc::prepare(request);
    REQUIRE(cmd);
    CHECK(slurp(work / "score/Artist/Title.ly") == "{ c4 d e f }\n");
    CHECK_FALSE(fs::exists(work / "score/Old"));
    CHECK(slurp(work / "score.ly") == "\\language \"english\"\n\\include \"Artist/Title.ly\"\n");
    CHECK(fs::is_directory(work / "out"));
    CHECK(cmd->program == "lilypond");
    CHECK(cmd->workingDirectory == work);
    CHECK(cmd->environment == std::vector<std::pair<std::string, std::string>>{{"LC_ALL", "C"}});
    CHECK(cmd->scoreCopy == work / "score/Artist/Title.ly");
    CHECK(cmd->svg == work / "out/score.cropped.svg");
    CHECK(cmd->arguments == std::vector<std::string>{
                                "-dbackend=svg", "-dcrop", "-I", "/songbook/zubuvzpevnik/lilypond",
                                "-I", (work / "score").generic_string(), "-o",
                                (work / "out/score").generic_string(), "score.ly"});

    request.germanNames = true;
    REQUIRE(sc::prepare(request));
    CHECK(slurp(work / "score.ly") == "\\language \"deutsch\"\n\\include \"Artist/Title.ly\"\n");
}

TEST_CASE("score prepare rejects invalid requests", "[score]") {
    TempDir dir;
    auto request = requestIn(dir);
    request.inputName = GENERATE(as<std::string>{}, "", "/abs/x.ly", "a/../b.ly", "Artist/Title",
                                 "Artist/Title.tex", "Artist/.ly", ".ly", "a\"b.ly", "a\\b.ly");
    INFO(request.inputName);
    CHECK_FALSE(sc::prepare(request));

    request = requestIn(dir);
    request.workDir = "relative/dir";
    CHECK_FALSE(sc::prepare(request));
}

TEST_CASE("germanNames follows zubuvzpevnik.sty", "[score]") {
    CHECK(sc::germanNames("czech"));
    CHECK(sc::germanNames("slovak"));
    CHECK(sc::germanNames("{czech}"));
    CHECK(sc::germanNames(" Czech "));
    CHECK_FALSE(sc::germanNames("english"));
    CHECK_FALSE(sc::germanNames(""));
    CHECK_FALSE(sc::germanNames("french"));
}

TEST_CASE("score errors are read from the log", "[score]") {
    const std::string log = "GNU LilyPond 2.24.3\n"
                            "Processing `score.ly'\n"
                            "/w/score/Artist/Title.ly:3:7: warning: unused\n"
                            "/w/score/Artist/Title.ly:12:5: error: syntax error, unexpected '}'\n"
                            "  c4 d\n"
                            "     }\n"
                            "/w/with: colon/X.ly:2:1: error: second\r\n"
                            "fatal error: failed files: \"score.ly\"\n";
    const auto all = sc::errors(log);
    REQUIRE(all.size() == 2);
    CHECK(all[0].file == "/w/score/Artist/Title.ly");
    CHECK(all[0].line == 12);
    CHECK(all[0].column == 5);
    CHECK(all[0].message == "syntax error, unexpected '}'");
    CHECK(all[1].file == "/w/with: colon/X.ly");
    CHECK(all[1].line == 2);
    CHECK(all[1].message == "second");
    CHECK(sc::firstError(log)->line == 12);
    CHECK_FALSE(sc::firstError("Success: compilation successfully completed\n"));
    CHECK_FALSE(sc::firstError("x: error: no position\n"));
}

TEST_CASE("newScore has the cursor inside the TabStaff", "[score]") {
    const auto n = sc::newScore();
    CHECK(n.text.starts_with("\\include \"zubuvzpevnik.ily\""));
    REQUIRE(n.cursor < n.text.size());
    CHECK(n.text.find("\\new TabStaff") < n.cursor);
    CHECK(n.text.find("\\tabFullNotation") < n.cursor);
    CHECK(n.text.find("ChordNames") < n.cursor);
    CHECK(n.text.find('}', n.text.find("\\tabFullNotation")) > n.cursor);
}

// Hidden: needs lilypond and the real ../ZubuvZpevnik checkout.
// Run with: build/debug/tests/zub_core_tests "[integration]"
TEST_CASE("every real score and the new-score template render", "[.integration][score]") {
#ifdef _WIN32
    SKIP("POSIX shell only");
#else
    const fs::path realRepo{ZUB_REAL_SONGBOOK_DIR};
    if (!fs::is_directory(realRepo / "songs"))
        SKIP("no songbook checkout at " << realRepo);
    if (std::system("command -v lilypond >/dev/null 2>&1") != 0)
        SKIP("lilypond is not installed");

    const auto render = [&](const std::string& name, const std::string& content, bool german) {
        // Returns whether it rendered; the log of a failure goes to INFO.
        TempDir dir;
        sc::Request request;
        request.songbookRoot = realRepo;
        request.workDir = dir.path / "work";
        request.inputName = name;
        request.content = content;
        request.germanNames = german;
        request.includeDir = realRepo / "zubuvzpevnik/lilypond";
        const auto cmd = sc::prepare(request);
        REQUIRE(cmd);
        std::string line =
            "cd '" + cmd->workingDirectory.string() + "' && LC_ALL=C " + cmd->program;
        for (const auto& a : cmd->arguments)
            line += " '" + a + "'";
        line += " >'" + (dir.path / "log").string() + "' 2>&1";
        const int status = std::system(line.c_str());
        const bool ok = status == 0 && fs::exists(cmd->svg);
        if (!ok)
            UNSCOPED_INFO(name << (german ? " (deutsch)" : "") << ": " << slurp(dir.path / "log"));
        return ok;
    };

    CHECK(render("New/Score.ly", sc::newScore().text, false));
    CHECK(render("New/Score.ly", sc::newScore().text, true));
    // The songs decide the note names: scores are found through the \lilypondfile references.
    std::map<std::string, bool> germanOf;
    for (const auto& entry : fs::recursive_directory_iterator(realRepo / "songs")) {
        if (!entry.is_regular_file() || entry.path().extension() != ".tex")
            continue;
        const auto text = zub::readTextFile(entry.path());
        REQUIRE(text);
        const auto meta = zub::parseSong(*text);
        if (!meta)
            continue;
        for (const auto& ref : zub::songbook::referencedFiles(*text))
            germanOf[ref] = sc::germanNames(meta->value("lang"));
    }
    int count = 0;
    for (const auto& entry : fs::recursive_directory_iterator(realRepo / "songs")) {
        if (!entry.is_regular_file() || entry.path().extension() != ".ly")
            continue;
        const auto content = zub::readTextFile(entry.path());
        REQUIRE(content);
        const auto rel = fs::relative(entry.path(), entry.path().parent_path().parent_path());
        const auto name = rel.generic_string();
        const auto it = germanOf.find(name);
        // An unreferenced score: either naming may be the right one.
        CHECK((render(name, *content, it != germanOf.end() && it->second) ||
               (it == germanOf.end() && render(name, *content, true))));
        ++count;
    }
    CHECK(count > 0);
#endif
}
