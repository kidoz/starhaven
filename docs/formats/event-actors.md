# Outdoor event actors (`.ddm`)

Status: **draft, evidence-backed — identity and position only.** Outdoor actor
records are a counted array of 548-byte records. This corrects the former
count-less-array interpretation.

## Array

Read `actor_count` at payload offset `0x798`; records begin at `0x79C` and use
a 548-byte stride. Across the 15 outdoor maps, the counts total 266 actors and
the following sprite-object count lands exactly at the computed array end.
`observed`

| Record offset | Size | Type | Field | Status |
| --- | ---: | --- | --- | --- |
| `+0x00` | 32 | char[] | name | observed |
| `+0x34` | 1 | u8 | `monster_id` | observed |
| `+0x35` | 1 | u8 | variant | inferred |
| `+0x7E` | 6 | i16[3] | x, y, z | observed |
| remaining | 510 | bytes | actor state | unknown |

`monster_id` indexes `DMONLIST.BIN`. The position field and its two copies
were verified against the matching outdoor terrain: 252 of 266 placements
land on the ground or within a model footprint.

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
- Indoor actor storage; `.dlv` files do not use this outdoor section layout.
