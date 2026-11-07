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
- `chords_converter` is binary-only (no `lib.rs`). `cargo run -- <url|file|-> latex` writes `out/latex/<Artist>/<Title>.tex` relative to the **current directory** (with `--manifest-path` from here, that means this repo's ignored `out/`).

## Architecture (hard rule)

```
core/    std-only C++23 library. NO Qt includes, NO Qt types. Links nothing from Qt.
         Song model, song .tex parser/writer, songbook project parser/writer,
         process running (lualatex, pdftoppm, chords_converter), paths/config.
app/     Qt 6 Widgets UI. Thin: owns windows, editor, PDF view, models for views.
         Converts std::string <-> QString / std::filesystem::path <-> QString only here.
tests/   Catch2 v3 unit tests for core/ (no Qt needed to build or run them).
```

- If something in `core/` seems to need Qt, stop and ask instead of adding the dependency.
- Prefer `std::filesystem`, `std::string_view`, `std::expected`/`std::optional`, ranges. No raw `new`/`delete` in `core/`.
- In `app/`, normal Qt idioms are fine: parent ownership, signals/slots. Keep logic out of widgets.
- Long-running work (lualatex, converter) must not block the UI thread.

## Build & test

```bash
cmake --preset debug                 # configure (Ninja, build/debug, compile_commands.json)
cmake --build --preset debug         # build
ctest --preset debug                 # run core unit tests (Catch2)
build/debug/app/zubuvzpevnik-ui      # run the app
QT_QPA_PLATFORM=offscreen timeout 3 build/debug/app/zubuvzpevnik-ui   # headless smoke test (exit 124 = ran fine)
```

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
- Don't write in `../chords_converter/` (e.g. adding a `lib.rs`) without asking. See Open decisions.
- Song files are Czech-heavy UTF-8. Keep encodings and line endings intact; don't reformat songs.
- No commits unless asked. Small, focused commits when asked.

## Workflow

- Work milestone by milestone from `docs/PLAN.md`; tick items off there.
- For non-trivial features, plan first (plan mode), then implement, then run the tests, then the `cpp-reviewer` agent.
- Agents in `.claude/agents/`: `songbook-latex` (LaTeX/song format, builds, linters), `chords-converter` (the Rust CLI), `cpp-reviewer` (read-only review of C++ changes).
- Skills: `/preview-song <path>`, `/import-song <url>`, `/new-feature <description>`.

## Open decisions

- **How the app calls the converter:** run the CLI as a subprocess (no change to that repo, current plan) vs adding a `lib.rs` + C ABI and linking it (faster, needs changes there). Ask before choosing the second.
- **Preview rendering:** QtPdf (`QPdfView`) vs `pdftoppm` → images. QtPdf is preferred if the package is available.
- **Single-song preview:** a wrapper `.tex` with the right class and one `\input`. Which class/options to use per songbook is not yet decided.
