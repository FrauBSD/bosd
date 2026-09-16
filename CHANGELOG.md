# Changelog

Newest first. Each section is a git tag; the bullets are the commits
that landed in that tag (from the previous tag, or from the start of
the repository for 1.0).

## 10.4 (2026-09-15)

- Add CHANGELOG.md
- Add 17 new example PNGs (created by 6 new python examples)
- Optimize example PNG generation performance
- Implement stadium stroke optimization for PNG generation

## 10.3 (2026-09-13)

- harden the warm-daemon IPC path

## 10.2 (2026-09-13)

- install README; PYTHON and optional tests

## 10.1 (2026-09-13)

- catalog the bitmap-font quarry

## 10.0 (2026-09-13)

- retell Why bosd for the catalog

## 9.9 (2026-09-13)

- scale the gauge bar with -s

## 9.8 (2026-09-13)

- gauge-alone -F colors bar labels

## 9.7 (2026-09-13)

- gauge-alone -p/-a/-f labels beside the bar

## 9.6 (2026-09-13)

- let test pause-mode ENTER skip standalone countdowns

## 9.5 (2026-09-13)

- split the gauge bar by concern and keep funcs small

## 9.4 (2026-09-13)

- smaller -c; panel-scaled -t above the gauge

## 9.3 (2026-09-13)

- stable Fixed captions; panel-scaled gauge

## 9.2 (2026-09-13)

- retune -T/-t defaults; refine countdown harness

## 9.1 (2026-09-13)

- ship tests and bsd glyph; gzip man pages on install

## 9.0 (2026-09-12)

- keep sources under 500 lines; add a visual test suite

## 8.0 (2026-09-10)

- honor -A/-O/-o on -t captions

## 7.0 (2026-09-10)

- honor -A/-O/-o on the gauge bar

## 6.0 (2026-09-10)

- remap -A/-O to peak opacity, not a baked-alpha multiply

## 5.8 (2026-09-10)

- error out when an icon name cannot be resolved

## 5.7 (2026-09-10)

- give -t its own slot and hold beside artwork and bar

## 5.6 (2026-09-10)

- -C with a show clears both slots before painting

## 5.5 (2026-09-10)

- hug icon outlines to soft alpha; no black under the glyph

## 5.4 (2026-09-10)

- equalize optical letterspacing in the BSD example

## 5.3 (2026-09-10)

- vertically center >100% gauge label on the bar

## 5.2 (2026-09-10)

- -s scales -t; caption sits above the gauge band

## 5.1 (2026-09-10)

- -f fontconfig face for -c/-t/-T/-b text

## 5.0 (2026-09-10)

- place -b beside the artwork's ink, not the canvas

## 4.9 (2026-09-10)

- -A/-O on icon badges; share outlined painter with -c/-T

## 4.8 (2026-09-10)

- -F colors countdown digits as well as -T/-t/-b

## 4.7 (2026-09-10)

- -F colors -T text and -b badges, not only -t

## 4.6 (2026-09-10)

- -A/-O on large text (-T); independent fill and halo alpha

## 4.5 (2026-09-10)

- -O opacity for the outline halo

## 4.4 (2026-09-10)

- -A opacity multiplies PNG alpha; outline stays opaque

## 4.3 (2026-09-09)

- clear badge from countdown digits; use regular face

## 4.2 (2026-09-09)

- bake PREFIX into man page and drop hard-coded /usr/local paths

## 4.1 (2026-09-09)

- gauge previous-percent watermark (-P); latch -B/-P

## 4.0 (2026-09-09)

- ship libbosd client library for in-process IPC

## 3.2 (2026-09-09)

- resolve all build deps via pkg-config

## 3.1 (2026-09-09)

- example BSD glyph and path fill in glyph.py
- glyph.py: Comments and move class below functions

## 3.0 (2026-09-08)

- small caption text (-t); giant text moves to -T

## 2.1 (2026-09-08)

- Massage README
- Bump version to 2.1

## 2.0 (2026-09-08)

- gauge bar (-g) alongside the artwork

## 1.0 (2026-09-08)

- on-screen display engine
- badge superscript and vertical offset
- shared glyph rasterizer (tools/glyph.py)
- honor -y on every show; clip offset shows at panel edges
- horizontal offset (-x)
- accept -h to print usage
- scale option (-s)
- list -h in both synopses; wrap usage under 80 columns
- no-outline option (-o); premultiply for XRender
- countdown shows (-c) and channel clear (-C)
- text shows (-t) with escape decoding, shared by -b
- catch the README and man page up to the code
- indefinite hold (hold\_seconds -1)
- direct render option (-D)
- captions above (-p) and below (-a) the artwork
- version option (-v)

