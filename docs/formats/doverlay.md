# Overlay table (`DOVERLAY.BIN`, Might and Magic VI)

Status: **decoded, layout corrected and verified.** A small lookup from an
overlay id and type to a sprite-frame group. Each claim is tagged `observed`,
`inferred`, or `unknown`.

## Scope

Covers all of `DOVERLAY.BIN`: the record layout and the join to `DSFT.BIN`.

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product and edition | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Container | `data/icons.lod`, SHA-256 `2e8f2c0d…p813bd18` |
| Entry | `DOVERLAY.BIN`, stored 394 bytes, inflating to 772 `observed` |
| Records | 96 `observed` |

## Container

The 48-byte-header + zlib container the other `D*.BIN` tables use (see
[`dtile.md`](dtile.md)). The decompressed block is a `u32` count (96) followed
by ninety-six 8-byte records; `4 + 96 × 8 = 772`, the declared length exactly.
`observed`

## Record (8 bytes)

| Offset | Size | Type | Field | Status | Notes |
| --- | ---: | --- | --- | --- | --- |
| +0x00 | 2 | i16 | overlay_id | observed | overlay lookup key |
| +0x02 | 2 | i16 | overlay_type | observed | overlay behavior/type |
| +0x04 | 2 | i16 | sft_index | observed | sprite-frame group/index |
| +0x06 | 2 | i16 | sft_group | observed | loader-resolved group/runtime field |

## The id

The earlier `u32 id` combined the adjacent `overlay_id` and `overlay_type`
words, producing the misleading thousands clusters. Consumers address the
overlay id/type and resolve `sft_index` through the sprite frame table.
`observed`

## The former scale reading

There is no scale field. The values formerly read as 8.8 fixed point are SFT
indices, which explains why they occupy the same numeric band as sprite-frame
groups. `observed`

## Invalid-input behavior

The parser rejects, deterministically and without reading out of bounds:

- an entry shorter than the 48-byte container header;
- a body that is not a zlib stream;
- a record count whose 8-byte records do not account for the inflated block
  exactly.

## Historical question status

> Audited in the [open-question register](../open-questions.md); the register
> supersedes unresolved hypotheses below.

Both questions are closed by splitting the four serialized `i16` fields at
their actual boundaries.
