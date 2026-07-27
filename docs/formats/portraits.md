# Character portraits and paperdolls (Might and Magic VI)

Status: **verified** for the naming and the sizes. Each claim is tagged
`observed`, `inferred`, or `unknown`.

## Scope

Covers where the twelve character faces live in `icons.lod` and how they are
named. The bitmap format itself is [`bitmap.md`](bitmap.md); these are ordinary
entries in it.

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product and edition | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Archive | `data/icons.lod`, 2,703 entries |
| Portraits | 12 families x 53 frames = 636 entries `observed` |

Reproduce with:

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/lod_browser list "$STARHAVEN_GAME_DIR/data/icons.lod" | grep -i "^male\|^girl"
./buildDir/starhaven OutA1.Odm --sheet 1     # draws one
```

## The twelve faces

The families are `MaleA` through `MaleH` and `GirlA` through `GirlD` — eight
and four — and each has exactly **53** frames, numbered `01` to `53` with a
leading zero. The archive spells the families inconsistently (`MALEA01`,
`maleH01`), which the reader folds. `observed`

Every one of the 636 is **59 x 79** pixels and satisfies the bitmap header's
own `size == width * height`. `observed`

What the 53 frames are is `unknown`. A character's face in the original changes
with mood and condition, so they are presumably expressions, and frame 1 is the
neutral one this engine draws. `inferred`

## Paperdolls

The same archive holds the body art the inventory screen dresses:
`pl1bod`, `pl1arm1`, `pl1arm2` and `pl1icon` per character slot, twelve
`BODY000`-`BODY011`, a `BACKDOLL` and `backhand`, and the armour pieces
(`chn1arm1`, `chn2arm1`, and so on). `pl1bod` is 120 x 198 and `pl1icon`
140 x 198. `observed`

How a slot's parts compose, and which piece an equipped item selects, is
`unknown` — nothing is decoded here beyond the entries existing.

## Open questions

- What the 53 frames of each face are. `unknown`
- How the paperdoll parts layer, and what indexes them. `unknown`
- Whether the portrait a character has is stored anywhere but a save file.
  `unknown`
