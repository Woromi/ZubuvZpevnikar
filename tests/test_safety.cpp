#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <vector>
#include <zub/core/safety.hpp>
#include <zub/core/textfile.hpp>

namespace fs = std::filesystem;
using Constructs = std::vector<std::string>;

namespace {
Constructs scan(std::string_view s) {
    return zub::safety::unknownConstructs(s);
}
} // namespace

TEST_CASE("known song commands are sorted and plausible", "[safety]") {
    const auto known = zub::safety::knownSongCommands();
    CHECK(std::ranges::is_sorted(known));
    CHECK(std::ranges::binary_search(known, std::string_view("beginsong")));
    CHECK(std::ranges::binary_search(known, std::string_view("lilypondfile")));
    CHECK_FALSE(std::ranges::binary_search(known, std::string_view("directlua")));
}

TEST_CASE("fixture songs have no unknown constructs", "[safety]") {
    const fs::path root = fs::path(ZUB_TEST_DATA_DIR) / "songbook" / "songs" / "chords";
    int count = 0;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".tex")
            continue;
        if (entry.path().parent_path().filename() == "Broken")
            continue;
        const auto content = zub::readTextFile(entry.path());
        REQUIRE(content);
        INFO(entry.path().string());
        CHECK(scan(*content).empty());
        ++count;
    }
    CHECK(count > 0);
}

TEST_CASE("unknown constructs are reported", "[safety]") {
    CHECK(scan("\\directlua{os.execute('x')}") == Constructs{"\\directlua"});
    CHECK(scan("\\immediate\\write18{rm}") == Constructs{"\\immediate", "\\write18"});
    CHECK(scan("\\input{/etc/passwd}") == Constructs{"\\input"});
    CHECK(scan("\\csname directlua\\endcsname") == Constructs{"\\csname", "\\endcsname"});
    CHECK(scan("^^5cdirectlua") == Constructs{"^^5c"});
    CHECK(scan("^^Z") == Constructs{"^^Z"});
    CHECK(scan("\\beginverse \\[]\\directlua{}\\[ \\endverse") == Constructs{"\\directlua"});
    CHECK(scan("\\foo \\bar \\foo* \\foo") == Constructs{"\\foo", "\\bar"});
    CHECK(scan("100\\% \\directlua") == Constructs{"\\directlua"});
}

TEST_CASE("harmless text is not reported", "[safety]") {
    CHECK(scan("%\\directlua{x}").empty());
    CHECK(scan("| ^\\[]\\[] | ^^ | ^^^ |").empty());
    CHECK(scan("\\[B&] \\[\\nochord] \\beginverse* \\endverse").empty());
    CHECK(scan("a \\\\ b \\, c \\& d \\ e").empty());
}
