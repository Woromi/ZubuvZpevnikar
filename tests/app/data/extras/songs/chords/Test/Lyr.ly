\include "zubuvzpevnik.ily"

tune = { c'4 d' e' f' | g'1 | }

words = \lyricmode { La la la la la }

\score {
  <<
  \new Staff { c'1 | c'1 | }
  % the notes the lyrics follow, printed nowhere
  \new Devnull = "vocal" { \tune }
  \new Lyrics \lyricsto "vocal" { \words }
  >>
}
