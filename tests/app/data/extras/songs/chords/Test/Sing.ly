\include "zubuvzpevnik.ily"

\score {
  <<
  \new ChordNames { \chordmode { c1 | g1:m7 | } }
  \new TabStaff {
    {
      \tabFullNotation
      <e, e>2 g'2 | a'2 e'2 |
    }
  }
  % the melody that times the lyrics, printed nowhere
  \new Devnull = "vocal" { c''4 d''8 e''8 f''2 | g''2 f''2 | }
  \new Lyrics \lyricsto "vocal" { la la -- la lo you all }
  >>
}
