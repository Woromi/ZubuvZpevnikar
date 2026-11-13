#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <zub/core/textfile.hpp>
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
          "[<e\\4 g\\3>4 r4 <e\\4 g\\3>2~ <e\\4 g\\3>2]");
    CHECK(run(Action::Tie, "a4\\3 r a\\3 a\\3 r a\\3", "a4", "a\\3 r a\\3") ==
          "[a4\\3 r a\\3~ a\\3 r a\\3]");
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
          "[\\deadNote e16\\4 \\deadNote g\\3]");
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

TEST_CASE("scoreregion ignores Scheme after # and $", "[scoreregion]") {
    // Made-up text: identifiers, quoted lists, strings and booleans that contain note names.
    const std::string text =
        "#banjo-open-g-tuning ##f #'(c d e) #(list \"a\" 'b) #\"g\" #-6 $e c4 d4 f4";
    CHECK(run(Action::StrokeDown, text, "c4", "f4") ==
          "#banjo-open-g-tuning ##f #'(c d e) #(list \"a\" 'b) #\"g\" #-6 $e "
          "[c4^\\strokeDown d4^\\strokeDown f4^\\strokeDown]");
    // A region that starts at a `##f`: the repeat wraps the notes, not the Scheme.
    const std::string t2 = "x ##f c4 d4";
    CHECK(shown(sr::apply(Action::Repeat, t2, {0, t2.size()})) ==
          "x ##f [\\repeat volta 2 { c4 d4 }]");
    // Nothing but Scheme: no token at all.
    const std::string t3 = "#banjo-open-g-tuning #'(a b c)";
    CHECK_FALSE(sr::expandToEvents(t3, {0, t3.size()}));
    // Balanced parentheses with a string holding `)` and a `;` comment.
    const std::string t4 = "#(foo \")\" ; a )\n b) c4";
    const auto only = sr::expandToEvents(t4, {0, t4.size()});
    REQUIRE(only);
    CHECK(t4.substr(only->begin, only->end - only->begin) == "c4");
}

namespace {

// A made-up score: chord names, a mark, a variable chord, lyrics and the tab voice.
const std::string madeUpScore =
    "emChord = <e,\\6 b,\\5 e\\4>4\n"
    "chordNames = \\chordmode { e1:m a2 b }\n"
    "words = \\lyricmode { la4 la8 be2 }\n"
    "\\score {\n"
    "  <<\n"
    "    \\new ChordNames \\chordNames\n"
    "    \\new TabStaff \\with { stringTunings = #banjo-open-g-tuning } {\n"
    "      \\mark \\default\n"
    "      \\markup { \\bold a b c }\n"
    "      \\emChord g'8\\3 a\\2 <b\\3 d'\\2>4\n"
    "    }\n"
    "    \\new Lyrics \\lyricsto \"x\" { a b c d e }\n"
    "  >>\n"
    "  \\addlyrics { a4 b c }\n"
    "}\n";

} // namespace

TEST_CASE("scoreregion actions never touch chord names, lyrics, marks or definitions",
          "[scoreregion]") {
    const sr::Range all{0, madeUpScore.size()};
    const auto touched = [&](Action action) {
        const auto result = sr::apply(action, madeUpScore, all);
        REQUIRE(result);
        return result->text;
    };
    // Only the tab voice changed: the lines above and below it stay byte for byte.
    const std::string head = madeUpScore.substr(0, madeUpScore.find("      \\emChord"));
    const std::string tail = madeUpScore.substr(madeUpScore.find("    }\n    \\new Lyrics"));
    for (const Action action : {Action::Tie, Action::Slur, Action::Glissando, Action::StrokeDown,
                                Action::StrokeUp, Action::DeadNotes, Action::Repeat}) {
        const std::string text = touched(action);
        CAPTURE(static_cast<int>(action));
        CHECK(text.starts_with(head));
        CHECK(text.ends_with(tail));
    }
    CHECK(touched(Action::StrokeDown).find("g'8\\3^\\strokeDown a\\2^\\strokeDown "
                                           "<b\\3 d'\\2>4^\\strokeDown") != std::string::npos);
    CHECK(touched(Action::Repeat).find("\\repeat volta 2 { g'8\\3 a\\2 <b\\3 d'\\2>4 }") !=
          std::string::npos);
    // Only a variable's definition in the range: nothing to do.
    const auto defOnly = sr::expandToEvents(madeUpScore, {0, madeUpScore.find('\n')});
    CHECK_FALSE(defOnly);
}

TEST_CASE("scoreregion regions take the music functions written before the first note",
          "[scoreregion]") {
    CHECK(run(Action::Repeat, "a \\deadNote c4 d4 e4", "c4", "d4") ==
          "a [\\repeat volta 2 { \\deadNote c4 d4 }] e4");
    CHECK(run(Action::Repeat, "x \\grace af'16 c4 d4", "af'16", "d4") ==
          "x [\\repeat volta 2 { \\grace af'16 c4 d4 }]");
    CHECK(run(Action::Repeat, "\\acciaccatura b8 c4 d4", "b8", "c4") ==
          "[\\repeat volta 2 { \\acciaccatura b8 c4 }] d4");
    // The grace note itself is a note; the region that starts after it does not take it.
    CHECK(run(Action::Repeat, "\\grace b8 c4 d4", "c4", "d4") ==
          "\\grace b8 [\\repeat volta 2 { c4 d4 }]");
    CHECK(run(Action::Repeat, "\\tweak color #red \\parenthesize c4 d4", "c4", "c4") ==
          "[\\repeat volta 2 { \\tweak color #red \\parenthesize c4 }] d4");
    CHECK(run(Action::Repeat, "\\once \\override NoteHead.color = #red c4 d4", "c4", "c4") ==
          "[\\repeat volta 2 { \\once \\override NoteHead.color = #red c4 }] d4");
    // A lasting \override is not part of the note.
    CHECK(run(Action::Repeat, "\\override NoteHead.color = #red c4 d4", "c4", "c4") ==
          "\\override NoteHead.color = #red [\\repeat volta 2 { c4 }] d4");
    // Something else in between ends the prefix.
    CHECK(run(Action::Repeat, "\\grace { e8 } c4 d4", "c4", "d4") ==
          "\\grace { e8 } [\\repeat volta 2 { c4 d4 }]");
}

TEST_CASE("scoreregion Repeat refuses a region that crosses a block", "[scoreregion]") {
    const std::string text = "\\new Voice { c4 d4 } \\new Voice { e4 f4 }";
    sr::Refusal why = sr::Refusal::NothingToApply;
    CHECK_FALSE(sr::apply(Action::Repeat, text, between(text, "d4", "e4"), &why));
    CHECK(why == sr::Refusal::CrossesBlock);
    // A block inside the region is fine, as are braces in strings, comments and Scheme.
    const std::string ok = "c4 \\tuplet 3/2 { d8 e f } g4 \"}\" #'(})";
    CHECK(sr::apply(Action::Repeat, ok, {0, ok.size()}));
    const std::string comment = "{ c4 % }\n d4 }";
    CHECK(sr::apply(Action::Repeat, comment, between(comment, "c4", "d4")));
    // Nothing to apply to is told apart.
    CHECK_FALSE(sr::apply(Action::Tie, "{ }", {0, 3}, &why));
    CHECK(why == sr::Refusal::NothingToApply);
}

TEST_CASE("scoreregion treats q (chord repetition) as a chord", "[scoreregion]") {
    const std::string text = "<e\\4 g\\3>2 q q4 d4";
    CHECK(run(Action::StrokeDown, text, "q", "q4") ==
          "<e\\4 g\\3>2 [q^\\strokeDown q4^\\strokeDown] d4");
    CHECK(run(Action::Tie, text, "<", "q4") == "[<e\\4 g\\3>2~ q~ q4] d4");
    CHECK(run(Action::Slur, text, "q", "q4") == "<e\\4 g\\3>2 [q( q4)] d4");
    // No pitch to kill.
    CHECK_FALSE(sr::apply(Action::DeadNotes, text, between(text, "q", "q4")));
    // Words that merely contain a q are none.
    CHECK_FALSE(sr::expandToEvents("seq qa", {0, 6}));
}

TEST_CASE("scoreregion articulations with spaces and arguments are part of the note",
          "[scoreregion]") {
    // Stroke Down, then Stroke Up on a spaced stroke: one stroke at the end.
    const std::string text = "<e\\4 g\\3>2. ^\\strokeDown a4";
    const auto down = sr::apply(Action::StrokeDown, text, {0, 14});
    REQUIRE(down);
    CHECK(down->text == text);
    const auto up = sr::apply(Action::StrokeUp, text, {0, 14});
    REQUIRE(up);
    CHECK(up->text == "<e\\4 g\\3>2. ^\\strokeUp a4");
    const auto back = sr::apply(Action::StrokeDown, up->text, up->selection);
    REQUIRE(back);
    CHECK(back->text == text);
    // A markup keeps its argument; the repeat does not split it.
    CHECK(run(Action::Repeat, "c4^\\markup { \\bold x } d4 e4", "c4", "d4") ==
          "[\\repeat volta 2 { c4^\\markup { \\bold x } d4 }] e4");
    CHECK(run(Action::Repeat, "c4^\\markup \"x\" d4", "c4", "c4") ==
          "[\\repeat volta 2 { c4^\\markup \"x\" }] d4");
    // An articulation with a Scheme argument; a tweak before one.
    CHECK(run(Action::Repeat, "<c\\3>4-\\bendAfter #+4 d4", "<", "bendAfter #+4") ==
          "[\\repeat volta 2 { <c\\3>4-\\bendAfter #+4 }] d4");
    CHECK(run(Action::Repeat, "c4-\\tweak color #red ^\"x\" d4", "c4", "c4") ==
          "[\\repeat volta 2 { c4-\\tweak color #red ^\"x\" }] d4");
    // `\markup` followed by a music command: that stays outside.
    CHECK(run(Action::Repeat, "c4^\\markup \\bold \"x\" \\time 3/4 d4", "c4", "c4") ==
          "[\\repeat volta 2 { c4^\\markup \\bold \"x\" }] \\time 3/4 d4");
}

TEST_CASE("scoreregion Slur refuses to nest", "[scoreregion]") {
    sr::Refusal why = sr::Refusal::NothingToApply;
    // A slur is open across the start of the region.
    const std::string open = "{ c4( d4 e4) f4 }";
    CHECK_FALSE(sr::apply(Action::Slur, open, between(open, "d4", "e4"), &why));
    CHECK(why == sr::Refusal::SlurPresent);
    // The region has slur marks of its own.
    const std::string inner = "{ c4 d4( e4) f4 }";
    CHECK_FALSE(sr::apply(Action::Slur, inner, between(inner, "c4", "f4"), &why));
    CHECK(why == sr::Refusal::SlurPresent);
    // A slur that closed before the region does not matter; the same slur is a no-op.
    const std::string closed = "{ c4( d4) e4 f4 g4( a4) }";
    CHECK(run(Action::Slur, closed, "e4", "f4") == "{ c4( d4) [e4( f4)] g4( a4) }");
    const std::string done = "{ c4 d4( e4) }";
    CHECK(run(Action::Slur, done, "d4", "e4") == "{ c4 [d4( e4)] }");
    // A slur of another block does not count; the strings and phrasing slurs neither.
    const std::string other = "{ a4( b4 } { c4 d4 }";
    CHECK(sr::apply(Action::Slur, other, between(other, "c4", "d4")));
    const std::string phrasing = "c4\\( d4^\"(x\" e4 f4\\)";
    CHECK(run(Action::Slur, phrasing, "d4", "e4") == "c4\\( [d4^\"(x\"( e4)] f4\\)");
}

TEST_CASE("scoreregion musicEvents lists only the music, in text order", "[scoreregion]") {
    const auto events = sr::musicEvents(madeUpScore);
    std::vector<std::string> found;
    for (const sr::Range& r : events)
        found.push_back(madeUpScore.substr(r.begin, r.end - r.begin));
    // The chord names, lyrics, marks, the variable's value: none of them.
    const std::vector<std::string> want = {"g'8\\3", "a\\2",
                                           "<b\\3 d'\\2>4"};
    CHECK(found == want);
    // Prefix functions belong to their event; `q` and rests are events.
    const std::string text = "\\deadNote c4 q8 r2 \\grace d16 e";
    const auto more = sr::musicEvents(text);
    REQUIRE(more.size() == 5);
    CHECK(text.substr(more[0].begin, more[0].end - more[0].begin) == "\\deadNote c4");
    CHECK(text.substr(more[3].begin, more[3].end - more[3].begin) == "\\grace d16");
    // eventAt: inside, at the begin, in the gap between events.
    CHECK(sr::eventAt(more, 3) == 0);
    CHECK(sr::eventAt(more, more[1].begin) == 1);
    CHECK_FALSE(sr::eventAt(more, more[0].end));
    CHECK(sr::eventAt(more, more[4].end - 1) == 4);
    CHECK_FALSE(sr::eventAt(more, text.size()));
}

// Run with: build/debug/tests/zub_core_tests "[integration]"
// Reads every real score (read-only, in memory) and applies every action to every music event and
// to a few contiguous ranges: the text outside the region stays byte for byte, the braces stay
// balanced (Repeat adds one pair), what lies between the events of the region survives, and
// applying again changes nothing (Repeat nests, Dead Notes toggles back).
TEST_CASE("every action on the real scores keeps the rest of the text", "[.integration][scoreregion]") {
    namespace fs = std::filesystem;
    const fs::path songs = fs::path(ZUB_REAL_SONGBOOK_DIR) / "songs";
    if (!fs::is_directory(songs))
        SKIP("no songbook checkout at " << songs);
    const auto count = [](const std::string& s, char c) { return std::ranges::count(s, c); };
    const std::vector<std::pair<const char*, Action>> actions = {
        {"Tie", Action::Tie},           {"Slur", Action::Slur},
        {"Glissando", Action::Glissando}, {"Repeat", Action::Repeat},
        {"StrokeDown", Action::StrokeDown}, {"StrokeUp", Action::StrokeUp},
        {"DeadNotes", Action::DeadNotes}};
    std::size_t files = 0, events = 0, applications = 0;
    std::map<std::string, std::size_t> applied, refused, nothing;
    for (const auto& entry : fs::recursive_directory_iterator(songs)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".ly")
            continue;
        const auto content = zub::readTextFile(entry.path());
        REQUIRE(content);
        const std::string& text = *content;
        const auto all = sr::musicEvents(text);
        ++files;
        events += all.size();
        if (all.empty())
            continue;
        // The ranges: every event alone, and a few windows of events.
        std::vector<sr::Range> ranges;
        for (const sr::Range& e : all)
            ranges.push_back(e);
        const std::size_t n = all.size();
        for (const std::size_t width : {std::size_t{2}, std::size_t{5}, std::size_t{12}, n}) {
            for (const std::size_t start : {std::size_t{0}, n / 3, n / 2, n > width ? n - width : 0}) {
                const std::size_t last = std::min(n, start + width) - 1;
                if (start <= last)
                    ranges.push_back({all[start].begin, all[last].end});
            }
        }
        for (const sr::Range& range : ranges) {
            const auto region = sr::expandToEvents(text, range);
            REQUIRE(region);
            // The gaps between the events of the region.
            std::vector<std::string> gaps;
            for (std::size_t i = 0; i + 1 < all.size(); ++i) {
                if (all[i].begin >= region->begin && all[i + 1].end <= region->end &&
                    all[i].end < all[i + 1].begin)
                    gaps.push_back(text.substr(all[i].end, all[i + 1].begin - all[i].end));
            }
            for (const auto& [name, action] : actions) {
                INFO(entry.path().string() << " " << name << " [" << range.begin << "," << range.end << ")");
                sr::Refusal why = sr::Refusal::NothingToApply;
                const auto result = sr::apply(action, text, range, &why);
                ++applications;
                if (!result) {
                    ++(why == sr::Refusal::NothingToApply ? nothing : refused)[name];
                    continue;
                }
                ++applied[name];
                const std::string& out = result->text;
                // Outside the region: untouched.
                REQUIRE(out.starts_with(text.substr(0, region->begin)));
                REQUIRE(out.ends_with(text.substr(region->end)));
                // Braces: balanced as before; Repeat adds exactly one pair.
                const long pair = action == Action::Repeat ? 1 : 0;
                REQUIRE(count(out, '{') == count(text, '{') + pair);
                REQUIRE(count(out, '}') == count(text, '}') + pair);
                // What lies between the events of the region (commands, bar lines, chord names
                // inside a long range) is still there, in order.
                std::size_t at = region->begin;
                for (const std::string& gap : gaps) {
                    const auto found = out.find(gap, at);
                    REQUIRE(found != std::string::npos);
                    at = found + gap.size();
                }
                // Again: nothing changes (Dead Notes toggles back to the original).
                const auto again = sr::apply(action, out, result->selection);
                if (action == Action::Repeat)
                    continue;
                if (action == Action::DeadNotes) {
                    REQUIRE(again);
                    // A toggle: braces and the text outside the (moved) region stay.
                    REQUIRE(count(again->text, '{') == count(text, '{'));
                    REQUIRE(count(again->text, '}') == count(text, '}'));
                    REQUIRE(again->text.starts_with(text.substr(0, region->begin)));
                    REQUIRE(again->text.ends_with(text.substr(region->end)));
                    continue;
                }
                REQUIRE(again);
                REQUIRE(again->text == out);
            }
        }
    }
    CHECK(files > 0);
    std::cerr << "scores: " << files << ", music events: " << events
              << ", applications: " << applications << "\n";
    for (const auto& [name, action] : actions) {
        std::cerr << "  " << name << ": applied " << applied[name] << ", refused " << refused[name]
                  << ", nothing to apply " << nothing[name] << "\n";
    }
}
