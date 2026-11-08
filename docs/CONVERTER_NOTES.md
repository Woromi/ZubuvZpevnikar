
## 0.6.0: `shrink` and `check`

- `suggest` is now `shrink`: same options, same output. `suggest` still
  works as an old name, but switch to `shrink`.
- Restructuring left `shrink` for a new command, `check SONG.tex [--json]`:
  corrections, whether the song fits or not (the user: Make Smaller may be
  disabled for a song that fits, and checking for mistakes doesn't make it
  smaller). Same JSON shape; proposals in song order; no `--fit`,
  `--measure` or `--songbook`. Give it its own button ("Check song…"), and
  after accepting a restructure, run Tidy. Typo checks will come to
  `check` later.
- `apply --id` takes ids from `shrink`, `join` and `check` alike.
- `shrink` no longer offers `\insertchorus*` for a chorus whose chords
  differ from the chorus before (more than one chord): the reminder prints
  no chords, so a chorus in another key keeps its own.
- Raise the minimum converter version to 0.6.0.
