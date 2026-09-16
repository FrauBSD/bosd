#!/usr/bin/env python3
############################################################ DOCSTRING

"""Display OSD glyphs (512x512 RGBA).

  mirror-on.png           white dual monitors (mirroring)
  mirror-off.png          white single monitor (laptop-only / leave)
  mirror-none.png         red dual monitors (cannot mirror; no external)
  mirror-extend.png       white side-by-side monitors (extend desktop)
  mirror-extend-none.png  red side-by-side (extend chosen, no external)
"""

############################################################ INCLUDES

from __future__ import annotations

import sys
from pathlib import Path

_here = Path(__file__).resolve().parent
for _d in (_here, _here.parent / "tools"):
	if (_d / "glyph.py").is_file():
		sys.path.insert(0, str(_d))
		break

from glyph import Canvas, emit  # noqa: E402

############################################################ GLOBALS

SIZE = 512
PAD = 56
WHITE = (255, 255, 255, 255)
RED = (220, 48, 48, 255)

############################################################ FUNCTIONS

def monitor(c, x, y, w, h, stroke, color):
	"""One monitor bezel plus stand."""
	c.rect_stroke(x, y, w, h, stroke, color)
	mx = x + w / 2
	c.line(mx, y + h, mx, y + h + 36, stroke * 0.7, color)
	c.line(mx - 40, y + h + 36, mx + 40, y + h + 36, stroke * 0.7, color)


def draw_mirror_pair(c, color, stroke=14):
	"""Offset dual monitors (mirroring metaphor)."""
	back = (PAD + 40, PAD + 60, 260, 200)
	front = (PAD + 140, PAD + 160, 260, 200)
	for x, y, w, h in (back, front):
		monitor(c, x, y, w, h, stroke, color)


def draw_single(c, color, stroke=16):
	"""Centered single monitor (laptop-only / leave mirror / leave extend)."""
	w, h = 300, 220
	x = (SIZE - w) / 2
	y = PAD + 80
	monitor(c, x, y, w, h, stroke, color)


def draw_extend(c, color, stroke=14):
	"""Side-by-side monitors bridged (extend desktop)."""
	w, h = 200, 180
	gap = 28
	total = w * 2 + gap
	x0 = (SIZE - total) / 2
	y = PAD + 90
	monitor(c, x0, y, w, h, stroke, color)
	monitor(c, x0 + w + gap, y, w, h, stroke, color)
	# Bridge at mid height between inner bezels.
	by = y + h / 2
	c.line(x0 + w, by, x0 + w + gap, by, stroke * 0.85, color)


############################################################ MAIN

def main(argv):
	outdir = Path(argv[1] if len(argv) > 1 else ".")
	jobs = (
		("on", draw_mirror_pair, WHITE),
		("off", draw_single, WHITE),
		("none", draw_mirror_pair, RED),
		("extend", draw_extend, WHITE),
		("extend-none", draw_extend, RED),
	)
	for name, drawer, color in jobs:
		c = Canvas(SIZE, SIZE)
		drawer(c, color)
		for path in emit(c, f"mirror-{name}.png", [outdir]):
			print(path)


if __name__ == "__main__":
	main(sys.argv)

################################################################################
# END
################################################################################
