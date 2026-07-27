# Sprite frame table (`DSFT.BIN`, Might and Magic VI)

Status: **verified.** The table that turns an animation name into the sprites
to draw, in order, at the right size and in the right colours. Each claim is
tagged `observed`, `inferred`, or `unknown`.

## Scope

Covers the whole of `DSFT.BIN`: the frame array, the alphabetical lookup, the
flag bits, and the joins to `DMONLIST.BIN`, `DDECLIST.BIN` and `SPRITES.LOD`.

This resolves two questions other specs left open:

- how a monster's B and C variants are recoloured, which
  [`dmonlist.md`](dmonlist.md) recorded as `unknown`;
- the relative size of a decoration sprite, which
  [`odm-decorations.md`](odm-decorations.md) recorded as `unknown`.

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product and edition | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Entry | `DSFT.BIN` in `data/icons.lod`, stored 37,237 bytes, inflating to 364,800 `observed` |
| Frames | 6,455 in 1,656 groups `observed` |

Reproduce with:

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/sft_info --check
./buildDir/sft_info arc1wka
./buildDir/sft_info Torch01
```

A stray developer log left in the same archive names the loader:
`errorlog.txt` reads `CSpriteFrameTable::load - Unable to open file: sft.def.`
`observed`

## Layout

The same 48-byte-header + zlib container `DTILE.BIN` uses (see
[`dtile.md`](dtile.md)). The decompressed block is:

| Order | Size | Contents |
| --- | --- | --- |
| 1 | 4 | `u32` frame count — 6,455 |
| 2 | 4 | `u32` group count — 1,656 |
| 3 | 56 × frames | the frame array |
| 4 | 2 × groups | an alphabetical lookup |

`8 + 6455 × 56 + 1656 × 2 = 364,800`, the declared length exactly. `observed`

### Frame (56 bytes)

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| +0x00 | 12 | char[12] | groupName | observed | set on a group's **first frame only** |
| +0x0C | 12 | char[12] | spriteName | observed | a SPRITES.LOD entry, or its base |
| +0x18 | 16 | — | runtime | observed | zero in all 6,455 records; the engine fills it in |
| +0x28 | 4 | i32 | scale | observed | 16.16 fixed point |
| +0x2C | 4 | u32 | flags | observed | see below |
| +0x30 | 2 | u16 | paletteId | observed | the palette to draw through |
| +0x32 | 2 | — | zero | observed | zero in all 6,455 records |
| +0x34 | 2 | u16 | duration | observed | how long this frame shows |
| +0x36 | 2 | u16 | groupLength | observed | total, first frame only |

### The lookup

1,656 `u16` frame indices — one per group, in **case-insensitive alphabetical
order of group name**. Every one of the 1,656 matches the group segmentation
derived independently from the names: an exact cross-check that the frame array
was split correctly. `observed`

StarHaven does not need it (it builds a map while parsing) but keeps it,
because it is what proves the segmentation.

## Groups

A frame with a non-empty `groupName` starts an animation; the frames after it,
up to the next named frame, belong to it.

Three facts make this self-checking, and all three hold on every record:

- **`groupLength` is the sum of the group's `duration` values** — 1,656 of
  1,656 groups. `observed`
- **Flag 0x4 is set on exactly the frames that carry a group name** — 6,455 of
  6,455. `observed`
- **Flag 0x1 is set on exactly the frames that are not their group's last** —
  4,799 of 4,799. `observed`

Five names are used twice: `light07`, `3CLAW`, `stlmite1`, `stlmite2`,
`stlmite3`. Four of the five pairs are identical; `light07` is not — one copy
is a one-frame `null` and the other a six-frame projectile. A lookup by name
can only reach one of each pair. `observed`

## Flags

| Bits | Meaning | Status |
| --- | --- | --- |
| 0x0001 | another frame of this group follows | observed |
| 0x0004 | this frame starts a group | observed |
| 0x0010 | single view: `spriteName` is the entry | observed |
| 0xE000 | five view directions: `spriteName` is a base | observed |
| others | | unknown |

0x0010 and 0xE000 are **mutually exclusive** — no frame sets both — and between
them cover 6,445 of the 6,455 frames. The ten exceptions are two duplicate
copies of one projectile group, which set 0x0020 instead. `observed`

### View directions

The rule is exact, and it is the reason a naive reader cannot find the art:

- 0xE000 clear: `spriteName` is the SPRITES.LOD entry. All 5,237 such names
  that have art resolve directly.
- 0xE000 set: `spriteName` is a **base**, and a digit 0..4 completes it. The
  plain name is present for **none** of the 1,164 directional frames, while
  `name0`..`name4` is present for 1,153 of them. `observed`

Guessing instead of reading this flag is expensive. Completing every monster's
stand animation with a blind `"0"` finds art for **31 of 173** monsters; going
through the frame table finds it for **173 of 173**. `observed`

## Scale

16.16 fixed point, and a size multiplier rather than a world size: 1.0 is the
commonest value, trees run to 2.1, pedestals down to 0.7. It is constant within
a group in 1,637 of 1,651 cases. `observed`

This gives correct sizes *relative* to each other. The absolute factor is still
not stated anywhere and remains a renderer calibration — see
[`odm-decorations.md`](odm-decorations.md). `inferred`

## Palette: how the A/B/C variants are recoloured

`+0x30` is the palette to draw the frame through, and it **overrides the
sprite's own header palette**.

That is what makes the monster variants work, and the evidence is direct:

| Animation | Sprite drawn | Palette |
| --- | --- | --- |
| `arc1sta` (Archer) | `ARC1STA0` | 150 |
| `arc2sta` (Master Archer) | `ARC1STA0` | 151 |
| `arc3sta` (Fire Archer) | `ARC1STA0` | 152 |
| `min1sta` (Minotaur) | `MIN1WKc` | 255 |
| `min2sta` | `MIN1WKc` | 256 |
| `min3sta` | `MIN1WKc` | 257 |

The three variants name **the same picture** and differ only in palette;
`ARC1STA0`'s own header says 150, the A variant's value; and `Pal150`, `Pal151`
and `Pal152` all exist in `BITMAPS.LOD`. `observed`

This corrects the earlier reading in [`dmonlist.md`](dmonlist.md), which
concluded that this install's `SPRITES.LOD` lacked the B and C art. It does
not: there was never separate art to lack.

Across the whole table the field equals the sprite's own header palette in
2,222 of 6,405 resolvable frames — the A variants and the non-variant sprites —
and names a different palette for the rest. A renderer must therefore prefer
the frame's value, which is why `AssetCache::sprite` takes a palette override.
`observed`

## Time

`duration` and `groupLength` count in a unit the file does not name. StarHaven
uses **15 units per second**, which puts a torch's ten-tick flicker at two
thirds of a second and an archer's eighteen-tick walk cycle at a little over
one. That is a calibration by eye, not a fact from the data. `inferred`

## The joins

| From | To | Result | Status |
| --- | --- | --- | --- |
| `DMONLIST.BIN` animation names | groups | 1,382 of 1,384 | observed |
| `DDECLIST.BIN` decoration names | groups | 149 of 230 | observed |
| frames | `SPRITES.LOD` | 6,405 of 6,455 resolve | observed |

The two `DMONLIST` misses are `seasdead` and `secsdead`, the Sea Serpent death
animations. The 81 `DDECLIST` names with no group are drawable as plain sprite
names, and overlap the decoration art this install is missing (see
[`odm-decorations.md`](odm-decorations.md)). `observed`

## Invalid-input behavior

The parser rejects, deterministically and without reading out of bounds:

- an entry too small for the 48-byte container header;
- a body that is not a zlib stream;
- counts that do not account for the inflated block exactly;
- a lookup index pointing past the last frame.

A group whose length is zero is treated as a still image rather than divided
by, so a hand-made table cannot make the animation code fault.

## The five views are relative to the viewer

A front or back view of a two-legged, two-armed creature is close to left-right
symmetric; a profile is not. Measuring that over all **1,153** directional
frames that have their five views answers the question without looking at a
picture:

| View | Mean symmetry |
| --- | ---: |
| 0 | 0.818 |
| 1 | 0.631 |
| 2 | 0.564 |
| 3 | 0.631 |
| 4 | 0.813 |

The curve is symmetric about view 2 and views 1 and 3 agree to three decimal
places. That is what relative angles look like: view 0 faces the viewer, view 4
faces away, view 2 is the profile, and views 1 and 3 are the two half-turns
between — the same two angles seen from opposite sides. Compass headings would
have no reason to pair up this way. `observed`

Five views therefore cover half a circle, and the other half is the same five
**mirrored**. Which side is mirrored is `unknown` — nothing in the table says,
and a sprite drawn on the wrong side is not something the data can contradict.

Reproduce with `sft_info --views`.

## Open questions

- The flag bits outside 0x1, 0x4, 0x10 and 0xE000 — including 0x0020, and the
  high bits that appear on 933 frames. `unknown`
- The real length of a time unit. `inferred`
- The sibling tables in the same archive that share this container:
  `DOBJLIST.BIN` (232 × 52), `DCHEST.BIN` (8 × 36), `DIFT.BIN` (61 × 32),
  `DPFT.BIN` (67 × 10), `DTFT.BIN` (19 × 20), `DOVERLAY.BIN` (96 × 8) and
  `DSOUNDS.BIN` (1,355 × 112). Each is a clean count-plus-fixed-stride array;
  none is decoded. `unknown`
