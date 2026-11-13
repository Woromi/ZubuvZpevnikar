#include <catch2/catch_test_macros.hpp>
#include <string>
using namespace std::string_literals;
#include <zub/core/scoreregion.hpp>

namespace sr = zub::scoreregion;
using sr::Action;

namespace {

// The range that starts at the first `from` and ends after the first `to` that follows it.
sr::Range between(const std::string& text, const std::string& from, const std::string& to) {
    const auto begin = text.find(from);
    REQUIRE(begin != std::string::npos);
    const auto end = text.find(to, begin);
    REQUIRE(end != std::string::npos);
    return {begin, end + to.size()};
}

// The result text with the new selection in [ ].
std::string shown(const std::optional<sr::Result>& result) {
    REQUIRE(result);
    std::string s = result->text;
    s.insert(result->selection.end, "]");
    s.insert(result->selection.begin, "[");
    return s;
}

std::string run(Action action, const std::string& text, const std::string& from,
                const std::string& to) {
    return shown(sr::apply(action, text, between(text, from, to)));
}

} // namespace

TEST_CASE("scoreregion offsetOf counts code points per line", "[scoreregion]") {
    const std::string text = "a\n% \xc4\x9b\xc5\xa1 e\nxy";
    CHECK(sr::offsetOf(text, 1, 0) == 0);
    CHECK(sr::offsetOf(text, 2, 0) == 2);
    CHECK(sr::offsetOf(text, 2, 2) == 4);
    CHECK(sr::offsetOf(text, 2, 4) == 8);   // after the two Czech letters (2 bytes each)
    CHECK(sr::offsetOf(text, 2, 5) == 9);   // the `e`
    CHECK(sr::offsetOf(text, 2, 99) == 10); // past the end: the end of the line
    CHECK(sr::offsetOf(text, 3, 1) == 12);
    CHECK(sr::offsetOf(text, 3, 99) == text.size());
    CHECK_FALSE(sr::offsetOf(text, 4, 0));
    CHECK_FALSE(sr::offsetOf(text, 0, 0));
    CHECK_FALSE(sr::offsetOf(text, 1, -1));
    CHECK(sr::offsetOf("", 1, 0) == 0);
}

TEST_CASE("scoreregion expandToEvents takes whole tokens", "[scoreregion]") {
    const std::string text = "e4\\1 <d\\4 g\\3>8^\\strokeDown a,16\\5( b,\\5) r8 c'";
    const auto chord = text.find('<');
    // A point or a range inside a chord: the whole chord, with duration and articulation.
    const auto inChord = sr::expandToEvents(text, {text.find("g\\3"), text.find("g\\3")});
    REQUIRE(inChord);
    CHECK(text.substr(inChord->begin, inChord->end - inChord->begin) ==
          "<d\\4 g\\3>8^\\strokeDown");
    CHECK(inChord->begin == chord);
    // From inside one note to inside another.
    const auto span = sr::expandToEvents(text, {text.find("d\\4"), text.find("b,") + 1});
    REQUIRE(span);
    CHECK(text.substr(span->begin, span->end - span->begin) ==
          "<d\\4 g\\3>8^\\strokeDown a,16\\5( b,\\5)");
    // The end offset is exclusive: a range ending where the next token starts does not take it.
    const auto exact = sr::expandToEvents(text, {0, text.find(' ')});
    REQUIRE(exact);
    CHECK(exact->end == 4);
    // Rests count; nothing at all gives nothing.
    CHECK(sr::expandToEvents(text, {text.find("r8"), text.find("r8") + 1}));
    CHECK_FALSE(sr::expandToEvents("\\time 3/4 % c d e", {0, 17}));
    CHECK_FALSE(sr::expandToEvents("", {0, 0}));
    // A reversed range is the same range.
    CHECK(sr::expandToEvents(text, {10, 2}) == sr::expandToEvents(text, {2, 10}));
}

TEST_CASE("scoreregion tie", "[scoreregion]") {
    CHECK(run(Action::Tie, "a4\\3 a4\\3 a4\\3", "a4", "a4\\3 a4\\3 a4\\3") ==
          "[a4\\3~ a4\\3~ a4\\3]");
    // Chords, rests (skipped), existing ties (skipped), post-events and articulations.
    CHECK(run(Action::Tie, "<e\\4 g\\3>4 r4 <e\\4 g\\3>2~ <e\\4 g\\3>2", "<", "2~ <e\\4 g\\3>2") ==
          "[<e\\4 g\\3>4~ r4 <e\\4 g\\3>2~ <e\\4 g\\3>2]");
    CHECK(run(Action::Tie, "g4\\3( g\\3) g\\3", "g4", "g\\3) g\\3") == "[g4\\3(~ g\\3)~ g\\3]");
    CHECK(run(Action::Tie, "g4\\3^\\strokeUp g\\3", "g4", "g\\3") == "[g4\\3^\\strokeUp~ g\\3]");
    // A tie written with a space before it counts.
    CHECK(run(Action::Tie, "g4\\3 ~ g\\3", "g4", "g\\3") == "[g4\\3 ~ g\\3]");
    // Multi-line, with a comment and a command between.
    CHECK(run(Action::Tie, "e4\\1 e\\1\n  % x y\n  \\time 3/4\n  e\\1 |", "e4", "e\\1 |") ==
          "[e4\\1~ e\\1~\n  % x y\n  \\time 3/4\n  e\\1] |");
    // One note: nothing to tie.
    CHECK_FALSE(sr::apply(Action::Tie, "e4\\1 e\\1", {0, 4}));
    // Twice: the same.
    const auto once = sr::apply(Action::Tie, "a4\\3 a4\\3 a4\\3", {0, 14});
    REQUIRE(once);
    const auto twice = sr::apply(Action::Tie, once->text, once->selection);
    REQUIRE(twice);
    CHECK(twice->text == once->text);
}

TEST_CASE("scoreregion slur", "[scoreregion]") {
    CHECK(run(Action::Slur, "a,16\\5 b,\\5 c\\5 d\\4", "a,", "d\\4") ==
          "[a,16\\5( b,\\5 c\\5 d\\4)]");
    CHECK(run(Action::Slur, "a,16\\5 b,\\5", "a,", "b,\\5") == "[a,16\\5( b,\\5)]");
    CHECK(run(Action::Slur, "<e\\4 g\\3>8 <e\\4 g\\3>", "<", "8 <e\\4 g\\3>") ==
          "[<e\\4 g\\3>8( <e\\4 g\\3>)]");
    // Rests at the ends are no slur ends.
    CHECK(run(Action::Slur, "r4 a\\3 b\\3 r4", "r4", "b\\3 r4") == "[r4 a\\3( b\\3) r4]");
    // Already slurred: unchanged.
    CHECK(run(Action::Slur, "a,16\\5( b,\\5)", "a,", "b,\\5)") == "[a,16\\5( b,\\5)]");
    // After a tie and articulations.
    CHECK(run(Action::Slur, "a4\\3~ b\\3^\\strokeUp", "a4", "strokeUp") ==
          "[a4\\3~( b\\3^\\strokeUp)]");
    CHECK_FALSE(sr::apply(Action::Slur, "a4\\3 r4", {0, 7}));
    CHECK_FALSE(sr::apply(Action::Slur, "a4\\3", {0, 4}));
    // Multi-line.
    CHECK(run(Action::Slur, "a\\3\n  b\\3\n  c\\3", "a", "c\\3") == "[a\\3(\n  b\\3\n  c\\3)]");
}

TEST_CASE("scoreregion glissando", "[scoreregion]") {
    CHECK(run(Action::Glissando, "a4\\3 b\\3", "a4", "b\\3") == "[a4\\3 \\glissando b\\3]");
    CHECK(run(Action::Glissando, "<e\\4 g\\3>4 b\\3", "<", "b\\3") ==
          "[<e\\4 g\\3>4 \\glissando b\\3]");
    CHECK(run(Action::Glissando, "r4 a4\\3 b\\3", "r4", "b\\3") == "[r4 a4\\3 \\glissando b\\3]");
    // Already there (also on the next line): unchanged.
    CHECK(run(Action::Glissando, "a4\\3 \\glissando b\\3", "a4", "b\\3") ==
          "[a4\\3 \\glissando b\\3]");
    CHECK(run(Action::Glissando, "a4\\3\n \\glissando b\\3", "a4", "b\\3") ==
          "[a4\\3\n \\glissando b\\3]");
    // One note is enough: it glides into whatever follows.
    CHECK(run(Action::Glissando, "a4\\3 b\\3", "a4", "a4\\3") == "[a4\\3 \\glissando] b\\3");
    CHECK_FALSE(sr::apply(Action::Glissando, "r4 r4", {0, 5}));
}

TEST_CASE("scoreregion repeat", "[scoreregion]") {
    CHECK(run(Action::Repeat, "e4\\1 g\\2 h\\3 d\\1", "g\\2", "h\\3") ==
          "e4\\1 [\\repeat volta 2 { g\\2 h\\3 }] d\\1");
    CHECK(run(Action::Repeat, "e4\\1 g\\2", "e4", "g\\2") == "[\\repeat volta 2 { e4\\1 g\\2 }]");
    // A region of rests only is wrapped too; articulations stay inside.
    CHECK(run(Action::Repeat, "r4 r4", "r4", "4 r4") == "[\\repeat volta 2 { r4 r4 }]");
    CHECK(run(Action::Repeat, "<e\\4 g\\3>4^\\strokeDown\n  e\\1", "<", "strokeDown") ==
          "[\\repeat volta 2 { <e\\4 g\\3>4^\\strokeDown }]\n  e\\1");
    // Twice: nested.
    const auto once = sr::apply(Action::Repeat, "a\\3 b\\3", {0, 7});
    REQUIRE(once);
    const auto twice = sr::apply(Action::Repeat, once->text, once->selection);
    REQUIRE(twice);
    CHECK(twice->text == "\\repeat volta 2 { \\repeat volta 2 { a\\3 b\\3 } }");
    CHECK_FALSE(sr::apply(Action::Repeat, "\\time 3/4", {0, 9}));
    // No space around: one is added.
    CHECK(shown(sr::apply(Action::Repeat, "{a4\\3 b\\3}", {1, 9})) ==
          "{[ \\repeat volta 2 { a4\\3 b\\3 } ]}");
}

TEST_CASE("scoreregion strokes", "[scoreregion]") {
    CHECK(
        run(Action::StrokeDown, "<d\\4 g\\3 d'\\2 g'\\1>8 a4\\3 <e\\4 g\\3>", "<", "<e\\4 g\\3>") ==
        "[<d\\4 g\\3 d'\\2 g'\\1>8^\\strokeDown a4\\3^\\strokeDown <e\\4 g\\3>^\\strokeDown]");
    CHECK(run(Action::StrokeUp, "<d\\4 g\\3>8 a4\\3", "<", "a4\\3") ==
          "[<d\\4 g\\3>8^\\strokeUp a4\\3^\\strokeUp]");
    // Replaced, not doubled; the same one stays.
    CHECK(run(Action::StrokeDown, "<d\\4 g\\3>8^\\strokeUp <e\\4 g\\3>8^\\strokeDown", "<",
              "strokeDown") == "[<d\\4 g\\3>8^\\strokeDown <e\\4 g\\3>8^\\strokeDown]");
    CHECK(run(Action::StrokeUp, "<d\\4 g\\3>8^\\strokeDown <e\\4 g\\3>8^\\strokeUp", "<",
              "strokeUp") == "[<d\\4 g\\3>8^\\strokeUp <e\\4 g\\3>8^\\strokeUp]");
    // After the duration and any articulation, slur or tie; rests get none.
    CHECK(run(Action::StrokeDown, "<d\\4 g\\3>8-. a4\\3~( r4 b\\3", "<", "b\\3") ==
          "[<d\\4 g\\3>8-.^\\strokeDown a4\\3~(^\\strokeDown r4 b\\3^\\strokeDown]");
    // Another articulation next to a stroke stays.
    CHECK(run(Action::StrokeUp, "<d\\4 g\\3>8^\\strokeDown-> a\\3", "<", "a\\3") ==
          "[<d\\4 g\\3>8^\\strokeUp-> a\\3^\\strokeUp]");
    // A doubled one is cleaned up.
    CHECK(run(Action::StrokeDown, "a4\\3^\\strokeDown^\\strokeUp", "a4", "strokeUp") ==
          "[a4\\3^\\strokeDown]");
    // Multi-line.
    CHECK(run(Action::StrokeDown, "<e\\4 g\\3>4\n  <e\\4 g\\3>4", "<", "4\n  <e\\4 g\\3>4") ==
          "[<e\\4 g\\3>4^\\strokeDown\n  <e\\4 g\\3>4^\\strokeDown]");
    // A rest alone: nothing.
    CHECK_FALSE(sr::apply(Action::StrokeDown, "r4", {0, 2}));
    // Twice: the same.
    const auto once = sr::apply(Action::StrokeUp, "<e\\4 g\\3>4 a\\3", {0, 14});
    REQUIRE(once);
    const auto twice = sr::apply(Action::StrokeUp, once->text, once->selection);
    REQUIRE(twice);
    CHECK(twice->text == once->text);
}

TEST_CASE("scoreregion dead notes", "[scoreregion]") {
    CHECK(run(Action::DeadNotes, "e16\\4 g\\3", "e16", "g\\3") ==
          "[\\deadNote e16\\4 \\deadNote g\\3]");
    CHECK(run(Action::DeadNotes, "<g'\\2 h'\\1>8 a\\3", "<", "a\\3") ==
          "[<\\deadNote g'\\2 \\deadNote h'\\1>8 \\deadNote a\\3]");
    // Some have it: the others get it.
    CHECK(run(Action::DeadNotes, "\\deadNote e16\\4 g\\3", "e16", "g\\3") ==
          "\\deadNote [e16\\4 \\deadNote g\\3]");
    CHECK(run(Action::DeadNotes, "<\\deadNote g'\\2 h'\\1>8", "<", ">8") ==
          "[<\\deadNote g'\\2 \\deadNote h'\\1>8]");
    // All have it: removed.
    CHECK(run(Action::DeadNotes, "\\deadNote e16\\4 \\deadNote g\\3", "e16", "g\\3") ==
          "[e16\\4 g\\3]");
    CHECK(run(Action::DeadNotes, "<\\deadNote g'\\2 \\deadNote h'\\1>8 \\deadNote a\\3", "<",
              "a\\3") == "[<g'\\2 h'\\1>8 a\\3]");
    // Toggle twice: the text is back.
    const std::string original = "r4 e16\\4 <g'\\2 h'\\1>8 a\\3 r4";
    const auto on = sr::apply(Action::DeadNotes, original, {3, 24});
    REQUIRE(on);
    CHECK(on->text ==
          "r4 \\deadNote e16\\4 <\\deadNote g'\\2 \\deadNote h'\\1>8 \\deadNote a\\3 r4");
    const auto off = sr::apply(Action::DeadNotes, on->text, on->selection);
    REQUIRE(off);
    CHECK(off->text == original);
    // Rests alone and `\deadNotesOn` are no dead notes.
    CHECK_FALSE(sr::apply(Action::DeadNotes, "r4 r4", {0, 5}));
    CHECK(run(Action::DeadNotes, "\\deadNotesOn e4\\3", "e4", "e4\\3") ==
          "\\deadNotesOn [\\deadNote e4\\3]");
    // Multi-line.
    CHECK(run(Action::DeadNotes, "e4\\3\n  g\\2", "e4", "g\\2") ==
          "[\\deadNote e4\\3\n  \\deadNote g\\2]");
}

TEST_CASE("scoreregion ignores what is no note", "[scoreregion]") {
    // `\key g \major` and `\relative c'` take pitches that are no notes; comments and strings too.
    const std::string text = "\\relative c' { \\key es \\major \\time 3/4 % a b c\n  \"a b\" e4\\1 "
                             "g\\2 } \\transpose c d x";
    CHECK(run(Action::Tie, text, "\\key", "g\\2") ==
          "\\relative c' { \\key es \\major \\time 3/4 % a b c\n  \"a b\" [e4\\1~ g\\2] } "
          "\\transpose c d x");
    const auto span = sr::expandToEvents(text, {0, text.size()});
    REQUIRE(span);
    CHECK(text.substr(span->begin, 6) == "e4\\1 g"s);
}

TEST_CASE("scoreregion survives malformed input and UTF-8", "[scoreregion]") {
    using sr::Range;
    for (const std::string text :
         {"<e\\4 g\\3", "<", "e4\\", "a4\\3(", "\"a b", "%{ a b", "\\key", "\\deadNote", "<<>>",
          ">>", "a4^", "a4^\\", "a4-", "\xc4\x9b\xc5\xa1 a4 \xff b", "<e\\4 g\\3>8^\\stroke"}) {
        for (const Action action : {Action::Tie, Action::Slur, Action::Glissando, Action::Repeat,
                                    Action::StrokeDown, Action::StrokeUp, Action::DeadNotes}) {
            for (std::size_t begin = 0; begin <= text.size(); ++begin) {
                for (std::size_t end = begin; end <= text.size() + 2; ++end) {
                    const auto result = sr::apply(action, text, Range{begin, end});
                    if (result) {
                        CHECK(result->selection.begin <= result->selection.end);
                        CHECK(result->selection.end <= result->text.size());
                    }
                }
            }
        }
    }
    // Czech text before the notes, and a tab-indented line: byte offsets stay exact.
    const std::string text = "% \xc4\x9b\xc5\xa1\xc4\x8d\n\ta4\\3 b\\3";
    CHECK(run(Action::Slur, text, "a4", "b\\3") == "% \xc4\x9b\xc5\xa1\xc4\x8d\n\t[a4\\3( b\\3)]");
    const auto at = sr::offsetOf(text, 2, 1);
    REQUIRE(at);
    CHECK(text.substr(*at, 2) == "a4");
}
