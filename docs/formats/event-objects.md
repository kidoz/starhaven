---
title: "Outdoor sprite objects in DDM data"
summary: "Array and record layout for placed loot and live projectile objects in Might and Magic VI outdoor event data."
doc_type: reference
status: verified
last_updated: 2026-08-01
tags:
  - mm6
  - objects
  - ddm
  - events
---
# Outdoor sprite objects (`.ddm`)

Status: **decoded, evidence-backed.** The second counted outdoor event array
stores placed loot and the same 100-byte structure used for live projectiles.

## Scope

This page covers the outdoor sprite-object array, its 100-byte record, and the
join to [`dobjlist.md`](dobjlist.md). Other outdoor and indoor event arrays are
documented in [`event-tables.md`](event-tables.md).

## Array

The array follows the actor records:

```text
0x79C + actor_count × 548:
    u32 sprite_object_count
    SpriteObject objects[sprite_object_count]  // 100 bytes each
```

The executable copies each record into a 1000-slot runtime pool. A zero
`descriptor_index` marks an unused runtime slot. `observed`

## Record

| Offset | Size | Type | Field | Status | Evidence |
| --- | ---: | --- | --- | --- | --- |
| `+0x00` | 2 | u16 | `object_id` | observed | repeats the selected descriptor's object id |
| `+0x02` | 2 | u16 | `descriptor_index` | observed | indexes `DOBJLIST.BIN` |
| `+0x04` | 12 | i32[3] | position x, y, z | observed | movement, terrain, and renderer accesses |
| `+0x10` | 6 | i16[3] | velocity x, y, z | observed | initialized for spawned projectiles |
| `+0x16` | 2 | u16 | facing | observed | used as the render/motion angle |
| `+0x18` | 2 | i16 | look angle | observed | vertical look/launch angle |
| `+0x1A` | 2 | u16 | attributes | observed | see the bit table below |
| `+0x1C` | 2 | i16 | room/sector id | observed | updated during spatial queries |
| `+0x1E` | 2 | u16 | age | observed | current lifetime |
| `+0x20` | 2 | u16 | maximum age | observed | expiry threshold |
| `+0x22` | 2 | i16 | light multiplier | observed | dynamic-light intensity |
| `+0x24` | 28 | bytes | [contained item](items.md) | observed | direct `ITEMS.TXT` id is the leading i32 |
| `+0x40` | 4 | i32 | spell id | observed | projectile spell |
| `+0x44` | 4 | i32 | spell skill | observed | spell power |
| `+0x48` | 4 | i32 | spell mastery/level | observed | spell mastery/level encoding |
| `+0x4C` | 4 | object ref | owner | observed | packed object reference |
| `+0x50` | 4 | object ref | target | observed | packed object reference |
| `+0x54` | 1 | u8 | range bucket | observed | 0..3 distance band |
| `+0x55` | 1 | u8 | attack type | observed | attack 1/2, spell 1/2, or explode |
| `+0x56` | 2 | — | padding | observed | |
| `+0x58` | 12 | i32[3] | previous/origin position | observed | copied from current position on spawn |

Attribute bits are:

| Bit | Meaning |
| ---: | --- |
| `0x001` | visible |
| `0x002` | temporary |
| `0x004` | halt turn-based processing |
| `0x008` | dropped by player |
| `0x010` | ignore range |
| `0x020` | no Z-buffer |
| `0x040` | skip a frame |
| `0x080` | attach to head |
| `0x100` | missile |
| `0x200` | removed |

The engine allocates and compacts these records in 100-byte units. Its spawn
path copies current position into `+0x58`, initializes velocity at `+0x10`,
and its update path compares the current and previous/origin triples.

The 129 serialized objects across the 15 outdoor maps use six descriptor
indices. For all 129, this invariant holds:

```text
DOBJLIST[object.descriptor_index].object_id == object.object_id
```

Their leading contained-item ids form the expected placed-loot values, while
their serialized velocities are zero. The same structure supports nonzero
velocity when the engine spawns a projectile.

All 129 contained-item ids also join directly to `ITEMS.TXT`, without a
one-based adjustment. The executable uses the same leading id to select its
compiled 40-byte item descriptor. See [`items.md`](items.md). `observed`

## Invalid-input behavior

Objects are exposed only after every counted outdoor section and the fixed
trailer validate. Extraction is bounded by both the decoded count and an
optional caller limit.

## Historical question status

> Audited in the [open-question register](../open-questions.md); the register
> supersedes unresolved hypotheses below.

All three questions are closed by the corrected record and attribute table
above.
