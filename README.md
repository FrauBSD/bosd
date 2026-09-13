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
  (daemon / `bosd` binary only)
- A compositor (e.g. picom) for translucency; opaque without one
- Python 3 (stdlib only) to author glyphs with `tools/glyph.py`
- C clients need only `libbosd` (no X11 link)

## Build / install

```sh
make
make install    # PREFIX=/usr/local by default
make clean
```

Installs `bosd`, `libbosd.so.4`, `bosd.h`, `bosd.pc`, and the man
pages. C clients:

```sh
cc $(pkg-config --cflags --libs bosd) -o hotkey hotkey.c
```

```c
#include <bosd.h>

struct bosd_req req;
bosd_req_init(&req);
strlcpy(req.spec, "airplane-on", sizeof(req.spec));
req.hold = 1.5;
if (bosd_alive("airplane"))
	bosd_show("airplane", &req);
```

Warm the channel first (`bosd -n airplane -d &`); `bosd_show` does
not paint locally when no daemon is running.

## Usage

```sh
bosd -n airplane -d &            # warm the channel at session start
bosd -n airplane airplane-on 1.5 # show a glyph for 1.5 s
bosd -n audio -b 2 audio-headphones 1.5 # superscript badge upper-right
bosd -n audio -y -20 audio-speakers     # shift up 20 px (positive = down)
bosd -n audio -x -300 audio-speakers    # shift left 300 px (positive = right)
bosd -n audio -s 2.0 audio-speakers     # twice the panel-derived size
bosd -n audio -A 0.8 audio-speakers     # multiply PNG alpha by 0.8 (outline stays opaque)
bosd -n audio -O 0.5 audio-speakers     # half-opaque outline halo
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
bosd -n volume -g 90 -P 95              # 90% now; dim shorts above 95%
bosd -n volume -g 115 -G '#CC2222'      # red bar, "115%" past its edge
bosd -g 45 -G red -B 3 audio-headphones # glyph + bar, each its own hold
bosd /path/to/glyph.png                 # one-shot, absolute path
```

With `-P`, a warm daemon latches the first previous percent when the
bar appears and keeps that watermark until the bar hides, so volume
or brightness key-chords show where the session started. Short ticks
at or above the watermark are a 50% dimmer shade of `-G`; the return
zone between current and previous stays full color. Tall ticks are
never dimmed.

Bare names resolve through `BOSD_PATH`, then the compiled share
directory (`share/bosd`). Consumers ship their own glyphs.

## Why bosd

Picture the evening you thought the bindings were done.

Mute maps to a glyph. Volume to a bar. Brightness to a string you
chose from `xlsfonts` on a quiet Tuesday, parked with a literal
`Y=40` because that is what the man page era taught you: absolute
pixels, absolute faith. On *this* head, under *this* `xrandr` mode,
with *this* root pixmap, it looks like a desktop. You `chmod +x`
the hook scripts and close the lid a little proud.

Wednesday someone docks a second output and the primary is no longer
the rectangle you dressed. Thursday the panel is taller after a
firmware bump. Friday (the one that stays in muscle memory) you are
inside a game that has taken the framebuffer somewhere the session
never rehearsed. You tap volume without leaving the match. What
rises is not the polite tick band you tuned under a status bar. It
is last night's geometry wearing today's pixels: a billboard of a
whisper, wrong seat, wrong century of the display. The kernel did
nothing wrong. X did nothing surprising. Your OSD still believed in
last night's `DisplayWidth`.

That is the older bargain, spoken with respect. xosd and its cousins
were honest Unix tools in the old sense: a shaped window, a string,
sometimes a bar, and the rest of `~/.xbindkeysrc`. Font size in
points or pixels that do not travel. Layout that does not survive
`xrandr --output ... --mode`. Every WM reinvented the same small
theatre in shell. Every hotplug sent you back to wardrobe. `fork`,
`exec`, flash; rapid media chords strobed like a broken bell.

The typeface story was the same debt in a smaller box. Too many OSD
stacks favored core X bitmap fonts (crisp on one DPI, a museum piece
on the next), and treated a real scalable face as either an exercise
left to the reader or a pipedream for "someday." In 2026 that is not
a nostalgic constraint; it is a refusal. HiDPI panels, mixed heads,
and in-game mode sets do not forgive a 10x20 glyph that looked fine
on a CRT. If the engine cannot ask fontconfig for a TrueType (or kin)
at a size derived from the panel, the operator is back in the
`xlsfonts` quarry with a chisel.

And when the toolkit finally admitted a PNG, it still asked you to
smuggle the black outline into the file (a second career for every
icon under `$PREFIX/share`, or a permanent treaty with whichever
root wallpaper you feared most). Alpha, if you got it, lived in the
asset pipeline, not as a dial on the evening you needed the fill
quieter and the halo softer without rebuilding the tree.

Meanwhile a real machine does not serialize your feelings.
XF86AudioRaiseVolume, a brightness Fn row, a Super chord, a script
from the greeter path: separate sentences that ought to arrive as
one composed reply on the glass. Spawn a new OSD process for each
and the desktop strobes. Stack them by accident and the gauge writes
through the mute glyph. Give them one timer and the bar dies when
the icon dies. Performance, here, is not a synthetic benchmark. It
is whether feedback feels like the session speaking, or like several
utilities arguing over the same overlay.

bosd exists to end that nightmare.

One background daemon per channel; a small datagram API; clients
that hand off and return (`bosd(1)` or `libbosd`). A vocabulary
instead of a kit: glyph, countdown, large confirmation, small
caption, gauge bar as a peer of the art. Discrete rendering zones
that do not borrow each other's air (center piece, bottom tick band,
caption stack clear of the gauge), each with its own hold, so volume
can linger on the bar while mute expires on its own clock.
Replace-in-place under rapid chords. No flash. No respawn lottery.
Click-through. RandR-honest about the primary or internal panel.
Greeter, game, ordinary session; WM-agnostic on purpose.

Geometry belongs to the panel height, not to a magic constant in a
shell script. Change the mode and the OSD keeps its seat and its
bearing; fewer pixels when the canvas is smaller, not a different
costume on the wrong head. Text is Xft through fontconfig: scalable
faces at panel-derived sizes (and `-f` when you want another family),
not a treasure hunt through bitmap XLFD names. Ship a clean PNG: by
default bosd grows a legibility halo from the coverage at paint time.
Prefer none? `-o`. Prefer a softer edge? `-O`. Outline is
presentation, not cargo in the tree. Alpha is first-class on every
layer: XRender translucency, respect for alphas already in the art,
overrides (`-A`, `-O`) without a rebuild. Gauge ticks, captions,
countdown ink, badges: the same contract.

You still decide what mute means. You still ship your own glyphs.
Policy stays in the caller (where Unix always said it should). What
leaves your desk is the second job: the pixel debt, the outline
gallery, the bitmap-font quarry, the process-per-keypress tax, the
apology after the last mode set.

That is the catalog item. Not another way to print green text on a
shaped window. A way to feel, once, the Friday-in-game volume
billboard under a foreign `xrandr` mode (and then never have to live
in that `$DISPLAY` again).

### Compared with the usual suspects

- **xosd / libxosd / libaosd** draw text through shaped windows (the
  green TV/VCR look); absolute layout is your problem. `bosd`
  composites true-color PNG art with real alpha via XRender and
  sizes from the panel.
- Bitmap-era OSD text (core X fonts, XLFD archaeology) leaves
  high-resolution type as homework. `bosd` uses Xft and fontconfig
  for scalable faces at panel-derived sizes; `-f` selects the family.
- Spawn-per-event OSDs strobe under rapid chords and leave stacking
  to chance. `bosd` runs one background daemon per channel with an
  API: discrete zones, independent holds, replace-in-place, no
  flash, no respawn.
- PNG-capable OSDs that still leave outlining to the asset pipeline
  force a baked halo (or none). `bosd` outlines from coverage by
  default, honors baked alpha in the file, and lets `-A` / `-O` /
  `-o` retune fill and halo without touching the PNG.
- **nbosd** shows battery and CPU frequency; fixed purpose. `bosd`
  shows whatever glyph you send it; policy lives in the caller.
- **xob** is only a bar. `bosd`'s gauge is one face of a larger
  vocabulary, and it coexists with artwork on independent holds.
- **dunst** / notify-osd are D-Bus notification queues. `bosd` is
  not a queue: one datagram, replace-in-place, no history chrome.
- Homegrown WM scripts reinvent volume and brightness OSD in every
  environment and inherit the pixel debt. `bosd` is the shared
  engine so the laptop, tablet, convertible, and desktop can speak
  the same dialect under any window manager.
- Libraries (libxosd, libaosd) want a C caller. `bosd` is a shell
  one-liner or a `libbosd` client against the same API.
  RandR-aware: centered on the primary or internal panel, correct
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
`examples/bsd.py` is a complete art script: the letters BSD in
FreeBSD red (#cb1008) at 80% opacity on a transparent ground.

```sh
python3 examples/bsd.py bsd.png
bosd bsd.png
```

or

```sh
make example
```

## Consumers

Written for [bvwm](https://github.com/FrauBSD/bvwm) and
[framework-keyboard](https://github.com/FrauBSD/framework-keyboard),
which install per-feature glyphs and warm their channels from the
session. Any window manager or script can drive it the same way.

X11-only today; the client protocol is display-agnostic by design.
