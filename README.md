# Zpěvníkář (ZubuvZpevnikar)

A desktop app for working on [Zubův Zpěvník](https://github.com/Woromi/ZubuvZpevnik), a LaTeX
songbook: edit its songs with a live PDF preview, import new ones from chord websites, check and
tidy them, and put songbooks together for print.

*Zpěvníkář* is Czech for "the songbook maker".

## What it does

- **Songs**: browse the songbook's songs (search, language, TODO notes), edit a song with syntax
  highlighting and see its PDF next to it, rebuilt as you type. Songs with commands outside the
  song format are only built after you confirm (LaTeX runs with `-shell-escape`).
- **Import**: paste a URL from a supported chords website (or a saved page) and get the song in the
  songbook's format, through [chords_converter](https://github.com/Woromi/chords_converter).
  Imported songs are marked "not checked yet" until you mark them checked.
- **Song actions** from chords_converter, each applied as one undo step with a preview of what
  changes:
  - *Tidy*: a chorus typed again becomes `\insertchorus`, chords move to the start of their
    syllable, spacing is cleaned up.
  - *Check Song*: typos where a line's copies differ, apostrophes typed two ways, a chorus typed as
    a verse.
  - *Make Smaller* and *Join Lines*: changes that make a song fit its page, rated by how readable
    they leave it.
- **Scan Songbook**: run those checks over all songs and list the ones with possible changes.
- **Songbooks**: pick songs into sections (sorted the way the printed index sorts them, or arranged
  by hand), choose the book's class (A4 book, A5 book, A5 camp booklet), cover and indexes, and
  export a PDF, the LaTeX source, or a self-contained folder that builds anywhere.

The app does not reimplement the songbook: songs, classes and build scripts come from a
ZubuvZpevnik checkout, and conversions from chords_converter.

## Building

Requirements:

- CMake ≥ 3.25 and a C++23 compiler (developed with GCC 13)
- Qt ≥ 6.5 with the Widgets and PDF modules
- KF6 SyntaxHighlighting
- Catch2 v3 (tests)
- Rust and cargo, to build the bundled chords_converter (optional)

To build songs and songbooks you also need what the songbook needs (see its `docs/building.md`):
TeX Live with LuaLaTeX and the `songs` and `lyluatex` packages, LilyPond, the Open Sans, Roboto Mono
and FreeSans fonts, and a Czech locale (`cs_CZ.UTF-8`) for the index order.

```bash
git clone --recurse-submodules https://github.com/Woromi/ZubuvZpevnikar.git
cd ZubuvZpevnikar
cmake --preset debug          # or: release
cmake --build --preset debug
ctest --preset debug          # unit tests
build/debug/app/zpevnikar
```

Without cargo, or without the submodule (`git submodule update --init`), the converter is not
built; the app then finds one you installed yourself, or you choose one in the import dialog.
`-DZUB_BUILD_APP=OFF` builds only the core library and its tests, without Qt.

`cmake --install build/release --prefix ~/.local` installs the app with its converter.

## First start

The app asks for your ZubuvZpevnik checkout (a folder with `songs/chords/` and `zubuvzpevnik/`) and
remembers it. Songbooks you create are stored by the app, not in the checkout; exports and previews
are built in the app's cache folder, never inside the checkout.

## Layout

- `core/`: the song and songbook logic in standard C++23, without Qt (parsing, generating LaTeX,
  preparing builds, reading the converter's output). Unit tests in `tests/`.
- `app/`: the Qt 6 Widgets user interface.
- `external/chords_converter`: the converter, as a git submodule.
- `docs/PLAN.md`: what is done and what is planned.

## License

GPL-3.0-or-later; see [LICENSE](LICENSE).
