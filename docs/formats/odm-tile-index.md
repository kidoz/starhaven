# ODM tile index and spawn points (Might and Magic VI)

Status: **verified.** The last two sections of an outdoor map: a list, per
terrain tile, of what stands near it, and the places the map puts monsters.
Each claim is tagged `observed`, `inferred`, or `unknown`.

## Scope

Covers everything after the decoration array, which is where every outdoor
payload ends. Together with
[`odm-decorations.md`](odm-decorations.md) and
[`odm-model-facets.md`](odm-model-facets.md) this accounts for the whole `.odm`
payload, byte for byte, on all fifteen maps.

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product and edition | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Map data | `data/Games.lod`, SHA-256 `28103a220212a0abed63f30d34d248203dbc78ec89b99629f631a65370964975` |
| Maps verified | all 15 `.odm` entries `observed` |
| Region size | 102,888 to 116,610 bytes `observed` |

Reproduce with:

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/odm_info Outa1.odm           # reports both sections
./buildDir/odm_info Outa1.odm --index   # checks the index against its rule
```

## Layout

The region begins immediately after the decoration array, whose end is
computable, and runs to the end of the payload.

| Order | Contents | Size |
| --- | --- | --- |
| 1 | `u32` entry count | 4 |
| 2 | `u16` entries | 2 × count |
| 3 | `u32` tile starts | 4 × 128 × 128 |
| 4 | `u32` spawn point count | 4 |
| 5 | spawn point records | 20 × count |

**All 15 maps are consumed exactly** by this model: no bytes left over, none
missing. `observed`

The dominant term is fixed — 65,536 bytes of tile starts on every map,
regardless of size — which is why the region varies by only 13% across maps
whose geometry differs roughly tenfold, and why a model of count-prefixed
sections could never describe it.

## The tile index

`starts[y * 128 + x]` is where tile *(x, y)*'s run begins in the entry array.
The run ends where the next tile's begins, and the last runs to the end.

Every run ends with a **zero terminator**, including the empty ones:
**245,760 of 245,760** tile runs across the fifteen maps hold exactly one zero
and it is the last element. `observed` A reader that treats the terminator as
an entry finds decoration 0 on every tile of the map.

### Entries are an id and a type

Each non-zero entry packs a type in its low three bits and an id above them:

```
id   = entry >> 3
type = entry & 7
```

Every non-zero entry in every shipped outdoor map has **type 5**, and the ids
are exactly `0 .. decoration_count - 1` — 6,210 decorations across the fifteen
maps, each one referenced. `observed` What the other seven type values mean is
`unknown`; nothing outdoors uses them.

### What a tile lists

A decoration is listed against tile *(x, y)* exactly when the distance from
the decoration to that **tile's centre** is at most **1024 units**, two tiles.
This reproduces the shipped index exactly: **6,210 of 6,210** footprints, with
no tile added and none missing. `observed`

The tile grid is centred on the origin, 512 units to a tile, and its rows run
against world y:

```
tile_x = floor(world_x / 512) + 64
tile_y = 64 - floor(world_y / 512)
```

The resulting footprint is a disc, not a square — it is a distance test, so the
corners of the 4×4 block a decoration usually spans are cut. Only 396 of the
6,210 footprints are full rectangles.

## Spawn points

Twenty bytes each:

| Offset | Type | Field |
| --- | --- | --- |
| 0x00 | `i32` | x |
| 0x04 | `i32` | y |
| 0x08 | `i32` | z |
| 0x0C | `u16` | radius |
| 0x0E | `u16` | kind |
| 0x10 | `u32` | index |

848 across the fifteen maps, 20 to 93 per map. `observed`

- Every one lies inside the map's own 128-tile square. `observed`
- **kind** is 3 on all 848. `observed` It is the same three-bit type the index
  entries carry, where 5 is a decoration; 3 is presumably the actor type.
  `inferred`
- **radius** is 32 on 777 and 0 on the remaining 71. `observed`
- **z** is 0 on 793 of the 848, and the 55 non-zero ones sit at heights the
  terrain does not explain, so the field is a height that is usually left for
  the ground to supply. `inferred`
- **index** runs 1 to 12, with 1, 2 and 3 covering 808 of the 848. What it
  selects — which monsters appear, or how hard they are — is `unknown`.

## Invalid-input behavior

The reader rejects, deterministically and without reading out of bounds:

- a declared entry count of zero, or one whose array runs past the payload;
- a tile start at or past the entry count, which would make that tile's run
  unreadable;
- a spawn point array longer than the bytes that remain.

A tile outside the 128×128 grid is answered with an empty run rather than an
error: positions at the map's edge ask about neighbours that do not exist.

## Open questions

- What the spawn `index` selects. `unknown`
- Whether indoor maps carry an equivalent index; the `.blv` tail remains
  undescribed (see [`blv.md`](blv.md)). `unknown`
- The six unused entry types. `unknown`
