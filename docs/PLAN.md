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

- [x] Songbook location: `--songbook <dir>`, else remembered in QSettings (app layer, not core), else the sibling `../ZubuvZpevnik`, else a folder dialog. File → Open Songbook Folder… / Reload.
- [x] core: scan `songs/chords/` → list of songs (artist, title, path), with per-file issues instead of failing.
- [x] core: parse song metadata from `\beginsong{Title}[by=…, key=…, lang=…]` (port of `scripts/songfile.py`; tests with real songs as fixtures in `tests/data/songbook`).
- [x] app: main window with a sortable song table, search (case- and accent-insensitive) and a language filter.

## 2. Editor

- [x] app: open a song in an editor tab (double-click / Enter in the table): QPlainTextEdit + KSyntaxHighlighting LaTeX definition, theme from the palette.
- [x] Save (Ctrl+S, atomic via `zub::writeTextFileAtomic`, byte-exact: nbsp, tabs, CRLF, missing final newline), `*` dirty marker, Save/Discard/Cancel on closing a tab, the window, or switching songbook; the table row refreshes after saving.
- [x] Reload when the file changes outside the app (silently if unmodified, asks if modified).
- [x] Custom syntax definition for songs (`app/syntax/zubuvzpevnik-song.xml`, bundled as a Qt resource): whole chords `\[…]` and `^`, song parts, bar lines/repeats, header keys and values, comments. The stock LaTeX one read `\[D]` as display math.

## 3. Live preview

- [x] core: single-song preview build (`zub::preview::prepare`): wrapper `.tex` + a copy of the editor text in the app cache dir, `lualatex -file-line-error` with TEXINPUTS; `firstError` reads the log. Never writes into ZubuvZpevnik/build/.
- [x] app: builds run asynchronously with QProcess (`PreviewRunner`); a new build kills the running one.
- [x] app: PDF preview pane (QPdfView) with a class chooser; rebuilt on open, tab switch, save, reload, class change, Rebuild/Ctrl+R. A failed build keeps the last good PDF.
- [x] app: build errors listed under the PDF; double-click jumps to the song line. Full log via "Log".
- [x] Safety: songs using commands outside the allowlist are not built automatically (Rebuild asks first). Preview builds are cancelled with their tab; LilyPond children are killed with lualatex; each app instance has its own work dir.
- [x] chords_converter: chord names keep only chord characters, comment text stays on one line (`src/printers/latex.rs`, approved by the user). The change is left uncommitted in that repo, to be finished there in its own session.
- [ ] Rebuild while typing (debounced) — easy now, since the preview already builds the editor text.

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
