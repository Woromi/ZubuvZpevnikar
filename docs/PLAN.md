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
- [x] chords_converter: chord names keep only chord characters, comment text stays on one line (`src/printers/latex.rs`, approved by the user). Committed there as `c62d66d`.
- [ ] Rebuild while typing (debounced) — easy now, since the preview already builds the editor text.

## 4. Import from a URL

- [x] The converter runs as a subprocess (`chords_converter <source> latex`) in a fresh temp dir; no changes to that repo needed.
- [x] core (`zub::songimport`): prepare the run, interpret exit code/stdout/stderr (bot check, unsupported site, network, HTTP, invalid input; a non-zero exit is a failure, and so is exit 0 without a written file, as a safety net for older converters), read the output, find the destination and an existing song (case-insensitive). Tests on real captured converter outputs.
- [x] app: File → Import Song… (Ctrl+I): URL or saved page, cancel, optional cookie/user agent for bot checks; the result opens as a new unsaved tab at `songs/chords/<Artist>/<Title>.tex`; the first save creates the folder and adds it to the table.
- [x] Existing song: Open Existing or Import Anyway (saving then asks before overwriting).
- [ ] Rebuild the converter from the app, or warn when its binary is older than its sources.
- [ ] Run `chords_converter convert <source> -o -` (song on stdout) instead of a temp work dir plus parsing `Saving output to:`. Needs a converter new enough to have `convert`; keep or drop the old form then.

## 5. Window layout: a song list page and a song page

Decided with the user (2026-11-07): three full columns (table | editor | PDF) side by side are too
much, and several songs open at once isn't needed. The window shows one page at a time:

- **Song list**: the table with search and language filter, full window.
- **Song**: the one open song, editor and PDF preview side by side.

Closing the song returns to the list. Not chosen: a narrow names-only list beside the editor,
editor tabs for several songs, a quick-open popup.

- [x] Song list page (full window). Double-click / Enter opens the song and switches to the song page; Import Song… does too.
- [x] Song page: editor and PDF preview side by side in a splitter (sizes remembered in QSettings); no editor tabs. The window title shows the song, with `*` when modified.
- [x] Close Song (Ctrl+W and a toolbar button) returns to the list, asking Save/Discard/Cancel when modified. Opening or importing a song while one is open closes it first (same question). Reload when the file changes outside the app keeps working.
- [x] Editor toolbar on the song page: Back to list (= Close Song), Save, Undo, Redo, Cut, Copy, Paste, and the song's "Artist/Title" on the right. Buttons are enabled only when they apply (undo/redo available, text selected, modified, not read-only). Rebuild stays in the preview pane's own bar (Ctrl+R). "Open Existing" on the song already open just keeps it. The song actions (§6) will add their buttons here.
- [ ] Songbooks (§7) become a third page later; decide then how to reach it (e.g. a button on the song list page).

## 6. Song actions

Optional, undoable edits that turn a song (an import or any open song) into a finished one: a
checklist in the import dialog, and a menu for any open song. Nothing is saved until Ctrl+S, and
untouched text stays byte-exact.

**Decided with the user (2026-11-07): the converter owns the song actions**, in three tiers
(details: `../chords_converter/docs/PLAN.md`):

1. **Always**, in the converter's printer (done): `\enquote` (an unclosed quote becomes
   `\textquotedbl{}`), chord-only parts as `\begininstrumental` in bars
   (`| \[G]\[] | \[Em]\[] |`), part content indented by 4 spaces (latexindent leaves a draft
   unchanged), single spacing, `lang` where the site gives it (kytary; English songs get English
   chord names).
2. **`convert --tidy` / `chords_converter tidy <song.tex>`** (done 2026-11-08, `8511329`, `fc9c865`): automatic fixes, e.g. an
   *identical* repeated chorus → `\insertchorus`, a chord moved to the start of its syllable.
3. **`chords_converter suggest <song.tex> [--fit a4|a5] --json` / `apply --id …`** (planned):
   proposals the user accepts or rejects, mainly fitting a page: first fit A4 with edits for all
   sizes (`\insertchorus*`, `\chordsoff`), then small paper only (`\chordsoffonsmallpage`,
   `\smalleronsmallpage`).

Tiers 2 and 3 edit **text spans** of the existing file (UTF-8 byte offsets) and never print the
song again, so untouched text stays byte-exact. A proposal is
`{id, kind, description, gain_lines, edits: [{start, end, replacement}]}`, applied as one undo
step. The app shows and applies proposals; it does not compute them. So the chorus `\chordsoff`,
fit-to-page and `lang` detection, and the song-body parser they needed, are not app work.

**Before any code: confirm the details with the user** (what the app shows, defaults at import,
what stays app-side, e.g. a properties panel).

- [ ] Agree on the app side with the user and record it here.
- [ ] app: run `tidy` on a draft / an open song and replace the editor text as one undo step. Interface: write the editor text to a temp file, run `chords_converter tidy TEMP -o -`; stdout is the song (unchanged when nothing applies; untouched bytes come back identical, a second run changes nothing), each change is a `<file>:<line>: <description>` line on stderr (show them), exit 1 if the file can't be read or written. A fresh import can use `convert <source> --tidy`. What it does: a chorus identical to the previous one → `\insertchorus` (already allowlisted); a chord inside its syllable moves to the syllable's start (czech/slovak/english songs only; this changes where the chord prints).
- [ ] app: list `suggest` proposals for an open song; apply one as one undo step, re-query after edits (when the converter has it).

Possibly app-side (from the songbook's history, `songbook-latex` agent, 2026-11-07; songs were
never committed as raw converter output, so this shows edits after a first hand pass):

| Candidate | Evidence |
|---|---|
| Song properties panel for `key`, `tem`, `met`, `cap`, `lang` | `met` (33) and `cap` (12) are the most common later edits |
| Remove placeholder comments (`% tem={}`, `% [TAB]` …); keep `% note=…` style comments | 0 placeholders in finished songs; 19 note comments kept |
| Chord memory: `\memorize` on the first verse, `^` in later verses with the same chords | 83 songs use `^` (could also be a converter tier 2/3 item) |

Converter-side, still open there: chord placement (15+ manual chord fixes after import point to
reading problems) and metadata the site provides but the parser misses (capo, meter).

## 7. Songbook assembler

- [ ] core: parse a project `.tex` (class, options, ordered `\input` list), and write it back, changing only the `\input` lines.
- [ ] app: project view where you add or remove songs from the library and keep the list alphabetical by title.
- [ ] app: build the whole songbook through the project's own `build.sh`, and show the output and the resulting PDF.
- [ ] Create a new songbook project from a template (e.g. the next camp year).

## Later ideas

- Run the linters (`checksongs.py`, `checkkeys.py`) from the app and show the results inline.
- Transpose a song, and an editor for chord diagrams.
- Find & replace in the editor (Ctrl+F / Ctrl+H), with a toolbar button.
- Packaging (AppImage / Flatpak), then Windows and macOS.
