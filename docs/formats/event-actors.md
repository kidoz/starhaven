# Event actors (`.ddm` / `.dlv`)

Status: **draft, evidence-backed — identity and position only.** Actor records
are a counted array of 548-byte records, in outdoor and indoor files alike.
This corrects the former count-less-array interpretation.

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
| `+0x35` | 1 | u8 | variant | inferred |
| `+0x7E` | 6 | i16[3] | x, y, z | observed |
| remaining | 510 | bytes | actor state | unknown |

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

## Open questions

- The remaining actor state and the roles of the repeated position triples.
- The `variant` byte at `+0x35`, which ranges well past the three A/B/C
  variants.

## What an actor record holds beyond its name and position

The 548-byte record carries **three copies of the position**: at 0x7E, 0x92 and
0x98. They are identical on **94 of 96** actors across the outdoor maps
sampled, which is what a current, a previous and a home position look like in a
file shipped before anything has moved. `inferred`

The 14 bytes between the first copy and the second — 0x84 to 0x91, where a
velocity and a facing would sit — are **zero on all 96**. `observed` So the
file has room for which way an actor faces and how fast it is going, and ships
neither: both are runtime state. An engine that makes monsters move and turn is
not contradicting the data, and is not reproducing it either.
