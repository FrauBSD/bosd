"""Vector-glyph rasterizer for bosd icon builders.

One canonical home for the drawing plumbing every `*-icons-build`
script previously carried its own copy of: an RGBA canvas with
optional supersampling, an antialiased disc brush and the strokes
built from it, convex polygon fill, a path fill (even-odd or nonzero)
for outlines with counters, and the PNG encoder.  Art scripts import
this module and keep only their geometry.

Coordinates are logical canvas units regardless of supersampling.
Stdlib only.
"""

from __future__ import annotations

import math
import struct
import zlib
from pathlib import Path

WHITE = (255, 255, 255, 255)


# -- path geometry (feeds Canvas.fill_path) --------------------------------

def arc_pts(cx, cy, r, a0, a1, steps=None):
	"""Points along a circular arc from angle a0 to a1 (radians)."""
	if steps is None:
		steps = max(2, int(abs(a1 - a0) * max(r, 1.0) / 2.0))
	return [(cx + r * math.cos(a0 + (a1 - a0) * i / steps),
	    cy + r * math.sin(a0 + (a1 - a0) * i / steps))
	    for i in range(steps + 1)]


def round_rect_pts(x0, y0, x1, y1, radii):
	"""Closed outline of a rectangle; radii = (tl, tr, br, bl)."""
	tl, tr, br, bl = radii
	pi = math.pi
	pts = []
	pts += arc_pts(x0 + tl, y0 + tl, tl, pi, 1.5 * pi) if tl > 0 \
	    else [(x0, y0)]
	pts += arc_pts(x1 - tr, y0 + tr, tr, 1.5 * pi, 2.0 * pi) if tr > 0 \
	    else [(x1, y0)]
	pts += arc_pts(x1 - br, y1 - br, br, 0.0, 0.5 * pi) if br > 0 \
	    else [(x1, y1)]
	pts += arc_pts(x0 + bl, y1 - bl, bl, 0.5 * pi, pi) if bl > 0 \
	    else [(x0, y1)]
	return pts


def outline(path, width, caps="butt"):
	"""Closed outline of an open centerline stroked `width` thick.

	caps is "butt" (cut square across the path) or "round".  Corners
	of the centerline must be gentle relative to width/2 or the
	offsets fold over.
	"""
	half = width / 2.0
	n = len(path)
	left = []
	right = []
	for i in range(n):
		ax, ay = path[max(0, i - 1)]
		bx, by = path[min(n - 1, i + 1)]
		dx, dy = bx - ax, by - ay
		d = math.hypot(dx, dy) or 1.0
		nx, ny = -dy / d * half, dx / d * half
		px, py = path[i]
		left.append((px + nx, py + ny))
		right.append((px - nx, py - ny))
	if caps != "round":
		return left + right[::-1]

	def cap(tip, back):
		# Semicircle around tip, bulging away from back.
		a = math.atan2(tip[1] - back[1], tip[0] - back[0])
		return arc_pts(tip[0], tip[1], half, a + math.pi / 2,
		    a - math.pi / 2)

	return (left + cap(path[-1], path[-2]) + right[::-1] +
	    cap(path[0], path[1]))


class Canvas:
	"""RGBA canvas; ss > 1 renders at ss x and box-filters down."""

	def __init__(self, w=512, h=512, ss=1):
		self.lw = w
		self.lh = h
		self.ss = ss
		self.w = w * ss
		self.h = h * ss
		self.rgba = bytearray(self.w * self.h * 4)

	# -- pixel plumbing ------------------------------------------------

	def _blend(self, i, cr, cg, cb, a):
		rgba = self.rgba
		if a <= 0:
			return
		oa = rgba[i + 3]
		na = min(255, a + oa * (255 - a) // 255)
		if na == 0:
			return
		for c, src in enumerate((cr, cg, cb)):
			val = (src * a + rgba[i + c] * oa * (255 - a) //
			    255) // na
			rgba[i + c] = max(0, min(255, val))
		rgba[i + 3] = na

	# -- brush primitives ----------------------------------------------

	def stamp(self, x, y, r, color=WHITE):
		"""Filled antialiased disc: the brush everything strokes with."""
		ss = self.ss
		x, y, r = x * ss, y * ss, r * ss
		cr, cg, cb, ca = color
		x0 = int(math.floor(x - r - 1))
		x1 = int(math.ceil(x + r + 1))
		y0 = int(math.floor(y - r - 1))
		y1 = int(math.ceil(y + r + 1))
		for ny in range(max(0, y0), min(self.h, y1 + 1)):
			for nx in range(max(0, x0), min(self.w, x1 + 1)):
				dist = math.hypot(nx - x, ny - y)
				if dist > r:
					continue
				cov = 1.0 if dist <= r - 1.0 else \
				    max(0.0, r - dist)
				self._blend((ny * self.w + nx) * 4,
				    cr, cg, cb, int(ca * cov))

	disc = stamp

	def punch(self, x, y, r):
		"""Erase an antialiased disc (cut a hole)."""
		ss = self.ss
		x, y, r = x * ss, y * ss, r * ss
		rgba = self.rgba
		x0 = int(math.floor(x - r - 1))
		x1 = int(math.ceil(x + r + 1))
		y0 = int(math.floor(y - r - 1))
		y1 = int(math.ceil(y + r + 1))
		for ny in range(max(0, y0), min(self.h, y1 + 1)):
			for nx in range(max(0, x0), min(self.w, x1 + 1)):
				dist = math.hypot(nx - x, ny - y)
				if dist > r:
					continue
				cov = 1.0 if dist <= r - 1.0 else \
				    max(0.0, r - dist)
				i = (ny * self.w + nx) * 4 + 3
				rgba[i] = int(rgba[i] * (1.0 - cov))

	def line(self, x0, y0, x1, y1, r, color=WHITE):
		steps = max(1, int(math.hypot(x1 - x0, y1 - y0) *
		    self.ss * 2))
		for i in range(steps + 1):
			t = i / steps
			self.stamp(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t,
			    r, color)

	def polyline(self, pts, r, color=WHITE, closed=False):
		n = len(pts)
		last = n if closed else n - 1
		for i in range(last):
			ax, ay = pts[i]
			bx, by = pts[(i + 1) % n]
			self.line(ax, ay, bx, by, r, color)

	def arc(self, cx, cy, rx, a0, a1, r, color=WHITE, steps=160,
	    ry=None):
		"""Stroke an arc (radians); elliptical when ry is given."""
		if ry is None:
			ry = rx
		for i in range(steps + 1):
			a = a0 + (a1 - a0) * i / steps
			self.stamp(cx + rx * math.cos(a),
			    cy + ry * math.sin(a), r, color)

	def circle(self, cx, cy, rad, r, color=WHITE):
		self.arc(cx, cy, rad, 0.0, 2.0 * math.pi, r, color)

	def rect_stroke(self, x, y, w, h, r, color=WHITE):
		self.polyline(((x, y), (x + w, y), (x + w, y + h),
		    (x, y + h)), r, color, closed=True)

	def roundrect(self, x, y, w, h, rad, r, color=WHITE):
		self.line(x + rad, y, x + w - rad, y, r, color)
		self.line(x + rad, y + h, x + w - rad, y + h, r, color)
		self.line(x, y + rad, x, y + h - rad, r, color)
		self.line(x + w, y + rad, x + w, y + h - rad, r, color)
		pi = math.pi
		self.arc(x + rad, y + rad, rad, pi, 1.5 * pi, r, color)
		self.arc(x + w - rad, y + rad, rad, 1.5 * pi, 2.0 * pi,
		    r, color)
		self.arc(x + w - rad, y + h - rad, rad, 0.0, 0.5 * pi,
		    r, color)
		self.arc(x + rad, y + h - rad, rad, 0.5 * pi, pi, r, color)

	# -- fills ---------------------------------------------------------

	def fill_poly(self, pts, color=WHITE):
		"""Fill a CONVEX polygon (2x2 coverage samples per pixel)."""
		ss = self.ss
		pts = [(px * ss, py * ss) for px, py in pts]
		cr, cg, cb, ca = color
		xs = [p[0] for p in pts]
		ys = [p[1] for p in pts]
		minx = max(0, int(math.floor(min(xs)) - 1))
		maxx = min(self.w - 1, int(math.ceil(max(xs)) + 1))
		miny = max(0, int(math.floor(min(ys)) - 1))
		maxy = min(self.h - 1, int(math.ceil(max(ys)) + 1))
		n = len(pts)
		offs = ((0.25, 0.25), (0.75, 0.25), (0.25, 0.75),
		    (0.75, 0.75))
		for y in range(miny, maxy + 1):
			for x in range(minx, maxx + 1):
				hits = 0
				for ox, oy in offs:
					px, py = x + ox, y + oy
					inside = True
					sign = None
					for i in range(n):
						ax, ay = pts[i]
						bx, by = pts[(i + 1) % n]
						e = (px - ax) * (by - ay) - \
						    (py - ay) * (bx - ax)
						if sign is None:
							sign = e >= 0
						elif (e >= 0) != sign:
							inside = False
							break
					if inside:
						hits += 1
				if hits:
					self._blend((y * self.w + x) * 4,
					    cr, cg, cb, ca * hits // 4)

	def fill_round_rect(self, x0, y0, x1, y1, rad, color=WHITE):
		self.fill_poly(((x0 + rad, y0), (x1 - rad, y0),
		    (x1 - rad, y1), (x0 + rad, y1)), color)
		self.fill_poly(((x0, y0 + rad), (x1, y0 + rad),
		    (x1, y1 - rad), (x0, y1 - rad)), color)
		for cx, cy in ((x0 + rad, y0 + rad), (x1 - rad, y0 + rad),
		    (x1 - rad, y1 - rad), (x0 + rad, y1 - rad)):
			self.stamp(cx, cy, rad, color)

	def fill_path(self, rings, color=WHITE, sub=4, rule="evenodd"):
		"""Fill closed polylines as one shape; holes are more rings.

		rule "evenodd" cuts a hole wherever rings overlap; "nonzero"
		unions rings of one orientation and cuts holes with rings
		of the opposite orientation.  Coverage is `sub` sub-scanlines
		per pixel row with exact horizontal span coverage, so edges
		antialias and a translucent color is laid down exactly once.
		"""
		ss = self.ss
		cr, cg, cb, ca = color
		nonzero = rule == "nonzero"
		edges = []
		for ring in rings:
			pts = [(px * ss, py * ss) for px, py in ring]
			n = len(pts)
			for i in range(n):
				x0, y0 = pts[i]
				x1, y1 = pts[(i + 1) % n]
				if y0 == y1:
					continue
				wind = 1
				if y0 > y1:
					x0, y0, x1, y1 = x1, y1, x0, y0
					wind = -1
				edges.append((y0, y1, x0, (x1 - x0) / (y1 - y0),
				    wind))
		if not edges:
			return
		edges.sort()
		miny = max(0, int(math.floor(edges[0][0])))
		maxy = min(self.h - 1,
		    int(math.ceil(max(e[1] for e in edges))))
		wgt = 1.0 / sub
		w = self.w
		for py in range(miny, maxy + 1):
			cov = [0.0] * (w + 1)
			touched = False
			for k in range(sub):
				yy = py + (k + 0.5) / sub
				xs = [(x0 + (yy - y0) * m, wd)
				    for y0, y1, x0, m, wd in edges if y0 <= yy < y1]
				if len(xs) < 2:
					continue
				xs.sort()
				spans = []
				depth = 0
				for i in range(len(xs) - 1):
					if nonzero:
						depth += xs[i][1]
						inside = depth != 0
					else:
						depth += 1
						inside = depth % 2 == 1
					if inside:
						spans.append((xs[i][0], xs[i + 1][0]))
				for xa, xb in spans:
					xa = max(0.0, xa)
					xb = min(float(w), xb)
					if xb <= xa:
						continue
					touched = True
					ia = int(xa)
					ib = int(xb)
					if ia == ib:
						cov[ia] += (xb - xa) * wgt
						continue
					cov[ia] += (ia + 1 - xa) * wgt
					for j in range(ia + 1, ib):
						cov[j] += wgt
					if ib < w:
						cov[ib] += (xb - ib) * wgt
			if not touched:
				continue
			row = py * w
			for px in range(w):
				c = cov[px]
				if c > 0.0:
					self._blend((row + px) * 4, cr, cg, cb,
					    int(ca * min(1.0, c) + 0.5))

	# -- output --------------------------------------------------------

	def _downscaled(self):
		if self.ss == 1:
			return (self.rgba, self.w, self.h)
		f = self.ss
		src, sw = self.rgba, self.w
		dw, dh = self.lw, self.lh
		out = bytearray(dw * dh * 4)
		n = f * f
		for y in range(dh):
			for x in range(dw):
				acc = [0, 0, 0, 0]
				for oy in range(f):
					row = ((y * f + oy) * sw + x * f) * 4
					for ox in range(f):
						i = row + ox * 4
						for c in range(4):
							acc[c] += src[i + c]
				j = (y * dw + x) * 4
				for c in range(4):
					out[j + c] = acc[c] // n
		return (out, dw, dh)

	def write(self, path):
		rgba, w, h = self._downscaled()
		rows = []
		for y in range(h):
			row = bytearray([0])
			row.extend(rgba[y * w * 4:(y + 1) * w * 4])
			rows.append(bytes(row))

		def chunk(tag, data):
			crc = zlib.crc32(tag + data) & 0xFFFFFFFF
			return (struct.pack(">I", len(data)) + tag + data +
			    struct.pack(">I", crc))

		ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
		png = b"\x89PNG\r\n\x1a\n"
		png += chunk(b"IHDR", ihdr)
		png += chunk(b"IDAT", zlib.compress(b"".join(rows), 9))
		png += chunk(b"IEND", b"")
		path = Path(path)
		path.parent.mkdir(parents=True, exist_ok=True)
		path.write_bytes(png)


def emit(canvas, name, dirs):
	"""Write canvas as name into each dir; skip unwritable ones."""
	written = []
	for d in dirs:
		try:
			path = Path(d) / name
			canvas.write(path)
			written.append(path)
		except OSError:
			pass
	return written


def _demo(path):
	"""Exercise the primitives: circle, slash, poly, roundrect, punch."""
	c = Canvas(512, 512, ss=2)
	c.circle(256, 256, 200, 16)
	c.fill_poly(((256, 120), (352, 320), (160, 320)))
	c.punch(256, 280, 40)
	c.roundrect(96, 400, 320, 72, 24, 10)
	c.line(120, 120, 392, 392, 18)
	c.write(path)


if __name__ == "__main__":
	import sys
	_demo(sys.argv[1] if len(sys.argv) > 1 else "/tmp/glyph-demo.png")
