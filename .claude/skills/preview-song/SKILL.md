---
name: preview-song
description: Build a single song from ../ZubuvZpevnik into a PDF in a temp dir and show what it looks like. Use to check a song's typesetting or LaTeX errors without building a whole songbook.
argument-hint: "<songs/chords/Artist/Title.tex or Artist/Title> [class]"
allowed-tools: Bash(lualatex *), Bash(pdfinfo *), Bash(pdftoppm *), Bash(mkdir *), Read, Write
---
Preview the song `$0` from ../ZubuvZpevnik.

1. Resolve the song to `Artist/Title` (strip `songs/chords/` and `.tex`). Check the file exists.
2. Create a temp dir `tmp/preview/<Artist>_<Title>/` in this repo (never use ../ZubuvZpevnik/build/).
3. Write `preview.tex` there. This minimal wrapper is verified to build (the song environment needs no index):
   ```latex
   \documentclass[chorded, onesongcolumn, noshading]{velkyzpevnik}
   \begin{document}
   \begin{songs}{}
   \input{Artist/Title}
   \end{songs}
   \end{document}
   ```
   If a class `$1` is given (e.g. `malyzpevnik`, `taborovyzpevnik`), copy the `\documentclass` line from the matching `../ZubuvZpevnik/projects/*/*.tex` instead.
4. Build from that dir with absolute paths (the song may contain LilyPond, so `-shell-escape` is needed):
   `cd tmp/preview/<Artist>_<Title> && TEXINPUTS=".:<abs>/ZubuvZpevnik:<abs>/ZubuvZpevnik/zubuvzpevnik:<abs>/ZubuvZpevnik/songs/chords:" lualatex -shell-escape -interaction=nonstopmode -halt-on-error preview.tex`
5. If it fails, show the first error from `preview.log` and the song lines it points at, and suggest a fix (do not edit the song unless asked).
6. If it succeeds, run `pdfinfo` for the page count and `pdftoppm -png -r 60 preview.pdf page`, then Read the PNGs to look at the result. Report the page count, whether the song fits one page, and anything that looks wrong (chords off syllables, overflow, odd breaks).

Once the app has its own preview pipeline, prefer what `core/` does over these steps and update this skill.
