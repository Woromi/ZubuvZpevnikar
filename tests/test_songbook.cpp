#include <catch2/catch_test_macros.hpp>

#include <zub/core/json.hpp>
#include <zub/core/songbook.hpp>
#include <zub/core/textfile.hpp>

#include <algorithm>
#include <fstream>

using namespace zub::songbook;

namespace {

const std::filesystem::path dataDir = ZUB_TEST_DATA_DIR;

Songbook sample() {
    Songbook b;
    b.title = "Můj \"zpěvník\" \\ 1";
    b.author = "Dan";
    b.bookClass = BookClass::Maly;
    b.chorded = false;
    b.twoColumns = true;
    b.shading = true;
    b.chordChart = false;
    b.printReady = true;
    b.campYear = "2026";
    b.coverImage = "/x/cover.png";
    b.authorIndex = false;
    b.sections = {{"", {}}, {"Ohně", {"Abba/OurLastSummer", "A/B"}}};
    return b;
}

const auto all = [](std::string_view) { return true; };

} // namespace

TEST_CASE("quote escapes JSON strings", "[songbook]") {
    CHECK(zub::json::quote("a\"b\\c\n\t\x01 š") == "\"a\\\"b\\\\c\\n\\t\\u0001 š\"");
}

TEST_CASE("songbook JSON round trip", "[songbook]") {
    const Songbook b = sample();
    const std::string json = toJson(b);
    CHECK(json.starts_with("{\n  \"version\": 1,\n  \"title\": "));
    CHECK(json.ends_with("}\n"));
    const auto back = fromJson(json);
    REQUIRE(back);
    CHECK(*back == b);
}

TEST_CASE("songbook JSON golden", "[songbook]") {
    Songbook b;
    b.title = "T";
    b.sections = {{"", {"A/B"}}, {"S", {}}};
    CHECK(toJson(b) == R"({
  "version": 1,
  "title": "T",
  "author": "",
  "class": "velkyzpevnik",
  "chorded": true,
  "twoColumns": false,
  "shading": false,
  "chordChart": true,
  "printReady": false,
  "campYear": "",
  "coverImage": "",
  "authorIndex": true,
  "sections": [
    {
      "name": "",
      "songs": [
        "A/B"
      ]
    },
    {
      "name": "S",
      "songs": []
    }
  ]
}
)");
}

TEST_CASE("songbook JSON defaults and errors", "[songbook]") {
    const auto b = fromJson(R"({"version": 1})");
    REQUIRE(b);
    CHECK(b->bookClass == BookClass::Velky);
    CHECK(b->chorded);
    CHECK_FALSE(b->twoColumns);
    CHECK(b->chordChart);
    CHECK(b->coverImage.empty());
    CHECK(b->authorIndex);
    CHECK_FALSE(fromJson(R"({"version": 1, "authorIndex": 1})"));
    CHECK_FALSE(fromJson(R"({"version": 1, "coverImage": 1})"));
    REQUIRE(b->sections.size() == 1);
    CHECK(b->sections[0] == Section{});
    CHECK(fromJson(R"({"version": 1, "sections": []})")->sections.size() == 1);
    // Loaded books are normalized: the unnamed part first.
    const auto n = fromJson(R"({"version": 1, "sections": [{"name": "S", "songs": ["A/B"]}]})");
    REQUIRE(n);
    CHECK(n->sections == std::vector<Section>{{"", {}}, {"S", {"A/B"}}});

    CHECK_FALSE(fromJson("{"));
    CHECK_FALSE(fromJson("[]"));
    CHECK_FALSE(fromJson(R"({"version": 2})"));
    CHECK_FALSE(fromJson(R"({})"));
    CHECK_FALSE(fromJson(R"({"version": 1, "class": "x"})"));
    CHECK_FALSE(fromJson(R"({"version": 1, "sections": {}})"));
    CHECK_FALSE(fromJson(R"({"version": 1, "title": 3})"));
    CHECK_FALSE(fromJson(R"({"version": 1, "chorded": "yes"})"));
    CHECK_FALSE(fromJson(R"({"version": 1, "sections": [{"songs": [1]}]})"));
}

TEST_CASE("class names", "[songbook]") {
    CHECK(className(BookClass::Tabor) == "taborovyzpevnik");
    CHECK(bookClassFromName("malyzpevnik") == BookClass::Maly);
    CHECK_FALSE(bookClassFromName("nope"));
}

TEST_CASE("escapeText", "[songbook]") {
    CHECK(escapeText("a\\b{c}&%$#_~^ č\nx") ==
          "a\\textbackslash{}b\\{c\\}\\&\\%\\$\\#\\_\\textasciitilde{}\\textasciicircum{} č x");
}

namespace {

bool has(const std::string& text, std::string_view part) {
    return text.find(part) != std::string::npos;
}

// A real file for the cover tests, removed with the object.
struct TempFile {
    std::filesystem::path path;
    explicit TempFile(const std::string& name) {
        path = std::filesystem::temp_directory_path() / ("zub-test-" + name);
        std::ofstream(path) << "x";
    }
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

Songbook sectioned(BookClass c) {
    Songbook b;
    b.title = "Velký & co";
    b.author = "Dan";
    b.bookClass = c;
    b.sections = {{"", {"A/B", "C/D"}}, {"Jedna & co", {"E/F"}}, {"Dvě", {"G/H"}}};
    return b;
}

using V = std::vector<std::string>;

} // namespace

TEST_CASE("generate velky", "[songbook]") {
    Songbook b = sectioned(BookClass::Velky);
    const auto g = generate(b, all);
    CHECK_FALSE(g.akordy);
    CHECK(g.indexes == V{"obsah", "idxs1", "idxs2", "obsahAutor"});
    CHECK_FALSE(g.coverSource);
    CHECK(g.warnings.empty());
    CHECK(has(g.tex, "\\newindex{idxs1}{idxs1}\n\\indexsongsas{idxs1}{\\thepage}\n"
                     "\\newindex{idxs2}{idxs2}\n\\indexsongsas{idxs2}{\\thepage}\n"));
    CHECK(has(g.tex, "\\newcommand{\\zubsectionpage}[2]{%\n  \\clearpage\n  \\thispagestyle{empty}"));
    CHECK_FALSE(has(g.tex, "\\ifodd"));
    CHECK(has(g.tex, "\\newcommand{\\zubcover}"));
    CHECK(has(g.tex, "\\begin{document}\n\n\\zubcover\n\\clearpage\\pagestyle{empty}\\pagenumbering{roman}\n{\n"));
    CHECK(has(g.tex, "  {\\large \\showindex[2]{}{obsah}}\n  \\vskip 2\\baselineskip\n"
                     "  {\\large \\showindex[2]{Jedna \\& co}{idxs1}}\n"));
    CHECK(has(g.tex, "  \\clearpage\n  {\\large \\showindex[2]{Obsah podle interpreta}{obsahAutor}}\n}\n"
                     "\\clearpage\\pagenumbering{arabic}\\pagestyle{fancy}\n"));
    CHECK_FALSE(has(g.tex, "\\insertindex"));
    CHECK(has(g.tex, "\\begin{songs}{obsah,obsahAutor}\n    \\input{A/B}\n    \\input{C/D}\n\\end{songs}\n\n"
                     "\\zubsectionpage{Jedna \\& co}{Jedna \\& co}\n"
                     "\\begin{songs}{idxs1,obsahAutor}\n    \\input{E/F}\n"));
    CHECK(has(g.tex, "\\zubsectionpage{Dvě}{Dvě}\n\\begin{songs}{idxs2,obsahAutor}"));
    CHECK(g.tex.find("\\input{A/B}") < g.tex.find("\\zubsectionpage{Jedna"));
    CHECK(g.tex.find("\\end{songs}\n\n\\insertallchords\n\n\\end{document}") != std::string::npos);

    b.authorIndex = false;
    const auto noAuthor = generate(b, all);
    CHECK(noAuthor.indexes == V{"obsah", "idxs1", "idxs2"});
    CHECK_FALSE(has(noAuthor.tex, "obsahAutor"));

    TempFile cover("velky.PNG");
    b.coverImage = cover.path.string();
    const auto withCover = generate(b, all);
    CHECK(withCover.coverSource == cover.path);
    CHECK(withCover.coverName == "cover.png");
    CHECK(has(withCover.tex, "\\makefrontcover{cover.png}\n"));
    CHECK_FALSE(has(withCover.tex, "zubcover"));
}

TEST_CASE("generate velky without sections", "[songbook]") {
    Songbook b;
    b.title = "T";
    b.sections = {{"", {"A/B"}}};
    const auto g = generate(b, all);
    CHECK(g.indexes == V{"obsah", "obsahAutor"});
    CHECK_FALSE(has(g.tex, "zubsectionpage"));
    CHECK_FALSE(has(g.tex, "newindex"));
    CHECK(has(g.tex, "{\\large \\showindex[2]{}{obsah}}\n  \\endgroup\n"));

    Songbook onlyNamed;
    onlyNamed.sections = {{"", {}}, {"S", {"A/B"}}};
    const auto n = generate(onlyNamed, all);
    CHECK(n.indexes == V{"idxs1", "obsahAutor"});
    CHECK_FALSE(has(n.tex, "showindex[2]{}{obsah}"));
    CHECK_FALSE(has(n.tex, "\\vskip 2\\baselineskip"));
    CHECK(has(n.tex, "\\zubsectionpage{S}{S}\n\\begin{songs}{idxs1,obsahAutor}"));

    // empty sections are not emitted and do not take an index number
    Songbook gaps;
    gaps.sections = {{"", {}}, {"Gone", {"X/Y"}}, {"S", {"A/B"}}};
    const auto gg = generate(gaps, [](std::string_view n2) { return n2 == "A/B"; });
    CHECK(gg.indexes == V{"idxs1", "obsahAutor"});
}

TEST_CASE("generate maly", "[songbook]") {
    Songbook b = sectioned(BookClass::Maly);
    b.printReady = true;
    const auto g = generate(b, all);
    CHECK(g.indexes == V{"obsah", "idxs1", "idxs2", "obsahAutor"});
    CHECK(has(g.tex, "\\ifodd\\c@page\\else\\thispagestyle{empty}\\null\\newpage\\fi\n  \\thispagestyle{empty}\\null\\vfill"));
    CHECK(has(g.tex, "\\begin{document}\n\n\\bindingleaf\n\\maketitlepage\n\n"));
    CHECK_FALSE(has(g.tex, "\\insertindex"));
    CHECK(has(g.tex, "\\insertallchords\n\\clearpage\n\\begingroup\\addtolength{\\hsize}{-8pt}%\n"
                     "  \\begingroup\\sepindexesfalse\n  {\\large \\showindex[2]{}{obsah}}\n"
                     "  \\vskip 1\\baselineskip\n  {\\large \\showindex[2]{Jedna \\& co}{idxs1}}\n"));
    CHECK(g.tex.ends_with("  \\clearpage\n  {\\large \\showindex[2]{Obsah podle interpreta}{obsahAutor}}\n"
                          "\\endgroup\n\\bindingleaf[\\makeimprint]\n\\padbookpages\n\n\\end{document}\n"));

    b.printReady = false;
    b.chordChart = false;
    b.authorIndex = false;
    TempFile cover("maly.jpeg");
    b.coverImage = cover.path.string();
    const auto plain = generate(b, all);
    CHECK(plain.indexes == V{"obsah", "idxs1", "idxs2"});
    CHECK(plain.coverName == "cover.jpeg");
    CHECK(has(plain.tex, "\\maketitlepage[cover.jpeg]\n"));
    CHECK_FALSE(has(plain.tex, "\\bindingleaf"));
    CHECK_FALSE(has(plain.tex, "\\insertallchords"));
    CHECK_FALSE(has(plain.tex, "\\clearpage\n  {\\large \\showindex[2]{Obsah"));
    CHECK(plain.tex.ends_with("\\endgroup\n\n\\end{document}\n"));
}

TEST_CASE("generate tabor", "[songbook]") {
    Songbook b;
    b.title = "Tábor";
    b.author = "Já";
    b.bookClass = BookClass::Tabor;
    b.campYear = "2026 #1";
    b.sections = {{"", {"X/Y"}}, {"Ráno", {"A/B"}}, {"Večer & co", {"C/D"}}};
    const auto g = generate(b, all);
    CHECK(g.indexes == V{"obsah", "idxs1", "idxs2"}); // never an author index
    CHECK(g.warnings.empty());
    CHECK(has(g.tex, "{taborovyzpevnik}\n\n\\title{Tábor}\n\\author{Já}\n\\renewcommand{\\campyear}{2026 \\#1}\n"));
    CHECK(has(g.tex, "\\newcommand{\\zubtitleindexes}{%\n  \\begingroup\\sepindexesfalse\n"
                     "  \\showindex[1]{}{obsah}\n  \\vskip 1\\baselineskip\n  \\showindex[1]{Ráno}{idxs1}\n"
                     "  \\vskip 1\\baselineskip\n  \\showindex[1]{Večer \\& co}{idxs2}\n  \\endgroup}\n"));
    CHECK(has(g.tex, "\\xpatchcmd\\csname\\string\\makefrontcover\\endcsname"));
    CHECK(has(g.tex, "{\\large \\showindex[1]{Obsah}{obsah}}{\\large\\zubtitleindexes}"));
    CHECK(has(g.tex, "\\newcommand{\\zubCoverInc}[2][]{}\n"));
    CHECK(has(g.tex, "\\let\\zubOrigInc\\includegraphics\\let\\includegraphics\\zubCoverInc\n"
                     "\\makefrontcover\n\\let\\includegraphics\\zubOrigInc\n"));
    CHECK(has(g.tex, "\\begin{songs}{idxs1}\n"));
    CHECK_FALSE(has(g.tex, "obsahAutor"));
    CHECK_FALSE(has(g.tex, "\\ifodd"));
    CHECK(has(g.tex, "\\padbookletpages\n\\makebackcover\n\n\\end{document}\n"));
    REQUIRE(g.akordy);
    CHECK(*g.akordy ==
          "% Generated by Zpěvníkář (ZubuvZpevnikar): the chord chart \\makebackcover inputs.\n"
          "\\begingroup\\centering\\insertallchords\\endgroup\n");
    b.campYear.clear();
    CHECK(generate(b, all).tex.find("campyear") == std::string::npos);

    TempFile png("tabor.png");
    b.coverImage = png.path.string();
    CHECK(has(generate(b, all).tex, "\\newcommand{\\zubCoverInc}[2][]{\\zubOrigInc[width=350pt]{cover.png}}\n"));
    TempFile pdf("tabor.pdf");
    b.coverImage = pdf.path.string();
    const auto withPdf = generate(b, all);
    CHECK(has(withPdf.tex, "\\makefrontcover[cover.pdf]\n"));
    CHECK_FALSE(has(withPdf.tex, "zubCoverInc"));
    CHECK(withPdf.coverName == "cover.pdf");
}

TEST_CASE("generate tabor index fallback", "[songbook]") {
    Songbook b;
    b.bookClass = BookClass::Tabor;
    b.sections = {{"", {"A/1", "A/2", "A/3", "A/4", "A/5", "A/6"}},
                  {"S", {"B/1", "B/2", "B/3", "B/4", "B/5", "B/6", "B/7", "B/8"}}};
    // every song its own initial: 6+9 + 8+12+3 = 38 > 28 -> flat
    const auto flat = generate(b, all);
    CHECK(flat.indexes == V{"obsah"});
    CHECK(flat.warnings == V{"The index is not split into sections: it would not fit the booklet's "
                             "single index page."});
    CHECK_FALSE(has(flat.tex, "newindex"));
    CHECK_FALSE(has(flat.tex, "xpatchcmd"));
    CHECK(has(flat.tex, "\\zubsectionpage{S}{S}\n\\begin{songs}{obsah}\n"));
    CHECK(has(flat.tex, "\\begin{songs}{obsah}\n    \\input{A/1}"));

    // the same songs with a shared initial per section fit: 6+1.5 + 8+1.5+3 = 20
    const auto split =
        generate(b, all, [](std::string_view n) { return std::string(n.substr(0, 1)) + "x"; });
    CHECK(split.indexes == V{"obsah", "idxs1"});
    CHECK(split.warnings.empty());
    CHECK(has(split.tex, "xpatchcmd"));

    Songbook small;
    small.bookClass = BookClass::Tabor;
    small.sections = {{"S", {"A/1", "A/2"}}};
    CHECK(generate(small, all).indexes == V{"idxs1"});

    // no sections at all: nothing to split, no warning
    Songbook rootOnly;
    rootOnly.bookClass = BookClass::Tabor;
    rootOnly.sections = {{"", {"A/1", "A/2", "A/3", "A/4", "A/5", "A/6", "A/7", "A/8", "A/9",
                               "A/10", "A/11", "A/12", "A/13", "A/14", "A/15", "A/16"}}};
    const auto r = generate(rootOnly, all);
    CHECK(r.indexes == V{"obsah"});
    CHECK(r.warnings.empty());
}

TEST_CASE("generate cover problems", "[songbook]") {
    Songbook b;
    b.sections = {{"", {"A/B"}}};
    b.coverImage = "/nonexistent/dir/cover.png";
    const auto missing = generate(b, all);
    CHECK_FALSE(missing.coverSource);
    CHECK(missing.coverName.empty());
    REQUIRE(missing.warnings.size() == 1);
    CHECK(has(missing.warnings[0], "/nonexistent/dir/cover.png"));
    CHECK(has(missing.tex, "\\zubcover"));

    TempFile gif("cover.gif");
    b.coverImage = gif.path.string();
    const auto unsupported = generate(b, all);
    CHECK_FALSE(unsupported.coverSource);
    CHECK(unsupported.warnings.size() == 1);
}

TEST_CASE("generate skips missing songs and empty sections", "[songbook]") {
    Songbook b;
    b.sections = {{"Gone", {"X/Y"}}, {"", {"A/B", "X/Z"}}};
    const auto g = generate(b, [](std::string_view n) { return n == "A/B"; });
    CHECK(g.skipped == std::vector<std::string>{"X/Y", "X/Z"});
    CHECK(g.tex.find("Gone") == std::string::npos);
    CHECK(g.tex.find("\\input{A/B}") != std::string::npos);
    CHECK(g.tex.find("X/Z") == std::string::npos);
}

TEST_CASE("generate never emits invalid names", "[songbook]") {
    Songbook b;
    b.sections = {{"", {"../x", "/abs", "a}b", "A/B"}}};
    const auto g = generate(b, all);
    CHECK(g.skipped == std::vector<std::string>{"../x", "/abs", "a}b"});
    CHECK(g.tex.find("../x") == std::string::npos);
    CHECK(g.tex.find("/abs") == std::string::npos);
    CHECK(g.tex.find("a}b") == std::string::npos);
    CHECK(g.tex.find("\\input{A/B}") != std::string::npos);
}

TEST_CASE("resolveSongs", "[songbook]") {
    Songbook b;
    b.sections = {{"", {"TheMamasAndThePapas/CaliforniaDreamin", "Abba/Nope", "../x"}},
                  {"S", {"TheMamasAndThePapas/CaliforniaDreamin", "/abs", "a}b"}}};
    const auto root = dataDir / "songbook";
    const auto r = resolveSongs(b, root);
    REQUIRE(r.songs.size() == 2);
    CHECK(r.songs[0].inputName == "TheMamasAndThePapas/CaliforniaDreamin");
    CHECK(r.songs[0].path ==
          root / "songs/chords/TheMamasAndThePapas/CaliforniaDreamin.tex");
    CHECK(r.songs[1].inputName == "Abba/Nope");
    CHECK(r.songs[1].path.empty());
    CHECK(r.invalid == std::vector<std::string>{"../x", "/abs", "a}b"});
}

TEST_CASE("generate all empty", "[songbook]") {
    Songbook b;
    b.sections = {{"S", {"X/Y"}}};
    const auto g = generate(b, [](std::string_view) { return false; });
    CHECK(g.tex.find("\\zubsectionpage{") == std::string::npos);
    CHECK(g.indexes == V{"obsah", "obsahAutor"});
    CHECK(g.tex.find("\\begin{songs}{obsah,obsahAutor}\n\\end{songs}\n\n") != std::string::npos);
}

TEST_CASE("index order against the songidx capture", "[songbook]") {
    const std::vector<SortEntry> entries = {
        {"The Bard's Song", "english"}, {"Čarodějnice z Amesbury", "czech"},
        {"Chlast", "czech"},            {"Counting Stars", "english"},
        {"Akordy", "czech"},            {"A Day in the Life", "english"},
        {"Hřbitov", "czech"},           {"Africa", ""},
        {"Cudzinka v tvojej zemi", "slovak"}};
    const auto sxd = zub::readTextFile(dataDir / "songidx/order.sxd");
    REQUIRE(sxd);
    CHECK(indexOrderInput(entries) == *sxd);

    const auto sbx = zub::readTextFile(dataDir / "songidx/order.sbx");
    REQUIRE(sbx);
    const auto order = parseIndexOrder(*sbx, 9);
    REQUIRE(order);
    CHECK(*order == std::vector<int>{8, 5, 1, 4, 9, 2, 6, 7, 3});

    CHECK_FALSE(parseIndexOrder(*sbx, 8));
    CHECK_FALSE(parseIndexOrder(*sbx, 10));
    CHECK_FALSE(parseIndexOrder("\\songlink{a}{1}\\songlink{b}{1}", 2));
    CHECK_FALSE(parseIndexOrder("\\songlink{a}{1}\\songlink{b}{3}", 2));
    CHECK_FALSE(parseIndexOrder("\\songlink{a}{x}", 1));
    CHECK(parseIndexOrder("\\songlink{a}{2}\\songlink{b}{1}", 2));
}

TEST_CASE("indexOrderInput keeps titles on one line", "[songbook]") {
    const std::vector<SortEntry> entries = {{"A\nB", ""}};
    CHECK(indexOrderInput(entries) == "TITLE INDEX DATA FILE\n%lang english\nA B\n1\nsong1-1.1\n");
}

TEST_CASE("indexOrderInput protects a leading percent", "[songbook]") {
    const std::vector<SortEntry> entries = {{"%x", ""}, {"a%", "cs"}};
    CHECK(indexOrderInput(entries) ==
          "TITLE INDEX DATA FILE\n%lang english\n %x\n1\nsong1-1.1\n"
          "%lang cs\na%\n2\nsong1-2.1\n");
}

TEST_CASE("sortCommand", "[songbook]") {
    const auto c = sortCommand("/t/texlua", "/r/songidx.lua", "cs_CZ.utf8", "/w/a.sxd", "/w/a.sbx");
    CHECK(c.program == "/t/texlua");
    CHECK(c.arguments == std::vector<std::string>{"/r/songidx.lua", "-l", "cs_CZ.utf8", "/w/a.sxd",
                                                  "/w/a.sbx"});
    const auto n = sortCommand("texlua", "s.lua", "", "a.sxd", "a.sbx");
    CHECK(n.arguments == std::vector<std::string>{"s.lua", "a.sxd", "a.sbx"});
}

TEST_CASE("referencedFiles", "[songbook]") {
    const auto text = zub::readTextFile(
        dataDir / "songbook/songs/chords/TheMamasAndThePapas/CaliforniaDreamin.tex");
    REQUIRE(text);
    CHECK(referencedFiles(*text) ==
          std::vector<std::string>{"TheMamasAndThePapas/CaliforniaDreamin.ly"});

    CHECK(referencedFiles("\\lilypondfile[a=b]{X/a.ly}\n% \\lilypondfile{no.ly}\n"
                          "\\narrowlilypondfile{X/b.ly} \\lilypondfile{X/a.ly}\n"
                          "\\lilypondfilex{no} \\lilypondfile no") ==
          std::vector<std::string>{"X/a.ly", "X/b.ly"});
    CHECK(referencedFiles("no files").empty());
}

TEST_CASE("planFolderExport", "[songbook]") {
    Songbook b;
    b.title = "Test";
    b.sections = {{"", {"TheMamasAndThePapas/CaliforniaDreamin", "Abba/Nope"}},
                  {"S", {"TheMamasAndThePapas/CaliforniaDreamin"}}};
    const auto root = dataDir / "songbook";
    const auto plan = planFolderExport(b, root, "my-book_1");
    REQUIRE(plan);
    CHECK(plan->skipped == std::vector<std::string>{"Abba/Nope"});

    const auto copied = [&](const std::filesystem::path& to) {
        return std::ranges::any_of(plan->copies, [&](const FileCopy& c) { return c.to == to; });
    };
    CHECK(copied("songs/TheMamasAndThePapas/CaliforniaDreamin.tex"));
    CHECK(std::ranges::count_if(plan->copies, [](const FileCopy& c) {
              return c.to == "songs/TheMamasAndThePapas/CaliforniaDreamin.tex";
          }) == 1);
    CHECK_FALSE(copied("songs/TheMamasAndThePapas/CaliforniaDreamin.ly")); // not in the fixture
    CHECK(copied("zubuvzpevnik/alpha.cls"));
    CHECK(copied("zubuvzpevnik/beta.cls"));
    CHECK(copied("scripts/build.sh"));
    CHECK(copied("scripts/cleanup.sh"));
    CHECK(copied("img/zubuvzpevnik.png"));
    CHECK_FALSE(copied("img/kane.jpg"));

    REQUIRE(plan->written.size() == 3);
    CHECK(plan->written[0].first == "my-book_1.tex");
    CHECK(plan->written[0].second.find("Abba/Nope") == std::string::npos);
    CHECK(plan->written[0].second.find("\\input{TheMamasAndThePapas/CaliforniaDreamin}") !=
          std::string::npos);
    CHECK(plan->written[1].first == "build.sh");
    CHECK(plan->written[1].second ==
          "#!/bin/bash\n"
          "# Builds my-book_1.pdf into out/ (needs lualatex, lilypond, texlua: see README.md).\n"
          "set -euo pipefail\n"
          "HERE=\"$(cd \"$(dirname \"$0\")\" && pwd)\"\n"
          "export TEXINPUTS=\".:$HERE:$HERE/zubuvzpevnik:$HERE/songs:${TEXINPUTS:-}\"\n"
          "mkdir -p \"$HERE/out\"\n"
          "cd \"$HERE/out\"\n"
          "\"$HERE/scripts/build.sh\" -i obsah -i idxs1 -i obsahAutor \"$HERE/my-book_1.tex\"\n");
    CHECK(plan->written[2].first == "README.md");
    CHECK(plan->written[2].second.starts_with("# Test\n"));
    CHECK(plan->written[2].second.find("./build.sh") != std::string::npos);

    b.bookClass = BookClass::Tabor;
    const auto tabor = planFolderExport(b, root, "t");
    REQUIRE(tabor);
    REQUIRE(tabor->written.size() == 4);
    CHECK(tabor->written[1].first == "akordy.tex");
    CHECK(tabor->written[2].second.find("obsahAutor") == std::string::npos);
    CHECK(tabor->written[2].second.find("-i obsah -i idxs1 ") != std::string::npos);
    CHECK(std::ranges::any_of(tabor->copies,
                              [](const FileCopy& c) { return c.to == "img/kane.jpg"; }));

    b.bookClass = BookClass::Maly;
    const auto maly = planFolderExport(b, root, "m");
    REQUIRE(maly);
    CHECK(std::ranges::none_of(maly->copies,
                               [](const FileCopy& c) { return c.to.parent_path() == "img"; }));
}

TEST_CASE("planFolderExport copies the cover", "[songbook]") {
    Songbook b;
    b.sections = {{"", {"TheMamasAndThePapas/CaliforniaDreamin"}}};
    TempFile cover("export.JPG");
    b.coverImage = cover.path.string();
    const auto plan = planFolderExport(b, dataDir / "songbook", "x");
    REQUIRE(plan);
    CHECK(plan->warnings.empty());
    CHECK(std::ranges::any_of(plan->copies, [&](const FileCopy& c) {
        return c.to == "cover.jpg" && c.from == cover.path;
    }));
    CHECK(has(plan->written[0].second, "\\makefrontcover{cover.jpg}"));
    b.coverImage = "/nonexistent/c.png";
    const auto bad = planFolderExport(b, dataDir / "songbook", "x");
    REQUIRE(bad);
    CHECK(bad->warnings.size() == 1);
    CHECK(std::ranges::none_of(bad->copies, [](const FileCopy& c) { return c.to == "cover.png"; }));
}

TEST_CASE("planFolderExport rejects bad input", "[songbook]") {
    Songbook b;
    const auto root = dataDir / "songbook";
    CHECK_FALSE(planFolderExport(b, root, ""));
    CHECK_FALSE(planFolderExport(b, root, "a b"));
    CHECK_FALSE(planFolderExport(b, root, "a/b"));
    CHECK_FALSE(planFolderExport(b, root, "č"));
    b.sections = {{"", {"../x", "/etc/passwd", "a}b", "TheMamasAndThePapas/CaliforniaDreamin"}}};
    const auto plan = planFolderExport(b, root, "x");
    REQUIRE(plan);
    CHECK(plan->invalid == std::vector<std::string>{"../x", "/etc/passwd", "a}b"});
    CHECK(plan->written[0].second.find("../x") == std::string::npos);
    CHECK(plan->written[0].second.find("a}b") == std::string::npos);
    CHECK(std::ranges::none_of(plan->copies, [](const FileCopy& c) {
        return c.to.string().find("..") != std::string::npos;
    }));
}

TEST_CASE("buildCommand", "[songbook]") {
    Songbook b;
    const auto c = buildCommand(b, "/r", "/w", "book", {"obsah", "idxs1", "obsahAutor"});
    CHECK(c.program == "bash");
    CHECK(c.arguments == std::vector<std::string>{"/r/scripts/build.sh", "-i", "obsah", "-i",
                                                  "idxs1", "-i", "obsahAutor", "/w/book.tex"});
    CHECK(c.workingDirectory == "/w");
    REQUIRE(c.environment.size() == 1);
    CHECK(c.environment[0].first == "TEXINPUTS");
    CHECK(c.environment[0].second ==
          ".:/w:/r/songs/chords:/r/songs/tinwhistle:/r:/r/zubuvzpevnik:");
    CHECK(c.pdf == "/w/book.pdf");
    CHECK(c.log == "/w/book.log");

    b.bookClass = BookClass::Tabor;
    CHECK(buildCommand(b, "/r", "/w", "book", {"obsah"}).arguments ==
          std::vector<std::string>{"/r/scripts/build.sh", "-i", "obsah", "/w/book.tex"});
}

TEST_CASE("paperWarnings", "[songbook]") {
    const std::string log =
        "Package zubuvzpevnik Warning: Song 'A' is not marked as fitting a5 paper on input line 3.\n"
        "Package zubuvzpevnik Warning: something else.\n"
        "Package foo Warning: not marked as fitting\n"
        "Package zubuvzpevnik Warning: Song 'B' is not marked as fitting a5 paper\n"
        // wrapped at max_print_line in the middle of the phrase
        "Package zubuvzpevnik Warning: Song 'A Very Long Title, Long Enough To Wrap The Log Line Right Here' "
        "is not marked as fi\n"
        "tting a5 paper; its pap key says 'a4'.";
    CHECK(paperWarnings(log) == 3);
    CHECK(paperWarnings("") == 0);

    // a long warning without the phrase, then one with it: counted once
    CHECK(paperWarnings("Package zubuvzpevnik Warning: " + std::string(150, 'x') +
                        " and so on.\n"
                        "Package zubuvzpevnik Warning: Song 'C' is not marked as fitting a5.\n") == 1);
    // wrapped before 119 bytes
    CHECK(paperWarnings("Package zubuvzpevnik Warning: Song 'D' is not marked as fi\n"
                        "tting a5 paper.\n\nPackage foo Warning: not marked as fitting\n") == 1);
    // a blank line ends the block
    CHECK(paperWarnings("Package zubuvzpevnik Warning: Song 'E' is not\n\nmarked as fitting\n") == 0);
}

TEST_CASE("normalize", "[songbook]") {
    Songbook b;
    b.sections = {{"S", {"A/B"}}, {"", {"X/Y", "X/Z"}}, {"T", {}}, {"", {"X/Z", "Q/R"}}};
    normalize(b);
    CHECK(b.sections ==
          std::vector<Section>{{"", {"X/Y", "X/Z", "Q/R"}}, {"S", {"A/B"}}, {"T", {}}});

    Songbook empty;
    normalize(empty);
    CHECK(empty.sections == std::vector<Section>{{"", {}}});

    Songbook onlyNamed;
    onlyNamed.sections = {{"S", {"A/B"}}};
    normalize(onlyNamed);
    CHECK(onlyNamed.sections == std::vector<Section>{{"", {}}, {"S", {"A/B"}}});
    const auto again = onlyNamed;
    normalize(onlyNamed);
    CHECK(onlyNamed == again);
}

TEST_CASE("generate prints the unnamed part before the sections", "[songbook]") {
    Songbook b;
    b.sections = {{"", {"A/B"}}, {"S", {"C/D"}}};
    const auto tex = generate(b, all).tex;
    const auto root = tex.find("\\input{A/B}");
    const auto section = tex.find("\\zubsectionpage{S}{S}");
    REQUIRE(root != std::string::npos);
    REQUIRE(section != std::string::npos);
    CHECK(root < section);
    CHECK(tex.find("\\zubsectionpage{}") == std::string::npos);
    CHECK(tex.find("\\input{C/D}") > section);
}

TEST_CASE("mergeIntoOrder", "[songbook]") {
    using V = std::vector<std::string>;
    CHECK(mergeIntoOrder({}, {"a", "b"}, {"a", "b"}) == V{"a", "b"});
    // front, middle, end
    CHECK(mergeIntoOrder({"b", "c"}, {"a", "b", "c"}, {"a"}) == V{"a", "b", "c"});
    CHECK(mergeIntoOrder({"a", "c"}, {"a", "b", "c"}, {"b"}) == V{"a", "b", "c"});
    CHECK(mergeIntoOrder({"a", "b"}, {"a", "b", "c"}, {"c"}) == V{"a", "b", "c"});
    // a manual order: the new name follows its alphabetical predecessor wherever that is now
    CHECK(mergeIntoOrder({"c", "a", "e"}, {"a", "b", "c", "d", "e"}, {"b"}) ==
          V{"c", "a", "b", "e"});
    CHECK(mergeIntoOrder({"c", "a", "e"}, {"a", "c", "d", "e", "f"}, {"d"}) ==
          V{"c", "d", "a", "e"});
    // several at once, in index order, one may follow another
    CHECK(mergeIntoOrder({"e", "a"}, {"a", "b", "c", "d", "e"}, {"d", "b", "c"}) ==
          V{"e", "a", "b", "c", "d"});
    CHECK(mergeIntoOrder({"z"}, {"a", "b", "z"}, {"b", "a"}) == V{"a", "b", "z"});
    // duplicates and names already present are ignored
    CHECK(mergeIntoOrder({"a", "c"}, {"a", "b", "c"}, {"b", "b", "a"}) == V{"a", "b", "c"});
    CHECK(mergeIntoOrder({"a"}, {"a"}, {"a"}) == V{"a"});
    // a name missing from the index order goes last
    CHECK(mergeIntoOrder({"a"}, {"a"}, {"q"}) == V{"a", "q"});
}
