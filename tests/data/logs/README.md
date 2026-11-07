Real `lualatex -file-line-error` logs of a single-song preview build (`preview.tex` wrapping
`AgFlek/BlazniUmirajiNadvakrat`, class velkyzpevnik), captured with the layout `zub::preview::prepare()`
uses: the work dir is `/work`, the song copy `/work/override/AgFlek/BlazniUmirajiNadvakrat.tex`,
the songbook `/songbook`. Personal paths were replaced.

| Log | What was broken | First error |
|---|---|---|
| clean.log | nothing | none (PDF produced) |
| undefined-macro.log | `\nonexistentmacro` added on line 20 | song line 20 |
| missing-endverse.log | first `\endverse` deleted | song line 24 (the next `\beginverse`) |
| unclosed-chord.log | `]` of the first chord on line 18 removed | song line 18 |
| missing-song.log | `\input{Nobody/NoSuchSong}` | `! LaTeX Error: File ... not found.`, located at `./preview.tex:4` (not in the song) |
