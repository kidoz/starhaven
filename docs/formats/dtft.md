# Texture frame table (`DTFT.BIN`, Might and Magic VI)

Status: **decoded and in use.** The wall textures that move. Each claim is
tagged `observed`, `inferred`, or `unknown`.

## Layout

The same 48-byte-header + zlib container the other `D*.BIN` tables use. The
decompressed block is a `u32` count (19) then 19 records of 20 bytes:

| Offset | Size | Type | Field | Status |
| --- | --- | --- | --- | --- |
| +0x00 | 12 | char[12] | texture name, a `BITMAPS.LOD` entry | observed |
| +0x0C | 2 | — | zero | observed |
| +0x0E | 2 | u16 | duration, in the frame tables' shared unit | observed |
| +0x10 | 2 | u16 | group total, on a group's first frame | observed |
| +0x12 | 2 | u16 | flags: bit 1 starts a group, bit 0 says another follows | observed |

The shape mirrors `DSFT.BIN` exactly — name, duration, total-on-first,
start and continue flags — and all 19 records verify against it: every
group's total is the sum of its durations. `observed` Reproduce with
`sft_info --dtft`.

## What the nineteen records hold

Four groups: a lone `null`; a six-frame alternation of `mossrk_2` and
`woodtl_1` at lengthening durations (8, 8, 16, 16, 32, 32); and the two
six-frame painting loops `john01`..`john06` and `paladn01`..`paladn06` —
the haunted portraits. StarHaven steps them on the sprite tables' shared
clock and shows each group's current frame under its first frame's name.

## What it does not hold: the water

No shipped table animates the ground water. `WtrTyl` is a single 128×128
entry with no frame run beside it, `DTFT.BIN` names only the walls above,
and `DTILE.BIN`'s water rows carry attribute bit 2 with nothing measured
that joins it to motion. The original's shimmer is most plausibly palette
rotation with ranges held in the executable; which indices the *original*
rotates is `unknown`. StarHaven measures the ring itself instead: each
water tile's own palette carries one long blue-dominant run (`WtrTyl`'s
spans 182..254 and covers four-fifths of the tile's pixels), and the
engine rotates that detected run a step at a time, re-baking the texture
— the detection is the palette's, the cadence the engine's. `inferred`
