---
title: "VID video container"
summary: "Directory layout and Smacker-stream joins for Might and Magic VI video archives."
doc_type: reference
status: verified
last_updated: 2026-08-01
tags:
  - mm6
  - vid
  - video
  - archive
---
# VID video container (Might and Magic VI)

Status: **verified.** The container holding MM6's cutscene and shop videos.
Each claim is tagged `observed`, `inferred`, or `unknown`.

## Scope

Covers the `.vid` archive directory. The videos inside it are Smacker streams,
documented in [`smacker.md`](smacker.md).

## Source provenance (non-expressive)

| Artifact | Value |
| --- | --- |
| `Anims/Anims1.vid` | 46,157,620 bytes, 40 entries `observed` |
| `Anims/Anims2.vid` | 208,887,084 bytes, 87 entries `observed` |
| Payload format | every one of the 127 entries begins `"SMK2"` `observed` |

Reproduce with:

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/play_smk --list
```

## Layout

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| 0x00 | 4 | u32 | count | observed | number of directory entries |
| 0x04 | count × 44 | Entry[] | directory | observed | |

### Entry (44 bytes)

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| +0x00 | 40 | char[40] | name | observed | NUL-padded; no extension |
| +0x28 | 4 | u32 | offset | observed | absolute file offset of the payload |

The directory stores no sizes: an entry runs from its offset to the next
entry's, and the last runs to the end of the file. `observed`

The arithmetic checks exactly on both archives — `4 + count × 44` equals the
first entry's offset (1,764 for 40 entries; 3,832 for 87), so the payloads
begin immediately after the directory with no padding. `observed`

Entry names carry no extension (`Bank`, `3dologo`, `Apthcmid`). Lookups are
case-insensitive, matching how the rest of the archives behave. `inferred`

## Invalid-input behavior

The reader rejects, deterministically and without reading out of bounds:

- a buffer too short to hold the count;
- a count of zero, or one whose directory would not fit in the file;
- an offset that lands inside the directory or past the end of the file;
- offsets that do not ascend, which would otherwise underflow a derived size.

Unlike the `.LOD` readers, this one keeps the file open and reads a single
entry on demand rather than holding the archive in memory: `Anims2.vid` is
208 MB and callers want one video at a time.

## Historical question status

> Audited in the [open-question register](../open-questions.md); the register
> supersedes unresolved hypotheses below.

- Whether any *other* MM6 build ships a `.vid` containing Bink rather than
  Smacker. In this GOG edition all 127 entries begin `"SMK2"` (no `"SMK4"`,
  no Bink) `observed`, so Bink is absent here; only other editions, which this
  install cannot speak for, could differ. `unknown` for those editions.

## The shop videos are keyed by trade and by quality

Of the 127 videos in the two archives, **31 fall into families with a quality
suffix** — `Por`/`Poor`, `Mid`, `Rch`/`Rich`, `Wch` — across thirteen trades:
`Apthc`, `Arm`, `Blcks`, `City`, `Genst`, `Mag`, `Magic`, `Merc`, `Orac`,
`Pyra`, `Tav`, `Temp`, `Thf`. `observed`

`2DEvents.txt` gives every establishment a `Picture` column, and for shops its
values are 1, 2 and 3 while houses carry 66 to 71 instead. `observed` So a
shop's backdrop is presumably its trade's video at the tier that column names,
and a house's is a specific picture. The mapping from a `Type` string to a
video family is `unknown` and has not been measured.

The rest of the names are dungeons (`D01`, `D02`, ...) and set pieces.

### The trade-to-family pairing, and what it does not cover

Reading the names against `2DEvents.txt`'s `Type` column pairs ten trades with
a family: weapon shop to `Blcks`, armour shop to `Arm`, magic shop to `Mag`,
general store to `Genst`, tavern to `Tav`, temple to `Temp`, town hall and city
council to `City`, thieves' guild to `Thf`, mercenary guild to `Merc`, oracle
and seer to `Orac`. `inferred`

Of the 97 shipped establishments of those trades, **82 resolve to a video the
archives actually hold**. The 15 that do not are families missing a tier or
spelling their stem differently: there is no `TavPoor`, no `OracMid`, no
`ThfMid`, and the magic shop's poor tier is `MagicPor` where its other two are
`MagMid` and `MagRch`. `observed`

That measurement used a stand-in for the quality tier. Reading
`2DEvents.txt`'s `Picture` column properly and taking it again gives
**14 of 97**, not 82 — so the stand-in was doing the work and the pairing of
that column to the videos' poor/middling/rich families is **wrong**.

The column's values for shops run 1 to 8 and beyond, not 1 to 3. `observed`
What selects a video's quality tier is `unknown`; it is not this column.
The trade-to-family pairing above still stands on the names, but which of a
family's three files a given shop shows does not.
