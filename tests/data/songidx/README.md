# songidx.lua capture

`order.sbx` is the real output of the songbook's title-index sorter for `order.sxd`
(the format `scripts/checkorder.py` writes: `%lang`, title, book position, link label):

    texlua ../ZubuvZpevnik/scripts/songidx.lua -l cs_CZ.utf8 order.sxd order.sbx

Captured 2026-11-11. Index order of the book positions: 8 5 1 4 9 2 6 7 3
(Africa, Akordy, The Bard's Song, Counting Stars, Cudzinka v tvojej zemi,
Čarodějnice z Amesbury, A Day in the Life, Hřbitov, Chlast): English articles
are rotated away (lang english only), Č sorts after C, Ch after H.
