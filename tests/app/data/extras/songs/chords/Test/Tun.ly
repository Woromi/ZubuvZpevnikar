\include "zubuvzpevnik.ily"

\score {
  <<
  \new TabStaff \with {
    stringTunings = #banjo-open-g-tuning
  } {
    \tabFullNotation
    g'\5 d'\3 |
  }
  \new TabStaff {
    \tabFullNotation
    e'\1 h\2 |
  }
  >>
}
