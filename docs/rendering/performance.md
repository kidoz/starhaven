---
title: "Renderer performance"
summary: "Reproducible performance measurements and bottleneck analysis for the StarHaven software renderer."
doc_type: explanation
status: historical
last_updated: 2026-08-01
tags:
  - rendering
  - performance
  - benchmark
  - software-rasterizer
---
# Renderer performance (StarHaven)

This historical benchmark snapshot records what the software rasterizer cost
on every map in an optimised build. The commands remain reproducible, but the
numbers are not a release performance guarantee.

## How to reproduce

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
just bench CD1.Blv          # one map
just bench-all              # every map, slowest ten
```

`--bench N` renders N frames from a still camera and reports the median and
95th percentile for each stage. It **opens no window**: what is measured is the
rasterizer, not the compositor, which also means it runs anywhere.

## Build type matters more than anything here

The default `buildDir` is a **debug** build — `-O0 -g`. Numbers taken there say
nothing about the renderer:

| Goblinwatch (`D01.blv`) | debug | release |
| --- | ---: | ---: |
| load | 365 ms | 17 ms |
| geometry | 51.3 ms | 7.1 ms |
| billboards | 1.50 ms | 0.27 ms |
| median fps | **19** | **136** |

Outdoor maps populate themselves from their spawn points now (see
[`odm-tile-index.md`](../formats/odm-tile-index.md)), so every one of them
carries 60 to 400 monsters where it used to carry none. That is what moved the
billboard time; the release build still runs every outdoor map above 100 fps.

A factor of seven. `just bench` always builds optimised for this reason.

## All 67 maps

Measured at 640×480, 40 frames each, `--buildtype=release`, on an Apple
silicon laptop.

| | median fps |
| --- | ---: |
| slowest map (`CD1.Blv`, Castle Alamos, 5,315 faces) | 82 |
| second (`OutD1.Odm`, Silver Cove) | 83 |
| mean across all 67 | 335 |
| fastest (`zdtl01.blv`, a 30-face test level) | 3,432 |

Map load is 12 to 33 ms — including inflating the map, building the terrain
mesh, decoding ground textures and reading five global tables.

## What this settles

**The renderer does not need visibility culling.** The worst map in the game
runs at 82 fps with every face drawn every frame and no BSP, no portals and no
frustum rejection beyond the near plane. Adding any of those now would be
optimising a path that is already six times faster than it needs to be.

That is worth stating plainly because the indoor format *does* contain BSP data
(undecoded, see [`blv.md`](../formats/blv.md)), and the temptation is to treat
decoding it as a performance necessity. It is not one.

## Where the time goes

Geometry dominates on indoor maps, billboards on busy outdoor ones:

| Map | geometry | billboards |
| --- | ---: | ---: |
| `CD1.Blv` (5,315 faces) | 11.1 ms | 1.3 ms |
| `D01.blv` (2,291 faces) | 7.1 ms | 0.27 ms |
| `OutE3.Odm` (New Sorpigal, 478 decorations, 260 monsters) | 5.1 ms | 4.3 ms |
| `Outa1.odm` (727 decorations, 62 monsters) | 5.2 ms | 1.1 ms |

New Sorpigal is the one map where billboards cost as much as the world, which
is what a town full of animated townsfolk and torches should look like.

## Caveats

- One machine, one resolution. These are ratios worth trusting and absolute
  numbers worth re-measuring elsewhere.
- The camera does not move, so nothing here measures collision or the movement
  step.
- Presenting the frame through SDL is excluded. In the interactive walk that
  adds the cost of one 640×480 texture upload per frame.
