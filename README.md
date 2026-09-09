# bosd

On-screen display engine for BSD desktops.

`bosd` flashes transient, click-through feedback over the desktop,
the kind a desktop environment shows for media keys and hotkeys: a
PNG glyph, or giant countdown digits, or large outlined text (e.g.
a checkmark), or small caption text (a screenshot's filename), or
a gauge bar (the classic xosd tick look) at the panel bottom in a
centered ARGB32 overlay on the primary or internal panel. A warm
daemon per channel keeps everything on screen and repaints in
place, so rapid toggles never flash; the gauge and the artwork run
independent timers and coexist. When a daemon is warm, a client
invocation hands off the request and returns immediately; without
one, the same command draws the OSD itself.

Home: [FrauBSD/bosd](https://github.com/FrauBSD/bosd)

## Requirements

- X11 (Xrandr, Xrender, Xext shape), Xft + fontconfig, libpng
- A compositor (e.g. picom) for translucency; opaque without one
- Python 3 (stdlib only) to author glyphs with `tools/glyph.py`

## Build / install

```sh
make
make install    # PREFIX=/usr/local by default
make clean
```

## Usage

```sh
bosd -n airplane -d &            # warm the channel at session start
bosd -n airplane airplane-on 1.5 # show a glyph for 1.5 s
bosd -n audio -b 2 audio-headphones 1.5 # superscript badge upper-right
bosd -n audio -y -20 audio-speakers     # shift up 20 px (positive = down)
bosd -n audio -x -300 audio-speakers    # shift left 300 px (positive = right)
bosd -n audio -s 2.0 audio-speakers     # twice the panel-derived size
bosd -n audio -o audio-speakers         # no outline halo
bosd -c 3                               # full-screen 3-2-1 countdown
bosd -n shot -T '\u2713' 1              # big checkmark (escapes decode)
bosd -n shot -t 'Screenshot\x0ashot.png' 2 # small green caption lines
bosd -n shot -F orange -t saved         # small text, another color
bosd -n audio audio-speakers -1         # hold until -C or replaced
bosd -n shot -C                         # clear the channel's active render
bosd -D audio-speakers 1                # render directly, skip the daemon
bosd -p 'Shutdown in' -b s -c 10        # caption over a 10 s countdown
bosd -n airplane -a 'airplane mode off' airplane-off  # caption below
bosd -n volume -g 45                    # gauge bar, 45%, default green
bosd -n volume -g 115 -G '#CC2222'      # red bar, "115%" past its edge
bosd -g 45 -G red -B 3 audio-headphones # glyph + bar, each its own hold
bosd /path/to/glyph.png                 # one-shot, absolute path
```

Bare names resolve through `BOSD_PATH`, then the compiled share
directory (`share/bosd`). Consumers ship their own glyphs; `bosd`
ships none.

## Why another OSD?

- **xosd** draws text through shaped windows (the green TV/VCR look).
  `bosd` composites true-color PNG art with real alpha via XRender.
- **nbosd** shows battery and CPU frequency; fixed purpose. `bosd`
  shows whatever glyph you send it; policy lives in the caller.
- **xob** is only a bar; **dunst**/notify-osd are D-Bus
  notification queues. `bosd` has a gauge bar (the classic xosd
  tick look) beside its glyphs, with no bus, no queue, no daemon
  config -- one datagram, one OSD.
- Libraries (libxosd, libaosd) want a C caller. `bosd` is a shell
  one-liner, warm-daemon fast: repeated toggles repaint in place,
  no flash, no respawn.
- RandR-aware: centered on the primary or internal panel, correct
  on rotated and multi-head layouts.

## Authoring glyphs

`tools/glyph.py` (stdlib-only Python) is the shared rasterizer for
icon-build scripts: an RGBA canvas with optional supersampling, an
antialiased disc brush, stroke and fill primitives, and a PNG
encoder. Art scripts import it and keep only their geometry.

```python
from glyph import Canvas
c = Canvas(512, 512, ss=2)
c.circle(256, 256, 200, 16)
c.line(120, 120, 392, 392, 18)
c.write("share/bosd/my-glyph.png")
```

`python3 tools/glyph.py out.png` draws a primitive sampler.

## Consumers

Written for [bvwm](https://github.com/FrauBSD/bvwm) and
[framework-keyboard](https://github.com/FrauBSD/framework-keyboard),
which install per-feature glyphs and warm their channels from the
session. Any window manager or script can drive it the same way.

X11-only today; the client protocol is display-agnostic by design.
