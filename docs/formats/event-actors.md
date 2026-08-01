---
title: "Event actors in DDM and DLV data"
summary: "Record layout, runtime behavior, and evidence boundaries for actors stored in Might and Magic VI event data."
doc_type: reference
status: partial
last_updated: 2026-08-01
tags:
  - mm6
  - actors
  - ddm
  - dlv
---
# Event actors (`.ddm` / `.dlv`)

Status: **decoded, evidence-backed.** Actor records are a counted array of
548-byte records, in outdoor and indoor files alike. This corrects both the
former count-less-array interpretation and the former `variant` reading.

## Scope

This page covers actor-array framing, the 548-byte record fields established
by file census or executable reads, and the runtime AI and timer findings tied
to those fields. It does not define every unnamed actor field.

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

To test the "no switch" finding, the analysis selected one question — what
makes a monster flee — and followed it from the state word to whatever
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
  of the two is death is `inferred` from the pairing; the exact mapping remains
  `unknown`.
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

## The runtime record, as far as this batch got it

Seven fields fell out of reading the AI, each from an instruction rather
than from the table. `observed` throughout.

| Offset | Size | Field | Where it was read |
| --- | --- | --- | --- |
| `+0x24` | 4 | flags; bit `0x40000` cleared each tick, bit `0x20000` raised on death | `0x4021a3`, `0x40375a` |
| `+0x28` | 2 | **hit points** | the AI's damage pass, and the weapon-specials walk |
| `+0x34` | 1 | the monster's own row, indexing a 72-byte-per-monster table at `0x56c1c0` | `0x40193f` |
| `+0x3e` | 1 | animation, set to 4 by the hurt action | `0x403770` |
| `+0x47` | 1 | a percentage rolled each tick, returning disposition 1 | `0x421c91` |
| `+0x4d` | 1 | a percentage rolled each tick, returning disposition 2 | `0x421c6b` |
| `+0x4e` | 1 | a flag tested inside a disposition branch | `0x4021d4` |

The two percentage bytes are **not** `MONSTERS.TXT` columns — its `Hst` is a
small integer and `Pref` a letter — so both are runtime values written by
whatever afflicts the monster.

**Where the table's columns land was not found.** The plan was to work from
`MONSTERS.TXT` outward, but the routine that prepares an actor assembles the
record **field by field on the stack** from a large local frame rather than
copying columns in a loop, so there is no per-column trace to follow. Reading
it would mean reading the whole preparation routine, which is a batch of its
own. The record is not a wall — seven fields came out of the AI alone — but
it does not yield to the table-outward method the way the player record did.
`unknown` for the column map.

## The nine unnamed actions, grouped

Reading each action's opening record access does not name them, but it does
group them, which narrows the next attempt. `observed` for the offsets.

- **States 2, 12 and 13** (`0x404380`, `0x403f80`, `0x404160`) all open on
  the word at **`+0x7a`**, and state 2 reads **`+0x82`** beside it. Three
  variants of one thing.
- **State 1+3** (`0x403b60`) opens on two *pairs* — `+0x92`/`+0x94` against
  `+0x7e`/`+0x80` — which is the shape of comparing one position with
  another.
- **State 4** (death) opens on `+0x34`, the monster's row index, which is
  what it needs for the death sound. **State 16** opens on `+0xb6`, the same
  field state 4 reads second. **State 5b** opens on `+0xc4` alone.
- **States 6b, 9 and 10** read nothing from the record at all and take
  everything from their arguments.

Names remain `unknown`. What this adds is that the eleven actions are not
eleven independent behaviours: at least three are one family, and three more
are argument-driven helpers rather than decisions.

## And a further negative on what a condition costs

The per-character pass inside the time-advance runs **once a real second** —
gated by a due-time at `0x908ce0` that is advanced by **128 units** each
time, which is the clock unit confirmed again from a third place. Within
that pass nothing reads a condition timestamp's *age* and nothing subtracts
hit points for one.

So a condition's cost is not applied on the clock at all. Two batches have
now looked for it: the timestamps are only ever tested for being set. If
being poisoned costs anything in MM6 it is charged somewhere that does not
walk the party on a timer, and this engine's invented drain stays marked as
its own. `unknown`, with the search space narrowed to "not the clock".

## There is no column map, because the columns are not copied

The plan was to read the actor-preparation routine top to bottom for where
each `MONSTERS.TXT` column lands in the 548-byte record. Three candidates
were followed and none of them is that routine: `0x455ed0` is the encounter
roll, `0x41ce90` copies a record into a **display scratch** at `0x4cb3b8`
(and writes 7 into its sub-state at `+0xa2`, confirming that field), and
`0x46cd92` is the array's own **read/write to file**, 548 bytes a record.

The reason there is no such routine is that **the columns never enter the
record**. The actor carries a **monster id in the byte at `+0x34`**, and
everything from the table is looked up through it in a runtime table at
**`0x56c1c0`, 72 bytes a row**, whose `+0x10` is a **pointer to the
monster's name** — `0x455c8f` computes `72 × id` and runs `repne scasb` over
what it finds there. Fourteen sites index that table; the AI itself uses it
for the death sound. `observed`

So the question retires rather than being answered: there is no column map
to find. What a monster *is* stays in the table; what a monster is *doing*
is the record.

## A behaviour named: closing and backing off

States 2, 12 and 13 open on the same three words and differ in one constant.

All three build a point from the actor's own position — `+0x7e`, `+0x80`,
`+0x82` — offset along it by **0.75 × the word at `+0x7a`**, and hand it with
the party's position to the reachability test at `0x4080c0`.

- **State 2** (`0x4043b7`) multiplies by **+0.75**.
- **States 12 and 13** (`0x403fab`, `0x40418b`) multiply by **−0.75**.

One offsets toward the party and the others away from it. So **state 2 is
closing and states 12 and 13 are backing off** — and that is the fleeing
behaviour two earlier batches went looking for and could not find, sitting in
the sign of a floating-point constant. `observed`

`+0x7a` is what the offset scales — the actor's own radius, on the evidence
of how it is used — and `+0x7e`/`+0x80`/`+0x82` are its position triple.
`inferred` for the names, `observed` for the arithmetic.

## The 6a/6b pair: approach, and a raised aim

The two state-6 actions are near-identical and differ in **one condition**,
which is the same trick that named closing and backing off.

Both test a flag byte at **`+0x46`** and, when it is set, aim not at the
party's own height but at **`word[+0x78] + 512`** — five hundred and twelve
units above it — before handing the point to the pathfinder at `0x4046f0`.
When the flag is clear both aim at the party's height from `0x908c70`.
`observed` at `0x402724` and `0x4029ad`.

The only difference: **`0x402960` additionally requires the world kind at
`0x6107d4` to be 2** before it will use the raised aim. `0x4026e0` does not.

So the pair is one behaviour — approach — in two forms, one of which only
rises in one kind of world. A flag that lifts an actor's aim half a thousand
units, gated on being outdoors, is what flying looks like, and
`MONSTERS.TXT` has a `Fly` column; but that is the shape of the reading, not
the instruction, so **`+0x46` is `inferred` to be the fly flag** and the
arithmetic alone is `observed`.

### The 1 / 1+3 pair, attempted

Less conclusive, and recorded as such. `0x4038f0` ends by zeroing the
velocity triple at `+0x84`, `+0x86`, `+0x88`, setting a facing of **256** at
`+0x90`, and writing **zero** into the state word — so it is a *stop*, and
the earlier scan that filed it under "sets state 1" caught a different write
in the same body. `0x403b60` compares two pairs of words — `+0x92`/`+0x94`
against `+0x7e`/`+0x80` — which is the shape of testing one position against
another. Neither is named. `unknown`

## State 16 named: reanimation

`0x4034f0` has exactly one caller anywhere — `0x429253`, inside the case for
spell **89, Reanimate** — and its body says the rest. It writes **16** into
the state word, **5** into the sub-state at `+0xa2`, clears the action timer
at `+0xa8`, sets a facing at `+0x90` from a table indexed by `word[+0xb6]`,
and copies **`word[+0x5c]` into `word[+0x28]`** — the hit points. `observed`

A spell whose row reads "Creature gets N hit points per skill point", whose
case is this action's only caller, and whose action restores the hit points
from a stored word: **state 16 is a reanimated corpse**, and **`+0x5c` is
the actor's full hit points**. `observed` for the writes and the caller;
`inferred` that `+0x5c` is the maximum rather than some other stored total.

### State 9's body

`0x4035e0` calls the pathfinder at `0x4046f0` with the actor's packed handle,
copies the seven-dword result, and reads `word[+0xba]` to index a facing.
Consistent with the map's reading of it as the common ending every movement
falls into, but not enough on its own to name. `unknown`

States 5b, 7 and 10 were not reached this sitting.

## The last four attempted

**State 10 is a commanded move.** `0x404660` builds the actor's packed
handle, calls the pathfinder at `0x4046f0` with a target of **zero** — the
caller's own point rather than one it works out — copies the seven-dword
result, and writes the velocity pair at `+0x8a`/`+0x8c`. Every one of its
callers is outside the AI, including the aiming code at `0x420beb`. So it is
"go to this point", commanded rather than decided. `observed` for the call
and the writes; `inferred` for the name.

**State 7 branches on two of the actor's own timers.** `0x402dd0` reads the
64-bit pairs at `+0x114`/`+0x118` and `+0x128`, tests each for sign and for
zero, and folds the two into booleans before proceeding. Two 64-bit values
tested that way are timestamps, but which is `unknown`, and the action they
gate was not reached.

**States 5b and 9 remain unnamed.** State 9's body was read in an earlier
sitting — the pathfinder, the seven-dword copy, a facing from `word[+0xba]`
— and is consistent with being the common ending every movement falls into
without settling it. State 5b opens on `+0xc4` and was not read.

So the tally across three sittings: **death, hurt, reanimation and a
commanded move** named outright; **closing/backing off** and **approach**
named as families; **five actions and one field** still open. `unknown`

## The last three, and what blocks them

**State 5b surveys everyone.** `0x405d60` sets a pointer to the *first*
actor's record and walks the whole array, reading the 64-bit pair at
`+0xf4`/`+0xf8` on each and testing its sign and zero-ness. Whatever it does,
it does after looking at every actor rather than at the party. `observed`

**State 9** was read again and adds nothing: the pathfinder, the seven-dword
copy, a facing from `word[+0xba]`.

**What blocks all three.** States 5b, 7 and 9 branch on **64-bit pairs on the
actor record** — `+0xf4`/`+0xf8` for 5b, `+0x114`/`+0x118` and `+0x128` for
7 — and nothing in this project knows what those timers hold. They are the
same shape as the character's condition timestamps, which were named only
once three independent readings agreed on them. Until the actor's timers are
named the same way, reading these bodies further yields structure without
meaning.

So the remaining three are not "unfound"; they are **blocked on naming the
actor's 64-bit timer fields**, which is the next thing to do and a different
job from reading action bodies. `unknown`

## The actor's 64-bit timers

Reading every site that touches them settles their shape; their semantics
remain `unknown`.

`0x401655` shows the pattern whole. It first tests **bits `0x4000` and
`0x8000` of the flags dword at `+0x24`** and only looks further if either is
set. It then reads two 64-bit values and reduces each to a boolean by the
same three-instruction idiom — sign, then zero:

- one at **`+0x114`** (high half `+0x118`),
- one at **`+0x124`** (high half `+0x128`).

State 5b reads a third at **`+0xf4`**, and state 7 reads the `+0x114` and
`+0x124` pair.

**They sit on a sixteen-byte grid**: `+0xf4`, `+0x104`, `+0x114`, `+0x124`.
That is exactly the stride of the party's buff array and of each character's,
and the "is this 64-bit value greater than zero" test is exactly how both of
those decide whether a slot is still up. `observed`

## **Retracted: they are not a buff array**

On that spacing this file named the run a buff array of the actor's own, and
marked it `inferred` because no writer had been found. The writer was then
looked for properly, and **there is none**.

Every store in MM6.exe at `+0xf4`, `+0x104`, `+0x114` or `+0x124` — there are
twenty-five of them in the whole image — belongs to some other record, and
each can be told apart by its own neighbours:

- `0x429fc3`, `0x42a18c`, `0x42a31b` write `+0x128` beside `+0x142c`, which
  is a character's equipment anchor: that `+0x128` is the character's **item
  array**, not an actor field.
- `0x434f3d` and its run are a member-by-member reset of an array of
  **forty-byte records** — a dword, four words, two dwords and a byte apiece,
  bases at `0x20`, `0x48`, `0x70`, `0x98`, `0xc0`, `0xe8`. It writes `+0xf4`
  as a **word**, which no 64-bit expiry can be.
- The `0x44ce40`..`0x44dc9a` cluster writes `+0x104` and `+0x108` twelve times
  and `+0x114` or `+0x124` never.

And the absolute forms settle it from the other side: across actor zero's
`+0xf4` through `+0x128`, MM6.exe holds **ten references and not one of them
is a store**.

So the name goes. A buff array that nothing ever casts into is not a buff
array — the party's and the character's each have a writer *and* a clearing
walk, and this has neither.

What still stands, unchanged and `observed`: the reads, the two pairs, the
sign-then-zero idiom, the sixteen-byte spacing, and that the AI's last three
actions branch on them. `0x408f9a` is a third independent reading of the same
two pairs, and it proves the offsets really are the actor's: it holds
`0x56f59c` in `ebx` and reaches `+0x28`, `+0x5c`, `+0x6c`, `+0x7e` and `+0x92`
from it by negative displacement.

**What it leaves.** Either the values arrive from outside the code — the map's
or the save's actor block, read in wholesale — or they are never anything but
zero, in which case the three actions gated on them are unreachable in the
shipped game. Distinguishing the two needs the actor block's on-disk layout,
which is `unknown`.

## Settled: the three timers are never anything but zero

The withdrawal above left one question open — whether the values arrive from
outside the code, or are always zero and three AI actions are dead. Both ends
now answer.

**The block moves as a straight image.** The executable copies the whole actor
array in and out without touching a field:

```asm
0x0046dc92  mov edi, 0x56f478          ; the array
0x0046dc97  shl ecx, 4                 ; count * 17 ...
0x0046dca2  mov ecx, eax               ; ... * 8 + count, << 2  =  548 * count
            (byte copy from the map's buffer)

0x0046cd8b  mov eax, dword [0x5b22f8]  ; the actor count
0x0046cd92  push 0x224                 ; 548
0x0046cd97  push 0x56f478
0x0046cd9c  call 0x4ae63c              ; fwrite
```

The save reader at `0x48c0a6` and the save writer at `0x48b362` are the same
two shapes. `observed` — and it names `0x5b22f8` as **the actor count**, which
is why the AI reads it eighty-one times.

So a file *could* carry values the code never writes. **It does not.**
`evt_info --actor-timers` walks every actor block Games.lod ships:

```text
67 maps, 342 actors on disk
  +0xf4: 0 non-zero (0%)
  +0x104: 0 non-zero (0%)
  +0x114: 0 non-zero (0%)
  +0x124: 0 non-zero (0%)
```

Every authored actor in the game has all four 64-bit fields at zero. And a
save can only hold what memory held, which no instruction ever sets.

**Therefore the three AI actions gated on these timers are unreachable in the
shipped game.** Each reduces its field to "greater than zero" and each field is
always zero, so each branch always takes the same side. That is the answer the
withdrawal was owed: not "they come from the file", but that the fields are
dead and the behaviours behind them never run.

## What the per-tick preparation says, and one retraction

The routine at `0x401ac7` runs over an actor each tick and fills two of its
fields from tables. Both are worth having, and one of them costs a name.

**The radius comes from a second monster table.** 

```
mov  al, byte [ebp + 0x34]        ; the monster's row
lea  ecx, [eax + eax*8]           ; x9
lea  edx, [eax + ecx*4]           ; x37
mov  eax, dword [0x5e217c]
mov  cx,  word [eax + edx*4 - 0x94]   ; x4 -> a stride of 148
mov  word [ebp + 0x7a], cx
```

So there is a **second per-monster table of 148-byte rows**, behind the
pointer at `0x5e217c`, and the actor's radius at `+0x7a` is one of its
columns. `observed` — the 72-byte table at `0x56c188` is not the only one.

**And the retraction.** Eight instructions later:

```
mov  al, byte [ebp + 0x34]
lea  edx, [eax + eax*8]
mov  al, byte [edx*8 + 0x56c19a]      ; the 72-byte row's +0x12
mov  byte [ebp + 0x3e], al            ; the animation state
```

— guarded by the 64-bit pair at `+0xd4`/`+0xd8` having lapsed. `+0x3e` is the
animation state the hurt action sets to **4**. So the monster row's `+0x12` is
**an animation state**, not a disposition.

The reputation work read that same byte in the hundred-point penalty's guard
and called it the row's peacefulness. That name is **withdrawn**: what is
observed is that the double penalty applies only when the row's `+0x12` is
zero, and `+0x12` is now known to be the idle animation state. Whether a
zero there also marks a peaceful creature is `unknown` — it may well
correlate, since townsfolk and monsters need not share an idle set, but the
fit is no longer available as evidence.

The same routine tests the actor's `+0x114`/`+0x118` and `+0x124`/`+0x128`
pairs at `0x401b13` and `0x401b2d`, which is a fourth independent sighting of
the timers nothing writes.

**The column map is still not done.** Two columns of the 72-byte table now
have homes — `+0x12` the animation state, and the five referenced elsewhere —
and one column of the 148-byte table does. The rest needs the whole
preparation routine read, which remains a batch of its own. `unknown`

## What a monster's death does, beyond the body

The impact path at `0x431a78` is the place, and it does three things after a
blow that kills.

**The experience.** When the animation byte is 4 it takes the monster's row
index at `+0x34`, indexes the 72-byte table's dword at **`+0x38`**, and hands
it to `0x421520`. This was written up here as the death sound. It is not: the
parser's **case 6 writes `+0x38` from the `EXP` column**, and `0x421520` is the
**experience award** — see the player record for what it does with it. The
misreading is what kept the award hidden through four searches.

**The death action.** `0x431a97` calls `0x403050`, the state-4 body.

**And a one-in-five remark.** Immediately after:

```
0x00431a9c  call 0x4ae22b            ; rand()
0x00431aa7  idiv ecx                 ; % 100
0x00431aa9  cmp  edx, 0x14           ; 20
0x00431aac  jge  skip
0x00431aae  mov  ecx, dword [esi + 0x5c]   ; the actor's full hit points
0x00431ab6  cmp  ecx, 0x64                 ; 100
0x00431ab9  setge dl ; inc edx             ; line 1 or line 2
0x00431aca  call 0x488ca0
```

So **one kill in five draws a remark from a character, and which of two lines
it is depends on whether the thing had a hundred hit points or more**.
`observed` — the twenty, the hundred and the two lines are all the routine's.

The drop itself was already threaded from `MONSTERS.TXT`'s own treasure code,
which is what this search set out to find and found already done; what was
missing was anyone looking at the pile. Over sixteen world hours across four
maps, an armed level-twenty party killing **548 actors** collected **3,915
gold and 17 items** — rings, a crossbow, a staff.

## Three of the nine, and the test they share

`0x4080c0` is the **line of sight**. States 2, 12 and 13 all open by calling
it, and in 12 and 13 the opening is byte-identical:

```
fild dword [esp + 0xc]              ; the radius, from +0x7a
fmul qword [0x4b9340]               ; x -0.75
call 0x4ae24c                       ; back to an integer
movsx ecx, word [ebx + 0x82]        ; the actor's z ...
sub   ecx, eax                      ; ... lifted by three quarters of a radius
movsx edx, word [ebx + 0x80]
movsx eax, word [ebx + 0x7e]
mov   ecx, dword [0x908ca0]         ; the party's z
mov   edx, dword [0x908c78]         ; and its eye offset
add   edx, ecx
mov   ecx, dword [0x908c98]         ; the party's x
mov   edx, dword [0x908c9c]         ; and y
call  0x4080c0
```

So the doc's earlier note that states 2, 12 and 13 are "three variants of one
thing" because they all read `+0x7a` has a better reason behind it: **they all
need to see the party, and the radius is only there to lift the eye**. What
separates them is what each does once the answer comes back. `observed`

Two party fields fall out of it: `0x908c98`, `0x908c9c` and `0x908ca0` are the
party's position at `+0x28`, `+0x2c` and `+0x30`, and `0x908c78` — party
`+0x08` — is added to the z, which makes it the **eye height**.

**State 1-and-3** (`0x403b60`) opens differently: it takes `+0x92` and `+0x94`
against `+0x7e` and `+0x78`, and subtracts one pair from the other. A stored
pair differenced against the live position is a **vector home** — the actor
measuring how far it has drifted from where it belongs. `observed` for the
reads and the subtraction, `inferred` for the name.

Six of the eleven remain unnamed. What this adds is that the grouping was
right for the wrong reason, that the sight test now has an address, and that
the party's eye height has one too.

## What the sighted actions do with the answer

Both state 12 (`0x403f80`) and state 13 (`0x404160`) end their "cannot see"
branch identically, and it names another action:

```
test eax, eax          ; the line of sight
jne  can_see
push 0x40              ; 64
push esi               ; the actor
call 0x4026e0          ; the state-6 body
ret
```

So **state 6 is what a sighted action falls back to when the party is out of
sight**, and both callers pass the same constant **64**. State 6 is one of the
three bodies that read nothing from the actor record and take everything from
their arguments, which fits a helper rather than a decision. `observed`

**State 13 has a second gate.** Even with sight it tests a 64-bit pair before
acting:

```
mov eax, dword [ebx + 0x148]
...
mov eax, dword [ebx + 0x144]
...
push 0x40 ; call 0x4026e0        ; the same fallback
```

`+0x144`/`+0x148` is a **fifth** member of the actor's 64-bit family, read here
and written nowhere — the others being `+0xd4`, `+0x104`, `+0x114` and
`+0x124`. Since every one of them is always zero, state 13's *sighted* branch
is the one that always runs and the fallback never fires from this gate.
`observed`

**State 12's sighted branch** opens by building a handle:

```
lea ebp, [esi*8]
or  ebp, 3
```

— the index in the high bits and a **type tag of 3** in the low three, which is
the executable's own object-handle form. It hands that and a stack buffer on.
`observed`

## The 72-byte monster row, from the parser

The stated risk was that `MONSTERS.TXT`'s parser would fill a stack frame and
copy it wholesale, leaving the columns anonymous. **It does not.** It writes
the row directly, with the table's own stride — `[reg + 9 x row x 8 + column]`
— and scanning the parser's range for that form gives the map:

| offset | size | writes | what |
| --- | --- | --- | --- |
| `+0x00` | dword | — | the **name** pointer |
| `+0x0c` | dword | 1 | `unknown` |
| `+0x0f` | byte | 1 | `unknown` |
| `+0x10`, `+0x11` | byte | 5, 3 | `unknown` |
| `+0x12` | byte | 1 | the **idle animation state**, copied to the actor's `+0x3e` |
| `+0x13` | byte | — | a **skill**, packed with the `E` and `M` rank suffixes |
| `+0x14` | dword | 1 | `unknown` |
| `+0x16` | byte | 5 | 0 or **5** by a name comparison |
| `+0x18`..`+0x21` | byte | 20, 15, 9, 1, 6, 3, 1, 2, 10, 2 | `unknown` |
| `+0x22` | byte | 20 | an **element**, set from `"Cold"`, `"Mind"` and their siblings |
| `+0x23`..`+0x29` | byte | 1..3 each | `unknown` |
| `+0x2a` | byte | 3 | a second **element**, the same way |
| `+0x2c` | word | 1 | `unknown` |
| `+0x36` | word | — | the **movement speed** (from the actor preparation) |
| `+0x38` | dword | — | the **death sound** |
| `+0x3c` | dword | 1 | `unknown` |
| `+0x40` | dword | 1 | doubled while the actor's `+0x134` pair is positive |

`observed` for every offset and size. **Twenty-nine distinct columns** are
written, which is about what `MONSTERS.TXT` ships, so the row is close to one
field per column rather than a packed structure.

What is not settled is which column is which: the parser's write sites are
scattered across a thousand lines and its order is not the file's. Naming them
needs the parser walked in order against the file's own header row, which is
the next step and not this one's. But the wall the earlier attempt hit — a
stack frame with no per-column trace — was the *preparation* routine, not the
parser, and the parser does not have it.

## The monster row's columns, named

The parser dispatches on the **column index** — `cmp edi, 0x1f; jmp dword
[edi*4 + 0x44851c]`, thirty-two cases — so the *n*th case is the *n*th column
of `MONSTERS.TXT`'s header row. The alignment proves itself: cases **26
through 31** write **`+0x24` through `+0x29`**, consecutively, and the header's
columns 26 to 31 are **Fire, Elec, Cold, Pois, Phys, Mag**. Six resistances,
six bytes, in order.

| column | header | offset |
| --- | --- | --- |
| 5 | `AC` | `+0x34` |
| 8 | `Quest` | `+0x2c` |
| 10 | `Move` | `+0x10` |
| 11 | `AI Type` | `+0x11` |
| 12 | **`Hst`** | **`+0x12`** |
| 14 | `Rec` | `+0x13` |
| 17 | `Type` (attack 1) | `+0x16` |
| 19 | `Miss` (attack 1) | `+0x1a` |
| 20 | `Att%` | `+0x1b` |
| 21 | `Type` (attack 2) | `+0x1c` |
| 23 | `Miss` (attack 2) | `+0x20` |
| 24 | `Use%` | `+0x21` |
| 26..31 | `Fire`, `Elec`, `Cold`, `Pois`, `Phys`, `Mag` | `+0x24`..`+0x29` |

Following the computed pointers finishes almost all of it. The cases that
looked unreadable write through a pointer formed as `lea esi, [row + N]` or
`lea edi, [row]` a few instructions earlier, so the offset is there — just not
in the store:

| column | header | offset |
| --- | --- | --- |
| 0 | `#` | `+0x08` |
| 3 | `LVL` | `+0x09` |
| 4 | `HP` | `+0x30` (a dword; the part before the comma is scaled by a thousand) |
| 6 | `EXP` | `+0x38` (a dword) |
| 7 | `Treasure` | `+0x0a`, defaulting to 100 |
| 9 | `Fly` | `+0x0f` |
| 13 | `Spd` | `+0x3c` (a dword) |
| 16 | `Bonus` | `+0x14` and `+0x15` |
| 18 | `Damage` (attack 1) | `+0x17` and `+0x18` |
| 22 | `Damage` (attack 2) | `+0x1d` and `+0x1e` |
| 25 | `Spl,Mas,Skil` | `+0x22` and `+0x23` |

The two attack blocks come out parallel and six bytes apart — `Type` at
`+0x16` and `+0x1c`, the damage pair at `+0x17`/`+0x18` and `+0x1d`/`+0x1e`,
`Miss` at `+0x1a` and `+0x20`, the percentage at `+0x1b` and `+0x21` — which is
its own check on the alignment.

**Twenty-eight of the thirty-two columns** now have an offset. What is left is
`Picture` and `Name`, which keep pointers, and the `Rec`/`Pref` pair, whose two
cases share the code that writes `+0x13`; which of the two owns it is
`unknown`.

`observed`. Note that `Spd` is **`+0x3c`**, not the `+0x36` an earlier reading
suggested — that offset belongs to the *other* table, the one behind the
pointer at `0x55dd94`.

What the remaining cases do, without yet giving an offset:

- **1 `Picture`** and **2 `Name`** strip a leading quote and run `repne scasb`
  — they keep pointers, and `+0x00` is already known to be the name's.
- **4 `HP`** and **6 `EXP`** split their cell on commas and write through a
  **computed pointer** (`mov dword [esi], ecx`) rather than a literal offset,
  so the scan cannot attribute them; the two unclaimed dwords left in the row
  are `+0x0c` and `+0x14`, and which is which is `unknown`.
- **7 `Treasure`**, **15 `Pref`**, **16 `Bonus`**, **18** and **22 `Damage`**,
  and **25 `Spl,Mas,Skil`** likewise write through computed pointers or share
  a tail. `unknown`.

Seventeen of the thirty-two columns now have an offset, and the shape of every
one that does not is on record rather than guessed at.

### And the peacefulness name is restored

Column 12 is **`Hst`**, and it writes `+0x12`. Last batch withdrew the reading
of that byte as the row's hostility, on the grounds that `0x401b09` copies it
into the actor's animation state at `+0x3e`. The header settles it the other
way: the byte **is** `Hst`, and what `0x401b09` does is *seed* the actor's
state from the monster's hostility, which is a coherent thing for it to do.

So the reputation guard reads as it first did: the hundred-point penalty is
taken only when `Hst` is zero — **for angering something that was not hostile
to begin with**. The withdrawal was over-cautious and is itself withdrawn.
`observed`, now from the file's own header rather than from a fit.

## State 12 approaches; state 2 searches

**State 12, with sight** (`0x404004`), builds the handle and asks for a move:

```
lea  ebp, [esi*8] ; or ebp, 3      ; the actor's handle
lea  eax, [esp + 0x14]             ; a 28-byte buffer
call 0x4046f0                      ; -> a record
mov  ecx, 7 ; rep movsd            ; seven dwords of it, copied out
movsx eax, word [ebx + 0xb2]
mov  cx, word [esp + 0x28]
mov  word [ebx + 0x8a], cx         ; the actor's velocity pair
mov  eax, dword [0x55dd94]
mov  cx, word [eax + edx*8 + 0x36] ; and its speed
```

So `0x4046f0` is a **movement routine**: hand it an actor's handle and a
target and it answers with a 28-byte record, of which the caller takes a
velocity and writes it to `+0x8a`, then scales it by the speed the table at
`0x55dd94` gives for the index at the actor's `+0xb2`. **State 12 is the
approach.** `observed` for every step; `inferred` for the name.

**State 2, without sight** (`0x4043f9`), does something none of the others do
before falling back:

```
push 0x40                  ; 64, the same constant
call 0x4ae22b              ; rand()
and  eax, 0x80000001       ; ... reduced to +1 or -1
push eax
```

— it picks a **random direction** and passes it along, where states 12 and 13
pass none. So **state 2 is the search**: the action for a monster that knows
the party is near and cannot see it, turning one way or the other at random.
`observed` for the roll and the sign; `inferred` for the name.

## `0x4046f0` is "where is this handle", and the handle's own encoding

The routine state 12 calls is not a pathfinder. It opens by taking its
argument apart:

```
mov eax, ecx
and ecx, 7        ; the type tag
sar eax, 3        ; the index
add ecx, 0xfffffffe
cmp ecx, 3
jmp dword [ecx*4 + 0x404b78]
```

— four cases, tags **2 through 5**, and each fetches that kind of thing's
position into the caller's stack buffer. So it answers **where a handle is**,
and what state 12 takes out of the 28 bytes is a position, not a route. The
"movement routine" reading of it is corrected.

The four tags name the game's whole object space:

| tag | what | where its rows live |
| --- | --- | --- |
| 2 | a **projectile object** | 100-byte rows at `0x5c9ad8`; the case reads `+0x04`, `+0x08`, `+0x0c` |
| 3 | an **actor** | 548-byte rows at `0x56f478`; the case reads `+0x7a` and `+0x7e` |
| 4 | a **party member** | a second switch on the index, 0..4; index 0 answers with the party's own position at `0x908c98`, `0x908c9c` and `0x908c70` |
| 5 | a **decoration** | 28-byte rows at `0x5b23cc` |

`observed`. With `handle = index * 8 | tag`, this is the encoding every part of
the game passes around, and it now has all four of its kinds named.

## The decoration table at `0x5b23cc`

Handle type 5's case reads the row's first three dwords, so the array's base
and its shape are settled:

| offset | size | what |
| --- | --- | --- |
| `+0x00`, `+0x04`, `+0x08` | dword | the **position**, read by the handle case at `0x40493c` |
| `+0x12`, `+0x14`, `+0x16` | word | read elsewhere; `unknown` |

Rows are **28 bytes**. `observed`

Two scalars sit immediately before the array and are not columns of it:
`0x5b23c4`, read as a plain dword **sixty-six times** and written in three
places, and `0x5b23c8`, read thirty-four times. `unknown` what either holds,
though a count and a pointer is the obvious pair.

## `+0xb6` is the actor's speed index

State 16 (`0x4034f0`) is short enough to read whole, and it is a **movement
transition**:

```
movsx eax, word [edx*4 + 0x56f52e]   ; the actor's +0xb6
mov  word [esi + 0xa0], 0x10         ; the AI state, 16
mov  word [esi + 0xa2], 5            ; the sub-state
mov  dword [esi + 0xa8], 0           ; the action timer, cleared
mov  eax, dword [0x55dd94]
mov  dx, word [eax + edx*8 + 0x36]   ; the speed, indexed by +0xb6
```

That is the same three-field transition state 4 makes and the same speed
lookup state 12 does. So **`+0xb6` is the actor's index into the table behind
`0x55dd94`**, a second per-actor index beside the monster row at `+0x34`, and
the states that write `+0xa0`, `+0xa2` and `+0xa8` together are the ones that
change what the actor is doing. `observed`

## The monster row, complete

The last four columns split cleanly:

- **1 `Picture`** stores a pointer at **`+0x04`**;
- **2 `Name`** stores one at **`+0x00`**, confirming the column the two
  `repne scasb` sites had already implied;
- **14 `Rec`** is `atoi` into a dword at **`+0x40`**;
- **15 `Pref`** walks the cell's characters and writes **`+0x13`**, which is
  the byte that carries the `E` and `M` rank suffixes.

That completes it. **All thirty-two columns of `MONSTERS.TXT` now have an
offset in the 72-byte runtime row**:

| column | offset | | column | offset |
| --- | --- | --- | --- | --- |
| `Name` | `+0x00` | | `Type` (2) | `+0x1c` |
| `Picture` | `+0x04` | | `Damage` (2) | `+0x1d`, `+0x1e` |
| `#` | `+0x08` | | `Miss` (2) | `+0x20` |
| `LVL` | `+0x09` | | `Use%` | `+0x21` |
| `Treasure` | `+0x0a` | | `Spl,Mas,Skil` | `+0x22`, `+0x23` |
| `Fly` | `+0x0f` | | `Fire` | `+0x24` |
| `Move` | `+0x10` | | `Elec` | `+0x25` |
| `AI Type` | `+0x11` | | `Cold` | `+0x26` |
| `Hst` | `+0x12` | | `Pois` | `+0x27` |
| `Pref` | `+0x13` | | `Phys` | `+0x28` |
| `Bonus` | `+0x14`, `+0x15` | | `Mag` | `+0x29` |
| `Type` (1) | `+0x16` | | `Quest` | `+0x2c` |
| `Damage` (1) | `+0x17`, `+0x18` | | `HP` | `+0x30` |
| `Miss` (1) | `+0x1a` | | `AC` | `+0x34` |
| `Att%` | `+0x1b` | | `EXP` | `+0x38` |
| | | | `Spd` | `+0x3c` |
| | | | `Rec` | `+0x40` |

`observed` throughout, with the six resistances landing consecutively as the
proof of alignment.

**And `Rec` at `+0x40` explains an earlier loose end.** That column was noted
as "a dword doubled while the actor's `+0x134` pair is positive". It is the
**recovery**, and doubling it while a timer runs is exactly what a Slow does —
the same doubling this engine already applies to a slowed monster.

## How the game measures a distance

State 1-and-3 gives it away. It differences the stored pair at `+0x92`/`+0x94`
against the position at `+0x7e`/`+0x80`, takes both absolute values, and then:

```
cmp ecx, eax
jle ...
sar eax, 1
add eax, ecx        ; max + min / 2
```

That is the **octagonal approximation** — no square root anywhere. So every
threshold the AI tests, the awareness cut at **5120** and the second at
**1024**, is against `max + min / 2`, which overstates a diagonal by up to
about twelve percent against a Euclidean length. `observed` at `0x403b99`.

What the action itself is doing with that distance is measuring **how far the
actor has strayed from a post it remembers** — the stored pair is a position,
differenced against where it stands now. `inferred` for the name.

**State 10** (`0x404660`) opens exactly as state 12's sighted branch does: the
actor's pointer, the handle `index * 8 | 3`, and a stack buffer handed to the
position routine. So it too asks where something is before it acts, and its
one distinguishing feature remains that nothing inside the AI calls it.
`observed`

State 7 was not reached.
