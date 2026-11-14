\include "zubuvzpevnik.ily"

\score {
  \new TabStaff {
    \tabFullNotation
    \tempo 4 = 100
    % made-up bends and slides for the tests
    a'4\2\^ \grace h'8\2 a'4\2 | a'\2\preBend \^ \grace h'\2 a'8\2\^ \grace h'8\2\^ a'4\2 r4 |
    d'4\3 \glissando g'\2 g'\2 e'\4 | e'\4 \glissando <a\3 e'\2>
  }
}
