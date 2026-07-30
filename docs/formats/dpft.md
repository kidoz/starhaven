# Portrait frame table (`DPFT.BIN`, Might and Magic VI)

Status: **decoded, layout verified.** Sixty-seven rows of five small numbers,
with no names to lean on. Each claim is tagged `observed`, `inferred`, or
`unknown`.

## Scope

Covers all of `DPFT.BIN`: the record layout and the value distribution of each
field. The table carries no names, so the join to portrait art is `inferred`
from the values' shape rather than read from a reference.

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
| +0x00 | 2 | id | 0..57; 56 distinct | observed |
| +0x02 | 2 | cell_id | 1..53; tracks id | observed |
| +0x04 | 2 | width | 1, 2, 4, 8 or 16 | observed |
| +0x06 | 2 | height | 0, 1, 2, 4, 8, 16 or 26 | observed |
| +0x08 | 2 | count | 0, 1, 4 or 5 | observed |

`width` and `height` take only powers of two (with `height` adding 0 and 26),
which reads as a size in pixels on a portrait grid; that reading is `inferred`,
not stated by the data.

### Two shapes of row

Fifty-six rows carry a nonzero `id` (1..57) and a full width/height, and read
as portrait cells of the named size. The eleven rows with `id = 0` form a
distinct tail of small records — `cell_id` 1, 25..28, widths 2 or 4, heights 0
— which reads as a sub-table the `id` distinguishes from the portraits. What
those zero-id rows select is `unknown`.

## Invalid-input behavior

The parser rejects, deterministically and without reading out of bounds:

- an entry shorter than the 48-byte container header;
- a body that is not a zlib stream;
- a record count whose 10-byte records do not account for the inflated block
  exactly.

## Open questions

- What `id` joins to — the nameless rows prevent a direct read. `unknown`
- The eleven `id = 0` rows and what they select. `unknown`
- Whether `width`/`height` are genuinely pixel sizes. `inferred`
