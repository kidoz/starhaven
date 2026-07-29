# BLV indoor map format (Might and Magic VI)

Status: **draft, evidence-backed — geometry only.** The level geometry of the
52 indoor maps is decoded and verified; the sections after it are not. Each
claim is tagged `observed`, `inferred`, or `unknown`.

## Scope

Covers the zlib wrapper, the fixed header, the vertex array, the face array,
the per-face index arrays (vertex ids and texture coordinates), the face
texture names and the face-extra array — everything needed to draw the level —
plus the sector table (partly) and the lights. Does **not** cover the BSP
tree, doors or sprites that follow.

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
| 0x74 | 4 | u32 | eventStateBytes | observed | the saved-state size of the paired `.dlv`; see below |
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

### The header declares the event file's state size

The `u32` at `+0x74` is not about the level at all: it is **the number of bytes
of saved state the paired `.dlv` event file carries**, after that file's fixed
200-slot block and before its 256-byte trailer (see
[`event-tables.md`](event-tables.md)).

It agrees on **all 52 maps**, from 0 on the small `zddb*` levels to 8,028 on
the largest. `ddm_info <map>.dlv` opens the level and reports the comparison.
`observed`

This is the only size in the event file that the event file does not describe
itself, which is why an earlier attempt to model it from the inside failed.

### The six array pointers

The six u32 slots at +0x20 are runtime pointers, overwritten by the loader.
Their **on-disk values still encode the layout**: consecutive slots differ by
exactly `2 × (vertex_count + 1)` in every one of the 11,710 faces sampled,
which is what identifies each face as owning six `u16[vertex_count + 1]`
arrays. `observed`

They also give away **the address the payload was loaded at** in the process
that wrote the file. A face's first pointer is that address plus its own
array's offset in the index block, so subtracting the offset recovers it — and
every face in a map agrees, on all 52 maps. `blv_info` reports it. `observed`

That makes any other stale pointer in the file readable as a payload offset,
which is worth having even though it has not yet cracked anything: see the open
questions.

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

#### Bits 0x100/0x200/0x400 select the projection plane

**Exactly one of the three is set on every polygon in the game** — all 89,091
indoor faces and all 37,187 outdoor model facets — and which one is decided by
the face's own normal:

| Bit | Set when | Plane |
| --- | --- | --- |
| 0x100 | `|nz|` is the largest component | XY |
| 0x200 | `|ny|` is the largest | XZ |
| 0x400 | `|nx|` is the largest | YZ |

with ties resolved towards z, then y. Reproducing the shipped values needs that
tie order: of the six possible precedences, the other five agree on 94.7% to
98.5% of faces and **z-then-y-then-x agrees on 100.00%**, on both formats.
`observed`

This is the plane a polygon is flattened onto to be worked with in two
dimensions — for point-in-polygon tests and texture mapping — which is why the
bit tracks the normal rather than anything about the surface. It resolves the
"axis-selector pattern" [`odm-model-facets.md`](odm-model-facets.md) recorded
as `inferred`.

#### Bits that accompany an event id

Bits 0x02000000, 0x04000000 and 0x08000000 travel with a non-zero `+0x1A` in
the face's extra record: 5,380 of the 5,560 faces with such a value carry one
of them, and 147 faces carry one without a value. Close, but not a rule.
`inferred`

#### Bits still unread

| Bit | Faces | What is visible |
| --- | ---: | --- |
| 0x10 | 416 | 86% floors, and 90% of them are textured `wtrtyl`, `orwtrtyl`, `swprrf3` — liquid surfaces. But 1,395 faces with those textures do **not** set it, so it is not "this is water". `unknown` |
| 0x40000 | 2,509 | 82% walls; textures include `tdoord`, `d8dorb`, `d2lgdor` and a family of `bem*` beams. 70% carry an event id. Door-shaped, but fewer than half the faces are door-textured. `unknown` |
| 0x400000 | 7 | all ceilings, five of them textured `sky_*`. Too few to call. `unknown` |
| 0x8, 0x1000 | ~29,000 each | accompany the texture origins in the face's extra record (see below) |
| 0x20000000 | 2,836 | 96% walls, no event ids, no texture pattern. `unknown` |

The remaining bits appear on fewer than 120 faces each.

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

## The decoration block

A map's decorations are **not** a bare array to be scanned for. They are a
block with the same shape an outdoor map uses (see
[`odm-decorations.md`](odm-decorations.md)):

| Order | Contents | Size |
| --- | --- | --- |
| 1 | `u32` count | 4 |
| 2 | placement records | 28 x count |
| 3 | name records | 32 x count |

The placement record carries a kind word and the position as **three `i32`**;
the name record carries the name, flags and facing, and repeats the position as
`i16`. The 32-bit copy is the authoritative one.

| Offset | Size | Type | Field | Status |
| --- | --- | --- | --- | --- |
| +0x00 | 2 | u16 | zero on every shipped record | observed |
| +0x02 | 2 | u16 | 1 on every shipped record | observed |
| +0x04 | 4 | i32 | x | observed |
| +0x08 | 4 | i32 | y | observed |
| +0x0C | 4 | i32 | z | observed |
| +0x10 | 12 | | mostly zero; two records in ten set a byte | unknown |

**All 52 maps decode**, 5,776 decorations in total. `observed` On 49 of them the
block's name array begins exactly where the old scan anchored, which is the
cross-check; on the three with the fewest decorations — `Hive` (3),
`Sci-Fi` (1), `zddb04` (3) — the scan anchored elsewhere and the block's names
and coordinates are the plausible ones. `observed`

A very small count passes the validity filter by accident before the real block
on those same three maps, so the search keeps the **largest** candidate rather
than the first.

This does not open the region before it: the block's own offset is still found
by searching rather than computed. What it does is make the count and the
positions exact, and give the unknown region a hard right-hand edge —
17,028 bytes on `D01.blv`, 45,008 on `CD1.blv`.

## The region in front of the decorations is a sector table

The region opens with a count — 59 on `D01.blv`, 23 on `D03`, 181 on `CD1`,
13 on `T2` — which matches no header field and scales at roughly one per 20 to
45 faces. That is what a room or sector count looks like. `inferred`

What follows is **count-and-pointer pairs**: a small count, then a stale
pointer. The pointers cannot be followed — record 0024 established that most of
them address allocations that were never in the file — but they do not need to
be, because their **differences** are checkable:

- Sorted by pointer, **9,247 of 13,728** consecutive distinct pointers across
  the 52 maps are exactly `2 × count` bytes apart. `observed` A pointer
  difference matching a specific count by chance is a 2⁻³² event, so the
  agreement is structural; the shortfall is false candidates, since a pair is
  looked for at every word and nothing excludes a word that merely looks like
  one.
- Anchoring the lists so that the last ends at the decoration block,
  **6,361 of 13,685** land entirely on values below the map's face count.
  `observed`

So the lists are arrays of `u16` face indices, laid out contiguously in pointer
order, and the descriptors index into them.

The array itself is visible directly: on `D01` it runs from the region's
offset 6,844 to the decoration block at 17,028 — 5,092 entries against 2,291
faces, **2.22 to 1**. Every face is referenced, and **none is referenced only
once**: 2,051 appear exactly twice and 240 more often. `observed` That is what
a wall listed by the room on either side of it looks like.

### The record is 116 bytes, and holds six lists

`116 × count` lands **exactly** where the face-index array begins — 6,844 on
`D01.blv`, which is where an independent scan for a run of in-range indices
puts it, and the same on 46 of 51 maps. `observed`

Within a record, the pairs that pass the difference test sit at six fixed
offsets, the same six on 42 of the 49 maps where enough pairs validate:

| Offset in record | Validated pairs on `CD1` |
| --- | ---: |
| +0x08 | 99 |
| +0x10 | 180 |
| +0x18 | 180 |
| +0x48 | 61 |
| +0x50 | (fewer) |
| +0x58 | 81 |

So a sector carries **six count-and-pointer lists**, not one. Which is faces,
which portals, which decorations and which the rest is `unknown`; the two that
validate on nearly every record — +0x10 and +0x18 — are the likeliest
candidates for the face lists, since those are the ones every sector has.

What is still missing is the rest of the 116 bytes — the bounding box and
whatever else sits between the pairs — and therefore which faces belong to
which room. Reproduce the measurements with `blv_info <map> --sectors`.

## The lights, right after the decorations

The section following the decoration names is the level's static lights: a
`u32` count, then 12-byte records.

| Offset | Size | Type | Field | Status |
| --- | --- | --- | --- | --- |
| +0x00 | 6 | i16 x3 | position, map axes | observed |
| +0x06 | 2 | u16 | zero (one shipped 8) | unknown |
| +0x08 | 2 | u16 | brightness — 31 on every record | observed |
| +0x0A | 2 | u16 | radius, 8..640 | observed |

Every record's point lands inside the map's own vertex bounds on 48 of the
52 maps — the other four hold two or fewer lights, too few to discriminate
the stride — and the counts run from CD2's 3 to CD1's and the Pyramid's 400.
`observed` The engine bakes them per face at load: a linear falloff over
twice the written radius, over a dim floor, is this engine's own curve and
says so. Reproduce with `blv_info <map> --after`.

The section after the lights opens with a count of 8-byte i16 quads — 1,037
on `D01` against its 2,291 faces — and a closer look weakens the first
tree reading: fields 0 and 1 are index-shaped over the section's own count
with `-1` on roughly half, but field 2 is bounded far below it (9..169 on
`D01`, 7..217 on `CD1`) and matches no table counted so far — not sectors,
not vertices, not faces — while field 3 stays within 1..31. Read as
children, field 0's references split almost evenly between once, never and
repeatedly, which a binary tree would not do. The quads are measured and
the readings tried are recorded; what the section is remains `unknown`.
Reproduce with `blv_info <map> --bsp`.

## Open questions (next slice)

- The sections between the face extras and the decorations. The sector
  table at the region's front is partly read and the lights turned out to
  live *after* the decorations, so what remains here is the high-entropy
  middle — plausibly the BSP — and the exact section offsets; locating the
  decoration array by offset rather than by scanning still depends on
  them. `unknown`

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
- What follows the decoration block is now partly read: first the lights,
  then the quad section, both above. What the quads mean, and whatever
  trails them, stays `unknown`.
- The header fields at 0x00, 0x6C and 0x70. `unknown`
- The face-extra field at +0x1A is the **event id**: see
  [`map-events.md`](map-events.md). It was carried as unknown here until the
  scripts were found.
- The undecoded middle of the region after the face extras resists its
  own pointers too. Reading its stale pointers through the recovered load address does
  **not** open it: only 4% of the region's words map inside the payload at all,
  and of those just 7 land on a face boundary, none of them a face carrying the
  door or event bits. The pointers that are pointers mostly address other
  allocations the process made, which are not in the file. `observed`
- What arrays 1-3 of each face's six actually mean. `unknown`
- The unknown spans inside the face record (+0x10, +0x38, +0x4E). `unknown`
- Whether the `.dlv` files pair with `.blv` the way `.ddm` pairs with `.odm`.
