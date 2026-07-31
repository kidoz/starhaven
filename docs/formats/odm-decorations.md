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
| +0x00 | 2 | u16 | kind | observed | index into `DDECLIST.BIN` |
| +0x02 | 2 | u16 | flags | observed | touch/monster/object triggers, map visibility, chest, invisible, obelisk chest |
| +0x04 | 4 | i32 | x | observed | world position |
| +0x08 | 4 | i32 | y | observed | |
| +0x0C | 4 | i32 | z | observed | elevation |
| +0x10 | 4 | i32 | direction | observed | MM6 angle |
| +0x14 | 2 | i16 | event_variable | observed | persistent decoration variable |
| +0x16 | 2 | u16 | event_id | observed | normal event |
| +0x18 | 2 | i16 | trigger_radius | observed | proximity trigger distance |
| +0x1A | 2 | i16 | direction_degrees | observed | used when direction is zero |

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
| +0x40 | 2 | i16 | type | observed | decoration type/category |
| +0x42 | 2 | u16 | height | observed | collision cylinder height |
| +0x44 | 2 | i16 | radius | observed | collision cylinder radius |
| +0x46 | 2 | i16 | light_radius | observed | radius of emitted light |
| +0x48 | 2 | u16 | sprite_frame | observed | the `DSFT.BIN` frame index of this decoration's art (see below) |
| +0x4A | 2 | u16 | flags | observed | rendering, collision, flicker, fire/smoke, and dawn/dusk sound bits |
| +0x4C | 2 | u16 | sound id | observed | an ambient sound, resolved through `DSOUNDS.BIN` |
| +0x4E | 2 | u16 | padding | observed | zero on all 230 records |

A decoration's `kind` indexes this table, and the record's name matches the
map's own name array in **726 of 727** entries on `Outa1.odm`. `observed`

### Descriptor flags at `+0x4A`

| Bit | Meaning |
| ---: | --- |
| `0x001` | does not block movement |
| `0x002` | do not draw |
| `0x004` | slow flicker |
| `0x008` | medium flicker |
| `0x010` | fast flicker |
| `0x020` | marker |
| `0x040` | slow animation loop |
| `0x080` | emit fire |
| `0x100` | play sound at dawn |
| `0x200` | play sound at dusk |
| `0x400` | emit smoke |

These meanings explain the five nonzero shipped values directly: torches
flicker, `Party Start` is a marker, and `Shp` uses the slow-loop flag.
`observed`

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

## Collision size: `+0x42` and `+0x44`

`+0x42` is the collision-cylinder height and `+0x44` its radius. Collision and
chest placement consumers use them directly. `observed`

## `+0x46` is light radius

256 is set on exactly the six torches (`Torch`, `torchnf`, `TorcH2`,
`nwtrchnf`, `SkullTorch`, `Torch01`) and 16 on the `Pending!` placeholder; every
other record is 0. The renderer adds a stationary light with this radius.
`observed`

## Rendering

Names resolve as `SPRITES.LOD` entries, decoded through the shared `palXXX`
palette the sprite header names. `observed`

Two caveats:

- **Not every decoration name resolves directly.** On `Outa1.odm`, `tree27`/`tree28`/`tree29`,
  `stump5a` and `ped06` exist in `SPRITES.LOD`, but `Cactus24`, `Cactus25`,
  `Cactus27`, `Rock03`, `Rock05` and `uacrwn` do not, under any casing. Those
  cover 115 of the map's 727 placements. The name is descriptive, not the
  lookup key: `DDECLIST +0x48` selects the SFT group which resolves the art.
- **The world size of a sprite is partly stated.** Neither the record nor the
  `DDECLIST` entry gives one, but the decoration name is usually an animation
  name, and the sprite frame table carries a per-sprite multiplier: `tree01` is
  2.1, `tree27` is 1.0, `ped01` is 0.7 (see [`dsft.md`](dsft.md)). That fixes
  the sizes relative to each other. The original passes that frame scale
  directly to billboard projection; an extra eye-calibrated factor is not a
  format field. `observed`

## Invalid-input behavior

The decoder rejects, deterministically and without reading out of bounds:

- a payload that cannot hold the model array or a model's geometry;
- a decoration count whose records or names would extend past the payload.

## Historical question status

> Audited in the [open-question register](../open-questions.md); the register
> supersedes unresolved hypotheses below.

All five questions are closed: `+0x4A` is the decoration flag word; the placed
record tail is direction and event state; art and scale come through the SFT
id; and the following region is the terrain/spatial index documented in
[`odm-tile-index.md`](odm-tile-index.md).
