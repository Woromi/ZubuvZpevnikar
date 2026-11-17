\include "zubuvzpevnik.ily"

\score {
<<
  \new ChordNames { \chordmode { a1:m | a:m | } }
  \new TabStaff \with {
    tablatureFormat = #fret-number-tablature-format-banjo
    stringTunings = #banjo-open-g-tuning
    instrumentName = "Banjo "
  }
  {
    \tabFullNotation
    \time 4/4
    a4\3 e'8\1 a\3 h\2 e'\1 a4\3 |
    a4\3 e'8\1 a\3 h\2 e'\1 a4\3 |
  }
  \new TabStaff \with {
    instrumentName = "Kytara "
  }
  {
    \tabFullNotation
    \time 4/4
    <a,\5 e\4 a\3 c'\2 e'\1>1 |
    <a,\5 e\4 a\3 c'\2 e'\1>1 |
  }
>>
  \layout { indent = 0 }
}
