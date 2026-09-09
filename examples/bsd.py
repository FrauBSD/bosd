#!/usr/bin/env python3
############################################################ DOCSTRING

"""Example bosd glyph: the letters BSD on a transparent ground.

The letterforms follow the FreeBSD wordmark: a monoline, rounded-square sans.
Each letter is one nonzero fill (outer contours plus reverse-wound counters)
so joins stay seamless and a translucent color lands once.  Geometry is in
wordmark units (220-unit cap height, 24-unit stroke) and scaled to fit the
canvas.  The fill is FreeBSD red (rgb:cb/10/08) at 80% opacity to show
translucent rendering.
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

from glyph import Canvas, arc_pts, outline, round_rect_pts  # noqa: E402

############################################################ GLOBALS

PI = math.pi

# The red of the FreeBSD login greeter (rgb:cb/10/08), 80% opaque.
BSD_RED = (0xCB, 0x10, 0x08)
OPACITY = 0.8
COLOR = BSD_RED + (round(255 * OPACITY),)

# Wordmark units.
STROKE = 24		# weight of every stem, bar, and bowl
CAP = 220		# cap height
B_W, S_W, D_W = 160, 136, 168	# letter widths
B_ADV, S_ADV = 156, 152		# pen advances: S tucks under B's shoulder
SPAN = B_ADV + S_ADV + D_W	# ink extent of the word

############################################################ FUNCTIONS

def rr(x0, y0, x1, y1, tl, tr, br, bl):
	return round_rect_pts(x0, y0, x1, y1, (tl, tr, br, bl))


def ring(outer, inner):
	"""Outer contour plus a counter wound the other way (nonzero rule)."""
	return [outer, inner[::-1]]


def letter_b(x):
	"""Stem with two bowls; the upper is narrower and shallower."""
	stem = x + STROKE
	upper_w = 136		# upper bowl is one stroke narrower
	waist = 92		# top of the crossbar
	shoulder = 48		# outer radius of the bowl corners
	crown = 44		# top-left corner of the stem
	foot = 10		# bottom-left corner of the stem
	ease = 4		# slight softening where counters meet the stem
	# Where the upper bowl returns toward the stem: its outer arc
	# ends a touch below the lower shoulder's start so the two cross
	# in a small cleft.  The counter's turn is left rounder than a
	# concentric arc would be, trading a little stroke width for it.
	turn = 28
	counter_turn = 24
	cleft = 4
	# Upper bowl: open at the bottom, closed by the crossbar below.
	upper = ring(
	    rr(x, 0, x + upper_w, waist + cleft, crown, shoulder, turn, 0),
	    rr(stem, STROKE, x + upper_w - STROKE, waist,
	        crown - STROKE, shoulder - STROKE, counter_turn, 0))
	# Lower bowl: a ring whose top leg is the crossbar.
	lower = ring(
	    rr(x, waist, x + B_W, CAP, 0, shoulder, shoulder, foot),
	    rr(stem, waist + STROKE, x + B_W - STROKE, CAP - STROKE,
	        ease, shoulder - STROKE, shoulder - STROKE, 0))
	return upper + lower


def letter_s(x):
	"""Two stadium hooks joined by a 30 degree spine; round terminals."""
	half = STROKE / 2
	upper_w = 120		# upper hook is narrower than the lower
	inset = 8		# lower terminal sits inboard of the upper hook
	hook = 48 - half	# centerline radius of the hook corners
	turn = 40 - half	# tighter corners at the terminals
	sweep = 48		# centerline radius easing into the spine
	slope = PI / 6		# spine angle from horizontal
	overrun = PI / 15	# upper terminal runs past level before its cap
	# Centerline x of each vertical run.
	l_top = x + half
	r_top = x + upper_w - half
	l_bot = x + inset + half
	r_bot = x + S_W - half
	mid = []
	# Upper hook: terminal, top bar, then sweep into the spine.
	mid += arc_pts(r_top - turn, half + turn, turn, -overrun, -PI / 2)
	mid += arc_pts(l_top + hook, half + hook, hook, -PI / 2, -PI)
	mid += arc_pts(l_top + sweep, half + hook, sweep, PI, PI / 2 + slope)
	# Spine: straight at `slope` until it eases into the right leg.
	ax, ay = mid[-1]
	bx = r_bot - sweep + sweep * math.cos(slope - PI / 2)
	by = ay + (bx - ax) * math.tan(slope)
	cy = by + sweep * math.sin(PI / 2 - slope)
	mid += arc_pts(r_bot - sweep, cy, sweep, slope - PI / 2, 0.0)
	# Lower hook: right leg, bottom bar, terminal.
	mid.append((r_bot, CAP - half - hook))
	mid += arc_pts(r_bot - hook, CAP - half - hook, hook, 0.0, PI / 2)
	mid += arc_pts(l_bot + turn, CAP - half - turn, turn, PI / 2, PI)
	return [outline(mid, STROKE, caps="round")]


def letter_d(x):
	"""Stem and one wide bowl."""
	stem = x + STROKE
	shoulder = 88		# outer radius of the bowl corners
	crown = 24		# top-left corner of the stem
	foot = 12		# bottom-left corner of the stem
	ease = 8		# softening where the counter meets the stem
	return ring(
	    rr(x, 0, x + D_W, CAP, crown, shoulder, shoulder, foot),
	    rr(stem, STROKE, x + D_W - STROKE, CAP - STROKE,
	        ease, shoulder - STROKE, shoulder - STROKE, ease))


def draw_bsd(c, margin=24):
	letters = (letter_b(0), letter_s(B_ADV), letter_d(B_ADV + S_ADV))
	s = (c.lw - 2 * margin) / SPAN
	ox = margin
	oy = (c.lh - CAP * s) / 2
	# One fill per letter: a translucent color must land once even
	# where a letter's parts overlap, which nonzero winding unions.
	for rings in letters:
		c.fill_path([[(ox + px * s, oy + py * s) for px, py in r]
		    for r in rings], COLOR, rule="nonzero")


############################################################ MAIN

def main(argv):
	out = argv[1] if len(argv) > 1 else "bsd.png"
	c = Canvas(512, 512, ss=2)
	draw_bsd(c)
	c.write(out)


if __name__ == "__main__":
	main(sys.argv)

################################################################################
# END
################################################################################
