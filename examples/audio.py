#!/usr/bin/env python3
############################################################ DOCSTRING

"""Audio OSD glyphs: white strokes on a transparent ground.

Geometry follows Material Design 24dp silhouettes, scaled to 512px.

  audio-speakers.png     speaker cone with volume arcs
  audio-headphones.png   square cups, interior stems, straight headband
"""

############################################################ INCLUDES

from __future__ import annotations

import math
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
PAD = 48
S = (SIZE - 2 * PAD) / 24.0
STROKE = 10

############################################################ FUNCTIONS

def m(x, y):
	return PAD + x * S, PAD + y * S


def speakers(c):
	"""Material volume_up style speaker outline."""
	body = [m(3, 10), m(7, 10), m(12, 6), m(12, 18), m(7, 14), m(3, 14)]
	c.polyline(body, STROKE, closed=True)
	for cx, rad in ((14, 3), (16, 5), (18, 7)):
		px, py = m(cx, 12)
		mr = rad * S
		c.arc(px, py, mr, math.radians(-70), math.radians(70), STROKE)


def headphones(c):
	"""Square ear cups, interior-side stems, straight headband bar."""
	cup_w, cup_h, cup_r = 88, 124, 18
	cup_y = 252
	band_y = 164
	left_x = 118
	right_x = 306

	c.roundrect(left_x, cup_y, cup_w, cup_h, cup_r, STROKE)
	c.roundrect(right_x, cup_y, cup_w, cup_h, cup_r, STROKE)

	left_inner_x = left_x + cup_w
	right_inner_x = right_x
	stem_y = cup_y + cup_h * 0.10

	c.line(left_inner_x, stem_y, left_inner_x, band_y, STROKE)
	c.line(right_inner_x, stem_y, right_inner_x, band_y, STROKE)
	c.line(left_inner_x, band_y, right_inner_x, band_y, STROKE)


############################################################ MAIN

def main(argv):
	outdir = Path(argv[1] if len(argv) > 1 else ".")
	for name, drawer in (("audio-speakers", speakers),
	    ("audio-headphones", headphones)):
		c = Canvas(SIZE, SIZE)
		drawer(c)
		for path in emit(c, f"{name}.png", [outdir]):
			print(path)


if __name__ == "__main__":
	main(sys.argv)

################################################################################
# END
################################################################################
