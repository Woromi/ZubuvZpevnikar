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
    CHECK(std::ranges::binary_search(known, std::string_view("textquotedbl")));
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

namespace {
Constructs scanLy(std::string_view s) {
    return zub::safety::lilypondRisks(s);
}
} // namespace

TEST_CASE("lilypond scores: code is reported", "[safety][lilypond]") {
    CHECK(scanLy("x = #(system \"echo hi\")") == Constructs{"#(system"});
    CHECK(scanLy("#( system \"a\") #(system \"b\") #(ly:parser-include-string \"x\")") ==
          Constructs{"#(system", "#(ly:parser-include-string"});
    CHECK(scanLy("$(system \"a\")") == Constructs{"$(system"});
    CHECK(scanLy("$foo") == Constructs{"$foo"});
    CHECK(scanLy("#.(system \"a\")") == Constructs{"#."});
    CHECK(scanLy("#(define-music-function (p) () (#(system \"x\")))") ==
          Constructs{"#(define-music-function", "#(system"});
    // an allowed head does not allow what is nested in it
    CHECK(scanLy("#(ly:make-moment (system \"x\") 4)") == Constructs{"#(ly:make-moment"});
    CHECK(scanLy("#{ #(system \"x\") #}") == Constructs{"#(system"});
    CHECK(scanLy("\\include \"/etc/passwd\"") == Constructs{"\\include \"/etc/passwd\""});
    CHECK(scanLy("\\include \"other.ily\"\n\\include \"zubuvzpevnik.ily\"") ==
          Constructs{"\\include \"other.ily\""});
    CHECK(scanLy("\\include#(system \"x\")") == Constructs{"\\include \"\"", "#(system"});
}

TEST_CASE("lilypond scores: Scheme that hides code from a naive scanner", "[safety][lilypond]") {
    // '%' in quoted data is a symbol, not a comment
    CHECK_FALSE(scanLy("x = #'(%)  #(system \"touch P\")").empty());
    // a ';' comment hides a '"'
    CHECK_FALSE(scanLy("x = #'(a ; \"\n)\n#(system \"touch P\") % \"").empty());
    // whitespace after the sigil, splicing
    CHECK_FALSE(scanLy("# (system \"touch P\")").empty());
    CHECK_FALSE(scanLy("$ (system \"touch P\")").empty());
    CHECK_FALSE(scanLy("#\n(system \"touch P\")").empty());
    CHECK_FALSE(scanLy("#@(system \"touch P\")").empty());
    CHECK_FALSE(scanLy("$@(system \"touch P\")").empty());
    CHECK_FALSE(scanLy("#;(foo) #(system \"touch P\")").empty());
    CHECK_FALSE(scanLy("#!foo!# #(system \"touch P\")").empty());
    CHECK_FALSE(scanLy("#|foo|# #(system \"touch P\")").empty());
    CHECK_FALSE(scanLy("x = #'(a #| b |# c)").empty());
    // quoted data is otherwise data
    CHECK(scanLy("x = #'(a (b . \"c)\") ; )\n #\\( d) y = #3").empty());
}

TEST_CASE("lilypond scores: data is fine", "[safety][lilypond]") {
    CHECK(scanLy("\\include \"zubuvzpevnik.ily\"\n\\include \"tinwhistle.ily\"").empty());
    CHECK(scanLy("\\set X = #(ly:make-moment 1 4) \\partial #(ly:make-moment 3 8)").empty());
    CHECK(scanLy("#'sym #'(a b c) ##t ##f #-6 #3 #1.5 #\"text\" #UP #RIGHT").empty());
    CHECK(scanLy("\\override X #'y = #begin-of-line-invisible").empty());
    CHECK(scanLy("#fret-number-tablature-format #banjo-open-g-tuning").empty());
    CHECK(scanLy("{ c4 d % #(system \"x\")\n e }").empty());
    CHECK(scanLy("%{ #(system \"x\") %} c4").empty());
    CHECK(scanLy("\\markup \"#(system $x \\\" #(system)\" c4").empty());
    CHECK(scanLy("\\score { c4 } 5 $3").empty());
}

TEST_CASE("every real score passes the lilypond check", "[.integration][safety][lilypond]") {
    const fs::path songs = fs::path(ZUB_REAL_SONGBOOK_DIR) / "songs";
    if (!fs::is_directory(songs))
        SKIP("no songbook checkout at " << songs);
    int count = 0;
    for (const auto& entry : fs::recursive_directory_iterator(songs)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".ly")
            continue;
        const auto content = zub::readTextFile(entry.path());
        REQUIRE(content);
        INFO(entry.path().string());
        CHECK(scanLy(*content).empty());
        ++count;
    }
    CHECK(count > 0);
}
