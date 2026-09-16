#!/usr/bin/env python3
############################################################ DOCSTRING

"""Super+T tiling OSD glyphs: filled white solids.

  tile-on.png     2x2 panes (tiling mode)
  tile-float.png  overlapping window frames (floating mode)
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

############################################################ FUNCTIONS

def draw_tile(c):
	#
	# Outer desk plus four abutting panes: the Super+T packed grid.
	#
	ox0, oy0, ox1, oy1 = 64.0, 88.0, 448.0, 424.0
	c.roundrect(ox0, oy0, ox1 - ox0, oy1 - oy0, 22, 14)
	gap = 18.0
	inner = 28.0
	x0, y0 = ox0 + inner, oy0 + inner
	x1, y1 = ox1 - inner, oy1 - inner
	mid_x = (x0 + x1) / 2
	mid_y = (y0 + y1) / 2
	panes = (
		(x0, y0, mid_x - gap / 2, mid_y - gap / 2),
		(mid_x + gap / 2, y0, x1, mid_y - gap / 2),
		(x0, mid_y + gap / 2, mid_x - gap / 2, y1),
		(mid_x + gap / 2, mid_y + gap / 2, x1, y1),
	)
	for a, b, d, e in panes:
		c.fill_round_rect(a, b, d, e, 14)


def draw_float(c):
	#
	# Two cascaded frames with title bars: Super+T off / ripped-out float.
	#
	frames = (
		(72.0, 72.0, 340.0, 340.0),
		(172.0, 172.0, 440.0, 440.0),
	)
	for x0, y0, x1, y1 in frames:
		c.roundrect(x0, y0, x1 - x0, y1 - y0, 20, 13)
		c.fill_round_rect(x0 + 18, y0 + 18, x1 - 18, y0 + 58, 8)


############################################################ MAIN

def main(argv):
	outdir = Path(argv[1] if len(argv) > 1 else ".")
	for name, drawer in (("on", draw_tile), ("float", draw_float)):
		c = Canvas(512, 512)
		drawer(c)
		for path in emit(c, f"tile-{name}.png", [outdir]):
			print(path)


if __name__ == "__main__":
	main(sys.argv)

################################################################################
# END
################################################################################
