"""Vector-glyph rasterizer for bosd icon builders.

One canonical home for the drawing plumbing every `*-icons-build`
script previously carried its own copy of: an RGBA canvas with
optional supersampling, an antialiased disc brush and the strokes
built from it, convex polygon fill, and the PNG encoder.  Art scripts
import this module and keep only their geometry.

Coordinates are logical canvas units regardless of supersampling.
Stdlib only.
"""

from __future__ import annotations

import math
import struct
import zlib
from pathlib import Path

WHITE = (255, 255, 255, 255)


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
