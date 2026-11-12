#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>
#include <zub/core/library.hpp>
#include <zub/core/preview.hpp>
#include <zub/core/textfile.hpp>

namespace fs = std::filesystem;
namespace pv = zub::preview;

namespace {

// Fresh unique directory under the system temp dir, removed on destruction.
struct TempDir {
    fs::path path;

    TempDir() {
        std::random_device device;
        path = fs::temp_directory_path() /
               ("zub-preview-test-" + std::to_string(device()) + "-" + std::to_string(device()));
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

void touch(const fs::path& p, const std::string& content = "x") {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << content;
}

const fs::path dataDir{ZUB_TEST_DATA_DIR};
const fs::path fixtureRepo = dataDir / "songbook";
const fs::path songCopy = "/work/override/AgFlek/BlazniUmirajiNadvakrat.tex";

std::string logFile(const std::string& name) {
    return slurp(dataDir / "logs" / name);
}

pv::Request requestIn(const TempDir& dir) {
    pv::Request r;
    r.songbookRoot = fixtureRepo;
    r.workDir = dir.path / "work";
    r.inputName = "Artist/Title";
    r.songContent = "a\xC2\xA0"
                    "b";
    r.documentClass = "velkyzpevnik";
    return r;
}

} // namespace

TEST_CASE("prepare writes the wrapper, the song copy and the command", "[preview]") {
    TempDir dir;
    const auto request = requestIn(dir);
    const auto work = request.workDir;
    touch(work / "override/Other/Old.tex");
    touch(work / "preview.pdf");

    const auto cmd = pv::prepare(request);
    REQUIRE(cmd);

    CHECK(slurp(work / "preview.tex") ==
          "\\documentclass[chorded, onesongcolumn, noshading]{velkyzpevnik}\n"
          "\\begin{document}\n"
          "\\begin{songs}{}\n"
          "\\input{Artist/Title}\n"
          "\\end{songs}\n"
          "\\end{document}\n");
    CHECK(cmd->songCopy == work / "override" / "Artist" / "Title.tex");
    CHECK(slurp(cmd->songCopy) == "a\xC2\xA0"
                                  "b");
    CHECK_FALSE(fs::exists(work / "override/Other/Old.tex"));
    CHECK_FALSE(fs::exists(work / "override/Other"));
    CHECK_FALSE(fs::exists(work / "preview.pdf"));

    CHECK(cmd->program == "lualatex");
    CHECK(cmd->arguments == std::vector<std::string>{"-shell-escape", "-interaction=nonstopmode",
                                                     "-halt-on-error", "-file-line-error",
                                                     "-jobname=preview", "preview.tex"});
    CHECK(cmd->workingDirectory == work);
    CHECK(cmd->pdf == work / "preview.pdf");
    CHECK(cmd->log == work / "preview.log");

#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    const auto g = [](const fs::path& p) { return p.generic_string(); };
    const std::string root = g(fixtureRepo);
    const std::string expected = std::string(".") + sep + g(work / "override") + sep + root + sep +
                                 root + "/zubuvzpevnik" + sep + root + "/songs/chords" + sep;
    REQUIRE(cmd->environment.size() == 4);
    CHECK(cmd->environment[0].first == "TEXINPUTS");
    CHECK(cmd->environment[0].second == expected);
    CHECK(cmd->environment[1] == std::pair<std::string, std::string>{"max_print_line", "1000"});
    CHECK(cmd->environment[2] == std::pair<std::string, std::string>{"error_line", "254"});
    CHECK(cmd->environment[3] == std::pair<std::string, std::string>{"half_error_line", "238"});
}

TEST_CASE("prepare writes file overrides next to the song copy", "[preview]") {
    TempDir dir;
    auto request = requestIn(dir);
    const auto work = request.workDir;
    touch(work / "override/Artist/Stale.ly");
    request.fileOverrides = {{"Artist/Title.ly", "{ c4 }\n"}, {"Other/Score.ly", "{ d4 }\n"}};

    const auto cmd = pv::prepare(request);
    REQUIRE(cmd);
    CHECK(slurp(work / "override/Artist/Title.ly") == "{ c4 }\n");
    CHECK(slurp(work / "override/Other/Score.ly") == "{ d4 }\n");
    CHECK(slurp(cmd->songCopy) == request.songContent);
    CHECK_FALSE(fs::exists(work / "override/Artist/Stale.ly")); // removed with the old override/

    for (const char* bad : {"../x.ly", "/abs.ly", "a/b.tex", "a/b", "a\\b.ly", ""}) {
        request.fileOverrides = {{bad, "x"}};
        INFO(bad);
        CHECK_FALSE(pv::prepare(request));
    }
}

TEST_CASE("prepare rejects invalid requests", "[preview]") {
    TempDir dir;
    auto request = requestIn(dir);

    SECTION("input name") {
        request.inputName =
            GENERATE(as<std::string>{}, "", "/abs/x", "a/../b", "../a", "a/./b", "a//b", "a/",
                     "a\\b", "x{y}", "a%b", "a#b", "a$b", "a&b", "a~b", "a^b", "a\nb", "a\tb",
                     "C:x/y", "a:b", "a\"b", "a*b", "a?b", "a<b", "a>b", "a|b");
        INFO(request.inputName);
        const auto result = pv::prepare(request);
        REQUIRE_FALSE(result);
        CHECK_FALSE(result.error().empty());
    }
    SECTION("relative work directory") {
        request.workDir = "relative/work";
        const auto result = pv::prepare(request);
        REQUIRE_FALSE(result);
        CHECK(result.error().find("absolute") != std::string::npos);
        CHECK_FALSE(fs::exists("relative/work"));
    }
    SECTION("class") {
        request.documentClass = GENERATE(as<std::string>{}, "bad class", "a/b", "a{b}", "a.b");
        INFO(request.documentClass);
        CHECK_FALSE(pv::prepare(request));
    }
    CHECK_FALSE(fs::exists(request.workDir / "preview.tex"));
}

TEST_CASE("prepare accepts non-ASCII names", "[preview]") {
    TempDir dir;
    auto request = requestIn(dir);
    request.inputName = "\xC5\xBDlut\xC3\xA1/P\xC3\xADse\xC5\x88";
    const auto cmd = pv::prepare(request);
    REQUIRE(cmd);
    CHECK(fs::exists(cmd->songCopy));
}

TEST_CASE("documentClasses lists the cls stems sorted", "[preview]") {
    CHECK(pv::documentClasses(fixtureRepo) == std::vector<std::string>{"alpha", "beta"});
    CHECK(pv::documentClasses(fixtureRepo / "nope").empty());
}

TEST_CASE("firstError on real logs", "[preview]") {
    SECTION("clean") {
        CHECK_FALSE(pv::firstError(logFile("clean.log"), songCopy));
    }
    SECTION("undefined macro") {
        const auto e = pv::firstError(logFile("undefined-macro.log"), songCopy);
        REQUIRE(e);
        CHECK(e->message == "Undefined control sequence.");
        CHECK(e->line == 20);
        CHECK(e->inSong);
        REQUIRE_FALSE(e->context.empty());
        CHECK(e->context[0].find("l.20") != std::string::npos);
    }
    SECTION("missing endverse") {
        const auto e = pv::firstError(logFile("missing-endverse.log"), songCopy);
        REQUIRE(e);
        CHECK(e->message.starts_with("Package songs Error: Encountered \\beginverse"));
        CHECK(e->line == 24);
        CHECK(e->inSong);
    }
    SECTION("unclosed chord") {
        const auto e = pv::firstError(logFile("unclosed-chord.log"), songCopy);
        REQUIRE(e);
        CHECK(e->line == 18);
        CHECK(e->inSong);
    }
    SECTION("missing song") {
        const auto e = pv::firstError(logFile("missing-song.log"), songCopy);
        REQUIRE(e);
        CHECK(e->message.starts_with("LaTeX Error: File `Nobody/NoSuchSong.tex' not found."));
        CHECK(e->file == "./preview.tex");
        CHECK(e->line == 4);
        CHECK_FALSE(e->inSong);
    }
}

TEST_CASE("firstError ignores text that only looks like a location", "[preview]") {
    const std::string noise =
        "luaotfload | db : Reload initiated (formats: otf,ttf,ttc); reason: \"File not found: "
        "\"...\".\n"
        "Package hyperref Warning: Token not allowed in a PDF string (Unicode):\n"
        "Overfull \\hbox: something at line 12: x\n"
        "  /work/override/a.tex:5: indented\n"
        "see page 12: here\n"
        "Some words in a sentence.tex:7: oops\n"
        "/work/x.tex:20:  ==> Fatal error occurred, no output PDF file produced!\n";
    CHECK_FALSE(pv::firstError(noise, songCopy));
}

TEST_CASE("firstError handles synthetic shapes", "[preview]") {
    SECTION("first of the two shapes wins") {
        const auto e = pv::firstError("! Bang.\nl.3 x\n\n/work/override/AgFlek/"
                                      "BlazniUmirajiNadvakrat.tex:9: Later.\n",
                                      songCopy);
        REQUIRE(e);
        CHECK(e->message == "Bang.");
        CHECK(e->line == 3);
        CHECK(e->file.empty());
        CHECK_FALSE(e->inSong);
    }
    SECTION("bang without any location") {
        const auto e = pv::firstError("! Oops.   \n\nnothing here\n", songCopy);
        REQUIRE(e);
        CHECK(e->message == "Oops.");
        CHECK(e->file.empty());
        CHECK_FALSE(e->line);
    }
    SECTION("file-line first, later bang ignored") {
        const auto e = pv::firstError("/work/override/AgFlek/BlazniUmirajiNadvakrat.tex:7: A.  \n"
                                      "l.7 foo   \nbar\n\nnext\n! B.\n",
                                      songCopy);
        REQUIRE(e);
        CHECK(e->message == "A.");
        CHECK(e->line == 7);
        CHECK(e->inSong);
        CHECK(e->context == std::vector<std::string>{"l.7 foo", "bar"});
    }
    SECTION("relative file ending in the song copy") {
        const auto e =
            pv::firstError("./override/AgFlek/BlazniUmirajiNadvakrat.tex:2: X.\n", songCopy);
        REQUIRE(e);
        CHECK(e->inSong);
        const auto other = pv::firstError("./preview.tex:2: X.\n", songCopy);
        REQUIRE(other);
        CHECK_FALSE(other->inSong);
    }
    SECTION("context is limited to six lines") {
        const auto e = pv::firstError("a.tex:1: X.\n1\n2\n3\n4\n5\n6\n7\n8\n", "/b.tex");
        REQUIRE(e);
        CHECK(e->context.size() == 6);
    }
}

#ifndef _WIN32
namespace {

// Runs the prepared command through the shell, discarding output.
int run(const pv::Command& cmd) {
    std::string line = "cd '" + cmd.workingDirectory.string() + "' && ";
    for (const auto& [k, v] : cmd.environment)
        line += k + "='" + v + "' ";
    line += cmd.program;
    for (const auto& a : cmd.arguments)
        line += " " + a;
    line += " >/dev/null 2>&1";
    return std::system(line.c_str());
}

} // namespace
#endif

// Hidden: needs the real ../ZubuvZpevnik checkout and lualatex.
// Run with: build/debug/tests/zub_core_tests "[integration]"
TEST_CASE("a preview of a real song builds, and a broken one reports its line", "[.integration]") {
#ifdef _WIN32
    SKIP("POSIX shell only");
#else
    const fs::path realRepo{ZUB_REAL_SONGBOOK_DIR};
    if (!zub::validateSongbookRepo(realRepo))
        SKIP("no songbook checkout at " << realRepo);
    if (std::system("command -v lualatex >/dev/null 2>&1") != 0)
        SKIP("lualatex is not installed");

    const auto original =
        zub::readTextFile(realRepo / "songs/chords/AgFlek/BlazniUmirajiNadvakrat.tex");
    REQUIRE(original);

    TempDir dir;
    pv::Request request;
    request.songbookRoot = realRepo;
    request.workDir = dir.path / "work";
    request.inputName = "AgFlek/BlazniUmirajiNadvakrat";
    request.songContent = *original;
    request.documentClass = "velkyzpevnik";

    {
        const auto cmd = pv::prepare(request);
        REQUIRE(cmd);
        run(*cmd);
        REQUIRE(fs::exists(cmd->pdf));
        const auto log = zub::readTextFile(cmd->log);
        REQUIRE(log);
        const auto e = pv::firstError(*log, cmd->songCopy);
        if (e)
            UNSCOPED_INFO(e->message << " @ " << e->file << ":" << e->line.value_or(0));
        CHECK_FALSE(e);
    }
    {
        // Break line 20.
        std::string broken;
        int lineNo = 1;
        std::size_t pos = 0;
        while (lineNo < 20 && pos < original->size()) {
            pos = original->find('\n', pos);
            REQUIRE(pos != std::string::npos);
            ++pos;
            ++lineNo;
        }
        broken = original->substr(0, pos) + "\\nonexistentmacro " + original->substr(pos);
        request.songContent = broken;
        const auto cmd = pv::prepare(request);
        REQUIRE(cmd);
        run(*cmd);
        CHECK_FALSE(fs::exists(cmd->pdf));
        const auto log = zub::readTextFile(cmd->log);
        REQUIRE(log);
        const auto e = pv::firstError(*log, cmd->songCopy);
        REQUIRE(e);
        CHECK(e->inSong);
        CHECK(e->line == 20);
    }
#endif
}

TEST_CASE("firstError survives line numbers that do not fit in an int", "[preview]") {
    const auto error = zub::preview::firstError(
        "x.tex:99999999999999999999: overflow\n! Real error.\nl.99999999999999999999 x\n",
        "/work/override/A/B.tex");
    REQUIRE(error);
    CHECK(error->message == "Real error.");
    CHECK_FALSE(error->line);
}

TEST_CASE("prepare removes stale build outputs", "[preview]") {
    TempDir dir;
    const auto request = requestIn(dir);
    fs::create_directories(request.workDir);
    for (const auto* name : {"preview.pdf", "preview.log"})
        std::ofstream(request.workDir / name) << "stale";
    REQUIRE(pv::prepare(request));
    CHECK_FALSE(fs::exists(request.workDir / "preview.pdf"));
    CHECK_FALSE(fs::exists(request.workDir / "preview.log"));
}

TEST_CASE("firstError copes with CRLF logs", "[preview]") {
    const auto e = pv::firstError("! Bang.\r\nl.3 x\r\nmore\r\n\r\nafter\r\n", songCopy);
    REQUIRE(e);
    CHECK(e->message == "Bang.");
    CHECK(e->line == 3);
    CHECK(e->context == std::vector<std::string>{"l.3 x", "more"});
}
