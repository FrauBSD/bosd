#!/usr/bin/env python3
############################################################ DOCSTRING

"""Lid-policy OSD glyphs: filled white solids.

Base is one slab; lid anchors inside it; moon/sun float with a clear
air gap.

  lid-awake.png  clamshell + sun
  lid-s0ix.png   clamshell + moon
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

def draw_clamshell(c):
	hx, hy = 100.0, 370.0
	c.fill_round_rect(hx - 10, hy - 10, hx + 240, hy + 58, 14)
	# Tip left of center so the upper-right accent has room
	tip_x, tip_y = 240.0, 155.0
	dx, dy = tip_x - hx, tip_y - hy
	length = math.hypot(dx, dy)
	ux, uy = dx / length, dy / length
	nx, ny = -uy, ux
	if ny > 0:
		nx, ny = -nx, -ny
	th = 38.0
	ax, ay = hx - ux * 24, hy + 10
	pts = [
		(ax - nx * 2, ay - ny * 2),
		(tip_x - nx * 2, tip_y - ny * 2),
		(tip_x - nx * th, tip_y - ny * th),
		(ax - nx * th, ay - ny * th),
	]
	c.fill_poly(pts)
	# Fat hinge weld: erase any AA gap between lid and deck
	c.stamp(hx + 8, hy + 4, 32)
	return tip_x, tip_y


def draw_sun(c, cx, cy, r=30):
	c.disc(cx, cy, r)
	c.punch(cx, cy, r - 11)
	for i in range(8):
		ang = i * (math.pi / 4) + math.pi / 8
		x0 = cx + math.cos(ang) * (r + 12)
		y0 = cy + math.sin(ang) * (r + 12)
		x1 = cx + math.cos(ang) * (r + 40)
		y1 = cy + math.sin(ang) * (r + 40)
		c.line(x0, y0, x1, y1, 7)


def draw_moon(c, cx, cy, r=48):
	c.disc(cx, cy, r)
	c.punch(cx + r * 0.40, cy - r * 0.10, r * 0.78)


def paint(sun=False):
	c = Canvas(512, 512)
	draw_clamshell(c)
	# Fixed upper-right; >=48px canvas margin for outline dilation;
	# clear of the lid tip.
	if sun:
		draw_sun(c, 380, 105, r=28)
	else:
		draw_moon(c, 395, 110, r=44)
	return c


############################################################ MAIN

def main(argv):
	outdir = Path(argv[1] if len(argv) > 1 else ".")
	for name, sun in (("awake", True), ("s0ix", False)):
		for path in emit(paint(sun), f"lid-{name}.png", [outdir]):
			print(path)


if __name__ == "__main__":
	main(sys.argv)

################################################################################
# END
################################################################################
