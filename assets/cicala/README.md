# Cicala symbol

`mark.svg` is the existing public Cicala symbol from
[`website/public/brand/cicala-mark.svg`](https://github.com/giacomomellone/cicala/blob/ebefcde6907664c7ebc28b2fbcf245369e2bee89/website/public/brand/cicala-mark.svg).
Source SHA-256: `d880ea50ecbcc4ed5b7605c252e137dbe66db29284bb26285b2c567b8e4e772b`.

`python3 scripts/cicala/generate_logo.py` produces the 32 and 36 px constant
bitmaps in `src/cicala/CicalaLogoBits.h`. It supports this source's closed
polygon paths, uses the SVG even-odd fill rule, and thresholds 8× coverage
at 50%. The output is MSB-first, with one meaning transparent and zero ink.
`CicalaLogo.h` draws through the renderer's orientation-aware pixel API.
