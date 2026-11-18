% A synthetic score for the timing tests.
intro = { c'4 d' }
\score {
  <<
  \new Staff {
    \tempo 4 = 120
    \intro
    \repeat volta 2 { <e e'>4 g' } \alternative { { a'4 } { b'8 \tempo 4 = 60 c''8 } }
    d'8 \tuplet 3/2 { e'8 f' g' } \grace a'8 b'4 r4
  }
  \new TabStaff {
    \repeat volta 2 { <e,\6 e\4>4 g'\2 } \alternative { { a'4 } { b'8 c''8 } }
  }
  >>
  \layout { }
}
\book {
\bookOutputName "zub-playback"
\score {
  \unfoldRepeats {

  <<
  \new Staff {
    \tempo 4 = 120
    \intro
    \repeat volta 2 { <e e'>4 g' } \alternative { { a'4 } { b'8 \tempo 4 = 60 c''8 } }
    d'8 \tuplet 3/2 { e'8 f' g' } \once \set Staff.midiInstrument = "gunshot" \grace a'8 b'4 r4
  }
  \new TabStaff \with { midiInstrument = "acoustic guitar (steel)" } {
    \repeat volta 2 { <e,\6 e\4>4 g'\2 } \alternative { { a'4 } { b'8 c''8 } }
  }
  >>
  

  }
  \layout {
    \context { \Score skipTypesetting = ##t }
    \context { \Voice \consists \zubNoteLogger }
    \context { \TabVoice \consists \zubNoteLogger }
  }
  \midi {
    \tempo 4 = 100
    \context { \ChordNames \remove "Staff_performer" }
    \context { \Staff midiInstrument = "" }
    \context { \TabStaff midiInstrument = "" }
  }
}
}
