# Outdoor event sections (`.ddm`)

Status: **draft, evidence-backed.** This documents the counted actor,
sprite-object, and chest arrays in a decompressed outdoor `.ddm` payload. It
supersedes the earlier interpretation of `0x798` as an actor record.

## Scope

This layout applies to the 15 outdoor `.ddm` files examined. Indoor `.dlv`
sectioning remains unknown.

## Layout

| Payload offset | Size | Field | Status |
| --- | ---: | --- | --- |
| `0x000` | 8 | saved outdoor state | observed |
| `0x008` | 968 | fixed block | observed |
| `0x3D0` | 968 | fixed block | observed |
| `0x798` | 4 | `actor_count` | observed |
| `0x79C` | `actor_count × 548` | actor records | observed |
| next | 4 | `sprite_object_count` | observed |
| next | `sprite_object_count × 100` | sprite-object records | observed |
| next | 4 | `chest_count` | observed |
| next | `chest_count × 4204` | chest records | observed |
| next | 256 | fixed trailer | observed |

The counts, strides, and trailer account for every byte in all 15 shipped
outdoor payloads. Actor counts range from 0 to 58, sprite-object counts from 0
to 42, and `chest_count` is 20 in every examined map.

The executable's outdoor loader reads the count at `0x798`, copies
`actor_count × 548`, then repeats the same count-and-copy sequence for
100-byte sprite objects and 4204-byte chests. `observed`

## Correction to the earlier interpretation

The `u32` at `0x798` is the actor count, not a field in actor record zero. The
first actor begins four bytes later at `0x79C`. Treating the count as a record
field produced an apparent first-record “type” equal to the map's actor count
and shifted every decoded actor field by four bytes.

## Invalid-input behavior

The decoder rejects an outdoor layout when:

- the payload cannot reach the actor count;
- any count-times-stride section exceeds the payload or overflows;
- a following count cannot be read;
- the bytes after the chest array are not exactly the 256-byte trailer.

## Open questions

- Meanings of the two fixed 968-byte blocks and the 256-byte trailer.
- The 4204-byte chest record body.
- The corresponding section layout for indoor `.dlv` files.
