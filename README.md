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

The app does not reimplement the songbook: the songs come from a ZubuvZpevnik checkout, the
classes and build scripts from the [zubuvzpevnik-package](https://github.com/Woromi/zubuvzpevnik-package)
(the songbook's own `zubuvzpevnik/` submodule, else the copy bundled with the app), and
conversions from chords_converter (0.14.0 or newer).

## Install

Linux: each [release](https://github.com/Woromi/ZubuvZpevnikar/releases) has an AppImage
(Ubuntu 24.04, Debian 13, Fedora 40 or newer). Make it executable (`chmod +x
Zpevnikar-*.AppImage`) and run it; the converter and the LaTeX package are inside. Install the
programs the songs are built with yourself (see the requirements below). Windows is not supported
yet; CI builds it to show what is missing.

## Building

Requirements:

- CMake ≥ 3.25 and a C++23 compiler (developed with GCC 13)
- Qt ≥ 6.5 with the Widgets, Svg, PDF, Multimedia and Linguist tools modules
- KF6 SyntaxHighlighting
- Catch2 v3 (tests; downloaded at configure time when it is not installed)
- Rust and cargo, to build the bundled chords_converter (optional)

To build songs and songbooks you also need what the songbook needs (see its `docs/building.md`):
TeX Live with LuaLaTeX and the `songs` and `lyluatex` packages, LilyPond, the Open Sans, Roboto Mono
and FreeSans fonts, and a Czech locale (`cs_CZ.UTF-8`) for the index order.

Optional: `fluidsynth` and a General MIDI soundfont (e.g. `/usr/share/sounds/sf2/default-GM.sf2`) for
playing scores and the sound of a clicked fret; without them those buttons are disabled.

```bash
git clone --recurse-submodules https://github.com/Woromi/ZubuvZpevnikar.git
cd ZubuvZpevnikar
cmake --preset debug          # or: release
cmake --build --preset debug
ctest --preset debug          # unit tests
build/debug/app/zpevnikar
```

Without cargo, or without the submodule (`git submodule update --init`), the converter is not
built; the app then finds one you installed yourself (on `PATH` or in `~/.cargo/bin`).
`ZUB_CONVERTER=/absolute/path/to/chords_converter` makes the app use another build of it, e.g. to
try a converter change.
Without the `external/zubuvzpevnik-package` submodule the app has no package of its own and builds
only songbooks that have theirs.
`-DZUB_BUILD_APP=OFF` builds only the core library and its tests, without Qt.

`cmake --install build/release --prefix ~/.local` installs the app with its converter, its copy of
the package (`share/zpevnikar/zubuvzpevnik`), a launcher entry and the icon.

## First start

The app asks for your ZubuvZpevnik checkout (a folder with `songs/chords/`; clone it with
`--recurse-submodules` so its `zubuvzpevnik/` package is there too), or creates a new one, and
remembers it. Songbooks are the `.tex` books in its `songbooks/`; exports and previews are built
in the app's cache folder, never inside the checkout.

## Releasing

Set the version in `CMakeLists.txt` (`project(... VERSION x.y.z)`), commit, then tag and push
`vx.y.z`: the Release workflow builds the AppImage and makes a GitHub release with it (the tag
must match the version). Actions → Release → Run workflow builds it without releasing, as an
artifact to try first. `packaging/appimage.sh` packs a local release build the same way (it
needs linuxdeploy and its Qt plugin).

## Layout

- `core/`: the song and songbook logic in standard C++23, without Qt (parsing, generating LaTeX,
  preparing builds, reading the converter's output). Unit tests in `tests/`.
- `app/`: the Qt 6 Widgets user interface.
- `external/chords_converter`: the converter, as a git submodule.
- `external/zubuvzpevnik-package`: the LaTeX package and its build scripts, as a git submodule (the
  copy used for songbooks without their own).
- `docs/PLAN.md`: what is done and what is planned.

## License

GPL-3.0-or-later; see [LICENSE](LICENSE). The app icon was made with Google Gemini.
