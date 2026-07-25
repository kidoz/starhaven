# ODM decorations (Might and Magic VI)

Status: **verified.** The placed sprites of an outdoor map — trees, cacti,
rocks, stumps, pedestals and the party's start marker. Each claim is tagged
`observed`, `inferred`, or `unknown`.

## Scope

Covers the decoration array that follows the model geometry stream, and the
`DDECLIST.BIN` table its type ids index. Does not cover the ~100 KB that
follows the decorations in each payload.

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product and edition | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Map data | `data/Games.lod`, SHA-256 `28103a220212a0abed63f30d34d248203dbc78ec89b99629f631a65370964975` |
| Maps verified | all 15 `.odm` entries: 6,210 decorations `observed` |
| Table | `DDECLIST.BIN` in `icons.lod`, 230 records `observed` |

Reproduce with:

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/odm_info Outa1.odm      # prints the decoration count
./buildDir/walk_odm Outa1.odm      # draws them as billboards
```

## Location

The array begins immediately after the model geometry stream, whose end is
computable by walking each model's five arrays (see
[`odm-model-facets.md`](odm-model-facets.md)). **No scanning is needed** — this
is the deterministic counterpart of the indoor case, where the equivalent array
can only be found heuristically (see [`blv.md`](blv.md)).

| Order | Contents |
| --- | --- |
| 1 | `u32` count |
| 2 | count × 28-byte records |
| 3 | count × 32-byte names, parallel to the records |

The arithmetic closes exactly on every map: the bytes between the geometry end
and the name array equal `4 + count × 28` for all 15. `observed`

## Record (28 bytes)

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| +0x00 | 4 | u32 | kind | observed | index into `DDECLIST.BIN` |
| +0x04 | 4 | i32 | x | observed | world position |
| +0x08 | 4 | i32 | y | observed | |
| +0x0C | 4 | i32 | z | observed | elevation |
| +0x10 | 12 | — | unknown | unknown | zero in every observed record |

Coordinates are 32-bit, as with model meshes, and every one of the 6,210
observed positions lies inside the 65,536-unit world. `observed`

## Name array (32 bytes each)

A NUL-padded name per decoration, parallel to the records. `observed`

The two arrays cross-check each other: across all 15 maps there are **85
distinct type ids, and every one maps to exactly one name**. `observed`

## The `DDECLIST.BIN` table

Same container shape as `DTILE.BIN` (see [`dtile.md`](dtile.md)): a 48-byte
header, a zlib stream, then a `u32` count and 230 fixed 80-byte records.
`observed`

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| +0x00 | 32 | char[32] | name | observed | matches the map's name array |
| +0x20 | 32 | char[32] | group | observed | e.g. `"tree"`, `"cactus"`, `"test"` |
| +0x42 | 2 | u16 | unknown | unknown | 96 for trees, 52 for cacti; plausibly a radius |
| +0x44 | 2 | u16 | unknown | unknown | 76 for trees |
| +0x48 | 2 | u16 | unknown | inferred | consecutive across sibling entries (tree27→1158, tree28→1159, tree29→1160), so an id rather than a size |

A decoration's `kind` indexes this table, and the record's name matches the
map's own name array in **726 of 727** entries on `Outa1.odm`. `observed`

## Rendering

Names resolve as `SPRITES.LOD` entries, decoded through the shared `palXXX`
palette the sprite header names. `observed`

Two caveats:

- **Not every name resolves.** On `Outa1.odm`, `tree27`/`tree28`/`tree29`,
  `stump5a` and `ped06` exist in `SPRITES.LOD`, but `Cactus24`, `Cactus25`,
  `Cactus27`, `Rock03`, `Rock05` and `uacrwn` do not, under any casing. Those
  cover 115 of the map's 727 placements. Where the art actually lives is
  `unknown`.
- **The world size of a sprite is not stated.** Neither the record nor the
  `DDECLIST` entry gives one, and sprite pixels are plainly not world units — a
  tree drawn 1 unit per pixel stands shorter than a cottage door. The renderer
  uses a factor of 4, calibrated by eye against the models. `inferred`

## Invalid-input behavior

The decoder rejects, deterministically and without reading out of bounds:

- a payload that cannot hold the model array or a model's geometry;
- a decoration count whose records or names would extend past the payload.

## Open questions

- The 12 unknown bytes at the end of each record. `unknown`
- The `DDECLIST` fields at +0x42/+0x44/+0x48. `unknown`
- Where the missing sprites live. `unknown`
- The correct world scale for a decoration sprite. `unknown`
- The ~100 KB that follows the decoration array in every outdoor payload.
  `unknown`
