---
title: "ODM outdoor map format"
summary: "Container envelope and fixed header for Might and Magic VI outdoor map files."
doc_type: reference
status: partial
last_updated: 2026-08-01
tags:
  - mm6
  - odm
  - outdoor-map
  - binary-format
---
# ODM outdoor map format (Might and Magic VI)

Status: **draft, evidence-backed — outer format only.** This page documents
the file envelope (zlib wrapper + the fixed map header) and parses the header
metadata. Terrain grids, model geometry, and decorations are documented on
their canonical linked pages. Field layout is verified against a user-supplied
legal GOG.com installation, with each claim tagged `observed`, `inferred`, or
`unknown`.

## Scope

This document covers the **`.odm` outdoor map** file as stored inside
`Games.lod` (entries like `Outa1.odm`). It documents:

- the 8-byte zlib wrapper that surrounds the map data;
- the fixed header at the start of the decompressed payload (name, name2,
  version string, tileset name, and the dimension/region fields at 0xA0).

It does **not** specify:

- the tile and height maps; see [`odm-terrain.md`](odm-terrain.md);
- model records and geometry; see [`odm-models.md`](odm-models.md) and
  [`odm-model-facets.md`](odm-model-facets.md);
- decorations, the per-tile index, or spawn points; see
  [`odm-decorations.md`](odm-decorations.md) and
  [`odm-tile-index.md`](odm-tile-index.md).

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Container | `data/Games.lod`, entries `Outa1.odm` (161,415 B stored), `Outa2.odm` |
| Stored size | `Outa1.odm` 161,415 B; `Outa2.odm` 117,800 B |
| Decompressed size | `Outa1.odm` 585,346 B; `Outa2.odm` 576,640 B |
| Version string | `"MM6 Outdoor v1.11"` `observed` |

## Byte order

All integers little-endian.

## Outer structure

A `.odm` entry stored in `Games.lod` is **zlib-compressed** with an 8-byte
prefix:

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| 0x00 | 4 | u32 | streamSize | observed | size of the zlib stream that follows (= stored size − 8) |
| 0x04 | 4 | u32 | decompressedSize | observed | length of the decompressed payload |
| 0x08 | … | bytes | zlibData | observed | a standard zlib stream (`0x78 0x9c` header) |

`observed` on `Outa1.odm` (stored 161,415 B): u32 at 0x00 = 161,407 = stored − 8;
u32 at 0x04 = 585,346; feeding bytes `[8:]` to a standard zlib decoder yields
exactly 585,346 bytes, matching the u32 at offset 0x04.

Decompressing yields the **map payload**, whose layout follows.

## Map header (decompressed payload)

The decompressed payload begins with a fixed **176-byte (0xB0) header**. The
layout matches the engine's `OdmHeader` struct (verified against the GrayFace
MMExtension struct definitions and confirmed against real maps):

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| 0x00 | 32 | char[32] | name | observed | map name, e.g. `"blank"` |
| 0x20 | 32 | char[32] | file_name | observed | e.g. `"default.odm"` |
| 0x40 | 31+1 | char[31]+pad | version | observed | `"MM6 Outdoor v1.11"` + 1 pad byte |
| 0x60 | 32 | char[] | sky texture name | observed | `plansky2` on `Oute3.odm`, empty on the other fourteen; the executable's picker at `0x46df60` is now read in full: the sky re-rolls **once per game day** — 80% from the nine fair skies {1,3,6,7,8,9,12,14,15}, 20% from the seven others {2,5,10,13,16,18,19}, two authored tables at `0x4c1874`/`0x4c1898`, `sky01` the fallback — and nineteen `sky01`..`sky19` panoramas ship, with 4, 11 and 17 in neither table. `observed` |
| 0x80 | 32 | char[32] | ground_name | observed | ground tileset, e.g. `"grastyl"` |
| 0xA0 | 16 | TilesetDef[4] | tilesets | observed | 4 × (i16 group, i16 offset) |

The header is exactly 0xB0 bytes; the heightmap begins immediately at 0xB0.

### Tileset definitions (offset 0xA0)

Four ground tileset references, each 4 bytes:

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| +0x00 | 2 | i16 | group | observed | tileset group id |
| +0x02 | 2 | i16 | offset | observed | record index where this group begins in `DTILE.BIN` |

`observed` on `Outa1.odm`: tilesets = (6,162), (5,126), (6,162), (22,774). Each
`(group, offset)` is an index into `DTILE.BIN`: `group` is the tileset group id
and `offset` is the record index where that group's tiles begin. Across all 15
outdoor maps `tileset[1]` is always `(5,126)` (water) and `tileset[3]` always
`(22,774)`, while slots 0 and 2 are the map's terrain pair. See
[`dtile.md`](dtile.md) for the verification that every 36-entry group's offset
matches its first record index.

## After the header: terrain and geometry

After the 0xB0 header:

- **Terrain grids** (heightmap + tilemap, see [`odm-terrain.md`](odm-terrain.md))
  occupy fixed 128×128 byte sections starting at 0xB0.
- **Vertex / facet / model geometry** follows. The model-array framing is
  canonical in [`odm-models.md`](odm-models.md), and the complete per-model
  stream order is canonical in [`odm-model-facets.md`](odm-model-facets.md).

### Canonical geometry records

The model array contains 188-byte `MapModel` records. Each model's sequential
geometry block contains 12-byte vertices, 308-byte facets, facet ordering,
optional BSP nodes, and texture names. Exact counts, record fields, and stream
order belong to [`odm-models.md`](odm-models.md) and
[`odm-model-facets.md`](odm-model-facets.md); duplicating those layouts here
would create a second source of truth.

## Invalid-input behavior

The parser rejects, deterministically and without reading out of bounds:

- fewer than 8 bytes (cannot hold the wrapper);
- zlib failure or a decompressed length not matching the wrapper's
  decompressedSize;
- a decompressed payload shorter than the fixed header (0xB0 bytes);
- a version string that is not `"MM6 Outdoor v1.11"` (or, more loosely, not a
  recognized `MM6 Outdoor` form).

## Historical question status

> Audited in the [open-question register](../open-questions.md); the register
> supersedes unresolved hypotheses below.

- The nested vertex/facet/BSP arrays *inside* each model (each model record
  carries offset/count fields; the model array itself is now decoded — see
  [`odm-models.md`](odm-models.md)).
- The meaning of the remaining terrain attribute grids after the tilemap
  (a third 128×128 grid precedes the model array). That grid is now read:
  **every byte is zero on all fifteen shipped maps** — zeroed runtime
  state shipped empty, like the monster records' tails. `observed`
  Reproduce with `odm_info <map>`, which prints its nonzero count.
- The tileset group/offset → tile-graphic mapping is now resolved: each
  `(group, offset)` indexes `DTILE.BIN` by record — see
  [`dtile.md`](dtile.md).
- How decorations and spawns are stored.
