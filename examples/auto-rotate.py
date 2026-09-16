#!/usr/bin/env python3
############################################################ DOCSTRING

"""Auto-rotate OSD glyphs: white stroke on a transparent ground.

  auto-rotate-on.png   arc only (rotation enabled)
  auto-rotate-off.png  arc plus closed lock (held)

No tablet glyph. Lock only when rotation is held.
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
MARGIN = 40
STROKE = 16

############################################################ FUNCTIONS

def arrow_head(c, cx, cy, rad, tip_ang, head_len, half_w, stroke):
	"""Open chevron (two strokes).

	Tip on the circle; barb ends equally outside and inside on the
	same radial (balanced across the boundary).
	"""
	tipx = cx + rad * math.cos(tip_ang)
	tipy = cy + rad * math.sin(tip_ang)
	base_ang = tip_ang - head_len / rad
	rx = math.cos(base_ang)
	ry = math.sin(base_ang)
	ox = cx + (rad + half_w) * rx
	oy = cy + (rad + half_w) * ry
	ix = cx + (rad - half_w) * rx
	iy = cy + (rad - half_w) * ry
	c.line(tipx, tipy, ox, oy, stroke)
	c.line(tipx, tipy, ix, iy, stroke)


def draw_rotate_arc(c, cx, cy, rad, stroke):
	# Gap on the left. Span is about 81.6% of a full circle.
	tip_ang = 2.66
	start_ang = -2.46
	st = stroke * 0.95
	head_len = stroke * 4.5
	half_w = stroke * 2.8
	c.arc(cx, cy, rad, start_ang, tip_ang, st, steps=200)
	arrow_head(c, cx, cy, rad, tip_ang, head_len, half_w, st)


def draw_lock_closed(c, cx, cy, rad, stroke):
	"""Closed padlock; (cx, cy) is geometric center of body+shackle."""
	st = stroke * 0.95
	bw = rad * 0.50
	bh = rad * 0.39
	post = rad * 0.07
	left_inset = 0.22
	sh_rad = bw * (1.0 - 2 * left_inset) / 2
	sh_h = post + sh_rad
	total_h = sh_h + bh
	by = cy - total_h / 2 + sh_h
	bx = cx - bw / 2
	c.rect_stroke(bx, by, bw, bh, st)

	kh_y = by + bh * 0.36
	kh_r = rad * 0.055
	c.circle(cx, kh_y, kh_r, st * 0.7)
	c.line(cx, kh_y + kh_r * 0.7, cx, by + bh * 0.78, st * 0.7)

	left_x = bx + bw * left_inset
	right_x = bx + bw * (1.0 - left_inset)
	sh_cx = (left_x + right_x) / 2
	c.line(left_x, by, left_x, by - post, st)
	c.line(right_x, by, right_x, by - post, st)
	c.arc(sh_cx, by - post, sh_rad, math.pi, 2 * math.pi, st, steps=110)


def render(with_lock):
	c = Canvas(SIZE, SIZE)
	cx = cy = SIZE / 2
	rad = SIZE / 2 - MARGIN - STROKE
	draw_rotate_arc(c, cx, cy, rad, STROKE)
	if with_lock:
		draw_lock_closed(c, cx, cy, rad, STROKE)
	return c


############################################################ MAIN

def main(argv):
	outdir = Path(argv[1] if len(argv) > 1 else ".")
	for name, locked in (("on", False), ("off", True)):
		c = render(locked)
		for path in emit(c, f"auto-rotate-{name}.png", [outdir]):
			print(path)


if __name__ == "__main__":
	main(sys.argv)

################################################################################
# END
################################################################################
