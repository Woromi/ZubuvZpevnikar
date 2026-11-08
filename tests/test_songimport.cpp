#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <zub/core/safety.hpp>
#include <zub/core/songimport.hpp>
#include <zub/core/textfile.hpp>

namespace fs = std::filesystem;
namespace si = zub::songimport;

namespace {

const fs::path converterData = fs::path(ZUB_TEST_DATA_DIR) / "converter";
const fs::path songbook = fs::path(ZUB_TEST_DATA_DIR) / "songbook";

// Fresh unique directory under the system temp dir, removed on destruction.
struct TempDir {
    fs::path path;

    TempDir() {
        std::random_device device;
        path = fs::temp_directory_path() /
               ("zub-songimport-test-" + std::to_string(device()) + "-" + std::to_string(device()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

std::string slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

struct Capture {
    int code = 0;
    std::string out;
    std::string err;
};

Capture capture(const std::string& name) {
    return {std::stoi(slurp(converterData / (name + ".code"))),
            slurp(converterData / (name + ".out")), slurp(converterData / (name + ".err"))};
}

const fs::path successWorkDir = converterData / "success-workdir";
const fs::path successTex =
    successWorkDir / "out" / "latex" / "KarelKryl" / "BratrickuZavirejVratka.tex";

} // namespace

TEST_CASE("prepare accepts URLs and existing files", "[songimport]") {
    TempDir temp;
    const fs::path file = temp.path / "song.txt";
    std::ofstream(file) << "A - B\n=====\n";
    const fs::path workDir = temp.path / "work" / "run1";

    SECTION("http URL") {
        const auto command = si::prepare({"/usr/bin/chords_converter", workDir, "http://x.cz/s"});
        REQUIRE(command);
        CHECK(command->program == "/usr/bin/chords_converter");
        CHECK(command->arguments == std::vector<std::string>{"http://x.cz/s", "latex"});
        CHECK(command->workingDirectory == workDir);
        CHECK(fs::is_directory(workDir)); // created, parents included
    }
    SECTION("https URL") {
        CHECK(si::prepare({"conv", workDir, "https://x.cz/s"}));
    }
    SECTION("existing file") {
        const auto command = si::prepare({"conv", workDir, file.string()});
        REQUIRE(command);
        CHECK(command->arguments.front() == file.string());
    }
    SECTION("relative file becomes absolute") {
        struct CwdGuard {
            fs::path old = fs::current_path();
            ~CwdGuard() {
                std::error_code ec;
                fs::current_path(old, ec);
            }
        } guard;
        fs::current_path(temp.path);
        const auto command = si::prepare({"conv", workDir, "song.txt"});
        REQUIRE(command);
        const fs::path argument(
            std::u8string(command->arguments.front().begin(), command->arguments.front().end()));
        CHECK(argument.is_absolute());
        CHECK(fs::equivalent(argument, file));
    }
}

TEST_CASE("prepare rejects bad requests", "[songimport]") {
    TempDir temp;
    const fs::path workDir = temp.path / "run";

    SECTION("bare scheme") {
        CHECK_FALSE(si::prepare({"conv", workDir, "http://"}));
        CHECK_FALSE(si::prepare({"conv", workDir, "https://"}));
    }
    SECTION("not a URL and no such file") {
        const auto command = si::prepare({"conv", workDir, "nope.txt"});
        REQUIRE_FALSE(command);
        CHECK(command.error() == "not a web address and not an existing file: nope.txt");
        CHECK_FALSE(fs::exists(workDir));
    }
    SECTION("directory is not a file") {
        CHECK_FALSE(si::prepare({"conv", workDir, temp.path.string()}));
    }
    SECTION("empty source") {
        CHECK_FALSE(si::prepare({"conv", workDir, ""}));
    }
    SECTION("empty converter") {
        CHECK_FALSE(si::prepare({"", workDir, "http://x.cz/s"}));
    }
    SECTION("relative work dir") {
        CHECK_FALSE(si::prepare({"conv", "relative/run", "http://x.cz/s"}));
        CHECK_FALSE(fs::exists("relative/run"));
    }
}

TEST_CASE("interpret reads a successful run", "[songimport]") {
    const auto run = capture("success");
    REQUIRE(run.code == 0);
    const auto imported = si::interpret(run.code, run.out, run.err, successWorkDir);
    REQUIRE(imported);
    CHECK(imported->inputName == "KarelKryl/BratrickuZavirejVratka");
    CHECK(imported->content == slurp(successTex));
    CHECK(imported->content.contains("\\beginsong{Bratříčku, zavírej vrátka}"));
    CHECK(imported->warnings.empty());
}

TEST_CASE("a current converter draft passes the safety check", "[songimport][safety]") {
    // The captured draft has a chord-only intro (instrumental in bars) and an unclosed
    // quotation mark (\textquotedbl{}), so new converter output is built automatically.
    const auto content = slurp(successTex);
    REQUIRE(content.contains("\\textquotedbl{}"));
    REQUIRE(content.contains("\\begininstrumental"));
    CHECK(zub::safety::unknownConstructs(content).empty());
}

TEST_CASE("interpret collects warnings and tolerates CRLF", "[songimport]") {
    const auto imported =
        si::interpret(0,
                      "Saving output to: "
                      "out/latex/KarelKryl/BratrickuZavirejVratka.tex\r\nSuccessfully saved!\r\n",
                      "Warning: first\r\nsomething else\nWarning: second\n", successWorkDir);
    REQUIRE(imported);
    CHECK(imported->inputName == "KarelKryl/BratrickuZavirejVratka");
    CHECK(imported->warnings == std::vector<std::string>{"Warning: first", "Warning: second"});
}

TEST_CASE("interpret classifies the captured failures", "[songimport]") {
    struct Row {
        const char* name;
        si::FailureKind kind;
    };
    const Row rows[] = {
        {"nofile", si::FailureKind::InvalidInput},
        {"unsupported", si::FailureKind::UnsupportedSite},
        {"network", si::FailureKind::Network},
        {"botcheck", si::FailureKind::BotCheck},
        {"botcheckfile", si::FailureKind::BotCheck},
        {"http404", si::FailureKind::Http},
        {"readonly", si::FailureKind::Other},
    };
    for (const auto& row : rows) {
        CAPTURE(row.name);
        const auto run = capture(row.name);
        const auto result = si::interpret(run.code, run.out, run.err, successWorkDir);
        REQUIRE_FALSE(result);
        CHECK(result.error().kind == row.kind);
        CHECK_FALSE(result.error().message.empty());
        CHECK_FALSE(result.error().message.contains("Retrying ("));
    }
}

TEST_CASE("interpret failure messages", "[songimport]") {
    SECTION("retry lines are removed") {
        const auto run = capture("network");
        const auto result = si::interpret(run.code, run.out, run.err, successWorkDir);
        REQUIRE_FALSE(result);
        CHECK(
            result.error().message.starts_with("Could not fetch http://akordy.kytary.cz.invalid"));
        CHECK(result.error().message.contains("Should it keep happening"));
    }
    SECTION("bot check keeps the multi-line advice") {
        const auto run = capture("botcheck");
        const auto result = si::interpret(run.code, run.out, run.err, successWorkDir);
        REQUIRE_FALSE(result);
        CHECK(result.error().message.contains("CHORDS_COOKIE"));
        CHECK(result.error().message.contains("CHORDS_USER_AGENT"));
    }
    SECTION("a failed write") {
        const auto run = capture("readonly");
        CHECK(run.code == 1);
        const auto result = si::interpret(run.code, run.out, run.err, successWorkDir);
        REQUIRE_FALSE(result);
        CHECK(result.error().message.contains("Failed to create file"));
    }
    SECTION("exit 0 without writing the file (older converters)") {
        const auto result = si::interpret(
            0, "Saving output to: out/latex/KarelKryl/BratrickuZavirejVratka.tex\n",
            "Failed to create file: Permission denied (os error 13)\n", successWorkDir);
        REQUIRE_FALSE(result);
        CHECK(result.error().message.contains("Failed to create file"));
    }
    SECTION("empty stderr falls back to the exit code") {
        const auto result = si::interpret(3, "", "", successWorkDir);
        REQUIRE_FALSE(result);
        CHECK(result.error().kind == si::FailureKind::Other);
        CHECK(result.error().message == "chords_converter exited with code 3");
    }
    SECTION("exit 0 without a Saving line") {
        const auto result = si::interpret(0, "Successfully saved!\n", "", successWorkDir);
        REQUIRE_FALSE(result);
        CHECK(result.error().message == "chords_converter did not write the song");
    }
    SECTION("exit 0 and the file is missing") {
        TempDir temp;
        const auto result =
            si::interpret(0, "Saving output to: out/latex/A/B.tex\n", "", temp.path);
        REQUIRE_FALSE(result);
        CHECK(result.error().kind == si::FailureKind::Other);
    }
    SECTION("an HTTP status needs a digit") {
        const auto result =
            si::interpret(1, "", "the site answered with something odd\n", successWorkDir);
        REQUIRE_FALSE(result);
        CHECK(result.error().kind == si::FailureKind::Other);
    }
}

TEST_CASE("interpret ignores a dumped page on stdout when no parser exists", "[songimport]") {
    const auto result =
        si::interpret(1, "<html><body>Saving output to: nonsense</body></html>\n",
                      "Parser Error: No parser available for URL: http://x.cz/\n", successWorkDir);
    REQUIRE_FALSE(result);
    CHECK(result.error().kind == si::FailureKind::UnsupportedSite);
}

TEST_CASE("interpret rejects unexpected output paths", "[songimport]") {
    for (const char* path :
         {"out/latex/KarelKryl.tex", "out/latex/a/b/c.tex", "/etc/passwd", "out/latex/../x/y.tex",
          "out/latex/A/B.txt", "out/pdf/A/B.tex", "out/latex/A/.tex", "out/latex//B.tex"}) {
        CAPTURE(path);
        const auto result =
            si::interpret(0, std::string("Saving output to: ") + path + "\n", "", successWorkDir);
        REQUIRE_FALSE(result);
        CHECK(result.error().kind == si::FailureKind::Other);
        CHECK(result.error().message.contains("unexpected output path"));
    }
}

TEST_CASE("destination", "[songimport]") {
    SECTION("no such songbook") {
        const auto dest = si::destination("/book", "Abba/OurLastSummer");
        CHECK(dest.path == fs::path("/book/songs/chords/Abba/OurLastSummer.tex"));
        CHECK(dest.inputName == "Abba/OurLastSummer");
    }
    SECTION("reuses the spelling of an existing artist folder") {
        const auto dest = si::destination(songbook, "ABBA/NewSong");
        CHECK(dest.path == songbook / "songs" / "chords" / "Abba" / "NewSong.tex");
        CHECK(dest.inputName == "Abba/NewSong");
    }
    SECTION("unknown artist keeps its spelling") {
        const auto dest = si::destination(songbook, "NoBody/NewSong");
        CHECK(dest.path == songbook / "songs" / "chords" / "NoBody" / "NewSong.tex");
        CHECK(dest.inputName == "NoBody/NewSong");
    }
}

TEST_CASE("existingSong", "[songimport]") {
    SECTION("case-insensitive match") {
        const auto found = si::existingSong(songbook, "abba/ourlastsummer");
        REQUIRE(found);
        CHECK(*found == songbook / "songs" / "chords" / "Abba" / "OurLastSummer.tex");
    }
    SECTION("exact match") {
        CHECK(si::existingSong(songbook, "Abba/OurLastSummer"));
    }
    SECTION("no such song") {
        CHECK_FALSE(si::existingSong(songbook, "Abba/Nope"));
        CHECK_FALSE(si::existingSong(songbook, "Nobody/OurLastSummer"));
    }
    SECTION("malformed names") {
        CHECK_FALSE(si::existingSong(songbook, "bad"));
        CHECK_FALSE(si::existingSong(songbook, ""));
        CHECK_FALSE(si::existingSong(songbook, "a/b/c"));
        CHECK_FALSE(si::existingSong(songbook, "Abba/"));
        CHECK_FALSE(si::existingSong(songbook, "/OurLastSummer"));
    }
    SECTION("missing songbook") {
        CHECK_FALSE(si::existingSong(songbook / "nope", "Abba/OurLastSummer"));
    }
}

TEST_CASE("the real converter imports a text file", "[.integration][songimport]") {
#ifdef _WIN32
    SKIP("runs the converter through a POSIX shell");
#else
    const char* env = std::getenv("ZUB_CONVERTER");
    if (env == nullptr || !fs::exists(env)) {
        SKIP("set ZUB_CONVERTER to a built chords_converter");
    }
    TempDir temp;
    const fs::path input = temp.path / "in.txt";
    std::ofstream(input) << "Karel Kryl - Bratříčku, zavírej vrátka\n"
                            "======================================\n\n"
                            "[Ami]Lampióne, sví[G]tíš nad vodou,\n";
    const fs::path workDir = temp.path / "work";
    const auto command = si::prepare({env, workDir, input.string()});
    REQUIRE(command);

    const auto line = "cd '" + workDir.string() + "' && '" + command->program + "' '" +
                      command->arguments[0] + "' latex >stdout.txt 2>stderr.txt";
    REQUIRE(std::system(line.c_str()) == 0);

    const auto imported =
        si::interpret(0, slurp(workDir / "stdout.txt"), slurp(workDir / "stderr.txt"), workDir);
    REQUIRE(imported);
    CHECK(imported->inputName == "KarelKryl/BratrickuZavirejVratka");
    CHECK(imported->content.contains("\\beginsong{Bratříčku, zavírej vrátka}"));
#endif
}

TEST_CASE("interpret rejects names the songbook cannot use", "[songimport]") {
    const auto result =
        si::interpret(0, "Saving output to: out/latex/A%/B.tex\n", "", successWorkDir);
    REQUIRE_FALSE(result);
    CHECK(result.error().kind == si::FailureKind::Other);
    CHECK(result.error().message ==
          "the converter produced a name the songbook cannot use: A%/B (contains a character "
          "that is not allowed)");
}

TEST_CASE("interpret recognizes a page without a song", "[songimport]") {
    const std::string warning = "Warning: no song found on http://x.cz/s.";
    SECTION("by the warning") {
        const auto result = si::interpret(0, "Saving output to: out/latex/KarelKryl/X.tex\n",
                                          warning + "\n", successWorkDir);
        REQUIRE_FALSE(result);
        CHECK(result.error().kind == si::FailureKind::Other);
        CHECK(result.error().message == "No song was found on that page.\n" + warning);
    }
    SECTION("by the placeholder name") {
        const auto result = si::interpret(
            0, "Saving output to: out/latex/UnknownArtist/UnknownTitle.tex\n", "", successWorkDir);
        REQUIRE_FALSE(result);
        CHECK(result.error().message == "No song was found on that page.");
    }
}

TEST_CASE("interpret appends warnings to Other failures", "[songimport]") {
    SECTION("added when missing") {
        const auto result = si::interpret(2, "", "boom\nWarning: odd\n", successWorkDir);
        REQUIRE_FALSE(result);
        CHECK(result.error().message == "boom\nWarning: odd");
    }
    SECTION("not repeated when already there") {
        const auto result = si::interpret(2, "", "Warning: odd\n", successWorkDir);
        REQUIRE_FALSE(result);
        CHECK(result.error().message == "Warning: odd");
    }
    SECTION("exit code fallback gets them too") {
        const auto result = si::interpret(0, "", "", successWorkDir);
        REQUIRE_FALSE(result);
        CHECK(result.error().message == "chords_converter did not write the song");
    }
}

TEST_CASE("interpret: Ultimate Guitar's no-song warning is a failure too", "[songimport]") {
    const auto result = zub::songimport::interpret(
        0, "Saving output to: out/latex/A/B.tex\n",
        "Warning: no song data found on https://tabs.ultimate-guitar.com/x.\n", "/nonexistent");
    REQUIRE_FALSE(result);
    CHECK(result.error().message.starts_with("No song was found on that page."));
}
