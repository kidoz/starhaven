# The game screen's panels (`icons.lod`, Might and Magic VI)

Status: **in use.** The pieces the game's main screen is built from, as they
ship in `icons.lod`, and how their own sizes pin the layout. Each claim is
tagged `observed`, `inferred`, or `unknown`.

## The pieces

| Entry | Size | What it is | Status |
| --- | ---: | --- | --- |
| `BORDER3` | 468x8 | strip across the top of the view | observed |
| `BORDER4` | 8x344 | strip down the view's left | observed |
| `Border1.pcx` | 172x339 | the right column: windows, book buttons, food and gold, four medallions | observed |
| `Border2.pcx` | 469x109 | the portrait bar: four oval seats | observed |
| `FOOTER` | 483x24 | the message strip along the bottom | observed |
| `HITSFULL` | 6x78 | the hit-point gauge, green | observed |
| `MANAFULL` | 6x78 | the mana gauge, blue | observed |

## Two of them are PCX in the same container

`Border1.pcx` and `Border2.pcx` wear the standard 48-byte image container
(see [`bitmap.md`](bitmap.md)) with zeroed width and height; the payload
inflates to a PCX file — magic `0x0A`, version 5, RLE rows, three planes of
8 bits, no palette. `observed` The other `.pcx`-named entries this install
carries are single-plane with the 768-byte palette at the PCX tail.
StarHaven reads both shapes in `src/core/image/pcx.cpp`.

## The sizes pin the layout

On the 640x480 screen the pieces abut exactly one way: `BORDER3` at (0,0),
`BORDER4` at (0,8), `Border1.pcx` at (468,0) — 640 − 172 — `Border2.pcx` at
(0,352) and `FOOTER` at (0,456). That leaves the 3D viewport hole at
**(8,8), 460x344**, which `QUIKREF`'s 460x346 full-viewport art corroborates.
`inferred` from the fit; the sizes are `observed`. No shipped piece claims
the corner right of the portrait bar — (468,339) to (640,480) past the
footer's 483 — and StarHaven fills it dark and keeps its own readouts there.

## The portrait bar measures its own seats

`Border2.pcx`'s four dark ovals, measured from the decoded pixels, are 59
wide at the widest — exactly the portraits' width — spaced **113 pixels
apart with the first at x=22**, spanning rows 6..90 of the bar. A narrow
gauge groove sits either side of each seat, at the oval's left edge −5 and
+68. `observed` StarHaven stands `HITSFULL` in the left groove and
`MANAFULL` in the right, drawn from the bottom up by the fraction they
report; which groove the game meant for which gauge is not stated in the
art. `inferred`
