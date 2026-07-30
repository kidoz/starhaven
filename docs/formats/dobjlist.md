# Object descriptor table (`DOBJLIST.BIN`)

Status: **decoded, evidence-backed.** `DOBJLIST.BIN` is the global descriptor
table for loot sprites, projectiles, spell effects, and other sprite objects.
Each claim is tagged `observed`, `inferred`, or `unknown`.

## Container

The `icons.lod` entry uses a 48-byte archive-entry header followed by zlib
data. The inflated block is:

```text
u32 count
ObjectDescriptor records[count]  // 52 bytes each
```

In the examined table, `count = 232` and `4 + 232 × 52 = 12068`, accounting
for the inflated block exactly. `observed`

## Record

| Offset | Size | Type | Field | Status |
| --- | ---: | --- | --- | --- |
| `+0x00` | 32 | char[] | name | observed |
| `+0x20` | 2 | u16 | object id | observed |
| `+0x22` | 2 | u16 | radius | inferred |
| `+0x24` | 2 | u16 | height | inferred |
| `+0x26` | 2 | u16 | flags | observed | see the flag bits below |
| `+0x28` | 2 | u16 | DSFT frame index | observed |
| `+0x2A` | 2 | u16 | lifetime | inferred |
| `+0x2C` | 2 | u16 | — | observed | zero in all 232 records |
| `+0x2E` | 2 | u16 | speed | inferred |
| `+0x30` | 3 | u8[3] | trail RGB | observed | nonzero on 64 of 232 records |
| `+0x33` | 1 | u8 | padding | observed |

Every nonzero frame index at `+0x28` selects the first frame of a
`DSFT.BIN` animation group. `observed`

## Flag bits

The flags at `+0x26` are sparse — 112 of 232 records carry 0 — and the union of
every set bit is `0x07fc`. The values that occur, with their counts:

| Flags | Count |
| ---: | ---: |
| `0x0000` | 112 |
| `0x0010` | 1 |
| `0x0014` | 1 |
| `0x003c` | 26 |
| `0x0054` | 2 |
| `0x0074` | 26 |
| `0x013c` | 30 |
| `0x0150` | 1 |
| `0x0154` | 3 |
| `0x0174` | 25 |
| `0x0194` | 1 |
| `0x01d4` | 1 |
| `0x0374` | 1 |
| `0x0574` | 2 |

The bit pattern is regular — bits 2, 3, 4, 5, 6, 7, 8 and 10 recur — but no
shipped flow distinguishes one flag combination from another, so what each bit
selects stays `unknown`. `observed` for the set of values, `unknown` for the
meaning.

A map sprite object selects this table by `descriptor_index` and repeats the
descriptor's object id. This two-field join succeeds for all 129 objects
serialized in the 15 outdoor maps. `observed`

Four records have a demonstrated runtime role reached by constant object id
rather than through a map: `firetrap` (811), `coldtrap` (812), `electrap`
(813) and `poistrap` (814) are the chest-trap explosions — the chest-open
path in `MM6.exe` picks one of the four ids uniformly at random and spawns
it at the chest when a trapped chest's disarm roll fails. The object then
lives out its animation, and at expiry the per-frame updater routes exactly
these four ids into the detonation routine that damages the party (see
[`event-tables.md`](event-tables.md), the chest flags word). `observed`

## Invalid-input behavior

The decoder rejects a missing 48-byte header, invalid zlib data, and any count
whose 52-byte records do not account for the inflated block exactly.

## Open questions

- Confirm the dimensional, lifetime, speed, and trail-color semantics against
  individual runtime branches.
- What each flag bit at `+0x26` selects; the value set is enumerated above, the
  meanings are not.
