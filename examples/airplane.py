#!/usr/bin/env python3
############################################################ DOCSTRING

"""Airplane-mode OSD glyphs: filled white solids.

  airplane-on.png   airplane  (airplane mode active)
  airplane-off.png  wifi arcs (airplane mode inactive / radios on)
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

############################################################ FUNCTIONS

def draw_airplane(c):
	"""Classic OSD airplane: nose upper-right, wings, stabilizer."""
	cx, cy = 256.0, 270.0
	ang = -math.pi / 5.5
	ux, uy = math.cos(ang), math.sin(ang)
	nx, ny = -uy, ux
	half = 22.0
	nose = 175.0
	tail = 155.0
	body = [
		(cx + ux * nose - nx * (half * 0.35),
		    cy + uy * nose - ny * (half * 0.35)),
		(cx + ux * nose + nx * (half * 0.35),
		    cy + uy * nose + ny * (half * 0.35)),
		(cx - ux * (tail * 0.15) + nx * half,
		    cy - uy * (tail * 0.15) + ny * half),
		(cx - ux * tail + nx * (half * 0.55),
		    cy - uy * tail + ny * (half * 0.55)),
		(cx - ux * tail - nx * (half * 0.55),
		    cy - uy * tail - ny * (half * 0.55)),
		(cx - ux * (tail * 0.15) - nx * half,
		    cy - uy * (tail * 0.15) - ny * half),
	]
	c.fill_poly(body)

	wing = [
		(cx + ux * 10 - nx * 18, cy + uy * 10 - ny * 18),
		(cx + ux * 55 - nx * 12, cy + uy * 55 - ny * 12),
		(cx - ux * 5 - nx * 145, cy - uy * 5 - ny * 145),
		(cx - ux * 45 - nx * 130, cy - uy * 45 - ny * 130),
		(cx - ux * 55 - nx * 20, cy - uy * 55 - ny * 20),
		(cx - ux * 20 + nx * 10, cy - uy * 20 + ny * 10),
	]
	c.fill_poly(wing)
	wing2 = [
		(cx + ux * 10 + nx * 18, cy + uy * 10 + ny * 18),
		(cx + ux * 55 + nx * 12, cy + uy * 55 + ny * 12),
		(cx - ux * 5 + nx * 145, cy - uy * 5 + ny * 145),
		(cx - ux * 45 + nx * 130, cy - uy * 45 + ny * 130),
		(cx - ux * 55 + nx * 20, cy - uy * 55 + ny * 20),
		(cx - ux * 20 - nx * 10, cy - uy * 20 - ny * 10),
	]
	c.fill_poly(wing2)

	#
	# Tail: convex pieces (fill_poly is convex-only). Horizontals are
	# longer/wider so they read as stabs, not nubs; fin unchanged.
	#
	tailp = [
		(cx - ux * 90 - nx * 10, cy - uy * 90 - ny * 10),
		(cx - ux * 122 - nx * 12, cy - uy * 122 - ny * 12),
		(cx - ux * 142 - nx * 58, cy - uy * 142 - ny * 58),
		(cx - ux * 108 - nx * 54, cy - uy * 108 - ny * 54),
	]
	c.fill_poly(tailp)
	tailp2 = [
		(cx - ux * 90 + nx * 10, cy - uy * 90 + ny * 10),
		(cx - ux * 122 + nx * 12, cy - uy * 122 + ny * 12),
		(cx - ux * 142 + nx * 58, cy - uy * 142 + ny * 58),
		(cx - ux * 108 + nx * 54, cy - uy * 108 + ny * 54),
	]
	c.fill_poly(tailp2)

	fin = [
		(cx - ux * 88 - nx * 1, cy - uy * 88 - ny * 1),
		(cx - ux * 108 + nx * 12, cy - uy * 108 + ny * 12),
		(cx - ux * 158 + nx * 2, cy - uy * 158 + ny * 2),
		(cx - ux * 148 - nx * 6, cy - uy * 148 - ny * 6),
	]
	c.fill_poly(fin)


def draw_wifi(c):
	"""Three concentric upward arcs plus center disc (classic Wi-Fi)."""
	cx, cy = 256.0, 390.0
	c.stamp(cx, cy - 8, 22)
	for outer, thick in ((95, 28), (165, 30), (240, 32)):
		inner = outer - thick
		a0 = -math.pi / 2 - math.radians(55)
		a1 = -math.pi / 2 + math.radians(55)
		rm = (inner + outer) / 2
		c.arc(cx, cy, rm, a0, a1, thick / 2,
		    steps=max(int(outer * 2.5), 40))


############################################################ MAIN

def main(argv):
	outdir = Path(argv[1] if len(argv) > 1 else ".")
	for name, drawer in (("on", draw_airplane), ("off", draw_wifi)):
		c = Canvas(512, 512)
		drawer(c)
		for path in emit(c, f"airplane-{name}.png", [outdir]):
			print(path)


if __name__ == "__main__":
	main(sys.argv)

################################################################################
# END
################################################################################
