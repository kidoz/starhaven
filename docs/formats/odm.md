# ODM outdoor map format (Might and Magic VI)

Status: **draft, evidence-backed — outer format only.** This slice documents
the file envelope (zlib wrapper + the fixed map header) and parses the header
metadata. The tile grid, height map, vertex/facet geometry, and decorations are
**deferred** to later slices. Field layout is verified against a user-supplied
legal GOG.com installation. Each claim is tagged `observed`, `inferred`, or
`unknown`.

## Scope

This document covers the **`.odm` outdoor map** file as stored inside
`Games.lod` (entries like `Outa1.odm`). It documents:

- the 8-byte zlib wrapper that surrounds the map data;
- the fixed header at the start of the decompressed payload (name, name2,
  version string, tileset name, and the dimension/region fields at 0xA0).

It does **not** yet cover:

- the tile map (128×128 tile indices) and height map (128×128 heights);
- the vertex, facet, and model geometry (the 3D mesh of the terrain and props);
- decorations, spawns, and event hooks (see
  [`odm-tile-index.md`](odm-tile-index.md) for the per-tile index and the spawn
  point array that end the payload).

These are the subject of follow-up slices. The geometry in particular is a
large format (each ODM decompresses to ~570 KB) and will need its own slice.

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
- **Vertex / facet / model geometry** follows. The in-memory struct layouts are
  documented below, but the **file-level sectioning** (the exact order and
  count-prefix layout of the vertex, facet, and model arrays in the file) is
  **not yet confirmed** and is the subject of the next slice. Confirming it
  reliably requires analyzing `MM6.exe`'s map loader rather than guessing.

### Geometry struct anchors (in-memory layout, from the engine)

These are the engine's in-memory struct sizes, recorded as anchors for the
file-level decode. They are **not** sufficient on their own to parse the file:
the file stores count-prefixed variable-length arrays whose exact on-disk
order is still being verified.

- **MapVertex** — 6 bytes: X(i16), Y(i16), Z(i16). Terrain mesh vertices.
- **MapModel** — 0xBC (188) bytes: name[32], name2[32], bits, vertex/facet/
  ordering/BSP array pointers (file offsets), grid X/Y, position, bounding box.
  Models are the props/buildings/terrain features.
- **MapFacet** — 0x50 (80) bytes for MM6: normal (fixed-point), vertex id list,
  bitmap id, room, bounding box, polygon type, vertex count.
- **ModelVertex** — 12 bytes: X/Y/Z as i32 (model-local vertices).

The full geometry decode is deferred. The next experiment is to trace the map
loader in `MM6.exe` to confirm the file-level count-prefix order.

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
