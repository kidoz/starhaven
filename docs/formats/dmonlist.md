---
title: "Monster table (DMONLIST.BIN)"
summary: "Record layout and actor, sprite, and sound joins for the Might and Magic VI monster table."
doc_type: reference
status: verified
last_updated: 2026-08-01
tags:
  - mm6
  - dmonlist
  - monsters
  - binary-table
---
# Monster table (`DMONLIST.BIN`, Might and Magic VI)

Status: **verified.** The table an actor's monster id
indexes, giving each monster its name and its animation sprite base names.
Each claim is tagged `observed`, `inferred`, or `unknown`.

## Scope

Covers the container, the record's name field, default velocity, the eight
animation sprite names, body sizes, and four sound ids.

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product and edition | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Entry | `DMONLIST.BIN` in `icons.lod`, stored 5,221 bytes, container-uncompressed `observed` |
| Records | 173 of 148 bytes `observed` |

Reproduce with:

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/ddm_info outb2.ddm   # prints each actor's monster id
```

## Container

The same shape as `DTILE.BIN` (see [`dtile.md`](dtile.md)): a 48-byte header,
then a zlib stream that inflates to a `u32` record count followed by that many
fixed records. `173 × 148 + 4 = 25,608` matches the inflated length exactly,
which is what pins the record size. `observed`

## Record (148 bytes)

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| +0x00 | 2 | u16 | height | observed | 56..487; see below |
| +0x02 | 2 | u16 | radius | observed | 40..238 |
| +0x04 | 2 | u16 | default velocity | observed | 140 on all 173; actor preparation normally replaces it from `MONSTERS.TXT` |
| +0x06 | 2 | — | reserved | observed | unused in MM6 and zero on all 173 |
| +0x08 | 8 | u16[4] | sound ids | observed | attack, die, charge, fidget; see below |
| +0x10 | 32 | char[32] | name | observed | e.g. `"ArcherA"`, `"PeasantF1B"` |
| +0x30 | 80 | char[10][8] | animation sprites | observed | eight base names |
| +0x80 | 20 | — | zero | observed | zero on all 173; runtime scratch, like the frame table's |

### The four sound ids

`DSOUNDS.BIN`'s monster block runs `1000 + 10k` with the action as the
offset — attack, die, charge, fidget (see [`dsounds.md`](dsounds.md)) — and
its names carry the monster's own table id, `"49elemairA_attack"`. For every
one of the **31 monsters whose named set pins the answer, the u16 at +0x08
equals that set's base id — 31 of 31 — and across all 173 records the field
is a monster-block id**, so the monsters without a set of their own share
another's, the way B and C variants share A's palette-swapped art. No other
offset in the record matches even once. `observed` Reproduce with
`sft_info --sounds`. The engine plays them: a monster's swing is its attack
sound, its death its dying one.

The field was first read as one base id with the action as arithmetic, and
the record refutes that: it states **four u16 ids outright** at +0x08, and
the three Guards' quads run `1260 1261 1262 1264` — their fidget skips an
id no base could reach. 170 of 173 quads are consecutive, which is why the
arithmetic reading held as long as it did. `observed` Reproduce with
`sft_info --body`.

### The body at the record's front

The first three u16s are the monster's **height, radius, and default velocity
in world units**. Bats (56) and rats (59) sit at the bottom of the height range, Titans
(474) and Dragons (487) at the top, and spiders wider than they are tall
(64 by 109). `observed` for the values; that the body units are the world's is
`inferred` from their magnitudes against the maps' own geometry. The default
velocity at +0x04 is **140 on every record** and is normally replaced from
`MONSTERS.TXT` when an actor is prepared. +0x06 and the 20 bytes at +0x80 are
zero throughout, which is what the frame table's zeroed runtime fields look like. Reproduce with
`sft_info --body`.

### Animation slots

The eight 10-byte names at `+0x30`, in order: stand, walk, attack, attack
again, wince, death, defend, fidget. `inferred` — the ordering is read from
the names themselves (`arc1sta`, `arc1wka`, `arc1atk`, …), and the two attack
slots hold the same string in every record observed. `observed`

### Sprite names carry a view digit

A base name is completed with a view direction 0–4: `pfemsta` becomes
`PFEMSTA0` … `PFEMSTA4` in `SPRITES.LOD`, matched case-insensitively.
`observed`

## How actors reach this table

The 548-byte actor record holds the monster id as a `u8` at **+0x34** (see
[`event-actors.md`](event-actors.md)). That offset was found by requiring the
field to be a valid table index for all 266 actors *and* to select a monster
whose name matches the actor's own: **+0x34 is the only field satisfying both,
at 266/266**. `observed`

The ids actually used are 121, 122, 123, 133, 134 and 135 — six values, all
inside the contiguous `Peasant*` block at 120–143, none outside it. A wrong
field would scatter across all 173. `observed`

## The A/B/C variants share one picture

Monsters come in A/B/C triples (`ArcherA`/`ArcherB`/`ArcherC`). Completing a
stand animation name with a blind view digit finds art for **only 31 of the
173**: `arc1sta0` is present, `arc2sta0` and `arc3sta0` are not. `observed`

That is a defect in the guess, not a gap in the data. These names are
**animations**, and `DSFT.BIN` resolves them — going through the sprite frame
table finds art for **173 of 173** (see [`dsft.md`](dsft.md)). It also settles
the recolouring: `arc1sta`, `arc2sta` and `arc3sta` name the same picture,
`ARC1STA0`, with palettes 150, 151 and 152, and all three palettes ship in
`BITMAPS.LOD`. B and C are palette swaps of A, exactly as suspected, and the
mechanism is a field in the frame table rather than anything in this record.
`observed`

What the variants *are* is no longer open: `MONSTERS.TXT` holds one row per
variant, and text row *N* names binary record *N−1* across all 173 (see
[`text-tables.md`](text-tables.md)). `ArcherA` is a level-9 Archer with 35 hit
points; `ArcherC` is a level-29 Fire Archer with 171, a fire attack and a
Fireball spell. The three records being byte-identical here apart from their
names is therefore expected — everything that distinguishes them lives in the
text table. `observed`

An earlier revision of this document concluded that 84 monsters had no art in
this install. That was wrong, and wrong in an instructive way: the sprites were
there under names the guess could not construct. Nothing was missing except the
table that names them.

## Invalid-input behavior

The parser rejects, deterministically and without reading out of bounds:

- an entry too small for the 48-byte header;
- a body that is not a zlib stream;
- a record count that does not account for the inflated block exactly.

## The engine stands on the body

The party keeps a body's radius away and cannot walk through a titan,
aiming is a ray against the body's own cylinder — a dragon hard to miss, a
bat hard to hit — and a flier rides one body height above the ground. The
readings of radius as personal space and height as the ride are the
engine's. `inferred`

## Historical question status

> Audited in the [open-question register](../open-questions.md); the register
> supersedes unresolved hypotheses below.

The 140 word is a serialized default velocity. The loader can seed an actor
from it, while normal monster preparation replaces velocity from the varying
`MONSTERS.TXT` `Spd` value. A shared default therefore does not contradict the
per-monster movement speed.

## The eight animations are used, not just listed

Each record names eight animation groups — Stand, Walk, two Attacks, Wince,
Death, Defend and Fidget. **1,382 of the 1,384** names resolve to a group in
`DSFT.BIN`. `observed` A monster's variants share the A variant's art, so a
name that resolves for none of a triple falls back to the first of the three;
see [`dsft.md`](dsft.md).

Reproduce with `sft_info --check`.
