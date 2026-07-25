# Event-file actors (.ddm / .dlv) (Might and Magic VI)

Status: **draft, evidence-backed — name and position only.** The 548-byte
records at the start of an event file are actor placements: named monsters and
NPCs standing on the map. Their names and positions are verified; the rest of
each record is `unknown`. Each claim is tagged `observed`, `inferred`, or
`unknown`.

## Scope

Covers the name and position fields of the first event table's records, and how
the array ends. Does not cover the ~500 remaining bytes per record, the other
tables in the payload, or how an actor's name maps to a sprite.

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product and edition | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Files | the 67 `.ddm`/`.dlv` entries of `data/Games.lod` |
| Records found | 266 actors across 15 outdoor maps `observed` |

Reproduce with:

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/ddm_info outc1.ddm
```

## The array

The records `event-tables.md` describes — 548 bytes each, starting at payload
offset `0x798` — are actor placements. The stride is uniform: on `outb2.ddm`
the nineteen gaps between consecutive names are all exactly 548. `observed`

Only outdoor files carry them: 15 of the 67 event files have any, the largest
being `Outc2.ddm` with 58. Every `.dlv` (indoor) file has none, so indoor actors
either live elsewhere in the payload or are absent. `unknown`

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| +0x00 | 4 | i32 | unknown | unknown | 40 or 58 on the first record, 0 on the rest |
| +0x04 | ≤32 | char[] | name | observed | NUL-terminated, e.g. `"Peasant"` |
| +0x24 | 94 | — | unknown | unknown | stats and flags, unconfirmed |
| +0x82 | 6 | i16[3] | position | observed | world x, y, z |
| +0x88 | 14 | — | unknown | unknown | |
| +0x96 | 6 | i16[3] | position copy | observed | |
| +0x9C | 6 | i16[3] | position copy | observed | |
| +0xA2 | ~386 | — | unknown | unknown | |

### Three copies of the position

The triple appears three times, and all three agree in 263 of the 266 records.
`observed` — the natural reading is current, home and previous position, which
is how an actor that wanders and returns would be stored. `inferred`

### There is no count

Nothing before the table holds the record count: the 24 bytes preceding it are
zero in every file, and no `u32` or `u16` anywhere in the first `0x798` bytes
matches the count across all files. `observed`

The array is therefore read until a slot whose name is not plausible text —
at least two printable characters. That rule matters: several shipped files end
with a slot holding a one-character name and a position of `(0, 0, 31744)`,
which a looser rule reports as a real actor.

## Verification

Positions were checked against the terrain of the matching `.odm`:

| Result | Count |
| --- | --- |
| within one terrain cell (512 units) of the ground | 228 |
| otherwise inside a model's footprint — standing in or on a building | 24 |
| neither | 14 |

So **252 of 266** actors stand either on the ground or within a building.
`observed` The remaining 14 may be on upper storeys or bridges, or may indicate
that the coordinate reading is imperfect for some records. `unknown`

## Invalid-input behavior

The decoder stops cleanly at the end of the payload, at the first implausible
name, or after a caller-supplied record limit. It never reads past the payload.

## Open questions (next slice)

- The ~500 unknown bytes per record. Values that look like statistics are
  visible at +0x6C (140) and +0x70 (100), constant across records of the same
  name, and a value at +0x2C varies (3, 9). `unknown`
- How a name maps to a sprite. No `SPRITES.LOD` entry begins with `peas`, so an
  indirection is involved, as with ground tiles and decorations.
  `DMONLIST.BIN` in `icons.lod` is the obvious candidate: it has the same
  container shape as `DTILE.BIN` and holds 173 records of exactly 148 bytes,
  but its name field is not at offset 0. `unknown`
- Why every indoor file's table is empty. `unknown`
