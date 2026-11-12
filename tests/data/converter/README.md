# chords_converter captures

Real stdout (`.out`), stderr (`.err`) and exit code (`.code`) of the chords_converter CLI
(release build), run from a scratch work dir (shown as `/work`; `$HOME` as `/home/user`).
No real websites were contacted: URLs point at 127.0.0.1 or an `.invalid` host.

| case | command | notes |
|---|---|---|
| success | `chords_converter /work/in.txt latex` | text file below; output in `success-workdir/out/latex/MartaVymyslena/LampionPodMostem.tex` (a made-up song; a chord-only intro and an unclosed quote, so it has `\begininstrumental` and `\textquotedbl{}`; the `src` is normalised to `/work/in.txt`) |
| nofile | `chords_converter nope.txt latex` | exit 1, "Input Error:" |
| unsupported | `chords_converter http://127.0.0.1:9/x latex` | exit 1, "Parser Error: No parser available", with Retrying lines |
| network | `chords_converter http://akordy.kytary.cz.invalid/song/x latex` | exit 1, "the request failed", Retrying lines first |
| botcheck | `chords_converter http://127.0.0.1:8765/akordy.kytary.cz/x latex` | local server answering a page with `<title>Just a moment...</title>` |
| botcheckfile | `chords_converter /work/page.html latex` | a saved bot-check page (`<title>Just a moment...</title>`); exit 1, "is a bot check" |
| http404 | same host, `/akordy.kytary.cz/missing` | local python http.server, 404 |
| readonly | success input, with an empty read-only `out/latex/MartaVymyslena/LampionPodMostem.tex` pre-created | exit 1, stdout still says "Saving output to:", stderr "Failed to create file: Permission denied" |
| tidy | `chords_converter tidy /work/tidy-input.tex -o -` | input `tidy-input.tex`; stdout is the tidied song, stderr one `<file>:<line>: <description>` per change (a chord moved, a repeated chorus, a typed-out reminder); the line numbers are of the INPUT text (the reminder is on input line 32, line 29 of the output) |
| tidy-unreadable | `chords_converter tidy /work/nope.tex -o -` | exit 1, "Failed to read file" |
| shrink | `chords_converter shrink /work/shrink-input.tex --fit a5 --measure --songbook <ZubuvZpevnik> --json` | input `shrink-input.tex` (a made-up song, Test Artist / Paper Lanterns); both papers measured, A5 has 4 wide lines, 2 proposals (stage 2: `chordsoffonsmallpage@48` graded poor, `smalleronsmallpage@16` graded last resort); `.json` is stdout, stderr empty |
| shrink-a4only | same on `shrink-a4only-input.tex` (a made-up song, Test Artist / Two Voices at the Gate) | only `a4` under `measured` (it does not fit A4, so A5 is not measured), 5 proposals: stage 1 (`chordsoff@86`, `@81`), then stage 2 (`chordsoffonsmallpage@86`, `@81`, `smalleronsmallpage@9` graded last resort) |
| check | `chords_converter check /work/check-input.tex --json` | made-up English song with mistakes; not measured; in song order: `typo@14` ("Ill" → "I'll"), `apostrophes@15`, `restructure@18` (an unnumbered verse with the chorus's words), `typo@29` (a chord inside the word: no edits, fix by hand); all `score` 0, `grade` `correction`, each with a `dismiss` edit |
| check-apply | `chords_converter apply /work/check-input.tex --id typo@14 --id apostrophes@15 --id restructure@18 -o -` | the three together; stderr names each change |
| check-cs | `chords_converter check /work/check-cs-input.tex --json` | made-up Czech song (`lang={czech}`, the braces are needed): one `comma@5` finding ("vím že", kind `comma`, category `typo`) with its edit and its `dismiss` edit (`% nocheck` line) |
| join | `chords_converter join /work/join-input.tex --songbook <ZubuvZpevnik> --json` | made-up long song (6 verses of 8 short lines, 6 choruses of 4); both papers measured; 2 `joinlines` proposals |
| apply | `chords_converter apply /work/shrink-input.tex --id chordsoffonsmallpage@48 -o -` | stdout is the changed song (the first proposal of `shrink`) |
| apply-stale | `chords_converter apply /work/shrink-input.tex --id nosuch@999 -o -` | exit 1, "No proposal '...' for '...' (ask again with shrink, join or check)." |
| version | `chords_converter --version` | `chords_converter 0.10.0` |

All converter captures (everything but `success`, `readonly` and the failures, whose output did not change) were re-captured with `344cac5` (0.10.0) on 2026-11-12; the tidy output did not change. Older note: the tidy captures were from converter `41755c1`, the check/join/version ones from `e1db028`/`ef4faa9` (0.8.0), both 2026-11-08 (0.7.0: `suggest` renamed `shrink`; `check` lists corrections; proposals carry `score`, `grade`, `sources` and `category`; apply names the change on stderr). The song paths in the JSON are normalised to `/work`, the songbook to `/home/user/ZubuvZpevnik`.

The public repo holds no real lyrics, so `success`, `readonly`, `shrink`, `shrink-a4only`, `apply` and `apply-stale` (with their inputs) were re-captured with made-up songs on 2026-11-11 with converter `ef4faa9` (0.8.0). The made-up `shrink-input.tex` and `shrink-a4only-input.tex` were tuned until the converter gave the same situations as before (see the table). The success input `in.txt`:

```
Lampión pod mostem
Artist: Marta Vymyšlená

[Intro]
Ami  G  C  E

[Verse]
Ami          G
Lampióne, svítíš nad vodou,
C            E
ať se nám to vždycky vrátí,
Ami             G
řekl "stařík ještě včera,
```

Note: the unsupported case prints no dumped HTML on stdout (older versions did).
