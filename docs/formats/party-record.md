---
title: "Party runtime record"
summary: "Observed structure and partially named fields of the Might and Magic VI in-memory party record."
doc_type: reference
status: partial
last_updated: 2026-08-01
tags:
  - mm6
  - runtime
  - party
  - memory-layout
---
# The party record (`MM6.exe`, runtime)

Status: **structure settled, fields partly named.** Not a file format: this
is the in-memory party the executable keeps at **`0x908c70`**. Every claim is
tagged `observed` (read from an instruction) or `inferred`.

## Scope

This page covers the contiguous party record at `0x908c70`, the embedded four
player records, and party-level fields established from executable access
patterns. Player-field details are canonical in
[`player-record.md`](player-record.md).

## It is one record, not scattered globals

Tallying every absolute reference into `0x908c00`..`0x909100` gives **123
distinct offsets** in a dense, regular run from `0x908c70` upward, with no
gaps large enough to separate one structure from another. The arithmetic
closes it:

- header of **`0x2c4` = 708 bytes** from `0x908c70`,
- then the **four character records** of `0x161c` = 5660 bytes each, starting
  at `0x908f34` — which is exactly where the character array is known to
  begin,
- ending at **`0x90e7a4`**, which is exactly the bound Power Cure's case
  loops to when it heals the whole party.

Two independently-found numbers meeting at both ends is the confirmation.
`observed`

## The fields named so far

| Offset | Refs | Field | Where it was read |
| --- | --- | --- | --- |
| `+0x000` | 124 | the record's own base | passed as `this` throughout |
| `+0x028` | 155 | position **x** | the spell launcher, the AI's reach tests |
| `+0x02c` | 155 | position **y** | the same |
| `+0x030` | 120 | position **z** | the same |
| `+0x034` | 71 | facing | the launcher aims with it |
| `+0x038` | 37 | a second angle | beside the facing |
| `+0x098` | 239 | the **world clock**, low half | the calendar routine |
| `+0x09c` | 180 | the world clock, high half | the same |
| `+0x0d8` | 28 | a counter the AI subtracts 100 from | `0x403086` |
| `+0x0fc` | 11 | the **fatigue byte** | incremented hourly, zeroed by rest |
| `+0x1c4` | — | the **spell-buff array**, 16 records of 16 bytes | `0x47d170` clears it |
| `+0x2c4` | 99 | the **four character records** begin | everywhere |

`observed` throughout. The buff array running from `+0x1c4` for exactly 256
bytes and ending precisely where the characters begin is a second structural
check on both.

The world clock at `+0x098` is the most-referenced field in the executable's
whole data segment at 239 sites, which is what a clock every routine consults
should look like.

## Four of the dense ones, named

Taking the offsets by reference count and reading what touches them.

**`+0x1c0` (73 references) is the turn-based flag.** Every site compares it
against **1**, and the branch is decisive: at `0x403f27`, when it is 1 the
actor's recovery counter at `+0x6c` is set directly, and otherwise the value
is scaled by a float and the double **−32/15** — the same constant the
real-time recovery uses. So one branch is the turn-based clock and the other
the running one. `observed`

**`+0x0fd` (57 references) is the quest-bit array.** `0x4031bf` loads it as
`this` and calls `0x43fdf0` with a number and a set/clear flag; that routine
shifts the number right three for a byte index and masks the low three bits
for the bit — a plain bit array. The site sets **bit 202**. `observed`

**`+0x0e0` (41 references) is script variable 20**, a party-level dword. The
script-variable getter's own jump table lands variable 20 on it, and the
interface formats it with `%lu`. Which quantity variable 20 is remains
`unknown` — the engine names 16, 17, 21, 22, 12, 3, 5, 13 and 23, and 20 is
not among them.

**`+0x074` (44 references) travels with the position.** At `0x425e9e` it is
written in one group with `+0x030`, `+0x034` and `+0x038` — the z, the facing
and the second angle — which makes it part of a position-and-heading
snapshot rather than a field of its own. `observed` for the grouping;
`inferred` that it is a snapshot.

## What is measured but unnamed

Ninety-odd further offsets carry references and no name yet. The densest of the
remainder, worth a later sitting: `+0x078` (29), `+0x008` (22), and the run
of four at `+0x0ac`, `+0x0b0`, `+0x0b4`, `+0x0bc` (22–24 each).
`unknown` for all of them; the reference counts are recorded so the next
attempt can start with the ones that matter.

## `+0xd8` is the reputation, and what a kill actually pays

The search that ran here was for the experience a kill pays. It did not find
one — see the count of every touch of `+0x1420` in the player record — but it
found what a kill *does* pay, in the death handler itself.

`0x403730` is the actor-death handler: it reads the AI state at `+0xa0`, sets
the death bit `0x20000` in the flags at `+0x24` when the state is **7**, sets
the death animation `+0x3e = 4`, and then, once per death and with no test
above it:

```
0x00403778  mov eax, dword [0x908d48]
0x0040377d  sub eax, 0x32              ; 50
0x00403780  mov dword [0x908d48], eax
```

`0x908d48` is party `+0xd8`, which this document has carried as "a counter".
It is the **reputation**. Three instructions give it its whole span:

| where | what |
| --- | --- |
| `0x403778` | **-50** for every actor death |
| `0x403086` | **-100** somewhere else |
| `0x43c598` | tests it against **-1000** |

and what waits at `-1000` names the scale: the branch resets the counter,
increments `0x908d60`, and grants award **83** to all four characters — which
`Awards.txt` reads **"Served %u Prison Terms"**. So the bad end is `-1000`,
prison is the penalty, `0x908d60` counts the terms served, and a single death
is a twentieth of the way there. `observed`

Script property id **214** writes the same global, which is how a map event
moves the party's standing.

## The runtime monster table, indexed

While looking for the experience column, the table's own indexing came out.
`0x403efc` shows it whole:

```
mov al, byte [ebx + 0x34]      ; the actor's monster row
lea ecx, [eax + eax*8]         ; x9
mov eax, dword [ecx*8 + 0x56c1c8]   ; x8 -> a stride of 72
```

So the rows are **72 bytes** from a base of `0x56c188`, and five columns are
referenced anywhere in the image: `+0x00`, `+0x09`, `+0x12` (a byte, seven
readers), `+0x38` and `+0x40`. **None of them feeds an experience
accumulator**, which is the other half of the negative. `observed`

`+0x40` is worth a line of its own: `0x403f21` **doubles it** while the
actor's `+0x134`/`+0x138` pair is positive — one more behaviour gated on a
64-bit actor timer that nothing ever writes.

## The globals beside the reputation

`0x403070` is where an actor turns hostile — it ends by setting the AI state
at `+0xa0` to 4 and the sub-state to 5 — and the second penalty sits above it
behind two guards:

```
mov  al, byte [edx*8 + 0x56c19a]   ; the monster row's byte at +0x12
test al, al
jne  skip                          ; only when that byte is zero
mov  al, byte [0x908dbd]           ; a party flag
test al, al
jne  skip
sub  dword [0x908d48], 0x64        ; -100
```

So the hundred is for **angering something that was peaceful**, and a flag at
party `+0x14d` can waive it. `observed` for the guards and the amount;
`inferred` that the monster row's `+0x12` is the row's peacefulness, since it
is the only thing that decides whether provoking the creature costs anything.

Two more party globals fall out of the same search, both counters that are
handed out as awards:

| offset | global | what |
| --- | --- | --- |
| `+0xd8` | `0x908d48` | the **reputation** |
| `+0xe8` | `0x908d58` | the **deaths**, counted up at `0x453d52` beside award **82**, `Awards.txt`'s "%u Deaths" |
| `+0xf0` | `0x908d60` | the **prison terms**, counted up at `0x43c60d` beside award **83**, "Served %u Prison Terms" |
| `+0x14d` | `0x908dbd` | the flag that waives the provocation penalty, `unknown` otherwise |

`observed`. Both counters and the reputation now survive a save.

## `+0x95` and `+0x96`: interface offsets, not hireling slots

Withdrawing the hireling reading left these two bytes unnamed. Following their
five sites names their *shape*, if not their subject.

- `0x42dc32` reads `+0x95` as a **count**, adds it to a running index and
  compares the sum against a bound before drawing a row.
- `0x42eb52` draws `GLOBAL.TXT` row **90 — "Forward"** — when `+0x95` is
  non-zero.
- `0x42eb82` writes it and `0x42eb8d` clears it, both in the same routine.
- Property id 214 clears both in the same breath as it flags a roster record.

A count added to a list index, with a **"Forward"** control shown when it is
non-zero, is a **scroll offset**. `observed` for the reads, the bound and the
string; `inferred` that the pair are offsets into two lists, from that shape.

Which lists is `unknown` — but it explains why the hireling reading was wrong.
Property id 214 clears them because flagging somebody in the roster changes
what a list shows, not because the bytes held the hirelings. The party's
hirelings are the two sixty-byte records at `0x90e7a4`.

## The party's two hireling records, counted

They sit at `0x90e7a4` and `0x90e7e0`, sixty bytes each, in the roster's own
shape. Sweeping every byte of both for absolute references gives this:

| offset | slot 1 | slot 2 | what |
| --- | --- | --- | --- |
| `+0x00` | **79** | **39** | the person's **name**, a pointer |
| `+0x01` | 2 | — | `unknown` |
| `+0x04` | 1 | — | `unknown` |
| `+0x14` | 1 | — | a dword a script sets |
| `+0x18` | 4 | 4 | the **profession** id |
| `+0x24` | — | 1 | `unknown` |
| `+0x34` | 4 | 4 | a dword, written at `0x48822a` and `0x4961c3`; `unknown` |

`observed`. **Seven of the sixty bytes are reached by absolute address at
all**, and the name accounts for well over half of every reference. So what
the game keeps about a hireling, in practice, is who they are and what they
do; the rest of the record is barely touched.

**And the premise this was meant to serve was already met.** The motivation
given was that a hireling could not survive a save. It can: `SaveState`
already carries a `hired` list of npc id, profession and name, and round-trips
it. Adding a second store for the same thing was tried here and reverted; the
existing one is the right shape and matches what the two records actually
hold.
