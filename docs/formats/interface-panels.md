---
title: "Game-screen panels in icons.lod"
summary: "Asset names, dimensions, and layout relationships for the Might and Magic VI game-screen frame."
doc_type: reference
status: verified
last_updated: 2026-08-01
tags:
  - mm6
  - interface
  - icons-lod
  - layout
---
# The game screen's panels (`icons.lod`, Might and Magic VI)

Status: **in use.** The pieces the game's main screen is built from, as they
ship in `icons.lod`, and how their own sizes pin the layout. Each claim is
tagged `observed`, `inferred`, or `unknown`.

## Scope

This page covers the main game-screen border, portrait bar, message strip,
resource gauges, and the asset dimensions that constrain their placement. It
does not define interactive screen behavior or every book and dialog screen.

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

## `0x52d0a8`: the loading screen

The routine at `0x434e50` — a member-by-member reset of six forty-byte records
based at `0x20`, `0x48`, `0x70`, `0x98`, `0xc0` and `0xe8` — is the one that
disproved the actor buff grid, and it was left unidentified. It is the
constructor of a **static object at `0x52d0a8`**: `0x434e30` sets `ecx` to it,
calls the reset, and hands `0x435060` to `atexit`.

What the object is, out of its own loader at `0x438c30`:

| offset | what |
| --- | --- |
| `+0x00`..`+0x07` | four words set to **122, 151, 449, 56** — the progress bar's rectangle |
| `+0x0a` | a byte counted up once per use |
| `+0x0c` | which of its two modes was asked for |
| `+0x10` | `loading.pcx` |
| `+0x38` | `womover.pcx` |
| `+0x60` | `demover.pcx` |
| `+0x88` | `womover2.pcx` |
| `+0xb0` | `demover2.pcx` |
| `+0xd8` | `"fireball"`, loaded as a sprite rather than a PCX |
| `+0x120` | `"bardata"`, in the other mode |
| `+0x34` | non-zero once loaded; the loader returns immediately if it is set |

So the forty-byte record is a **loaded-image slot** — `0x409e50` fills one from
a named PCX — and the six of them are the loading screen's pictures. `observed`

That is the gamble's stated risk landing exactly: a rendering structure with
no bearing on the game. What it buys is that the routine which broke the buff
grid is no longer an unknown standing behind that withdrawal, and that the
forty-byte image slot is a shape worth recognising elsewhere.
