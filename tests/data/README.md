# Test data

`songbook/` is a miniature ZubuvZpevnik checkout for the core tests.

- `songs/chords/*` except `Broken/` and `Synthetic/` keep the headers of real songs from ../ZubuvZpevnik
  (titles and artists are not protected), chosen because they trip naive parsers (nested braces in the
  title, commented-out keys, commas and `\ ` inside values, `\flt`, `\&`). The bodies (lyrics and chords)
  are invented: the repo is public and real lyrics must not be in it. The invented bodies keep the structure
  the tests rely on (chords, `\chordsoff`, `\multivoice`, `\rep`, `\lilypondfile`, ...).
  They are copies on purpose: the real repo changes on its own.
- `Broken/` holds files the scanner must report as issues.
- `Synthetic/` holds valid hand-written songs for edge cases (e.g. a commented-out second header).
- `.ly` and `.log` files must be ignored by the scanner.
- `zubuvzpevnik/*.cls` are empty placeholders: only their names matter (document class discovery).
