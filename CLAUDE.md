# ZubuvZpevnikUi

A desktop app (Linux first, other platforms later) for working on my LaTeX songbook:

1. Paste a URL of a chords website, and the song is converted to my LaTeX song format.
2. Edit the song `.tex` with syntax highlighting and a live PDF preview.
3. Assemble songbooks: pick which songs go into which project (the `\input` lists).

The app does not reimplement the songbook or the converter. It drives two sibling repos.

## The three repos

| Repo | Path | What it is | Docs |
|---|---|---|---|
| this one | `.` | C++23 / Qt 6 desktop app | `docs/PLAN.md` |
| ZubuvZpevnik | `../ZubuvZpevnik` | LaTeX package, classes, ~200 songs, songbook projects, build + lint scripts | `README.md`, `zubuvzpevnik/README.md` (song format), `docs/building.md`, `docs/tools.md` |
| chords_converter | `../chords_converter` | Rust CLI: URL / saved HTML / text → LaTeX song | `README.md` |

Read the sibling docs when a task touches them. Don't copy their content in here.

Key facts from them:
- A song is `songs/chords/<Artist>/<Title>.tex` (`\beginsong{Title}[by=..., key=...]` … `\endsong`).
- A songbook is `projects/<name>/<name>.tex`: a `\documentclass` from `zubuvzpevnik/` plus an alphabetical list of `\input{Artist/Title}`.
- Builds need `lualatex -shell-escape` (lyluatex runs `lilypond`), with `TEXINPUTS` pointing at `zubuvzpevnik/`, `songs/chords/`, the repo root and the project dir. See `projects/velkyzpevnik/build.sh`.
- Song header parsing in `core/` (`zub::parseSong`, `zub::latex::*`) is a port of `scripts/songfile.py`. Keep them in agreement; if one changes, check the other.
- Linters: `scripts/checksongs.py`, `scripts/checkkeys.py`, `scripts/checkpaper.py` (read-only, except `checkpaper.py --write`).
- `chords_converter` is binary-only (no `lib.rs`); the app runs it as a subprocess (`ImportDialog`, `zub::songimport`). It writes `out/latex/<Artist>/<Title>.tex` relative to the **current directory**, prints `Saving output to: <path>`, exits 1 when the song could not be read or written and 2 for a wrong command line (converters before `9d00844` exited 0 after a failed write). `chords_converter convert <source> -o -` prints the song to stdout instead. Captured real outputs are in `tests/data/converter/`. The app finds the converter via the remembered path (chosen in the import dialog), next to its own executable, PATH, then `~/.cargo/bin`; no guesses.
- `chords_converter` is also a git submodule at `external/chords_converter` (pinned to a commit on GitHub). CMake builds it with cargo and copies it next to the app (`ZUB_BUILD_CONVERTER`, on by default; skipped without cargo or without `git submodule update --init`). Converter work still happens in `../chords_converter`; once it is pushed, bump the pin with `git submodule update --remote external/chords_converter` and commit that. Never commit inside the submodule. The `[integration]` import test uses the converter in `ZUB_CONVERTER` (e.g. `build/debug/app/chords_converter`).
- The app needs converter **≥ 0.8.0** (`chords_converter --version`); the converter bumps the minor version whenever its command line, exit codes, `Saving output to:` lines or the proposals' JSON change. Always pass `--songbook <root>` to `shrink` and `join` (they would otherwise guess `../ZubuvZpevnik`); `shrink --measure` and `join` (which always measures) build the song with lualatex, so they need the preview's safety approval; `check` builds nothing.
- `tidy` reports each change on stderr as `<file>:<line>: <description>`, where the line is in the **input** text; the app maps it to the result with `zub::diff::mapOldLine`.
- The converter owns the song actions (`tidy`; `shrink`, `join`, `check` listing proposals as text-span edits, `apply` making them); the app only shows and applies them. See "Song actions" in PLAN.md. `shrink --measure` and `join` build the song with `lualatex -shell-escape` (via the songbook's `checkpaper.py`), so they need the same safety check as the preview.

## Architecture (hard rule)

```
core/    std-only C++23 library. NO Qt includes, NO Qt types. Links nothing from Qt.
         Song model, song .tex parser/writer, songbook project parser/writer,
         preparing external builds (zub::preview: files + command line) and reading their logs.
         core never starts processes: std C++ cannot run them asynchronously or cancel them.
app/     Qt 6 Widgets UI. Thin: owns windows, editor, PDF view, models for views.
         Converts std::string <-> QString / std::filesystem::path <-> QString only here.
tests/   Catch2 v3 unit tests for core/ (no Qt needed to build or run them).
```

- If something in `core/` seems to need Qt, stop and ask instead of adding the dependency.
- Prefer `std::filesystem`, `std::string_view`, `std::expected`/`std::optional`, ranges. No raw `new`/`delete` in `core/`.
- In `app/`, normal Qt idioms are fine: parent ownership, signals/slots. Keep logic out of widgets.
- Long-running work (lualatex, converter) must not block the UI thread. app/ runs it with QProcess (see `PreviewRunner`: newest request wins, the old process is killed).

## Build & test

```bash
cmake --preset debug                 # configure (Ninja, build/debug, compile_commands.json)
cmake --build --preset debug         # build
ctest --preset debug                 # run core unit tests (Catch2)
build/debug/app/zubuvzpevnik-ui      # run the app
QT_QPA_PLATFORM=offscreen timeout 3 build/debug/app/zubuvzpevnik-ui   # headless smoke test (exit 124 = ran fine)
```

- Never pass `--songbook` when running the app for a test: a command-line songbook is remembered in the user's settings (`~/.config/Modrozoubek/ZubuvZpevnikUi.conf`, `songbook/path`), so `--songbook tests/data/songbook` leaves the user's app on the 5-song fixture, whose stand-in classes can't build.
- `-DZUB_BUILD_APP=OFF` builds only core + tests (no Qt needed).
- Layout: public core headers in `core/include/zub/core/` (`#include <zub/core/...>`), namespace `zub`. App classes in `app/`, one class per `.hpp/.cpp` pair.
- New tests: add the `.cpp` to `tests/CMakeLists.txt`. Catch2 discovers the test cases.
- Test data: `tests/data/songbook/` is a miniature ZubuvZpevnik checkout (copies of real tricky songs + `Broken/`). Tests get its path from `ZUB_TEST_DATA_DIR`; never read the real sibling repo from normal tests.
- `build/debug/tests/zub_core_tests "[integration]"` runs the hidden tests against the real `../ZubuvZpevnik` (e.g. every song parses). Run it after changing parsers.
- Warnings: every target links `zub_warnings` (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion ...`). Keep the build warning-free.
- Formatting is `.clang-format` (a hook runs clang-format on C++ files Claude edits). clangd reads `build/debug/compile_commands.json` via `.clangd`.

## Guardrails

- **Sibling repos are not ours to restructure.** ZubuvZpevnik has CI and GitHub releases. Never commit, push, or change their layout, scripts or class files without asking. Editing song `.tex` files and project `\input` lists is the app's job and is fine when the task is about that.
- **Previews never write into `../ZubuvZpevnik/build/`.** Build previews in a temp dir (the app's cache dir, or `tmp/` here). Reuse `build/tmp-ly` only by symlink/read.
- Don't write in `../chords_converter/` (e.g. adding a `lib.rs`) without asking.
- Song files are Czech-heavy UTF-8. Keep encodings and line endings intact; don't reformat songs.
- Preview builds run in `QStandardPaths::CacheLocation/preview` (kept between builds for LilyPond's `tmp-ly` cache), from the editor's text via a song copy in `override/` that shadows the real file on TEXINPUTS. Errors come from `lualatex -file-line-error`; real logs for tests are in `tests/data/logs/`.
- **Songs are code.** Previews run `lualatex -shell-escape`, so `\directlua` etc. in a song would run with the user's rights. `zub::safety::unknownConstructs` checks songs against an allowlist of the commands real songs use (`knownSongCommands`); songs with anything else are never built automatically and only after confirmation. When the song format gains a command, add it to the allowlist (the `[integration]` test fails otherwise). Never weaken this check to make a test pass.
- Song highlighting is `app/syntax/zubuvzpevnik-song.xml` (KSyntaxHighlighting XML, loaded from the Qt resource path `:/org.kde.syntax-highlighting/syntax-addons`). When the song format gains commands, update it.
- Saving a song must be byte-exact for the parts not edited. `SongEditor` opens any file that would not round-trip (BOM, invalid UTF-8, mixed line endings, U+2028) read-only; keep that guarantee when touching the editor.
- No commits unless asked. Small, focused commits when asked.

## Workflow

- Work milestone by milestone from `docs/PLAN.md`; tick items off there.
- For non-trivial features, plan first (plan mode), then implement, then run the tests, then the `cpp-reviewer` agent.
- Agents in `.claude/agents/`: `songbook-latex` (LaTeX/song format, builds, linters), `chords-converter` (the Rust CLI), `cpp-reviewer` (read-only review of C++ changes), `cpp-implementer` (Sonnet, writes code from a precise spec).
- Division of labour (an experiment, started at milestone 2): the main session designs APIs and does cross-cutting work; well-specified implementation goes to `cpp-implementer`; `cpp-reviewer` (Opus) reviews. Compare cost and quality per milestone and adjust.
- Skills: `/preview-song <path>`, `/import-song <url>`, `/new-feature <description>`.

## Open decisions

Record new ones here (what, the options, who decides).

- Run `chords_converter tidy` over the whole songbook? It would change 87 songs (62 choruses → `\insertchorus`, 147 chord moves). Options: run it (then review the diff), or leave it to song-by-song use from the editor. The user decides; never run it on `../ZubuvZpevnik` without asking.
