# Plan

Milestones are small on purpose. Each one ends with something you can run and try.
Tick items as they're done. Use `/new-feature <item>` to work through one.

## 0. Prerequisites & skeleton

- [x] Install the dev packages (verified available 2026-11-07: Qt 6.10.2 and KF6 6.24 from the TUXEDO repo, Catch2 3.4):
      `sudo apt install qt6-base-dev qt6-pdf-dev libkf6syntaxhighlighting-dev catch2 ninja-build clangd clang-tidy`
- [x] CMake skeleton: top-level `CMakeLists.txt`, `CMakePresets.json` (debug/release, Ninja, `CMAKE_EXPORT_COMPILE_COMMANDS`), C++23 (for `std::expected`), warnings on.
- [x] `core/` static library, std-only, which must not link Qt. One trivial function plus a test.
- [x] `tests/` with Catch2 v3 (`Catch2::Catch2WithMain`, `catch_discover_tests`) registered in CTest.
- [x] `app/` Qt Widgets executable that links `core`, showing an empty main window.
- [x] Update the "Build & test" section of CLAUDE.md with the real commands.

## 1. Open the songbook and browse songs

- [ ] core: config with the path to the ZubuvZpevnik repo (default `../ZubuvZpevnik`, overridable, persisted).
- [ ] core: scan `songs/chords/` → list of songs (artist, title, path).
- [ ] core: parse song metadata from `\beginsong{Title}[by=…, key=…, lang=…]` (tests with real songs as fixtures).
- [ ] app: main window with a filterable song list (artist / title / language).

## 2. Editor

- [ ] app: open a song in an editor tab (QPlainTextEdit + KSyntaxHighlighting LaTeX definition).
- [ ] app: save, dirty-state marker, "reload from disk" when the file changes outside the app.
- [ ] (later) highlighting of chords `\[...]`, and song-part commands.

## 3. Live preview

- [ ] core: single-song preview build: wrapper `.tex` in a temp dir, `lualatex` with the right TEXINPUTS, captured log, extraction of the first error. Never writes into ZubuvZpevnik/build/.
- [ ] core: run builds asynchronously; cancel the previous one when a new one starts.
- [ ] app: PDF preview pane (QPdfView), rebuilt on save (later: debounced while typing).
- [ ] app: show build errors and jump to the song line.

## 4. Import from a URL

- [ ] Decide how to call the converter (see Open decisions in CLAUDE.md). Start with the subprocess.
- [ ] core: run `chords_converter <url> latex`, locate the output, report bot-check errors clearly.
- [ ] app: "Import from URL" dialog → opens the result in the editor as a new, unsaved song with a proposed path.
- [ ] Warn if the song already exists.

## 5. Songbook assembler

- [ ] core: parse a project `.tex` (class, options, ordered `\input` list), and write it back, changing only the `\input` lines.
- [ ] app: project view where you add or remove songs from the library and keep the list alphabetical by title.
- [ ] app: build the whole songbook through the project's own `build.sh`, and show the output and the resulting PDF.
- [ ] Create a new songbook project from a template (e.g. the next camp year).

## Later ideas

- Run the linters (`checksongs.py`, `checkkeys.py`) from the app and show the results inline.
- Transpose a song, and an editor for chord diagrams.
- Packaging (AppImage / Flatpak), then Windows and macOS.
