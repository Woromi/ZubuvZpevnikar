---
name: import-song
description: Convert a song from a chords website (or saved page) into the ZubuvZpevnik LaTeX format with chords_converter, then lint it and propose where it goes. Use when the user gives a song URL to add.
argument-hint: "<url | path/to/saved.html>"
disable-model-invocation: true
allowed-tools: Bash(cargo run *), Bash(../ZubuvZpevnik/scripts/checksongs.py *), Bash(../ZubuvZpevnik/scripts/checkkeys.py *), Read, Grep, Glob
---
Import the song from `$ARGUMENTS`.

1. From this repo's root run `cargo run --release --manifest-path ../chords_converter/Cargo.toml -- "$ARGUMENTS" latex`. The converter writes to `out/` relative to the current directory, so the result lands in this repo's git-ignored `out/`, not in ../chords_converter. If it reports a bot check, explain the two workarounds from its README (saved page, or CHORDS_COOKIE + CHORDS_USER_AGENT) and stop.
2. Find the produced file under `out/latex/<Artist>/<Title>.tex` and show it.
3. Check whether ../ZubuvZpevnik/songs/chords/<Artist>/ already has this song (also look for a similar title). If it does, show a diff instead of proposing a new file.
4. Review it against ../ZubuvZpevnik/zubuvzpevnik/README.md: metadata keys (`by`, `lang`, `key`, ...), song parts, chord placement that looks wrong, missing tempo. List the suggested fixes.
5. Propose copying it to ../ZubuvZpevnik/songs/chords/<Artist>/<Title>.tex and which project `\input` lists it could join (alphabetical by title). Ask before writing anything into ../ZubuvZpevnik.
6. After the user accepts and the file is copied, run `checksongs.py` and `checkkeys.py` on it and report.

This is the manual version of what the app's import feature will automate (milestone 4 in docs/PLAN.md).
