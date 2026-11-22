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
    // Every command of the songs package, except the file-opening and name-building ones.
    for (const std::string_view name : {"prefersharps", "preferflats", "songcolumns", "echo",
                                        "transposehere", "gtab", "nolyrics", "textnote"})
        CHECK(std::ranges::binary_search(known, name));
    for (const std::string_view name :
         {"csname", "expandafter", "newindex", "newauthorindex", "newscripindex", "showindex",
          "write", "immediate", "openout", "input", "catcode"})
        CHECK_FALSE(std::ranges::binary_search(known, name));
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
    // TeX decodes ^^xx before it reads a name: \^^64irectlua is \directlua. The backslash must
    // not hide the escape as the control symbol \^.
    CHECK(scan("\\^^64irectlua{os.execute('x')}") == Constructs{"\\^^", "^^64"});
    CHECK(scan("\\[\\^^5cdirectlua]") == Constructs{"\\^^", "^^5c"});
    CHECK(scan("\\^^M") == Constructs{"\\^^", "^^M"});
    // A bare CR ends a TeX line: the comment stops there, and the CR itself is reported.
    CHECK(scan("% x\r\\directlua{}") == Constructs{"^^M", "\\directlua"});
    CHECK(scan("\\beginverse \\[]\\directlua{}\\[ \\endverse") == Constructs{"\\directlua"});
    CHECK(scan("\\foo \\bar \\foo* \\foo") == Constructs{"\\foo", "\\bar"});
    CHECK(scan("100\\% \\directlua") == Constructs{"\\directlua"});
}

TEST_CASE("harmless text is not reported", "[safety]") {
    CHECK(scan("%\\directlua{x}").empty());
    CHECK(scan("| ^\\[]\\[] | ^^ | ^^^ |").empty());
    CHECK(scan("\\[B&] \\[\\nochord] \\beginverse* \\endverse").empty());
    CHECK(scan("a \\\\ b \\, c \\& d \\ e").empty());
    CHECK(scan("\\^{e} \\^e \\^ ^ ^^").empty());  // the accent, not the escape
    CHECK(scan("a\r\nb % c\r\n% d\r\n").empty()); // CRLF files
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

TEST_CASE("books: their own commands are allowed, file names are checked", "[safety][book]") {
    const auto known = zub::safety::knownBookCommands();
    CHECK(std::ranges::is_sorted(known));
    const auto book = [](std::string_view s) { return zub::safety::unknownBookConstructs(s); };
    CHECK(book("\\documentclass[chorded]{velkyzpevnik}\n\\title{T}\n\\newindex{indexA}{A}\n"
               "\\begin{document}\\makefrontcover{img/x}\\insertindex\n"
               "\\songsection{S}\\begin{songs}{indexA}\\newpage\n"
               "\\beginkeeporder\n\\input{A/B}\n\\input{ A/C }\n\\endkeeporder\n"
               "\\end{songs}\\end{document}\n") == Constructs{});
    // Song commands are fine in a book too, and comments are ignored.
    CHECK(book("\\beginsong{T}\\endsong % \\directlua").empty());
    CHECK(book("\\directlua{os.execute('x')}") == Constructs{"\\directlua"});
    CHECK(book("\\input{/etc/passwd}") == Constructs{"\\input{/etc/passwd}"});
    CHECK(book("\\input{../../secret/X}") == Constructs{"\\input{../../secret/X}"});
    CHECK(book("\\input foo") == Constructs{"\\input"});
    CHECK(book("\\newindex{i}{../../.bashrc}") == Constructs{"\\newindex{..}{../../.bashrc}"});
    CHECK(book("\\newindex{i}") == Constructs{"\\newindex"});
    CHECK(book("\\newindex{i}{01011001}").empty());
    CHECK(book("\\begin{luacode}os.execute('x')\\end{luacode}") ==
          Constructs{"\\begin{luacode}", "\\end{luacode}"});
    CHECK(book("\\begin{center}\\section*{A}\\gtab{C}{X32010}\\end{center}").empty());
    // Songs still may not open files.
    CHECK(scan("\\newindex{i}{x}") == Constructs{"\\newindex"});
}

TEST_CASE("every real book passes the book check", "[.integration][safety][book]") {
    const fs::path books = fs::path(ZUB_REAL_SONGBOOK_DIR) / "songbooks";
    int count = 0;
    for (const auto& entry : fs::recursive_directory_iterator(books)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".tex")
            continue;
        const auto content = zub::readTextFile(entry.path());
        REQUIRE(content);
        INFO(entry.path().string());
        CHECK(zub::safety::unknownBookConstructs(*content) == Constructs{});
        ++count;
    }
    CHECK(count > 5);
}

TEST_CASE("books: \\transposesong takes a count and plain key options", "[safety][book]") {
    const auto book = [](std::string_view s) { return zub::safety::unknownBookConstructs(s); };
    CHECK(book("\\transposesong[key=G]{-5}\n\\input{A/B}").empty());
    CHECK(book("\\transposesong[key=F\\shrp m]{3} % fixed\n").empty());
    // the package has no capo option (a LaTeX error): not accepted
    CHECK(!book("\\transposesong[capo,key=F\\shrp m]{3}\n").empty());
    CHECK(!book("\\transposesong[capo]{3}\n").empty());
    CHECK(book("\\transposesong[key=B\\flt]{+11}").empty());
    CHECK(book("\\transposesong{4}").empty());
    CHECK(!book("\\transposesong[key=G]{12}").empty());
    CHECK(!book("\\transposesong[key=G]{x}").empty());
    CHECK(!book("\\transposesong[key=G]").empty());
    CHECK(!book("\\transposesong[key=\\directlua]{1}").empty());
    CHECK(!book("\\transposesong[key=G{}]{1}").empty());
    CHECK(!book("\\transposesong[shell=1]{1}").empty());
    CHECK(!book("\\transposesong[capo,]{1}").empty());
    CHECK(!book("\\transposesong[key=]{1}").empty());
}

TEST_CASE("books: class, index and cover names are checked", "[safety][book]") {
    const auto book = [](std::string_view s) { return zub::safety::unknownBookConstructs(s); };
    CHECK(book("\\documentclass[chorded]{taborovyzpevnik}").empty());
    CHECK(book("\\documentclass{evil}") == Constructs{"\\documentclass{evil}"});
    CHECK(book("\\documentclass{../../velkyzpevnik}") ==
          Constructs{"\\documentclass{../../velkyzpevnik}"});
    CHECK(book("\\begin{songs}{obsah, obsahAutor}\\end{songs}").empty());
    CHECK(book("\\begin{songs}{../../x}") == Constructs{"\\begin{songs}{../../x}"});
    CHECK(book("\\begin{songs}{-foo}") == Constructs{"\\begin{songs}{-foo}"});
    CHECK(book("\\begin{songs}") == Constructs{"\\begin{songs}{}"});
    CHECK(book("\\newindex{i}{-x}") == Constructs{"\\newindex{..}{-x}"});
    CHECK(book("\\makefrontcover{img/zubuvzpevnik}\\maketitlepage[img/maly]"
               "\\makefrontcover[cover.pdf]\\makebackcover[]\\makefrontcover")
              .empty());
    CHECK(book("\\makefrontcover{x;touch y.eps}") == Constructs{"\\makefrontcover{x;touch y.eps}"});
    CHECK(book("\\maketitlepage[/etc/x.png]") == Constructs{"\\maketitlepage{/etc/x.png}"});
    // A Tabor book's back cover takes text.
    CHECK(book("\\makebackcover[Find more at \\texttt{a.cz}]").empty());
}
