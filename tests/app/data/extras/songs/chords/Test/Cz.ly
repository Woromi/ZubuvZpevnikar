\include "zubuvzpevnik.ily"

emChord = <e,\6 h,\5 e\4 g\3 h\2 e'\1>4
chordNames = \chordmode { e1:m a2:m d2 }
words = \lyricmode { la4 la8 be2 }

\score {
  <<
  \new ChordNames \chordNames
  \new TabStaff {
    {
      \tabFullNotation
      % ěščř poznámka
      \emChord e'\1 g'\1 |
	a,16\5 h,\5 <d\4 g\3 d'\2 g'\1>8 <d\4 g\3 d'\2 g'\1>8 |
      %{ ěščř %} e16\4 g\3 a\3 h\2 | \break
      <e,\6 h,\5 e\4>4 e'\1 g'\1 |
      a,16\5 h,\5 <d\4 g\3 d'\2>8 e16\4 g\3 |
    }
  }
  >>
}
