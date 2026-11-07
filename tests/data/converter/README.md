# chords_converter captures

Real stdout (`.out`), stderr (`.err`) and exit code (`.code`) of the chords_converter CLI
(release build), run from a scratch work dir (shown as `/work`; `$HOME` as `/home/user`).
No real websites were contacted: URLs point at 127.0.0.1 or an `.invalid` host.

| case | command | notes |
|---|---|---|
| success | `chords_converter in.txt latex` | text file "Karel Kryl - Bratříčku, zavírej vrátka"; output in `success-workdir/out/latex/KarelKryl/BratrickuZavirejVratka.tex` |
| nofile | `chords_converter nope.txt latex` | exit 1, "Input Error:" |
| unsupported | `chords_converter http://127.0.0.1:9/x latex` | exit 1, "Parser Error: No parser available", with Retrying lines |
| network | `chords_converter http://akordy.kytary.cz.invalid/song/x latex` | exit 1, "the request failed", Retrying lines first |
| botcheck | `chords_converter http://127.0.0.1:8765/akordy.kytary.cz/x latex` | local server answering a page with `<title>Just a moment...</title>` |
| http404 | same host, `/akordy.kytary.cz/missing` | local python http.server, 404 |
| readonly | success input, with an empty read-only `out/latex/KarelKryl/BratrickuZavirejVratka.tex` pre-created | exit 0, stdout still says "Saving output to:", stderr "Failed to create file: Permission denied" |

Note: the unsupported case printed no dumped HTML on stdout in this version.
