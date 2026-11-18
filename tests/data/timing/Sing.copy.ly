% A synthetic score for the sounding tests: chord names, a staff, a tab staff and a melody that
% makes no sound (Devnull) that times the lyrics.
\score {
  <<
  \new ChordNames { \chordmode { c2 g2:m7 } }
  \new Staff { \tempo 4 = 120 c'4 d' e'2 }
  \new TabStaff { <e, e>2 g'2 }
  \new Devnull = "vocal" { c''4 d''8 e''8 f''2 }
  \new Lyrics \lyricsto "vocal" { la la -- la lo }
  >>
  \layout { }
}
\book {
\bookOutputName "zub-playback"
\score {
  \unfoldRepeats {

  <<
  \new ChordNames { \chordmode { c2 g2:m7 } }
  \new Staff { \tempo 4 = 120 c'4 d' e'2 }
  \new TabStaff \with { midiInstrument = "acoustic guitar (steel)" } { <e, e>2 g'2 }
  \new Devnull = "vocal" { c''4 d''8 e''8 f''2 }
  \new Lyrics \lyricsto "vocal" { la la -- la lo }
  >>
  

  }
  \layout {
    \context { \Score skipTypesetting = ##t }
    \context { \Voice \consists \zubNoteLogger }
    \context { \TabVoice \consists \zubNoteLogger }
    \context { \ChordNames \consists \zubNoteLogger }
    \context { \Devnull \consists \zubNoteLogger }
    \context { \Lyrics \consists \zubNoteLogger }
  }
  \midi {
    \tempo 4 = 100
    \context { \ChordNames \remove "Staff_performer" }
    \context { \Staff midiInstrument = "" }
    \context { \TabStaff midiInstrument = "" }
  }
}
}
