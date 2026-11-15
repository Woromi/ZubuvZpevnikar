# Marking a check finding right (`% nocheck`)

Decided with the user 2026-11-15. One grammar for every checker of a song: the converter's
`check`, the package's `checkkeys.py` and `checksongs.py`, and what the app writes when the user
confirms a finding. It may become more precise later.

## The mark

A line of its own, a TeX comment, so LaTeX ignores it:

```
% nocheck <kind> [<kind> …]
```

- **Above a line:** directly above the line a finding is about, it confirms the findings of
  those kinds on that line and nothing else. Other checks of the same line stay on. Several
  marks may stand one above the other, or one mark may name several kinds
  (`% nocheck typo comma`).
- **Song-wide kinds** (a finding about the whole song, not one line): the mark may stand
  anywhere in the song. Today: `apostrophes`, `tie` (converter). The app puts it before the
  first part.
- **The header:** a mark may stand inside the song header's `[…]`, on a line of its own, e.g. a
  confirmed key directly above the `key=` line:

  ```
  \beginsong{Shy}[
      by={Sonata Arctica},
      % nocheck key
      key={Dm},
  ]
  ```

- **A bare `% nocheck`** (no kind) confirms every finding on the next line. It is read for older
  songs, but nothing writes it any more. The songbook's 23 bare marks were converted to their
  kinds on 2026-11-15: 22 `comma`, 1 `typo`.
- **Indentation:** the mark is indented like the line it is about.

## Kinds

Each checker reports a `kind` per finding in its `--json` output, together with the 1-based line
it is about. A finding about the header gives the line of the key it concerns (e.g. the `key=`
line), or the `\beginsong` line when the key is missing.

| Checker | Kinds |
|---|---|
| converter `check` | `restructure`, `typo`, `comma`, `chordlines`, `apostrophes` (song-wide), `tie` (song-wide) |
| `checkkeys.py` | `key` (the stated key disagrees with the chords; a missing key is no finding to confirm, it is filled in) |
| `checksongs.py` | one kind per check, named in its `--json` (e.g. `tem`, `met`: "this song really has none") |

A checker skips a finding when a mark for its kind (or a bare mark) stands right above its line,
or anywhere for a song-wide kind.

## Writing a mark

The converter gives each correction a `dismiss` edit that adds the mark (since 0.15.0 with the
kind; when a kinded mark already stands above the line, the kind is added to it). The app applies
such edits as one undo step and writes the same form for the package's checkers.
