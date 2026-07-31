# Event actors (`.ddm` / `.dlv`)

Status: **decoded, evidence-backed.** Actor records are a counted array of
548-byte records, in outdoor and indoor files alike. This corrects both the
former count-less-array interpretation and the former `variant` reading.

## Array

Outdoor: read `actor_count` at payload offset `0x798`; records begin at
`0x79C`. Indoor: the same, at `883` and `887` (see
[`event-tables.md`](event-tables.md)). The stride is 548 either way.

Across the 15 outdoor maps the counts total 266 actors and the following
sprite-object count lands exactly at the computed array end; across the 52
indoor maps they total 76. `observed`

| Record offset | Size | Type | Field | Status |
| --- | ---: | --- | --- | --- |
| `+0x00` | 32 | char[] | name | observed |
| `+0x34` | 1 | u8 | `monster_id` | observed |
| `+0x35` | 1 | u8 | level | observed |
| `+0x7E` | 6 | i16[3] | x, y, z | observed |
| `+0x84` | 6 | i16[3] | velocity | observed |
| `+0x8A` | 2 | i16 | direction | observed |
| `+0x8C` | 2 | i16 | look angle | observed |
| `+0x8E` | 2 | i16 | room/sector | observed |
| `+0x90` | 2 | i16 | current action length | observed |
| `+0x92` | 6 | i16[3] | start/home position | observed |
| `+0x98` | 6 | i16[3] | guard position | observed |
| `+0x9E` | 2 | i16 | guard radius | observed |
| `+0xA0` | 2 | u16 | AI state | observed |
| `+0xA2` | 2 | u16 | graphic state | observed |
| `+0xA4` | 4 | i32 | carried item id/state | observed |
| `+0xA8` | 4 | i32 | current action time | observed |
| `+0xAC` | 16 | u16[8] | frame ids | observed |
| `+0xBC` | 8 | u16[4] | sound ids | observed |
| `+0xC4` | 224 | bytes | 14 spell buffs | observed |
| `+0x1A4` | 4 | i32 | group | observed |
| `+0x1A8` | 4 | i32 | ally | observed |
| `+0x1AC` | 96 | bytes | eight schedules | observed |
| `+0x20C` | 4 | object ref | summoner | observed |
| `+0x210` | 4 | object ref | last attacker | observed |
| remaining | 16 | bytes | reserved/runtime tail | observed |

### `monster_id` is 1-based

`monster_id` is the id in `MONSTERS.TXT`, which is the `DMONLIST.BIN` index
**plus one** — not the index itself. Two independent measurements agree:

- The record's own `name` equals `MONSTERS.TXT[monster_id]`'s name for **all
  266 outdoor actors**. `observed`
- `DMONLIST.BIN[monster_id − 1]` matches `MONSTERS.TXT[monster_id]`'s picture
  for **173 of 173** monsters, while `DMONLIST.BIN[monster_id]` matches **0 of
  173**. `observed`

The distinction was invisible outdoors, where the placed monsters are mostly
peasants occupying a contiguous block of the table, so either reading produced
a plausible-looking "Peasant". The indoor files settle it: `hive.dlv` places
`"Reactor"` with id 173, and `DMONLIST.BIN` has no index 173 — only 0..172.
`observed`

Indoor records also **override the name**: only 10 of the 76 match
`MONSTERS.TXT`, because dungeon uniques carry a custom display name over a base
monster's statistics — `"Snergle"` over the Dwarf Lord, `"Slicker
Silvertongue"` over the Priest of Baa. `observed`

### Position

The position field and its two copies were verified against the matching
outdoor terrain: 252 of 266 placements land on the ground or within a model
footprint. Indoors, all 76 fall inside the paired `.blv`'s vertex extents.
`observed`

## Correction

Earlier documentation placed these fields four bytes later and treated the
count as part of record zero. Correcting the record start changes name from
`+0x04` to `+0x00`, monster identity from `+0x38` to `+0x34`, and position
from `+0x82` to `+0x7E`.

## Invalid-input behavior

Actors are exposed only after the complete outdoor section layout validates.
A caller-supplied limit may cap returned records without changing the decoded
count.

## Level, variants, and encounter difficulty

The byte at `+0x35` is the monster's level copied from its monster properties,
not an A/B/C selector. Values 1, 2, and 3 happened to correlate with several
low-level A/B/C rows; 6, 10, 12, and 15 expose the false premise. `observed`

The outdoor encounter slot already names its M1/M2/M3 monster family.
`Dif 1..5` selects the random A/B/C tier with these percentages:

| Dif | A | B | C |
| ---: | ---: | ---: | ---: |
| 1 | 90 | 8 | 2 |
| 2 | 70 | 20 | 10 |
| 3 | 50 | 30 | 20 |
| 4 | 30 | 40 | 30 |
| 5 | 10 | 50 | 40 |

The table is read directly by the outdoor encounter spawner. Placed quest
actors do not need to match those odds because they bypass random encounter
selection. `observed`

## Historical question status

> Audited in the [open-question register](../open-questions.md); the register
> supersedes unresolved hypotheses below.

All three questions are closed by the corrected field layout and the outdoor
encounter probability table above.

## What an actor record holds beyond its name and position

The three triples are current, start/home, and guard positions. The bytes
between them are velocity, direction, look angle, room, and action length.
They often ship equal or zero because the actor has not moved yet, not because
their roles are unknowable. `observed`

## The AI, visited a third time — and there is no switch to find

Three sittings have gone looking for the monster AI's decision point, on the
assumption that a body of code that large would dispatch through a table the
way the spell code and the special-bonus code do. **It does not**, and that
is the finding.

What is there:

- **The AI state is the word at `+0xa0`** of the 548-byte actor record. The
  states written across the cluster are **0, 4, 5, 6, 7, 8, 9 and 16**.
  `observed`
- The recovery countdown at `0x401b5d` — the same routine that drains the
  `Rec` counter at `+0x6c` — **transitions state 4 to state 5** when the
  counter reaches zero, clearing `+0x90` and `+0xa8` with it. So "recovered"
  is a state change, not just a number reaching zero. `observed`
- Several states are treated as one group: at `0x401e13` the code tests
  `+0xa0` against 6, 0, 1 and 9 and takes the same branch for all four, and
  state 8 joins them when the byte at `+0x46` is set. `observed`

What is not there is a dispatch. The cluster branches on the state with
scattered comparisons — `cmp word [reg+0xa0], N` at two dozen sites — rather
than through a selector and a jump table, so there is no single place that
decides what a monster does. Reading the AI would mean reading the whole
cluster, function by function, and not one table. That is why two earlier
visits and this one all came away empty, and it is recorded so a fourth does
not start from the same wrong assumption. `unknown` for the states' meanings.

Two neighbouring dispatches were identified along the way and are not the
AI: `0x432750` applies one of ten effects to an actor named by the packed
handle, and `0x42fb85` is a 170-case dispatch on an unrelated id range.
