# Object descriptor table (`DOBJLIST.BIN`)

Status: **draft, evidence-backed.** `DOBJLIST.BIN` is the global descriptor
table for loot sprites, projectiles, spell effects, and other sprite objects.

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
| `+0x26` | 2 | u16 | flags | observed |
| `+0x28` | 2 | u16 | DSFT frame index | observed |
| `+0x2A` | 2 | u16 | lifetime | inferred |
| `+0x2C` | 2 | u16 | unknown | unknown; zero in all 232 records |
| `+0x2E` | 2 | u16 | speed | inferred |
| `+0x30` | 3 | u8[3] | trail RGB | inferred |
| `+0x33` | 1 | u8 | padding | observed |

Every nonzero frame index at `+0x28` selects the first frame of a
`DSFT.BIN` animation group. `observed`

A map sprite object selects this table by `descriptor_index` and repeats the
descriptor's object id. This two-field join succeeds for all 129 objects
serialized in the 15 outdoor maps. `observed`

## Invalid-input behavior

The decoder rejects a missing 48-byte header, invalid zlib data, and any count
whose 52-byte records do not account for the inflated block exactly.

## Open questions

- Confirm the dimensional, lifetime, speed, and trail-color semantics against
  individual runtime branches.
- Decode the flag bits at `+0x26`.
