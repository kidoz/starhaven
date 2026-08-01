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

Reading every site that touches them settles their shape, if not yet their
names.

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
