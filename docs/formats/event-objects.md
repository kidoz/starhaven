# Outdoor sprite objects (`.ddm`)

Status: **draft, evidence-backed.** The second counted outdoor event array
stores placed loot and the same 100-byte structure used for live projectiles.

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
| `+0x18` | 2 | u16 | sound id | inferred | executable use is not yet isolated |
| `+0x1A` | 2 | u16 | attributes | observed | individual bits are tested by update code |
| `+0x1C` | 2 | u16 | sector id | inferred | updated during spatial queries |
| `+0x1E` | 2 | u16 | sprite frame/time | inferred | participates in frame resolution |
| `+0x20` | 4 | bytes | spell-owner state | unknown | two runtime words |
| `+0x24` | 28 | bytes | [contained item](items.md) | observed | direct `ITEMS.TXT` id is the leading i32 |
| `+0x40` | 24 | bytes | projectile/spell state | inferred | executable accesses; meanings incomplete |
| `+0x58` | 12 | i32[3] | previous/origin position | observed | copied from current position on spawn |

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

## Open questions

- Exact meanings of the words at `+0x18..+0x22`.
- The complete projectile-state field layout.
- Which attribute bits distinguish loot, missiles, and transient effects.
