# ODM models (Might and Magic VI)

Status: **draft, evidence-backed.** The model-array section that follows the
terrain grids, confirmed by tracing `MM6.exe`'s map loader in radare2 and
verified against real maps. Each claim is tagged `observed`, `inferred`, or
`unknown`. The nested vertex/facet/BSP arrays *inside* each model are deferred
(see Open questions).

## Scope

This document covers the **model array** — the list of named props/buildings/
terrain features placed on an outdoor map (bridges, houses, decorations). Each
model is a `MapModel` record (0xBC bytes) carrying its name, world position, and
bounding box. The model's own mesh (its vertices, facets, and BSP tree, stored
elsewhere in the file and referenced by offsets inside the model record) is the
subject of a later slice.

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Reference | `MM6.exe` loader `fcn.0046c5d0` (traced in radare2) |
| Maps verified | `Outa1.odm` (23 models), `Outb1.odm`, `Outc2.odm` |
| Record size | 188 bytes (0xBC) `observed` |

## How this was confirmed

Static analysis of `MM6.exe`'s outdoor loader (`fcn.0046c5d0`, reached via the
`"MM6 Outdoor v1.11"` string cross-reference) shows, after the header and three
16384-byte terrain grids, the sequence:

1. read a `u32` count;
2. read `count × 188` bytes.

The arithmetic `lea ecx,[eax+eax*2]; shl ecx,4; sub ecx,eax; shl ecx,2` at
`0x46d411` evaluates to `188 × count`, matching the engine's `MapModel` struct
size (`0xBC`). On `Outa1.odm` the count is 23 and the first model is named
`bridge2ns`. `observed`.

## Model array location

In the decompressed ODM payload:

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| 0xC0B0 | 4 | u32 | model_count | observed | number of MapModel records |
| 0xC0B4 | model_count × 188 | MapModel[] | models | observed | the model records |

`0xC0B0 = 0xB0 (header) + 3 × 0x4000 (three 128×128 grids)`. The three grids
are the heightmap (0xB0), the tilemap (0x40B0), and one further attribute grid
(0x80B0); only the heightmap and tilemap are decoded by the engine today.

## MapModel record (188 bytes / 0xBC)

Field offsets from the engine struct (verified against real model bytes):

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| 0x00 | 32 | char[32] | name | observed | e.g. `"bridge2ns"` |
| 0x20 | 32 | char[32] | name2 | observed | usually equals name |
| 0x40 | 4 | u32 | bits | inferred | attributes/flags |
| 0x44 | 4 | u32 | vertex_offset | inferred | offset to this model's vertices (nested) |
| 0x48 | 4 | u32 | vertex_count | inferred | number of model-local vertices |
| 0x4C | 4 | u32 | facet_offset | inferred | offset to facets (nested) |
| 0x50 | 2 | i16 | convex_facets_count | inferred | |
| 0x54 | 4 | u32 | facet_count | inferred | |
| 0x58 | 4 | u32 | ordering_offset | inferred | BSP/ordering data (nested) |
| 0x5C | 4 | u32 | bsp_node_count | inferred | |
| 0x60 | 4 | u32 | bsp_offset | inferred | |
| 0x68 | 4 | i32 | grid_x | observed | center grid X |
| 0x6C | 4 | i32 | grid_y | observed | center grid Y |
| 0x70 | 4 | i32 | pos_x | observed | world position X |
| 0x74 | 4 | i32 | pos_y | observed | world position Y (height) |
| 0x78 | 4 | i32 | pos_z | observed | world position Z |
| 0x7C | 4 | i32 | min_x | observed | bounding box min X |
| 0x80 | 4 | i32 | min_y | observed | bounding box min Y |
| 0x84 | 4 | i32 | min_z | observed | bounding box min Z |
| 0x88 | 4 | i32 | max_x | observed | bounding box max X |
| 0x8C | 4 | i32 | max_y | observed | bounding box max Y |
| 0x90 | 4 | i32 | max_z | observed | bounding box max Z |

This slice decodes the **observed** fields (name, position, bounding box). The
nested-array offset/count fields are read but not yet followed.

## Verified example (Outa1.odm)

- model 0: `bridge2ns` at (−13536, −8000, 640), bbox ~336×2176×256 — a bridge.
- model 1: `burnt1E` at (4624, 10992, 320), bbox ~1024×896×512 — a burnt building.
- model 2: `hsemid1W` at (6112, 12272, 320).

`observed`.

## Invalid-input behavior

The decoder rejects, deterministically and without reading out of bounds:

- a decompressed payload shorter than `0xC0B4` bytes (cannot hold the count);
- a model count whose records would extend past the end of the payload;
- any individual record field read past the payload end.

## Open questions (next slice)

- ~~The model facet stream that sits between models' vertex arrays.~~ Decoded:
  the record is a fixed 308 bytes and every model's mesh now walks. See
  [`odm-model-facets.md`](odm-model-facets.md).
- The third 128×128 terrain grid (offset 0x80B0) — its meaning is unknown.
- ~~How model positions relate to the heightmap coordinate system (units,
  handedness).~~ Resolved: model coordinates are absolute world units on the
  terrain's own scale, with X/Y horizontal and Z up. See
  [`odm-model-facets.md`](odm-model-facets.md).

The record's +0x44/+0x4C fields are labeled `vertex_offset`/`facet_offset` in
the table above, from the engine struct. On disk they hold the vertex and facet
**counts**; the loader overwrites them with pointers at runtime. See
[`odm-model-mesh.md`](odm-model-mesh.md).
