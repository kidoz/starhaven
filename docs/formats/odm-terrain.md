# ODM terrain maps (Might and Magic VI)

Status: **draft, evidence-backed.** This document covers the fixed-size terrain
grids that follow the `.odm` header (see [`odm.md`](odm.md) for the outer format
and header). The vertex/facet/model geometry that follows these grids is
**deferred**. Each claim is tagged `observed`, `inferred`, or `unknown`.

## Scope

This document covers the **height map** and **tile-type map** — the two
128×128 byte grids stored immediately after the fixed map header. These define
the terrain elevation and ground tile selection for an outdoor region.

It does **not** cover:

- the remaining attribute grids (tile flags, ground attributes, etc.) that
  follow the tile-type map — their exact per-grid meaning is not yet confirmed;
- the vertex, facet, and model geometry tables (the 3D mesh of terrain and
  props) — a large, complex section deferred to a later slice;
- decorations, spawns, and event hooks.

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Maps verified | `Outa1.odm`, `Outb1.odm`, `Outc2.odm` |
| Grid size | 128 × 128 = 16,384 bytes per grid `observed` |
| Grid element | 1 byte (u8) `observed` |

## Grid dimensions

MM6 outdoor maps are **128 × 128 tiles**. Each terrain grid is 16,384 bytes,
one byte per tile, laid out row-major (y × 128 + x). `observed`.

## Height map

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| 0xB0 | 16,384 | u8[128×128] | heightmap | observed | terrain elevation per tile |

`observed` across all sampled maps:
- `Outa1.odm`: range 0–127, 128 distinct values, most common heights 5 (5037
  tiles), 24 (2186).
- `Outb1.odm`: range 0–111.
- `Outc2.odm`: range 0–96 (flatter terrain).

Heights are small non-negative integers. The exact vertical scale (units per
height step) is `unknown` in this slice.

## Tile-type map

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| 0x40B0 | 16,384 | u8[128×128] | tilemap | observed | ground tile-type index per tile |

`observed`:
- `Outa1.odm`: range 1–211, most common 90 (8989 tiles), 162 (873).
- `Outb1.odm`: range 1–212, most common 90 (6688), 1 (1992).
- `Outc2.odm`: range 1–212, most common 1 (4641), 2 (2375), 3 (2350) — different
  terrain palette than the grassy maps.

Tile-type indices reference the ground tileset named in the header (e.g.
`"grastyl"`). The mapping from index to a specific tile graphic is `unknown`
here and is resolved against tileset data in a later slice.

## Decoding

1. Obtain the decompressed ODM payload (see `odm.md`).
2. The heightmap occupies bytes `[0xB0, 0x40B0)`.
3. The tilemap occupies bytes `[0x40B0, 0x80B0)`.
4. Index a tile `(x, y)` as `grid[y * 128 + x]`.

## Invalid-input behavior

The terrain extraction rejects, deterministically and without reading out of
bounds:

- a decompressed payload shorter than `0x80B0` bytes (cannot hold both grids);
- this is in addition to the outer-format checks in `odm.md`.

## Unknown / open questions (next slice)

- The exact meaning and order of the remaining attribute grids after 0x80B0.
- The vertex/facet/model geometry section layout (the bulk of the payload).
- The vertical scale of heightmap values and the tile-index-to-graphic mapping.
