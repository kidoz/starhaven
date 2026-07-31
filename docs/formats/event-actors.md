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

## Following one behaviour, and why it does not isolate

The follow-up to the "no switch" finding was to pick a single question —
what makes a monster flee — and follow it from the state word to whatever
reads it. Three states came out of it, and the question did not.

- **State 4 is acting.** Entered at `0x403094` together with a sub-state 5
  in the word at `+0xa2`, the action timer at `+0xa8` cleared, and a value
  read from `+0xb6`. The recovery countdown at `0x401b5d` transitions **4 to
  5** when the counter lapses, so acting and recovered are a pair.
- **State 7 is moving.** `0x40302e` sets the velocity words `+0x8a` and
  `+0x8c` and calls the mover at `0x44c140`.
- **State 9 is standing.** `0x40365d` clears the velocity triple at `+0x84`,
  `+0x86` and `+0x88`, sets a facing at `+0x90` from a table, and rolls a
  five-in-a-hundred chance.
- The cluster chooses between two behaviours on a **distance of 1024**
  (`0x402317`), calling `0x402960` when nearer and `0x402b30` when further,
  both with the same kind argument.

**Fleeing was not found, and the reason is structural.** No state sets a
facing away from the party, and nothing in the cluster turns a direction by
a half-circle. The states are *movement* states — acting, moving, standing —
shared by every behaviour, and what would distinguish fleeing from
approaching is the target handed to the mover, not the state. So a single
behaviour cannot be lifted out: the trail runs through the movement code and
stops.

Taken with the previous finding, the answer for future batches is that **the
AI must be read wholesale or not at all** — there is neither a dispatch to
enumerate nor a behaviour that can be isolated. Anyone spending a batch on it
should plan to read the cluster function by function, and should not expect a
table. `unknown` for every behaviour.

## The AI cluster, mapped

Read wholesale, as the two failed visits concluded it had to be. The cluster
`0x401030`..`0x40a000` holds **39 functions**; seventeen of them end by
putting the actor into a state, and one of the others calls almost all of
them. `observed` throughout — the map is mechanical, from function bounds,
the `mov word [reg+0xa0], N` in each body, and a call graph over the whole
executable.

### The decision routine

**`0x4017a0`** is it — 3904 bytes, the largest in the cluster, entered from
outside at `0x453b5e` and from nowhere else, and it calls **eleven** of the
seventeen action functions. It is not a switch and never was: it is a long
chain of conditions that ends in one call. That is why looking for a
dispatch failed twice.

It is also the only function in the cluster that calls the resistance
routine at `0x421dc0`, so a monster's blow is struck from inside it.

### The actions, by the state each leaves behind

| Function | Bytes | State | Called by |
| --- | --- | --- | --- |
| `0x4038f0` | 624 | 1 | the decision routine, `0x407460` |
| `0x403b60` | 1056 | 1 and 3 | the decision routine, `0x407460` |
| `0x404380` | 512 | 2 | the decision routine, `0x406ef0` |
| `0x403050` | 1184 | 4 | the decision routine; and four sites outside it |
| `0x4017a0` | 3904 | 5 | — (the decision routine itself) |
| `0x405d60` | 1648 | 5 | the decision routine |
| `0x4063d0` | 928 | 5 | `0x405d60` |
| `0x407220` | 576 | 5 | `0x405d60` |
| `0x4026e0` | 640 | 6 | five callers inside the cluster |
| `0x402960` | 1136 | 6 | the decision routine, `0x407460` |
| `0x402dd0` | 640 | 7 | the decision routine; and four sites outside |
| `0x403730` | 448 | 8 | the decision routine; and four sites outside |
| `0x4035e0` | 336 | 9 | **eight** callers — the common ending |
| `0x404660` | 1360 | 10 | four sites outside the cluster only |
| `0x403f80` | 480 | 12 | the decision routine, `0x406ef0` |
| `0x404160` | 544 | 13 | the decision routine, `0x406ef0` |
| `0x4034f0` | 240 | 16 | one site outside, `0x429253` |

Fourteen distinct states, then: **1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 16**
and 0. `0x4035e0` (state 9) is called from eight places and clears the
velocity triple, which makes it the routine every movement ends in.

### What the outside callers say

Four of the actions are reachable from outside the AI, and where from is the
strongest evidence about what they mean:

- **State 4** (`0x403050`) and **state 8** (`0x403730`) are both called from
  the spell code (`0x429946`, `0x429955`) and from the projectile-impact
  code (`0x4312b3`, `0x431314`, `0x431a97`, `0x43183b`, `0x431942`) — in
  pairs, a few bytes apart at each site. So they are what *happens to* a
  monster that is hit: one of the pair for a blow it survives and the other
  for one it does not. State 8's body sets the animation byte at `+0x3e` to
  4 and raises bit `0x20000` in the flag dword at `+0x24`. `observed`; which
  of the two is the death is `inferred` from the pairing and not yet read.
- **State 7** (`0x402dd0`) is called from the effect applier at `0x432732`
  and from four sites in `0x461xxx`/`0x462xxx`.
- **State 10** (`0x404660`) is reached *only* from outside, including from
  the aiming code at `0x420beb`.
- **State 16** (`0x4034f0`) has exactly one caller anywhere, in a spell case.

### What this map does not say

It says what the pieces are and how they connect; it does not name the
behaviours. Which state is fleeing, which is patrolling and which is
pursuing needs the decision routine's chain of conditions read line by line,
which is the next batch's work and not this one's. What it does establish is
that such a reading is now bounded: eleven calls out of one function, each
to a body under 1.7 kB. `unknown` for the behaviours.

## The decision routine, read

`0x4017a0` line by line, as far as its structure goes. `observed` throughout.

### First, a damage pass

The routine opens by walking the actor array in strides of 548 and applying
damage over time: for each actor it calls the **resistance routine**
(`0x421dc0`) against the record at `+0x00`, subtracts the result from the
**hit points at `+0x28`**, and then branches on the sign:

- hit points now **negative** → call the **state-4** action, then play a
  sound chosen by the monster's own row through a 72-byte-per-monster table
  at `0x56c1c0` indexed by the byte at `+0x34`;
- still positive → call the **state-8** action.

So **state 4 is death and state 8 is being hurt** — which also explains why
the spell and impact code call the two a few bytes apart at every site: the
blow that kills takes one and the blow that lands takes the other.

### Then, per actor

1. A flag bit is cleared in the dword at `+0x24`.
2. Whether the **recovery counter at `+0x6c`** has reached zero is computed
   once and kept — "ready to act".
3. **If the distance is 5120 or more the actor idles.** That is the AI's
   awareness range, and it is a hard cut before anything else is considered.
4. Within range, `0x421c50` decides the actor's disposition, and the routine
   switches on its three-way answer.

### The disposition is a roll, not a state

`0x421c50` reads **two percentage bytes** off the actor and rolls against
each in turn: `rand() % 100` against the byte at **`+0x4d`** returns **2**,
otherwise the same against **`+0x47`** returns **1**, otherwise **0**.
Neither byte comes from `MONSTERS.TXT` — its `Hst` column is a small integer
and `Pref` is a letter — so both are runtime values, set by whatever afflicts
a monster.

That is a different shape from this engine's, which holds fear and charm as
timers with a fixed effect while they last. In the original a monster rolls
its condition **every tick**, so an afflicted monster wavers rather than
being wholly one thing.

### Within a disposition

Each of the three branches then tests a flag byte at `+0x4e`, whether the
recovery has lapsed, and the distance again — **1024** is the second
threshold — before selecting one of the eleven actions. Those inner
conditions are read but the actions they pick are still only known by their
state numbers. `unknown` for the remaining nine behaviours; what is settled
is death, being hurt, the two distance thresholds and the per-tick roll.
