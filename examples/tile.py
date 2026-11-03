#!/usr/bin/env python3
############################################################ LICENSE
#
# Copyright (c) 2026 Devin Teske <dteske@FreeBSD.org>
#
# SPDX-License-Identifier: BSD-2-Clause
#
############################################################ IDENT(1)
#
# $Title: Make window tile-mode PNG glyphs $
# $Copyright: 2026 Devin Teske. All rights reserved. $
# $FrauBSD: bosd/examples/tile.py 2026-11-03 10:54:46 -0800 Devin Teske $
#
############################################################ DOCSTRING

"""Super+T and Super-arrow tiling OSD glyphs: filled white solids.

  tile-on.png             2x2 panes (tiling mode)
  tile-float.png          overlapping window frames (floating mode)
  tile-left.png           left half
  tile-right.png          right half
  tile-up.png             working area (Super+Up)
  tile-down.png           window dropped out of the grid
  tile-top.png            top half
  tile-bottom.png         bottom half
  tile-top-left.png       top-left quarter
  tile-top-right.png      top-right quarter
  tile-bottom-left.png    bottom-left quarter
  tile-bottom-right.png   bottom-right quarter
  tile-insert.png         grid, window joined at the top
  tile-rotate.png         grid, window stepped back
  tile-next.png           window moved to the next screen
  tile-prev.png           window moved to the previous screen
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


def stroke_desk(c):
	ox0, oy0, ox1, oy1 = 64.0, 88.0, 448.0, 424.0
	c.roundrect(ox0, oy0, ox1 - ox0, oy1 - oy0, 22, 14)


def cell_box(col, row, cols, rows):
	#
	# Same inset and gap as the Super+T grid, so a quarter sits on a
	# tile-on pane and a half spans a pair of them.
	#
	ox0, oy0, ox1, oy1 = 64.0, 88.0, 448.0, 424.0
	inner = 28.0
	gap = 18.0
	x0, y0 = ox0 + inner, oy0 + inner
	x1, y1 = ox1 - inner, oy1 - inner
	cw = (x1 - x0 - gap * (cols - 1)) / cols
	ch = (y1 - y0 - gap * (rows - 1)) / rows
	a = x0 + col * (cw + gap)
	b = y0 + row * (ch + gap)
	return a, b, a + cw, b + ch


def fill_cell(c, col, row, cols, rows):
	a, b, d, e = cell_box(col, row, cols, rows)
	c.fill_round_rect(a, b, d, e, 14)


def stroke_grid(c, cols, rows):
	for col in range(cols):
		for row in range(rows):
			a, b, d, e = cell_box(col, row, cols, rows)
			c.roundrect(a, b, d - a, e - b, 14, 14)


def draw_span(c, col, row, cols, rows):
	stroke_desk(c)
	fill_cell(c, col, row, cols, rows)


def draw_left(c):
	draw_span(c, 0, 0, 2, 1)


def draw_right(c):
	draw_span(c, 1, 0, 2, 1)


def draw_up(c):
	#
	# Super+Up fills the working area. The top half is tile-top.
	#
	draw_span(c, 0, 0, 1, 1)


def draw_down(c):
	#
	# Super+Down leaves the grid. The frame hangs out the bottom of
	# the desk; tile-float stays the Super+T off glyph.
	#
	stroke_desk(c)
	x0, y0, x1, y1 = 150.0, 280.0, 390.0, 488.0
	c.roundrect(x0, y0, x1 - x0, y1 - y0, 20, 13)
	c.fill_round_rect(x0 + 18, y0 + 18, x1 - 18, y0 + 58, 8)


def draw_top(c):
	draw_span(c, 0, 0, 1, 2)


def draw_bottom(c):
	draw_span(c, 0, 1, 1, 2)


def draw_top_left(c):
	draw_span(c, 0, 0, 2, 2)


def draw_top_right(c):
	draw_span(c, 1, 0, 2, 2)


def draw_bottom_left(c):
	draw_span(c, 0, 1, 2, 2)


def draw_bottom_right(c):
	draw_span(c, 1, 1, 2, 2)


def draw_insert(c):
	#
	# Ctrl+Up joins the layout. The other three cells stay outlines
	# so this is not the top-left quarter.
	#
	stroke_desk(c)
	stroke_grid(c, 2, 2)
	fill_cell(c, 0, 0, 2, 2)


def draw_rotate(c):
	#
	# Ctrl+Down steps backward. The lit cell is the lower one, with
	# the rest of the grid outlined (not the bottom half).
	#
	stroke_desk(c)
	stroke_grid(c, 2, 2)
	fill_cell(c, 0, 1, 2, 2)


def draw_monitors(c):
	#
	# Two outputs, left then right. The gap is where the chevron sits.
	#
	frames = (
		(40.0, 124.0, 220.0, 388.0),
		(292.0, 124.0, 472.0, 388.0),
	)
	for x0, y0, x1, y1 in frames:
		c.roundrect(x0, y0, x1 - x0, y1 - y0, 18, 12)
	return frames


def draw_moved_window(c, x0, y0, x1, y1):
	ix0 = x0 + 28.0
	iy0 = y0 + 48.0
	ix1 = x1 - 28.0
	iy1 = y1 - 36.0
	c.roundrect(ix0, iy0, ix1 - ix0, iy1 - iy0, 16, 11)
	c.fill_round_rect(ix0 + 12, iy0 + 12, ix1 - 12, iy0 + 46, 6)


def draw_chevron(c, to_right):
	if to_right:
		c.fill_poly(((238.0, 232.0), (238.0, 280.0), (274.0, 256.0)))
	else:
		c.fill_poly(((274.0, 232.0), (274.0, 280.0), (238.0, 256.0)))


def draw_next(c):
	#
	# The window has landed on the screen to the right.
	#
	frames = draw_monitors(c)
	draw_chevron(c, True)
	draw_moved_window(c, *frames[1])


def draw_prev(c):
	#
	# The window has landed on the screen to the left.
	#
	frames = draw_monitors(c)
	draw_chevron(c, False)
	draw_moved_window(c, *frames[0])


############################################################ MAIN

def main(argv):
	outdir = Path(argv[1] if len(argv) > 1 else ".")
	glyphs = (
		("on", draw_tile),
		("float", draw_float),
		("left", draw_left),
		("right", draw_right),
		("up", draw_up),
		("down", draw_down),
		("top", draw_top),
		("bottom", draw_bottom),
		("top-left", draw_top_left),
		("top-right", draw_top_right),
		("bottom-left", draw_bottom_left),
		("bottom-right", draw_bottom_right),
		("insert", draw_insert),
		("rotate", draw_rotate),
		("next", draw_next),
		("prev", draw_prev),
	)
	for name, drawer in glyphs:
		c = Canvas(512, 512)
		drawer(c)
		for path in emit(c, f"tile-{name}.png", [outdir]):
			print(path)


if __name__ == "__main__":
	main(sys.argv)

################################################################################
# END
################################################################################
