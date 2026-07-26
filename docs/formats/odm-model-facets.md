# ODM model facets and the geometry stream (Might and Magic VI)

Status: **draft, evidence-backed — all models walk.** This document records the
layout of the per-model geometry stream and of one facet record, verified
against every outdoor map in one legal installation. Each claim is tagged
`observed`, `inferred`, or `unknown`.

It supersedes the "facets are variable-length" conclusion in
[`odm-model-mesh.md`](odm-model-mesh.md); see "Correction" below.

## Scope

Covers the geometry stream that follows the model array: how one model's
vertices, facets, ordering array, BSP nodes and facet texture names are laid
out, and the fields of a facet record that a renderer needs. Does **not** cover
the BSP node layout (no shipped map uses one) or the meaning of every facet
attribute bit.

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product and edition | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Binary | `MM6.exe`, SHA-256 `28d2b83e75db45134d161da1da767afcbdb3e381921d3de61c2784ac85cd00ce` |
| Map data | `data/Games.lod`, SHA-256 `28103a220212a0abed63f30d34d248203dbc78ec89b99629f631a65370964975` |
| Maps verified | all 15 `.odm` entries: 921 models, 37,187 facets `observed` |
| Reference | loader `fcn.0046c5d0`, allocation/copy sequence at `0x46d4d9`–`0x46d67e` |

Reproduce the summary counts with:

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/odm_info Outa1.odm     # prints model/vertex/facet/texture counts
```

## Correction to the previous slice

[`odm-model-mesh.md`](odm-model-mesh.md) concluded that facets are
variable-length, because no fixed record size among 80/52/44/28 bytes placed
model 1's first vertex inside its bounding box. That inference was wrong: the
record is a fixed **308 bytes**, which that set did not include. The evidence
that settles it:

- For all 22 inter-model gaps in `Outa1.odm`, the bytes between one model's
  vertex array and the next model's equal `facet_count × 320` **exactly** —
  308 bytes of facet plus 12 bytes of trailing per-facet data. `observed`
- The loader allocates `308 × facet_count` bytes for the facet array. The
  arithmetic at `0x46d50f` (`lea edx,[eax+eax*8]; lea edx,[eax+edx*2];
  lea eax,[eax+edx*4]; shl eax,2`) evaluates to `308 × facet_count`. `observed`

## The geometry stream

The stream begins immediately after the model array, at
`0xC0B4 + model_count × 188`, and contains one block per model in model-array
order. There is **no offset table**: block N can only be found by walking
blocks 0..N-1. `observed`

Within one block, in order:

| # | Array | Element size | Count | Status |
| --- | --- | --- | --- | --- |
| 1 | vertices | 12 | `vertex_count` (record +0x44) | observed |
| 2 | facets | 308 | `facet_count` (record +0x4C) | observed |
| 3 | facet ordering | 2 | `facet_count` | observed |
| 4 | BSP nodes | 8 | `bsp_node_count` (record +0x5C) | observed |
| 5 | facet texture names | 10 | `facet_count` | observed |

This order and these sizes are the loader's own: it allocates each array and
`rep movs`-copies it from a single advancing source pointer, in exactly this
sequence (`0x46d4d9` vertices, `0x46d509` facets, `0x46d543` ordering,
`0x46d626` BSP nodes, `0x46d650` names). `observed`

`bsp_node_count` is **0 in all 921 models** of the shipped maps, so arrays 4 is
empty in practice — but the loader reserves it, and a decoder that ignores it
would desynchronize on any map that used one. `observed`

## Facet record (308 bytes / 0x134)

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| 0x00 | 4 | i32 | normal_x | observed | 16.16 fixed point |
| 0x04 | 4 | i32 | normal_y | observed | 16.16 fixed point |
| 0x08 | 4 | i32 | normal_z | observed | 16.16 fixed point |
| 0x0C | 4 | i32 | plane_distance | observed | 16.16 fixed point |
| 0x10 | 12 | — | unknown | unknown | zero in most observed facets |
| 0x1C | 4 | u32 | attributes | inferred | bit flags; see below |
| 0x20 | 40 | u16[20] | vertex_ids | observed | indices into this model's vertices |
| 0x48 | 40 | i16[20] | u | observed | per-vertex texture coordinate, texels |
| 0x70 | 40 | i16[20] | v | observed | per-vertex texture coordinate, texels |
| 0x98 | 150 | — | unknown | unknown | three further 20-entry i16 arrays and a tail |
| 0x12E | 1 | u8 | vertex_count | observed | the polygon's size |
| 0x12F | 5 | — | unknown | unknown | |

### The plane

The normal is unit length in all 37,187 facets, and
`normal · vertex + plane_distance == 0` holds for 166,681 of the 166,702
vertex-facet pairs, with a worst-case residual of 103 world units on the
remaining few. `observed` — this is the check that confirms both the plane
fields and the vertex-id decode at once, since it relates two independently
decoded arrays.

### The vertex-id list

Only the first `vertex_count` entries are meaningful. Polygon sizes observed
across all maps: 3 (3,667), 4 (24,353), 5 (3,256), 6 (3,338), and a long tail
up to 20. One degenerate facet with `vertex_count` 2 exists in `Oute3.odm`.
`observed`

When `vertex_count < 20` the game also writes a copy of `vertex_ids[0]` at
index `vertex_count`, closing the ring. This holds in every facet with room for
it, and is what let the list be located before the count field was found.
`observed` — decoders should not rely on it: the two 20-vertex facets in
`Outc2.odm` have no room for the closing copy.

Every id in every facet indexes inside its own model's vertex array. `observed`

### Texture coordinates

`u`/`v` are per-vertex texture coordinates in texels, parallel to
`vertex_ids`. Dividing by the texture's width/height gives normalized
coordinates that tile correctly on real geometry. `observed`

They are close to the vertex world position projected onto the two axes the
facet's normal does *not* dominate, but not identically so: a per-facet offset
is involved, and the exact rule reproduces only about half the facets.
`unknown` — this does not block rendering, because the values are stored
explicitly rather than derived.

### Attributes

49 distinct values across all maps. The most common are `0x200` (10,940),
`0x400` (9,829) and `0x100` (9,184), and these three are the **projection
plane**: exactly one is set on every one of the 37,187 facets, and it is always
the plane perpendicular to the normal's largest component, with ties resolved
towards z then y. Under that tie order the rule holds on **37,187 of 37,187**.
`observed`

An earlier revision recorded this as `inferred` at 35,072 of 37,187. The 2,115
apparent exceptions were ties being broken the other way, not counterexamples.
The same three bits mean the same thing on indoor faces, where the rule holds
on all 89,091; see [`blv.md`](blv.md) for the derivation.

The remaining bits (`0x8`, `0x1000`, `0x2000000`, …) are `unknown`.

### Texture names

Array 5 is parallel to the facet array: name N belongs to facet N. Each is a
10-byte field naming an entry in `BITMAPS.LOD` (matched case-insensitively, as
with ground tiles). `observed`

Every one of the 37,187 fields is NUL-terminated — none fills all ten bytes —
but 6,326 of them carry stale bytes *after* the terminator (for example
`"STRCTR\0" "1\0\0"`), so the field is reused memory rather than cleared
memory. Reading the fixed width and stopping at the first NUL handles both
forms. `observed`

One name in `Outb2.odm` begins with a NUL byte, yielding an empty name. That is
the data as shipped, not a decode failure. `observed`

`Outa1.odm` references 58 distinct facet textures; `Outc2.odm`, 181.
`observed`

## World placement

Model vertices are absolute world coordinates on the same scale as the terrain
grid — no per-model translation is applied. `observed`

MM6 world space is **X/Y-horizontal with Z up**. Comparing each model's base
elevation against the heightmap cell under it gives a mean error of 744 world
units under that assignment, versus 10,211 under the alternative (X/Z
horizontal, Y up) on `Outa1.odm`; on `Outb1.odm`, 462 versus 14,377.
`observed`

## Invalid-input behavior

The decoder rejects, deterministically and without reading out of bounds:

- a geometry block whose five arrays would extend past the payload;
- a facet whose `vertex_count` exceeds the record's 20-entry capacity;
- a facet referencing a vertex id its own model does not have;
- counts large enough to overflow the size arithmetic (all of it is done in
  64-bit).

Because the stream has no offset table, a failure anywhere rejects the whole
map's geometry rather than yielding a partial decode that would silently
mis-attribute later models' facets.

## Open questions (next slice)

- The unknown spans at +0x10, +0x98 and +0x12F. The three 20-entry i16 arrays
  in the +0x98 span carry small signed values (0, ±1, ±2, ±3) and are
  plausibly per-vertex intercept displacements. `unknown`
- The meaning of the facet ordering array (array 3). Its values are not a
  permutation of the facet indices and are all zero in some models. `unknown`
- The BSP node layout (8 bytes), unexercised by any shipped map. `unknown`
- Which attribute bit marks a facet two-sided or invisible — needed before
  backface culling can be enabled for models.
- The exact rule that derives `u`/`v` from world coordinates.
