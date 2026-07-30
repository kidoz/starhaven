# Chest appearance table (`DCHEST.BIN`, Might and Magic VI)

Status: **verified.** The eight chest graphics. Each claim is tagged `observed`,
`inferred`, or `unknown`.

## Scope

Covers all of `DCHEST.BIN`: the name and sprite-object join, and how a placed
chest reaches its art through [`DOBJLIST.BIN`](dobjlist.md).

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product and edition | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Container | `data/icons.lod`, SHA-256 `2e8f2c0d…b813bd18` |
| Entry | `DCHEST.BIN`, stored 135 bytes, inflating to 292 `observed` |
| Records | 8 `observed` |

Reproduce against your own install with:

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
# the parser is exercised by tests/test_chest_table.cpp on synthetic input
```

## Container

The 48-byte-header + zlib container the other `D*.BIN` tables use (see
[`dtile.md`](dtile.md)). The decompressed block is a `u32` count (8) followed by
eight 36-byte records; `4 + 8 × 36 = 292`, the declared length exactly.
`observed`

## Record (36 bytes)

| Offset | Size | Type | Field | Status | Notes |
| --- | ---: | --- | --- | --- | --- |
| +0x00 | 32 | char[32] | name | observed | display name |
| +0x20 | 2 | u16 | frame_index | observed | constant `0x0a0e` (2574) on all eight records |
| +0x22 | 2 | u16 | object_id | observed | 1..8, a row of `DOBJLIST.BIN` |

The constant `frame_index` reads as a `DSFT.BIN` frame index (it is below the
6,455-frame table), but the table never varies it, and what selecting it would
draw is not established; the engine reaches the art through `object_id`.
`observed` for the value, `unknown` for a use.

## The eight rows

| Index | Name | object_id |
| ---: | --- | ---: |
| 0 | `wooden chest` | 1 |
| 1 | `chest02` | 2 |
| 2 | `sack` | 3 |
| 3 | `chest04` | 4 |
| 4 | `chest05` | 5 |
| 5 | `chest06` | 6 |
| 6 | `chest07` | 7 |
| 7 | `metal chest` | 8 |

The object ids 1..8 are each a row of [`DOBJLIST.BIN`](dobjlist.md): every
value is present in that table's `object_id` column. `observed`

## Invalid-input behavior

The parser rejects, deterministically and without reading out of bounds:

- an entry shorter than the 48-byte container header;
- a body that is not a zlib stream;
- a record count whose 36-byte records do not account for the inflated block
  exactly.

## Open questions

- What the constant `frame_index` `0x0a0e` selects, if anything — the engine
  draws the chest through `object_id`. `unknown`
