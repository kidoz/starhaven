# BLV indoor map format (Might and Magic VI)

Status: **draft, evidence-backed — geometry only.** The level geometry of the
52 indoor maps is decoded and verified; the sections after it are not. Each
claim is tagged `observed`, `inferred`, or `unknown`.

## Scope

Covers the zlib wrapper, the fixed header, the vertex array, the face array,
the per-face index arrays (vertex ids and texture coordinates), the face
texture names and the face-extra array — everything needed to draw the level. Does **not** cover the rooms/sectors, BSP tree,
lights, doors or sprites that follow, which are 19–38% of each payload.

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product and edition | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Map data | `data/Games.lod`, SHA-256 `28103a220212a0abed63f30d34d248203dbc78ec89b99629f631a65370964975` |
| Maps verified | all 52 `.blv` entries: 114,833 vertices, 89,091 faces `observed` |

Reproduce with:

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/blv_info CD1.blv
```

## Outer structure

A `.blv` entry uses the **same 8-byte wrapper as `.odm`** (see
[`odm.md`](odm.md)): u32 stream size, u32 decompressed size, then a zlib
stream. `observed`

### Two shipped maps have a corrupt checksum

`CD3.blv` and `d08.blv` fail a checksum-verifying inflate with "incorrect data
check", yet inflating them as **raw deflate** — skipping the 2-byte header and
ignoring the Adler-32 trailer — produces exactly the byte count the wrapper
declares (739,920 and 274,472). The deflate data is intact; only the trailer is
wrong, and the original engine plainly never verified it. `observed`

A decoder should therefore fall back to raw deflate, but must still require the
output length to match the declared size — otherwise a genuinely truncated
stream would be accepted as a short map.

## Header

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| 0x00 | 4 | u32 | kind | unknown | 1 on most maps, 6 on some |
| 0x04 | ≤76 | char[] | name | observed | e.g. `"Dwarf Hold"`, `"No Name Level"` |
| 0x50 | ≤24 | char[] | name2 | observed | e.g. `"war1a"`, `"test"` |
| 0x68 | 4 | u32 | index_block_bytes | observed | total size of the per-face index arrays |
| 0x6C | 4 | u32 | unknown | unknown | |
| 0x70 | 4 | u32 | unknown | unknown | |
| 0x74 | 4 | u32 | unknown | unknown | |
| 0x78 | 16 | — | reserved | observed | zero in every observed map |
| 0x88 | 4 | u32 | vertex_count | observed | |
| 0x8C | count × 6 | Vertex[] | vertices | observed | |

The two name fields' declared widths are `inferred`: the bytes between each
name and the next known field are NUL in every observed map, so reading to the
next field's offset is safe but may be wider than the real declaration.

`index_block_bytes` is a genuine cross-check rather than a stored constant: it
equals `Σ(vertex_count + 1) × 2 × 6` over all faces, **exactly**, on all 52
maps. `observed`

### Vertex (6 bytes)

| Offset | Size | Type | Field | Status |
| --- | --- | --- | --- | --- |
| +0x00 | 2 | i16 | x | observed |
| +0x02 | 2 | i16 | y | observed |
| +0x04 | 2 | i16 | z | observed |

Indoor coordinates are 16-bit, unlike the 32-bit coordinates of outdoor model
meshes. As outdoors, **x and y are horizontal and z is up**: observed extents
run to ±30,000 horizontally but only ±2,300 vertically. `observed`

## Face array

A `u32` face count follows the vertex array, then that many 80-byte records.

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| 0x00 | 4 | i32 | normal_x | observed | 16.16 fixed point |
| 0x04 | 4 | i32 | normal_y | observed | |
| 0x08 | 4 | i32 | normal_z | observed | |
| 0x0C | 4 | i32 | plane_distance | observed | |
| 0x10 | 12 | — | unknown | unknown | zero in most observed faces |
| 0x1C | 4 | u32 | attributes | inferred | bit flags; see below |
| 0x20 | 24 | u32[6] | array pointers | observed | runtime pointers, not file data |
| 0x38 | 21 | — | unknown | unknown | |
| 0x4D | 1 | u8 | vertex_count | observed | the polygon's size |
| 0x4E | 2 | — | unknown | unknown | |

The record size was found by requiring every face's leading four i32s to form a
unit-length normal: **80 bytes is the only stride that satisfies that**, and it
does so on all three maps first sampled and all 52 thereafter. `observed`

The plane is verified the same way as the outdoor facets: `normal · v +
plane_distance == 0` holds for **all 48,610** vertex-face pairs across the
first three maps, worst residual 2.3 world units. `observed`

### The six array pointers

The six u32 slots at +0x20 are runtime pointers, overwritten by the loader.
Their **on-disk values still encode the layout**: consecutive slots differ by
exactly `2 × (vertex_count + 1)` in every one of the 11,710 faces sampled,
which is what identifies each face as owning six `u16[vertex_count + 1]`
arrays. `observed`

The six arrays are, in order:

| Index | Contents | Status |
| --- | --- | --- |
| 0 | vertex ids | observed |
| 1, 2, 3 | small signed values, only -3..3 ever seen | inferred |
| 4 | per-vertex texture u, in texels | observed |
| 5 | per-vertex texture v, in texels | observed |

Arrays 4 and 5 are identified by their ranges tracking the level's own world
extents (on `d01.blv`, u spans -20672..20754 against an x extent of
-20672..18672), and confirmed visually: dividing them by the texture's
dimensions makes walls, floors and ceilings tile correctly. `observed`

Arrays 1-3 take only seven distinct values across a whole level, matching the
three 20-entry spans in the outdoor facet record; they are plausibly intercept
displacements and are not needed for rendering. `inferred`

### Attributes

Bit 0 marks a face the renderer should skip. Every one of the 335 untextured
faces across the sampled maps sets it, and no untextured face lacks it; 20
textured faces also carry it. `observed` — the natural reading is a
portal or otherwise invisible face, which a portal-based indoor renderer needs.
`inferred`

Bits 0x100/0x200/0x400 dominate the rest, the same axis-selector pattern the
outdoor facets show (see [`odm-model-facets.md`](odm-model-facets.md)).
`inferred`

## Per-face index arrays

Immediately after the face array, and `index_block_bytes` long. Faces appear in
order; each contributes its six arrays back to back, each array holding
`vertex_count + 1` u16 entries. The extra entry repeats the first, closing the
ring — the same convention the outdoor facets use. `observed`

Because the arrays are variable-length and there is no offset table, walking
them is the only way to reach the texture names that follow.

## Face texture names

After the index block: one 10-byte NUL-padded name per face, parallel to the
face array, naming a `BITMAPS.LOD` entry. An empty name means the face carries
no texture. `observed`

## Invalid-input behavior

The parser rejects, deterministically and without reading out of bounds:

- an entry too small for the wrapper, or one whose stream will not inflate;
- a raw-deflate fallback whose length does not match the declared size;
- a payload shorter than the header;
- vertex, face, index or name sections extending past the payload;
- a face referencing a vertex the map does not have;
- an index block that does not match the sum over the faces' vertex counts.

## Face extras

Immediately after the texture names: a `u32` count, then that many 36-byte
records. Each names a face, and there are far fewer of them than faces
(ratios of 0.14 to 0.93 across the shipped maps), so they are extra data
attached to selected faces rather than a parallel array.

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| +0x00 | 12 | — | unknown | unknown | zero in every observed record |
| +0x0C | 2 | u16 | face_index | observed | always a valid face index |
| +0x0E | 2 | u16 | marker | observed | **0xffff in all 35,485 records** |
| +0x10 | 4 | — | unknown | unknown | |
| +0x14 | 2 | i16 | texture origin u | observed | −min(u) of the face |
| +0x16 | 2 | i16 | texture origin v | observed | −min(v) of the face |
| +0x18 | 2 | — | unknown | unknown | |
| +0x1A | 2 | u16 | unknown | unknown | non-zero on ~1 record in 6 |
| +0x1C | 20 | — | unknown | unknown | zero in all but 3 observed records |

Both invariants — the face index being in range and the `0xffff` marker — hold
across **all 52 maps and all 35,485 records**, and the parser enforces them.
The marker is what makes a misaligned read impossible to miss. `observed`

The array is in **ascending face order on every map**, and a face may be named
more than once: 17 faces across the 52 maps carry two records. `observed`

### Attribute bit 0x80000000 says a face has an extra

The face's own attribute word announces it. Across all 52 maps:

| | faces |
| --- | ---: |
| flagged and described | 35,433 |
| flagged with no record | **0** |
| described without the flag | 35 |
| neither | 53,623 |

The 35 exceptions are one per map and are all **face index 0** — the sentinel
record every array begins with, whose other fields are zero. Setting the
exception aside, the bit and the record agree exactly, in both directions.
`observed`

Reproduce with `blv_info <map>.blv`, which reports the comparison.

### The texture origin at +0x14 and +0x16

These two are **the face's texture origin**: signed offsets that bring its
lowest texture coordinate to zero. Where the field is set,

- `+0x14 == −min(u)` in **28,877 of 29,731** records (97.1%)
- `+0x16 == −min(v)` in **29,360 of 29,657** records (99.0%)

taking each as `i16`. Read unsigned they are nonsense: most faces have a
positive minimum, so most origins are negative, and −1 reads as 65,535.
`observed`

A zero field is **not** an origin of zero — it is a record that carries none,
and 5,751 do. Measured over the whole array instead, agreement falls to 82%,
which is what an earlier revision of this document reported.

Of the ~1,000 records that do not match exactly, most differ by a whole
multiple of 64, consistent with the offset being taken modulo a texture
dimension. `inferred`

`blv_info <map>.blv` reports the comparison; over all 52 maps it is
**58,237 of 59,388** (98.1%).

**StarHaven does not apply these to its texture coordinates.** Dividing the raw
`u`/`v` by the texture's dimensions already tiles walls, floors and ceilings
correctly, and shifting each face's origin to zero would change that alignment.
The value is much more likely to be what the original renderer used to size a
texture-space span or lightmap. Recording what the field *is* does not settle
what it was *for*.

### The sparse field at +0x1A

Non-zero on about one record in six. It takes 81 distinct values across the
whole game, runs 0 to 295, and repeats within a map, so several faces share
one. Of the 934 values shared by more than one face, 501 name a run of
**consecutive** face indices, and the median index span is 4 — which is what a
door or a trigger built from a handful of adjacent faces would look like, and
also what several other things would look like. `inferred`

It is **not** a room id: a room id would run 1..N densely, and only 6 of the 52
maps have values forming a contiguous range from 1. `observed`

Its presence is associated with face attribute bit `0x02000000` — 85.7% of
records with a non-zero value name a face carrying that bit. `observed`

## Face-extra names

The face-extra array is followed by one **10-byte name per extra**, the same
arrangement faces and their texture names use. `observed`

Almost all are empty: of the 12,198 slots across the shipped maps, 12,184 hold
nothing, 14 hold printable text (`ca25a_x$`, `li22b_s$`, `no25b_s`,
`no33c_c`), and **none holds garbage**. That zero-garbage result over 12,198
slots is what identifies the section — a wrong size or offset would produce
random bytes almost everywhere.

The names' meaning is `unknown`; their form resembles the game's asset naming
but they resolve to no archive entry tried so far.

## Decorations (located by scanning, not by offset)

Somewhere in the tail sits an array of 32-byte decoration records — the placed
sprites and markers of the level.

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| +0x00 | 22 | char[22] | name | observed | NUL-terminated, e.g. `"Torch01"` |
| +0x16 | 2 | u16 | flags | inferred | only 0 and 1 observed |
| +0x18 | 2 | i16 | x | observed | |
| +0x1A | 2 | i16 | y | observed | |
| +0x1C | 2 | i16 | z | observed | |
| +0x1E | 2 | i16 | angle | inferred | facing; units unconfirmed |

Observed names include `Party Start`, `Torch01`, `TorcH2`, `Brazieroff`,
`CampfireOn`, `Barrel` and `tree09`. `Party Start` is the party's spawn point,
and every coordinate observed falls inside its own level's vertex extents.
`observed`

**The array's position cannot be computed.** The sections between the face
extras and this array are undecoded, and no count for it appears
anywhere nearby — searching the whole payload before the array finds the count
value at most a handful of times, never within 4 KB of it. The decoder
therefore *scans*: it looks for a run of at least four consecutive records
whose names are printable and whose coordinates lie inside the level's vertex
extents, then extends in both directions.

That filter is strong in practice — 50 of the 52 maps yield decorations,
5,780 in total — but it is a heuristic, and `hive.blv` and `zddb04.blv` yield
none. Whether those two genuinely have no decorations or the scan misses them
is `unknown`. The API keeps this separate from `parse_blv` for that reason.

## Open questions (next slice)

- The sections between the face extras and the decorations: rooms/sectors, the
  BSP tree, lights and doors. Locating the decoration array by offset rather
  than by scanning depends on these. `unknown`

  Sliding-window stride detection over that region on `D03.blv` segments it
  into a **stride-8** run, a high-entropy run with no stride above noise
  (plausibly a BSP or bit-packed structure), then a **stride-28** run ending at
  the decorations.

  Three models have now been ruled out rather than merely untried:

  1. **Count-prefixed sections.** Searched exhaustively as stride sequences,
     jointly across ten maps — every map must advance by the same sequence and
     land exactly on its own decoration array — to a depth of eight. **No
     sequence closes**, on ten maps or even on one. So at least one section is
     not `u32 count` followed by `count × stride`.
  2. **A linear model** over the header counts, vertex and face counts: fitted
     exactly on 49 maps, no solution.
  3. **A leading zero block as empty sections.** This was what defeated the
     first search attempt, and it turned out not to be a section at all: it is
     the face-extra name array, now decoded. Finding it moved the boundary
     forward but did not make the count-prefixed model work for what follows.
- Whatever follows the decoration array — 32 KB on `d01.blv`, 70 KB on
  `CD1.blv`. `unknown`
- The header fields at 0x00, 0x6C, 0x70 and 0x74. `unknown`
- What arrays 1-3 of each face's six actually mean. `unknown`
- The unknown spans inside the face record (+0x10, +0x38, +0x4E). `unknown`
- Whether the `.dlv` files pair with `.blv` the way `.ddm` pairs with `.odm`.
