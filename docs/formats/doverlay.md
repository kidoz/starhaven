# Overlay table (`DOVERLAY.BIN`, Might and Magic VI)

Status: **decoded, layout verified.** A small lookup the engine addresses an
overlay by. Each claim is tagged `observed`, `inferred`, or `unknown`.

## Scope

Covers all of `DOVERLAY.BIN`: the record layout, the id distribution, and the
scale field. What the engine does with an overlay id is not established from
the data alone.

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
| +0x00 | 4 | u32 | id | observed | distinct on all 96; see below |
| +0x04 | 2 | u16 | scale | observed | stored raw; see below |
| +0x06 | 2 | u16 | — | observed | zero on all 96 records |

## The id

The ids are distinct across the table and run 10 to 11,220. They cluster in
thousands: 23 sit in 11000..11999, 16 in 10000..10999, 14 in 0..999, then
between 2 and 9 per thousand-block below that. That shape is consistent with a
family-and-member id (the high thousands a family, the low hundreds a member),
but nothing in this table joins it to another to confirm which. `observed` for
the values, `inferred` for the family reading.

## The scale

The raw u16 takes 34 distinct values. The common band is 486..536; read as
**8.8 fixed point** that is 1.90 to 2.09 — near a doubling — with a sparser
upper band at 631..726 (2.47..2.84). One record carries 0. `observed` for the
stored words; `inferred` for the fixed-point reading. A size multiplier for an
overlay sprite is the natural reading, and the doubling is what an "oversized
sprite" looks like, but the data does not name the unit.

## Invalid-input behavior

The parser rejects, deterministically and without reading out of bounds:

- an entry shorter than the 48-byte container header;
- a body that is not a zlib stream;
- a record count whose 8-byte records do not account for the inflated block
  exactly.

## Open questions

- What the `id` joins to — a sprite family and member is the shape, but no
  shipped table points here to confirm it. `unknown`
- Whether `scale` is genuinely 8.8 fixed point and what it scales. `inferred`
