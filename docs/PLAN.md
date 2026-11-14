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

- [x] Songbook location: `--songbook <dir>`, else remembered in QSettings (app layer, not core), else a folder dialog. File → Open Songbook Folder… / Reload. (No guesses such as `../ZubuvZpevnik`: removed 2026-11-08 at the user's request.)
- [x] core: scan `songs/chords/` → list of songs (artist, title, path), with per-file issues instead of failing.
- [x] core: parse song metadata from `\beginsong{Title}[by=…, key=…, lang=…]` (port of `scripts/songfile.py`; tests with real songs as fixtures in `tests/data/songbook`).
- [x] app: main window with a sortable song table, search (case- and accent-insensitive) and a language filter.

- [x] Song list columns Tempo (`tem`), Meter (`met`) and Capo (`cap`) after Key; tempo and capo sort as numbers (decided with the user 2026-11-13: just these three, no column chooser). Hidden in the songbook picker.
- [x] Favourites and labels (asked by the user 2026-11-13; personal, so no ratings). Decided: labels live in the song header as a `tags` key, `\beginsong{T}[by={X}, tags={fav, táborák}]` (comma-separated); the label `fav` marks a favourite (shown as ★). The songbook declares the key (`\newsongkey{tags}{}{}` in `feature/metadata.cfg`). core: `zub::tags` (`parse`, `invalidTag`, `setTags`: rewrites only the tags option, everything else byte-identical, CRLF kept). App: ★ and Labels columns in the list (Labels hidden in the picker, ★ shown), click the ★ to toggle, right-click → Labels… (several songs at once, tri-state checkboxes, new labels), filters "Only favourites" and by label in the list and the songbook picker, search matches labels, song toolbar ★ and Labels…. A song that is not open is rewritten on disk (only if it round-trips like the editor requires); an open song gets the change in its editor as one undo step, unsaved.
## 2. Editor

- [x] app: open a song in an editor tab (double-click / Enter in the table): QPlainTextEdit + KSyntaxHighlighting LaTeX definition, theme from the palette.
- [x] Save (Ctrl+S, atomic via `zub::writeTextFileAtomic`, byte-exact: nbsp, tabs, CRLF, missing final newline), `*` dirty marker, Save/Discard/Cancel on closing a tab, the window, or switching songbook; the table row refreshes after saving.
- [x] Reload when the file changes outside the app (silently if unmodified, asks if modified).
- [x] Custom syntax definition for songs (`app/syntax/zubuvzpevnik-song.xml`, bundled as a Qt resource): whole chords `\[…]` and `^`, song parts, bar lines/repeats, header keys and values, comments. The stock LaTeX one read `\[D]` as display math.
- [x] Edit menu with song-editing helpers (core `zub::songedit`, one undo step each, only while the song is editable): Insert Chord Alt+A (wraps a one-line selection; inside a chord the new one goes after it, chords do not nest); Insert Multivoice Alt+M (`{\multivoice{}{}}`, a one-line selection becomes the first voice); Insert Role Alt+L (` \role{}` at the end of the line, a selection is copied as the name; an existing trailing role only gets the cursor); Remove Element at Cursor Alt+Delete (the innermost chord, role or multivoice, which keeps its first voice); Move Line Up/Down Alt+Up / Alt+Down (the cursor's line or the selected lines as a block, like VSCodium), Ctrl+Up / Ctrl+Down scroll the view by a line without moving the cursor; Move Chord Left/Right Alt+Left / Alt+Right (a letter, or a neighbour chord); to Previous/Next Word Alt+Shift+Left / Alt+Shift+Right (Alt+Left is Back only off the song page); Insert Part… Ctrl+Return (a popup at the cursor: verse, unnumbered verse, chorus, pre-chorus, bridge, intro, outro, instrumental, `\insertchorus`, `\insertchorus*`, song header when there is none; the selected lines go inside).
- [x] Line numbers in the song and score editors (`CodeTextEdit`, colours from the syntax theme), since Tidy, Check and the build errors report line numbers.

- [x] Enter keeps the current line's indentation (spaces and tabs before the cursor), one undo step with the line break (2026-11-13; Tidy restores the songbook's full indentation).
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
- [x] The converter is a git submodule (`external/chords_converter`), built by CMake with cargo and found next to the app; no `../chords_converter` guess.
- [x] Decided with the user (2026-11-08): the app uses the converter built with it (the version it is tested with). It never installs or updates a converter on the computer. Another converter is used only when chosen in the import dialog ("Use Built-in" goes back); PATH and `~/.cargo/bin` are fallbacks for builds with `ZUB_BUILD_CONVERTER=OFF`. Standalone use: `cargo install` as before, or `cmake --install … --prefix ~/.local`, which installs the app and its converter side by side. Not chosen: linking the converter as a library (big change to its repo; a process can be killed and can't crash the editor), installing or replacing it automatically.
- [x] Compatibility check: `chords_converter --version` (see "converter version check" in §6).
- [x] Not checked yet (decided with the user 2026-11-08): an imported song gets `% TODO: not checked yet (imported by the app)` under its header (`zub::todo`), the songbook's existing TODO-comment convention, so it travels with git and LaTeX ignores it. **Mark Checked** (song toolbar) removes it as one undo step (save to keep it). The song list has a TODO column (every `% TODO` note of a song) and **Only songs with TODOs**, which shows imported songs not checked yet and the songs with other TODOs alike.
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
- [ ] Welcome page instead of the folder dialog at start: no songbook remembered → "Open Songbook Folder…"; the remembered one missing → say where it was, with Try Again / Choose Another….
- [ ] `--songbook` applies to that run only and is not remembered (today it overwrites the remembered songbook).
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
3. **`chords_converter suggest <song.tex> [--fit a4|a5] --json` / `apply --id …`** (done 2026-11-08, `d3c4c88`):
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

- [x] Agree on the app side with the user and record it here. Decided 2026-11-08 for `tidy`:
      - Import: a "Tidy the song" checkbox in the Import dialog, on by default, remembered. The app imports the raw draft, then runs the same Tidy as the toolbar (not `convert --tidy`), so Ctrl+Z shows the raw draft and change lines are correct.
      - Open song: a **Tidy** toolbar button applies at once as one undo step; nothing is saved until Ctrl+S.
      - What changed: a closable list under the editor (double-click jumps to the line), plus a **Changes** toggle that shows the diff of the saved file vs the editor text.
      - `suggest`/`apply` (tier 3), decided 2026-11-08: a **Make Smaller** button, not an automatic fit. Each click runs `suggest --measure` on the editor text for the paper of the preview's class, shows the page count and the proposals (rated by readability, most readable first; each shows its score and grade, and selecting one moves the editor to its line). Next to the list, a sketch of the song after the selected change, laid out like the PDF (labels "1." / "R:" / "P:", choruses indented, each part's first line with its chords above, "…" at the end of the lyrics when the part goes on, struck-through chords for "no chords on small paper"; scrolled to the first change), with the parts it changes highlighted (and "12 → 6 lines" for joined lines), so proposals with the same description can be told apart (`zub::songoutline`, from the proposal's `edits`; part names follow the converter's: verses by count, choruses by occurrence, repeats included), and the user applies one or none (one undo step); the preview shows the effect, and the user clicks again as long as needed. No automatic re-suggest.
- [x] app: run `tidy` on a draft / an open song and replace the editor text as one undo step. Interface: write the editor text to a temp file, run `chords_converter tidy TEMP -o -`; stdout is the song (unchanged when nothing applies; untouched bytes come back identical, a second run changes nothing), each change is a `<file>:<line>: <description>` line on stderr (show them), exit 1 if the file can't be read or written. A fresh import can use `convert <source> --tidy`. What it does: a chorus identical to the previous one → `\insertchorus`; a hand-typed chorus reminder (one sung line, chords off, ending in `...`, rest commented out) → `\insertchorus*` (converter `41755c1`; typesets identically only with the songbook's fixed `zubuvzpevnik/lua/refren.lua`, uncommitted there on 2026-11-08); a chord inside its syllable moves to the syllable's start (czech/slovak/english songs only; this changes where the chord prints). Both macros are already allowlisted. `tidy` has no switches to pick rules. Since converter `105df85` it also joins part setup onto the opening line (`\beginverse\chordsoff`), writes a chords-off chorus copy as `\insertchorus[][\chordsoff]` and a reminder without the chorus's two-voice label as `\insertchorus*[][\rolesoff]` (both need the songbook's `239ed49`; `\rolesoff`/`\roleson` allowlisted).
- [x] app: **Changes** toggle on the song page: the editor area shows a line diff (`zub::diff`, core) of the saved file vs the editor text (an unsaved import: the raw draft vs the editor text, so only tidy and your edits show); enabled while modified.
- [x] app: converter version check: `chords_converter --version` must report ≥ 0.10.0 (the interface: `convert -o`, exit codes, `tidy`, the `shrink`/`join`/`check` JSON; the converter bumps the minor version on any interface change). Older or unparseable → Tidy / Make Smaller refuse with a message; the import dialog shows the version.
- [x] app: list `suggest` proposals for an open song; apply one as one undo step, re-query after edits. Interface (converter `d3c4c88`):
      - `chords_converter suggest SONG.tex [--fit a4|a5] [--measure] [--songbook DIR] --json` prints `{file, fit, fits, note, measured, proposals}`. A proposal is `{id, kind, stage, line, description, gain_lines, score, grade, edits: [{start, end, replacement}]}`; edits are UTF-8 byte offsets into the file as read. Since 0.3.0 `score` (0–100, how readable the song stays) and `grade` (good, fair, poor, last resort) rate each proposal; within a stage they come by score, then lines saved (the first is still the recommended next step), and one saving no line is not offered. Since 0.4.0 (`336e108`, pinned): kind `joinlines` (score 90, stage 1, only with `--measure`, one proposal per part kind; each line break of a pair becomes a space); `sources` per proposal (`[{line, start, end}]`, the parts it relies on: the part still showing the chords, the chorus an `\insertchorus*` stands for), tinted in the sketch; a chords-off proposal can be a move (hide some, show others again; ids are opaque); A5 is measured even when A4 does not fit (0.4.0 only).
      - 0.5.0 (`1ee0a2f`, pinned), decided with the user 2026-11-08: one button per action. **Make Smaller** shows `suggest`'s `category: "fit"` proposals. **Join Lines** runs `chords_converter join SONG --songbook DIR --json` (suggest's JSON; always typesets A4 and A5, seconds, so it needs the build approval and has no unmeasured fallback); `suggest` no longer offers `joinlines`. **Restructure** runs `suggest` without `--measure` (fast, nothing built) and shows the `category: "restructure"` proposals (a part with the chorus's words typed as a verse becomes a chorus, chords kept; `gain_lines` 0); after applying it, the app offers Tidy, which makes the copies `\insertchorus`. Several proposals can be picked at once (Ctrl/Shift+click): the sketch shows them together, and one `apply --id A --id B` makes them as one undo step; ones that touch the same text are caught in the sketch (Apply disabled), the converter refuses the rest of the conflicts with a message. `description` is meant to be shown as is; `stage` 1 acts on every paper, 2 on small paper only.
      - 0.6.0/0.7.0 (`e1db028`, pinned): `suggest` is renamed `shrink` (the old name still works; the app uses `shrink`). Restructuring left `shrink` for **`check SONG --json`** (corrections whether the song fits or not, in song order; builds nothing; no `--songbook`), so the Restructure button became **Check Song**. `check` also lists typos (`category: "typo"`: a line's copy differing by a slip, apostrophes typed both ways, `~` in an English song); a typo with a chord inside the word has no `edits` (shown, "fix by hand", Apply disabled). Corrections have `score` 0 and `grade` `correction` (shown without a score). After applying a restructure, Tidy runs by itself (its own undo step). `shrink` no longer offers `\insertchorus*` for a chorus in another key.
      - 0.8.0 (`ef4faa9`, pinned): `shrink` kind `rep` (score 85: an instrumental row written several times in a row printed once with `\rep{n}`; the sketch shows "(×n)" and "3 → 1 lines"), `join` joins instrumental rows too, `tidy` puts a space before a chord glued to a word's end when words follow, drafts get no `cap` line when the site says no capo. No interface change for the app beyond the version.
      - 0.9.0/0.10.0 (`344cac5`, pinned): chords off that leaves a part without a chorded neighbour is offered as a "last resort" (score 30; `apply` takes such proposals chosen together, but still refuses overlapping ones); `shrink --fit a5 --measure` on a song that does not fit A4 lists the stage 1 and the stage 2 proposals (the note says so; the dialog marks stage 2 "(small paper only)"). `check` has a kind `comma` (category `typo`: a Czech/Slovak clause word with no comma before it), and every finding carries a `dismiss` edit, the `% nocheck` line that marks it right (null for proposals that are not corrections). **Mark as Correct** in the Check Song dialog applies the selected findings' `dismiss` edits locally (`zub::songoutline::applyEdits`, one undo step, no converter run), also for the ones to fix by hand. Minimum version 0.10.0.
      - 0.11.0 (`472c2c6`; `c54d315` pinned: join lowers a capital where joined lines meet mid-sentence): `join --json` has a top-level `not_joined`, a list of strings, one per kind of part not offered, with the reason (`"verse: the parts have 3 and 5 lines; …"`, `"chorus: joined, a joined line is too wide for A5"`); the app shows them under "Not offered:" in the Join Lines dialog's summary and in the message when nothing can be joined (`Suggestions::notJoined`). `join` now also pairs parts of an odd number of lines (the last line stays alone) when every part of the kind has that number and the last line is long enough. `check` has a new kind `chordlines` (category `typo`, with `dismiss`): a chord at the end of a line in one part where the other parts of the kind have it at the start of the next line, or the other way round; its `sources` is the part it follows. Nothing else changed.
      - `--measure` adds `fits` (else null) and `measured.{a4,a5}: {pages, wraps: [{line, in_chorus}], fits}`; `wraps` are lines too wide for the column, to re-break by hand (`line` is null inside a chorus). It takes ~2 s per paper. **It builds the song with the songbook's `checkpaper.py`, i.e. `lualatex -shell-escape`: run `zub::safety::unknownConstructs` first, as for the preview.** It never writes into the songbook; its LilyPond cache is `~/.cache/chords_converter/tmp-ly`.
      - Flow: write the editor text to a temp file, `suggest`, show the proposals, apply the accepted one from its `edits` (or `apply SONG --id ID … -o -`) as one undo step, then `suggest` again (offsets go stale after any change). `apply` refuses ids that no longer exist, overlapping edits, and choices that would leave a verse without a chorded neighbour.
      - Once a song fits, the converter doesn't write `pap`; the songbook records it with `scripts/checkpaper.py --measure a5 --write`. Whether the app offers that is still open.
      - No new macros: `\chordsoff`, `\chordsoffonsmallpage`, `\insertchorus*`, `\smalleronsmallpage` are already allowlisted.

Possibly app-side (from the songbook's history, `songbook-latex` agent, 2026-11-07; songs were
never committed as raw converter output, so this shows edits after a first hand pass):

| Candidate | Evidence |
|---|---|
| Song properties panel for `key`, `tem`, `met`, `cap`, `lang` | `met` (33) and `cap` (12) are the most common later edits |
| Remove placeholder comments (`% tem={}`, `% [TAB]` …); keep `% note=…` style comments | 0 placeholders in finished songs; 19 note comments kept |
| Chord memory: `\memorize` on the first verse, `^` in later verses with the same chords | 83 songs use `^` (could also be a converter tier 2/3 item) |

Converter-side, still open there: chord placement (15+ manual chord fixes after import point to
reading problems) and metadata the site provides but the parser misses (capo, meter).

- [x] app: **Songs → Scan Songbook…** (decided with the user 2026-11-08): runs `tidy`, `check` and/or `shrink --measure` (the preview's paper; ~2 s a song, off by default) on all songs or the ones shown in the list, one at a time (`SongbookScan`, its own `ConverterRunner`), and marks the songs with possible changes in the list: a "Possible changes" column ("Tidy 3 · Check 1 · Too big (A5: 2 pages)", details in its tooltip) and **Only songs with possible changes**. This session only, nothing changed or saved; the user opens a song and decides with the song actions. Saving a song clears its mark; songs with commands outside the song format are not measured. On 2026-11-08 the whole songbook took 1 s for tidy + check (0 tidy changes, 6 check findings).

- Converter `41651cd` (2026-11-13, still 0.10.0): Tidy also sets the indentation as the songbook's latexindent does; drafts end their prelude with `pap={},`; `lang` is read with or without braces (reported from here).

## 7. Songbook assembler

Decided with the user (2026-11-11): the app stores songbooks itself (`AppDataLocation/songbooks/<id>.json`); nothing in `ZubuvZpevnik/projects/` is read or written. A songbook has a class (velkyzpevnik / malyzpevnik / taborovyzpevnik), title, author, options, and one unnamed section or several named ones (`\songsection`, one `songs` environment each, one shared title + author index); songs are in title-index order within a section (the songbook's own `scripts/songidx.lua`, as `checkorder.py` runs it). Songs are picked on a Songbooks page, not in the main list (there a click opens a song). Export: PDF (built in the app's cache dir with the repo's `scripts/build.sh`, never in `ZubuvZpevnik/build/`), or LaTeX: just the `.tex` (builds inside a checkout) or a self-contained folder (songs + their `.ly`, `zubuvzpevnik/`, the build scripts, the class's image, a `build.sh` and README).

- [x] core (`zub::songbook`): model + JSON, `.tex` generation per class, index-order input/output for `songidx.lua`, files a song references, folder-export plan, build command.
- [x] app: Songbooks page (list, settings, a tree with the songs outside sections at its root and the named sections below; a picker with Ctrl/Shift selection, Add/Remove and dragging onto the tree; drag to reorder within a section, into another section a song goes to its alphabetical place; Sort Alphabetically for the book or one section), sorting with `texlua songidx.lua`.
- [x] app: export PDF / `.tex` / folder.
- [x] Cover image (none by default; png/jpg/pdf), a title page per named section, the title index split into sections, the author index optional (2026-11-11). The camp booklet falls back to one title index when the split would not fit its single index page.

## 8. Scores (LilyPond)
Asked by the user 2026-11-12: edit the songs' LilyPond scores (40 of the 47 `.ly` files are guitar tablature) in the app, eventually by clicking on a fretboard, with MIDI playback.
- [x] Step 1: score tabs with a live score preview.
  - `zub::score` (core): `prepare` (the score copy, the `score.ly` wrapper and the `lilypond -dbackend=svg -dcrop` command; point-and-click links stay in the SVG), `germanNames` (Czech and Slovak songs, as `zubuvzpevnik.sty`), `errors`/`firstError` (LilyPond's `file:line:col: error:`; the command sets `LC_ALL=C`), `newScore` (skeleton). The wrapper always sets `\language` (`english` or `deutsch`): LilyPond itself defaults to Dutch note names.
  - `zub::safety::lilypondRisks`: Scheme that runs code, `$` variables, `\include` of anything but the shared files. Every real `.ly` passes (`[integration]`). The PDF export and the scan's measuring check the referenced scores too (`zub::songbook::scoreRisks`).
  - `zub::preview::Request::fileOverrides`: the song's PDF preview builds unsaved scores as they are in their editors.
  - `planFolderExport` copied `\lilypondfile` files from `<song folder>/<ref>`, which is wrong (the reference is relative to `songs/chords`); fixed, with a test.
  - App: a tab per `\lilypondfile` of the open song (missing file: "Create It"), `ScoreRunner` + `ScorePreviewPane` (SVG, errors, log, Rebuild; 600 ms debounce), the safety approval for `.ly` texts, Edit → New Score…, Czech texts.
- [x] A discarded new score removes its `\lilypondfile` line from the song (one undo step); a score that exists neither on disk nor in an editor stops the PDF preview with a message. Each score tab has Width: Full / Narrow, which switches the song between `\lilypondfile` and `\narrowlilypondfile` (`zub::songedit::setScoreWidth`).
- Decided with the user 2026-11-13: the width stays a choice in the app. An automatic `\autolilypondfile` was researched (narrow when the score starts beside the A4 metadata box; one pass, works) but matches only 28 of the 38 existing hand-made choices, since what decides is the score's ink, which LaTeX cannot see; not added to the songbook.
- [x] Step 2: a fretboard under the score editor, and point-and-click in the preview (2026-11-13).
  - `zub::fretboard` (core): `Tuning` (guitar 64 59 55 50 45 40; banjo open G 62 59 55 50 67 with the short 5th string, drawn from fret 5), `pitchName`/`noteToken` (absolute pitch + `\N`, German or English, sharps or flats), `prefersFlats` (song key), `positionsAt` (the `{string, fret}` of the note or chord at the cursor), `effectiveDuration`, and one-line edits `insertNote`, `addToChord`, `insertRest`, `insertBarCheck`.
  - **Tuning rule:** the fretboard follows the score's own TabStaff tuning, not the song's `tun` header: standard guitar unless the score sets `stringTunings = #banjo-open-g-tuning` (Zuzana, Rozárka); the two E♭-tuned songs still use the standard tab tuning. The note names come from the song (`lang`: German for Czech/Slovak; `key`: flats for `\flt` keys, F, Dm, Gm, Cm, Fm).
  - App: `FretboardWidget` (strings, nut, frets 0-15, markers, hover, palette colours), `ScoreInputBar` (length 1/2/4/8/16, dot, Rest, Bar line; shows the length in force at the cursor), under the score editor; the "Fretboard" toggle in the tab's width bar is remembered. Clicks stage positions (one per string; a click on the staged fret removes it), drawn as filled dots; Insert (button, Ctrl+Return anywhere on the tab) writes them as one note or one chord `<e,\\6 h,\\5 ...>` (lowest string first) in one undo step and clears the staging, Clear (Escape) drops it, a double-click stages and inserts at once; staging is cleared when the tuning changes. Hollow rings mark the ONE selected note or chord (a cursor between notes shows none). Selecting exactly one event whose notes all have a `\\N` (`zub::fretboard::positionsOfEvent`) loads its positions into the staging (editable) and the Insert button reads Replace (Nahradit): `zub::fretboard::replacePitches` changes only the note-or-chord part (`fis'\\1` <-> `<e\\4 g\\3>`; prefix functions, duration, ties, articulations stay; a rest takes the notes with its duration, `q` is refused), in one undo step, the replaced event stays selected; the loaded staging is cleared when the selection moves off the event (a rest or a plain note loads nothing and Insert keeps inserting). (`zub::fretboard::addToChord` stays in core, unused by the app.)
  - Preview: `ScorePreviewPane` reads LilyPond's `textedit://` links (only those into the score copy), a click on a note within 14 px moves the score editor's cursor to it (`SongEditor::goToLineColumn`; LilyPond's columns are 0-based code points).
- [x] Step 2b: selecting notes and acting on them (2026-11-13).
  - Selecting notes works like selecting text, not like an area (replaced the first rubber-band version). The units are the music events of the score text (`zub::scoreregion::musicEvents`: notes, chords, `q`, rests; chord names, lyrics, marks, markup, Scheme and the value of a variable definition such as `emChord = <...>4` are none: the scanner skips `\chordmode`, `\lyricmode`, `\lyricsto`, `\addlyrics`, `\markup`, `\mark`, `\with` arguments and Scheme after `#`/`$`), in text order. A click on a note places the score cursor (a thin caret in the highlight colour at the note's x, as high as its staff: the staff lines are read from the SVG) and moves the editor's text cursor there; **dragging from a note or Shift+click selects the notes from the anchor to the one nearest the mouse in text/playing order** (so a drag onto the next system continues the selection like text), highlighted note by note, and the editor selects exactly from the first event's start (with its prefix functions `\deadNote`, `\grace`, `\tweak`, `\once \override`) to the last event's end. No rectangle. A click on empty space changes nothing; only links into music events are clickable (`ScorePreviewPane::setSelectableNotes`), and clicks are ignored while the preview is older than the text. Both ways: a selection in the editor highlights the notes of the events it touches (debounced), a collapsed text cursor shows the caret like a text cursor: behind the event that ends exactly there, else before the event it is in or before, past the last event behind the last note. A click picks the nearest note on the staff and a side: left of the note's x puts the text cursor at the event's begin, right of it at its end (`Range.end`), so you can click after the last note of a system; drag and Shift+click select the events lying between the two caret offsets (after A to after C selects B..C, before A to after A selects A), a double-click selects exactly one event. (Left/Right arrow keys on the preview are not done.)
  - Actions on the selection, each ONE undo step with the result selected and the preview rebuilt, from the preview's context menu, the score editor's context menu and a "Selection:" row of the input bar (enabled with a selection): Tie (`~` after each note that a note follows: never before a rest), Slur (`(` first, `)` last; refused when a slur is open across the start or the selection has slur marks), Glissando (` \glissando` after the first), Repeat (`\repeat volta 2 { ... }`; refused when the `{ }` of the selection are not balanced), Stroke Down / Stroke Up (`^\strokeDown`/`^\strokeUp` after every chord and note, replacing the other), Dead Notes (`\deadNote` before every note, also in chords; removed again when every note has it). All in core (`zub::scoreregion::apply`, byte ranges, multi-line); rests get none of the note actions; already-done actions change nothing (Repeat nests).
  - The tokeniser of `zub::fretboard` moved to `core/src/scorescan.hpp` (shared): it now also knows articulations after a note, rests, every pitch of a chord, and that the pitch arguments of `\key`, `\relative`, `\transpose` are no notes.
  - Fix: `insertNote`/`insertRest` take the duration in force before the insertion; the next note, chord or rest that wrote no duration (it carried the old one) gets it written, so its length does not change (also when it is on a later line: `carriedDurationFix`, same undo step).
  - Limits: the preview's context menu opens on a right click anywhere on the score (the actions are enabled by the editor's selection); a following note on a later line gets its explicit duration from `SongEditor`, not from core's `insertNote` (which sees one line).
- [ ] Step 3: MIDI playback of the score.
- Later: `zubuvzpevnik/lilypond` may move into the app; `score::Request::includeDir` is a parameter, so only the caller changes.

## Name

- [x] Renamed to **ZubuvZpevnikar** (decided by the user 2026-11-11; display name "Zpěvníkář", executable `zpevnikar`): CMake project, executable, window title, application name, generated file headers, folder-export marker (the old `.zubuvzpevnikui-export` is still accepted). On its first start the app copies the settings from `ZubuvZpevnikUi.conf` (kept) and moves the data and cache folders to the new name (`main.cpp`, `migrateFromOldName`).
- [ ] The user: the GitHub repo `ZubuvZpevnikar`, and renaming the folder `../ZubuvZpevnikUi` (Claude Code keeps its project memory per folder path: move `~/.claude/projects/-home-modrozoubek-data-git-ZubuvZpevnikUi` to the new path's name too).

## Czech UI
Asked by the user 2026-11-11. Qt Linguist translations in `app/i18n/`.
- [x] Infrastructure: `qt_add_translations` (`zpevnikar_cs.ts`, plural-only `zpevnikar_en.ts`, `.qm` embedded under `:/i18n`), translators for the app and Qt's `qtbase` installed in `main()`, Settings → Language (System Default / Čeština / English, `ui/language`, applies at the next start), UI texts wrapped in `tr()`, count messages use `%n`.
- [x] Translate `app/i18n/zpevnikar_cs.ts` (Czech, three plural forms; 380 texts, 2026-11-11). Glossary agreed with the user: píseň, zpěvník, sekce (section), sloka, refrén, předrefrén, předehra, dohra, mezihra, tónina, kapodastr, náhled, sestavit, Učesat (Tidy), Zmenšit, Spojit řádky, Zkontrolovat píseň, Označit jako zkontrolované, Prohledat zpěvník, Změny, Zpět (Back), Vrátit (Undo), Znovu, obálka, obsah (title index), rejstřík interpretů (author index), převodník.
- [ ] The command-line `--help` text is still English (the parser runs before the translators are installed).
- Texts that come from core or the converter (error messages, proposal descriptions) stay English unless they get translated at the source.

## Later ideas
- Section images (asked by the user 2026-11-11): each named section can have an image on its title page (like the cover: optional, png/jpg/pdf; copied next to the generated .tex as `section<k>.<ext>`, also into the folder export). `\zubsectionpage` would take it as a third argument and place it under the title (sized to fit the page; check A5 and the malyzpevnik right-hand title page). Stored per section in the songbook JSON (`image`).
- Songbook key (asked by the user 2026-11-11): set a songbook to a certain key and transpose every song to match it. The semitones per song come from its `key` header (and `\transpose` it already has); the generated `.tex` would put `\transpose{n}` before each `\input` (the songs package's transposition; check how it interacts with `capo`, a song's own `\transpose`, and minor keys: matching relative major/minor or the written key). Open: one key for the whole book or per section/song, and what "match" means for minor songs.
- Import an existing project (`projects/*/*.tex`: class, title, `\input` lists, `\songsection`s) as an app songbook. Not now (decided 2026-11-11).

- Run the linters (`checksongs.py`, `checkkeys.py`) from the app and show the results inline.
- Transpose a song, and an editor for chord diagrams.
- Get the Songbook…: `git clone` the ZubuvZpevnik repo into a chosen folder, from the welcome page.
- Find & replace in the editor (Ctrl+F / Ctrl+H), with a toolbar button.
- Packaging (AppImage / Flatpak), then Windows and macOS.
