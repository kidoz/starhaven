# Decoration frame table (`DIFT.BIN`, Might and Magic VI)

Status: **decoded, layout corrected and verified.** This is the frame table for
icon animations such as decoration glows. Each claim is tagged `observed`,
`inferred`, or `unknown`.

## Scope

Covers all of `DIFT.BIN`: the record layout, group timing, flags, and the join
from each icon name to `icons.lod`.

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product and edition | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Container | `data/icons.lod`, SHA-256 `2e8f2c0d…p813bd18` |
| Entry | `DIFT.BIN`, stored 272 bytes, inflating to 1,956 `observed` |
| Records | 61 in 7 groups `observed` |

## Container

The 48-byte-header + zlib container the other `D*.BIN` tables use (see
[`dtile.md`](dtile.md)). The decompressed block is a `u32` count (61) followed
by sixty-one 32-byte records; `4 + 61 × 32 = 1,956`, the declared length
exactly. `observed`

## Record (32 bytes)

| Offset | Size | Type | Field | Status | Notes |
| --- | ---: | --- | --- | --- | --- |
| +0x00 | 12 | char[12] | group_name | observed | set on a group's first frame; empty after |
| +0x0C | 12 | char[12] | icon_name | observed | a per-frame `icons.lod` reference |
| +0x18 | 2 | u16 | time | observed | frame duration in 1/16 second |
| +0x1A | 2 | u16 | total_time | observed | total duration of the group |
| +0x1C | 2 | u16 | flags | observed | bit 0: another frame; bit 2: group start |
| +0x1E | 2 | u16 | icon_index | observed | resolved by the loader; runtime field |

This is the icon counterpart of `DSFT.BIN`; unlike DSFT, its clock is 1/16
second. See [`dsft.md`](dsft.md).

## Groups

A record with flag `0x04` starts a group; bit `0x01` says another frame follows.
The non-empty group name on the first record provides its lookup key.

| Group | Frames | Sum of times |
| --- | ---: | ---: |
| *(ungrouped icon `pending`)* | 1 | 0 |
| `glow01` | 6 | 6 |
| `glow02` | 6 | 6 |
| `glow03` | 6 | 6 |
| `glow04` | 6 | 6 |
| `glow05` | 6 | 6 |
| `fire` | 30 | 30 |

The old `frame_count` interpretation was a shifted parse: `+0x1A` is group
time and `+0x1C` is the flags word. `observed`

## The sprite names

The frame names — `glow01a`..`glow01f`, `fr1`..`fr30` — are icon resource
names. They resolve through `icons.lod`; searching only `SPRITES.LOD` and
`BITMAPS.LOD` created the earlier false open question. `observed`

## Invalid-input behavior

The parser rejects, deterministically and without reading out of bounds:

- an entry shorter than the 48-byte container header;
- a body that is not a zlib stream;
- a record count whose 32-byte records do not account for the inflated block
  exactly.

## Historical question status

> Audited in the [open-question register](../open-questions.md); the register
> supersedes unresolved hypotheses below.

Both questions are closed by the corrected layout: names select `icons.lod`
resources, and the flags are the group-start/continuation bits at `+0x1C`.
