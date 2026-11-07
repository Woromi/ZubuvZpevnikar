# Test data

`songbook/` is a miniature ZubuvZpevnik checkout for the core tests.

- `songs/chords/*` except `Broken/` and `Synthetic/` are verbatim copies of real songs from ../ZubuvZpevnik, chosen
  because they trip naive parsers (nested braces in the title, commented-out keys, commas and
  `\ ` inside values, `\flt`, `\&`). They are copies on purpose: the real repo changes on its own.
- `Broken/` holds files the scanner must report as issues.
- `Synthetic/` holds valid hand-written songs for edge cases (e.g. a commented-out second header).
- `.ly` and `.log` files must be ignored by the scanner.
- `zubuvzpevnik/*.cls` are empty placeholders: only their names matter (document class discovery).
