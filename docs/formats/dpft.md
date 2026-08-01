---
title: "Portrait frame table (DPFT.BIN)"
summary: "Record layout, timing, and group semantics for Might and Magic VI portrait animation frames."
doc_type: reference
status: verified
last_updated: 2026-08-01
tags:
  - mm6
  - dpft
  - portraits
  - binary-table
---
# Portrait frame table (`DPFT.BIN`, Might and Magic VI)

Status: **decoded, layout corrected and verified.** Sixty-seven portrait
animation frame rows. Each claim is tagged `observed`, `inferred`, or
`unknown`.

## Scope

Covers all of `DPFT.BIN`: group ids, portrait-frame indices, timing, and group
flags. [`portraits.md`](portraits.md) covers the expressions those indices draw.

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product and edition | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Container | `data/icons.lod`, SHA-256 `2e8f2c0d…p813bd18` |
| Entry | `DPFT.BIN`, stored 264 bytes, inflating to 674 `observed` |
| Records | 67 `observed` |

## Container

The 48-byte-header + zlib container the other `D*.BIN` tables use (see
[`dtile.md`](dtile.md)). The decompressed block is a `u32` count (67) followed
by sixty-seven 10-byte records; `4 + 67 × 10 = 674`, the declared length
exactly. `observed`

## Record (10 bytes)

Five little-endian `u16`:

| Offset | Size | Field | Values | Status |
| --- | ---: | --- | --- | --- |
| +0x00 | 2 | group_id | 0..57; animation/expression group | observed |
| +0x02 | 2 | frame_index | 1..53; portrait texture/expression index | observed |
| +0x04 | 2 | time | frame duration in 1/32 second | observed |
| +0x06 | 2 | total_time | total duration of the group | observed |
| +0x08 | 2 | flags | bit 0: another frame; bit 2: group start | observed |

The eleven `group_id = 0` rows are ungrouped/sentinel frames. The former
`width`, `height`, and `count` names were not pixel measurements; their
power-of-two-looking values are animation times and flags. `observed`

## Invalid-input behavior

The parser rejects, deterministically and without reading out of bounds:

- an entry shorter than the 48-byte container header;
- a body that is not a zlib stream;
- a record count whose 10-byte records do not account for the inflated block
  exactly.

## Historical question status

> Audited in the [open-question register](../open-questions.md); the register
> supersedes unresolved hypotheses below.

All three questions are closed by the corrected frame-table layout above.
