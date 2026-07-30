# ODM decorations (Might and Magic VI)

Status: **verified.** The placed sprites of an outdoor map — trees, cacti,
rocks, stumps, pedestals and the party's start marker. Each claim is tagged
`observed`, `inferred`, or `unknown`.

## Scope

Covers the decoration array that follows the model geometry stream, and the
`DDECLIST.BIN` table its type ids index. What follows the decorations is
covered by [`odm-tile-index.md`](odm-tile-index.md).

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
./buildDir/starhaven Outa1.odm      # draws them as billboards
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
| +0x40 | 2 | u16 | — | observed | zero on all 230 records |
| +0x42 | 2 | u16 | radius | observed | collision radius: 96 for trees, 52 for cacti (see below) |
| +0x44 | 2 | u16 | extent | observed | a second size dimension; a radius/height pair with `+0x42` (see below) |
| +0x46 | 2 | u16 | light | inferred | 256 on the six torches, 16 on `Pending!`, else 0 — a light-source flag |
| +0x48 | 2 | u16 | sprite_frame | observed | the `DSFT.BIN` frame index of this decoration's art (see below) |
| +0x4A | 2 | u16 | — | unknown | nonzero on five records (32 on `Party Start`, 64 on `Shp`, 8 on three torches) |
| +0x4C | 2 | u16 | sound id | observed | an ambient sound, resolved through `DSOUNDS.BIN` |
| +0x4E | 2 | u16 | — | observed | zero on all 230 records |

A decoration's `kind` indexes this table, and the record's name matches the
map's own name array in **726 of 727** entries on `Outa1.odm`. `observed`

### Ambient sound

Seven of the 230 types name a sound at `+0x4C`, and all seven resolve through
[`DSOUNDS.BIN`](dsounds.md) to what their names suggest — `CampfireOn` to
`campfire`, `Statue` to `fountain`, `Cauldron` to `bubbling cauldron01`. The
other 223 are zero, which is correct rather than incomplete: a tree makes no
noise. `observed`

## `+0x48` is the sprite frame table index

The field at `+0x48` is the decoration's art: a **frame index into
`DSFT.BIN`** that names the animation the decoration plays. This is the join
the renderer needs.

The evidence is direct, over all 230 records:

- **228 of 230** carry a nonzero `+0x48` that lands on a valid `DSFT.BIN` frame
  (the two zeros are `uacrwn` and `Party Start`, placeholders with no art).
  `observed`
- **228 of 228** nonzero indices point at a **named group-start frame** — never
  a mid-animation frame — which is the entry point for playing an animation.
  `observed`
- The `DSFT` group name at that index matches the decoration name. 137 match
  by identical trailing number (`tree01`→`tree01`, `ped01`→`ped01`), and the
  rest match as abbreviations the sprite table carries: `Cactus01`→`Cac1`,
  `Crystal03`→`crys5`, `Barrel`→`bigbarel`, `Pending!`→`Pending`. `observed`

`tree27`→`1158`, `tree28`→`1159`, `tree29`→`1160` — the consecutive run that
first marked the field as an id — is now seen to be three consecutive
`DSFT.BIN` group-start frames. `observed`

## The size pair: `+0x42` and `+0x44`

`+0x42` is the collision radius (96 for trees, 52 for cacti; the variation
between kinds is what the layout respects — see the radius note below). `+0x44`
is a second size dimension, a radius/height pair with it: the ship carries 378
(its full footprint), torches 7–8 (thin), plates 16 (flat and low), stalactites
64, trees 52–96. `observed` for the values; that one is a horizontal radius and
the other a vertical extent is `inferred` from how they vary by kind, not stated
by the data.

## `+0x46` marks a light source

256 is set on exactly the six torches (`Torch`, `torchnf`, `TorcH2`,
`nwtrchnf`, `SkullTorch`, `Torch01`) and 16 on the `Pending!` placeholder; every
other record is 0. Torches are the only decorations that emit dynamic light, so
the field reads as a light-source flag. `inferred`

## Rendering

Names resolve as `SPRITES.LOD` entries, decoded through the shared `palXXX`
palette the sprite header names. `observed`

Two caveats:

- **Not every name resolves.** On `Outa1.odm`, `tree27`/`tree28`/`tree29`,
  `stump5a` and `ped06` exist in `SPRITES.LOD`, but `Cactus24`, `Cactus25`,
  `Cactus27`, `Rock03`, `Rock05` and `uacrwn` do not, under any casing. Those
  cover 115 of the map's 727 placements. The sprite frame table does not
  rescue them either — none of the `Cactus*` names is one of its 1,656 groups —
  so where that art lives is still `unknown`.
- **The world size of a sprite is partly stated.** Neither the record nor the
  `DDECLIST` entry gives one, but the decoration name is usually an animation
  name, and the sprite frame table carries a per-sprite multiplier: `tree01` is
  2.1, `tree27` is 1.0, `ped01` is 0.7 (see [`dsft.md`](dsft.md)). That fixes
  the sizes relative to each other. The absolute factor is still nowhere in the
  data; the renderer multiplies by 4, calibrated by eye against the models.
  `inferred`

## Invalid-input behavior

The decoder rejects, deterministically and without reading out of bounds:

- a payload that cannot hold the model array or a model's geometry;
- a decoration count whose records or names would extend past the payload.

## Open questions

- The `DDECLIST` field at `+0x4A`, nonzero on only five records (32 on
  `Party Start`, 64 on `Shp`, 8 on three torches). `unknown`
- The 12 unknown bytes at the end of each record. `unknown`
- Where the missing sprites live. `unknown`
- The correct world scale for a decoration sprite. `unknown`
- The ~110 KB that follows the decoration array in every outdoor payload.
  `unknown`

  Two models are ruled out rather than untried. A **joint stride search** —
  requiring one sequence of count-prefixed sections to advance all 15 maps from
  their own start to their own end, over strides 1..256 to depth 10 — finds
  **nothing**. A **linear model** of the span over the decoration and model
  counts has no integer solution.

  The reason both fail is visible in the spans themselves: they run 102,888 to
  116,610 bytes, a spread of 13.3%, across maps whose geometry differs roughly
  tenfold. **About 103 KB is present regardless of map size and at most 13.7 KB
  varies**, so the region is dominated by fixed-size structures and cannot be
  described by count-driven sections at all. `observed`

  The header's three 128×128 grids show the format is willing to store large
  fixed tables, which is the obvious thing to test next.

## The field at +0x42 is a radius

`DDECLIST.BIN`'s `+0x42` is 96 for trees and 52 for cacti, and it behaves like
the room a decoration takes up: across the near pairs of decorations on a map,
**none is ever placed closer than the sum of the two values** — 0 of 1,737 on
Sweet Water and 0 of 2,001 on New Sorpigal — while giving every kind the same
96 does produce violations (8 on New Sorpigal, 15 on Goblinwatch). It is the
*variation* between kinds that the layout respects, which a constant cannot
explain. `observed`

The engine uses it as the collision radius for monsters walking past.
`observed` for the field being a radius (the placement analysis above), `unknown`
only for the absolute unit.
