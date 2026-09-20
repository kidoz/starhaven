---
title: "Map event scripts"
summary: "Container framing, opcode semantics, and runtime joins for Might and Magic VI map scripts."
doc_type: reference
status: partial
last_updated: 2026-09-19
source_files:
  - src/core/world/collision.cpp
  - tests/test_collision.cpp
  - src/game/script_object_effects.cpp
  - src/game/script_loot.cpp
  - tests/test_script_loot.cpp
  - src/core/world/map_script.cpp
  - src/game/script_walk.hpp
  - src/game/party_event.cpp
  - tests/test_party_event.cpp
  - src/game/script_faces.cpp
  - src/game/script_objects.cpp
  - src/game/temporary_objects.cpp
  - tests/test_temporary_objects.cpp
  - tools/evt_info.cpp
tags:
  - mm6
  - events
  - scripts
  - opcodes
---
# Map event scripts (Might and Magic VI)

Status: **verified** for the container and record structure. The original
dispatch table below distinguishes observed handlers from tentative semantic
readings. Current engine coverage is measured separately in the
[event-script audit](../explanation/event-script-coverage.md), including its
distinction between dispatch presence and partial runtime behavior. Each claim is tagged
`observed`, `inferred`, or `unknown`.

## Scope

Covers where a map's script lives, how its records are framed, how a face
names one, and the opcodes that have been pinned down.

## They are not in `Games.lod`

`Games.lod` holds only geometry and saved state — 52 `.blv`, 52 `.dlv`, 15
`.odm`, 15 `.ddm`, and nothing else. `observed` The scripts are in
**`icons.lod`**, alongside the design tables: **83 `.EVT`** entries and
**76 `.STR`**, 738 to about 2,000 stored bytes each. Sixty-seven of the
scripts are the maps'; the other sixteen have no map of their own — see
"The scripts without maps" below. `observed`

That is worth stating because two earlier searches for map events looked in the
wrong file. The `.odm` payload is now accounted for byte for byte with no room
for a script, and the `.ddm`'s undecoded prefix is 1,947 zero bytes out of
1,948 on New Sorpigal — saved state that starts empty, not code. `observed`

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product and edition | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Archive | `data/icons.lod` |
| Scripts | 83 `.EVT`, 15,504 records `observed` |
| Strings | 76 `.STR`, 1,274 strings `observed` |
| Executable | `MM6.exe`, 857,720 bytes, SHA-256 `28d2b83e75db45134d161da1da767afcbdb3e381921d3de61c2784ac85cd00ce`; PE32 i386, image base `0x400000`; analysis read-only in radare2 6.1.8 `observed` |

Reproduce with:

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/evt_info D01          # events and strings
./buildDir/evt_info OutE3 5      # one event, step by step
```

## The container

The same 48-byte container the design tables use, with the unpacked size at
`0x28` and zlib after it; zero there means the entry is stored as it is. See
[`text-tables.md`](text-tables.md). `observed`

## A script is a run of size-prefixed records

| Offset | Size | Type | Field |
| --- | --- | --- | --- |
| +0x00 | 1 | u8 | bytes that follow |
| +0x01 | 2 | u16 | event id |
| +0x03 | 1 | u8 | sequence within the event |
| +0x04 | 1 | u8 | opcode |
| +0x05 | n | | arguments |

**All 83 scripts are consumed exactly** by this walk — 15,504 records, no bytes
left over on any map — and event ids are non-decreasing, so an event's steps
are contiguous. `observed`

Sequence numbers count from zero within an event: 5,176 records are step 0,
1,407 are step 1, 1,115 step 2, and so on down. `observed`

**90 distinct opcodes** appear in the raw bytes, but the executable dispatches
only **1..43** (see "The complete opcode table"): opcodes 44..53 are a six-use
template and 54..90 are single-use trailing junk, all skipped by the executor's
default case. The most common are 4 (2,192 uses), 14, 1, 15, 16, 18, 29 and 30.
Argument lengths are fixed per opcode.

### Three of them are named

Testing whether an argument indexes the string table is not decisive on its
own — most small numbers are valid indices either way. Two things together are:
the argument must **never** leave its map's own string count, which varies from
about 10 to 100 across maps, and it must **use** that range rather than staying
small.

| Opcode | Uses | Out of range | Median of max ÷ strings | Reading |
| ---: | ---: | ---: | ---: | --- |
| 29 | 650 | 0 | 0.78 | show a message |
| 30 | 132 | 0 | 0.76 | show a longer message |
| 35 | 142 | 0 | 0.23 | name what is being looked at |
| 5 | 57 | 0 | — | the map's own name |
| 4 | 2,061 | **522** | — | not a string index |

What the strings say confirms it. Opcode 29 points at `"The door is locked."`,
`"Refreshing!"`, `"You pick an apple."`, `"Poison!"`, `"+2 Luck permanent"`.
Opcode 30 points at `"Etched into the tree a message reads: ..."`. Opcode 35
points at `"Door"`, `"Sign"`, `"Chest"` — the noun for a thing you are looking
at, which is exactly what `D01.STR`'s list of `"Exit Door"`, `"Chest"`,
`"Switch"`, `"Door"` is for. `observed`

### A fourth: opcode 5 says where you are

Opcode 5 appears 57 times and its argument is a string on all of them. Of the
54 that resolve, **53 point at the map's own display name** in `MapStats.txt` —
`CD1` at "Castle Alamos", `CD2` at "Castle Darkmoor", `D01` at "GoblinWatch" —
and one points at another map's. `observed` So it announces the place rather
than travelling to it.

### Opcode 4 opens an event

Of the 3,332 events across the 83 scripts, **2,182 contain opcode 4, and every
one of those begins with it** — 2,182 of 2,182. It appears anywhere else only
ten times. `observed` So it is the event's opening step rather than an action:
whatever it declares applies to the event as a whole.

Correlating its byte against what each event's body does splits it in two:
on events whose body enters an establishment, the header equals the enter
step's `2DEvents.txt` row id on **620 of 633** — and both are compared by
low byte, which is what the 13 misses look like, ids past 255 truncated.
On chest, door, fountain and message events the header equals nothing about
the body (39 of 346 against the chest id, 38 of 645 against the door, chance
level) because it is not about the body at all: read as an index into the
map's own `.STR`, it names a non-empty string on **1,523 of 1,542** such
events, and the strings are the interactable nouns — "Door" 419 times,
"Chest" 244, "Lever" 51, "Switch" 50, "Exit" 44, "Drink from Fountain" 33,
"Burial niche", "Suspicious Floor". GoblinWatch's five levers head their
events with 9..13, and its strings 9..13 read "B", "C", "D", "E", "F".
`observed` Reproduce with `evt_info --headers`.

So the header is **what the thing calls itself** — the label the original
shows when the cursor rests on it. An establishment's door is the one case
that stores an id in another table instead, and there the original has a name
to show anyway: the establishment's own, from `2DEvents.txt`. On the 19
misses the index lands on an empty string or past the table.

One reading was tested against `Trans.txt` and fails, recorded so it is not
tried again: its filled prose rows are ids 1, 2 and 153..233, and every
header that "hits" one is a header of value 1 or 2 — base rate, not a join.
Excluding those two ids resolves 0 of 1,542.

### Events without a header are a different kind

Of the 3,332 events, 2,182 open with opcode 4 and **1,150 do not**, and the two
groups do not use the same opcodes. `observed`

| Group | Events | Opcodes, most used first |
| --- | ---: | --- |
| headed | 2,182 | 4, 14, 15, 1, 2, 18, 29 |
| unheaded | 1,150 | 1, 14, **30**, **16**, 18, **32**, **36** |

An unheaded event opens with 30, 14, 15 or 6 rather than 4, and its id sits far
higher: median 45 against 27, and up to **808** where a headed event never
passes 262. `observed` Reproduce with `evt_info --unheaded`.

With the conditional machinery decoded, the class comes apart into pieces
rather than one thing:

- Part is a **framing artifact**: `OUT.EVT` and its kin carry no sequence
  byte (see "The scripts without maps"), so the map-script walk misreads
  their records into one-use opcodes 54..90 and inflated ids. Those are not
  events of a second class; they are another format.
- The rest, spread across every map's script, open with the ordinary working
  opcodes — a check, a long message, a door, a travel — and read as event
  bodies that simply begin with work instead of a header. What, if anything,
  the missing header withholds from them is still `unknown`.

The `NPCdata.txt` lead closed twice and reopened once: its event columns are
`npctopic.txt`/`npctext.txt` ids resolving 298 of 298 — *and* those same ids
are `GLOBAL.EVT` events on 170 of the 298, because topic, prose and logic
share one id space. See "The quest bank speaks" below. What they are not is
ids of the *map* scripts' events, which is all the original test tried.

### Faces fall back to the shared script

What did fall out of the reattack is a join: of the **88** face event ids
that resolve in no map's own script, **66 are `GLOBAL.EVT` events** — the
quest bank is reachable from doors and switches in the world, not only from
dialogue. The engine walks it: a face whose event its map does not define
runs the global script's event instead. `observed`

### The quest bank speaks `npctext.txt`, and topics are its events

`GLOBAL.EVT` has no `.STR`, and the table its message indices name was found
by content, not by range. Its event 1 reads, decoded: check for item 505,
and on the pass say index 1, give 1,000 gold, clear quest bit 81 and set
bit 82; on the fail say index 3. Item 505 is **The Letter**; bits 81 and 82
are the journal's *show Sulman's letter to Andover Potbello* and *bring it
to Regent Humphrey*; and `npctext.txt` rows 1 and 3 are *"Oh!  The Seal.
Here, I'm supposed to give you this money."* and *"Since you don't have a
letter with a Seal, you get no money!"* — the two branches, word for word.
`observed`

So **topic id, `npctext.txt` row and `GLOBAL.EVT` event share one id
space**: the label, the prose and the logic. 170 of the 298 topic ids the
NPC table hands out are global events — the earlier conclusion that the
overlap was a coincidence of range is withdrawn; the 128 without an event
are plain conversation. `observed` The engine runs it both ways: a face that
points into the bank speaks through `npctext.txt`, and asking a quest giver
about such a topic walks its event — Andover Potbello takes the letter's
seal, pays the thousand gold, and the journal moves on. An earlier note here
said a global walk acts without speaking; it speaks now.

`GLOBAL.TXT`, tested first, fit the range and failed on content: it is an
alphabetized word list, not prose.

### What was ruled out looking for the opcode that enters a building

Opcode 4 is the commonest at 2,192 uses and remains `unknown`. Three readings
were tested and fail:

- **a string index**: its argument leaves the map's string range 522 times;
- **an index into a per-map count** — decorations, actors, objects, chests,
  facets, models: the values that never exceed a count use so little of it
  (1% to 13% of the range) that the bound is not evidence;
- **a `2DEvents.txt` building id on that map**: 352 of 846, 42%. Opcodes 2, 7
  and 19 score 55%, 9% and 21% on the same test, which is what chance looks
  like against a set that large;
- **the subject the next step acts on**: on a house the pair reads
  `op4(39) | op2(39,1,0,0)` and on a creature `op4(1) | op7(1)`, which looks
  like the argument being repeated — but across all scripts it matches the next
  step's first byte only 542 times against 1,636, 25%. The sampled cases had
  small ids and agreed by arithmetic accident.

### What the shape of an event does say

Grouping the 1,758 outdoor event facets by the model they sit on, and counting
the opcodes each group's events use, separates them cleanly. `observed`

| Model kind | Events | Opcodes, most used first |
| --- | ---: | --- |
| house (`Hse*`) | 96 | **2** (119), 4 (116), 1 (39) |
| sign | 48 | 4 (102), 29 (52), 1 (50), **2** (46) |
| fountain | 10 | 18 (171), 29 (78), 1 (61), 4 (55) |
| chest | 36 | 7 (429), 4 (352), 14 (66) |
| creature | 27 | 4 (135), 7 (135) |

**Opcode 2 appears on houses and signs and on no fountain, chest or creature**,
and it is the entry: its argument is a **`u32` `2DEvents.txt` row id**, and
**474 of the 504** distinct values across the fifteen outdoor maps are ids of a
building on that very map. `observed`

The counts settle it independently. Per map, the number of distinct opcode-2
arguments tracks the number of establishments the design table places there:

| Map | Establishments | Distinct opcode-2 arguments |
| --- | ---: | ---: |
| Frozen Highlands | 52 | 53 |
| Free Haven | 95 | 93 |
| Mire of the Damned | 40 | 42 |
| Dragonsand | 12 | 12 |
| Misty Islands | 30 | 29 |

An earlier pass read this argument as a single byte and scored 4 of 35, which
looked like a dead lead. The ids run past 255; the argument is four bytes
wide.

### Opcode 6 moves the party

A transition has to name where it goes, and 99 of opcode 6's 242 uses carry an
ASCII map file name in their arguments — no other opcode carries one anywhere
in the 15,504 records, apart from two designer leftovers in scripts no shipped
map uses. `observed` Reproduce with `evt_info --scan`.

Note when measuring this yourself: two of the 83 scripts — `D08.evt` and
`Pyramid.evt` — ship with a lowercase extension, so a case-sensitive filter
scans 81 and quietly loses both maps' transitions. An earlier pass here did.

The argument's shape, from `evt_info --transitions`:

| Offset | Size | Field | Evidence |
| --- | --- | --- | --- |
| +0 | 4×i32 | X, Y, Z, facing/yaw | coordinates within world range; facing ≤ 1920, under MM6's 0..2047 angle scale |
| +16 | i32 | pitch | vertical facing angle |
| +20 | i32 | vertical speed | carried into the destination transition |
| +24 | u8 | house id | destination house/interior id |
| +25 | u8 | exit picture id | transition picture selector |
| +26 | n | destination, NUL-terminated | `"0"` on 126 uses, a map file name on 93 |

Of the 99 named destinations, **97 are maps the design table lists**; the
two others are `d8.blv`, twice, in `LWSPIRAL.EVT` — a script with no shipped
map, naming a file that is not in `Games.lod` (`D08.blv`'s own script spells
its name with the zero). Thirteen more uses carry zero or one argument byte
and cannot travel. 130 say `"0"`. `observed`

The pairs settle the reading independently: they are **symmetric**. Every
dungeon's exit names its region's outdoor map and that outdoor map's entrance
names the dungeon back — `D01 -> OutE3.Odm` and `OUTE3 -> D01.Blv`,
GoblinWatch and New Sorpigal, and so on across the world. A destination of
`"0"` with coordinates is a teleporter within the map. `observed`

The engine walks them: using an exit door loads the named map through the same
loader the command line uses and stands the party at the recorded X, Y, Z,
facing where the record says. How the 0..2047 facing maps onto the renderer's
yaw is `inferred`.

### The scripts without maps

Thirteen of the 83 scripts name no map: `GLOBAL`, `OUT`, `LWSPIRAL`,
`D4BM`, `DBM1`..`DBM5`, `DDB1`, `DWJ1`, and `ZDTL1`/`ZDTL2`. `observed`
An earlier count reached sixteen by comparing names with case: `SCI-FI` is
`Sci-Fi.Blv`'s script — the Control Center, the Oracle's sibling — and
`ZDTL01`/`ZDTL02` are `zdtl01.blv`'s and `zdtl02.blv`'s.

`OUT.EVT` stands apart in format: its records carry **no sequence byte** —
`[size][id u16][opcode][args]` where every map script writes a sequence
before the opcode. Read that way it is 89 events, no strings, and almost all
of them are two-step stubs: a header carrying its own event id, then an
enter with a one-byte argument of zero. Three records do more: event 1 also
names a title, and events 89 and 266 are travels — `Sewer.blv` at
(1086, -1786, 945), which is Free Haven Sewer, and `sub03bz.blv`, a file no
archive ships. An earlier reading here — that the 89 events were a table of
travel destinations — was wrong: two are. What the stubs are for is
`unknown`. `observed` for the framing and contents; reproduce with
`evt_info --out [stem]`.

`GLOBAL.EVT`, by contrast, frames exactly like a map script and is dense
with the conditional machinery — checks of quest items, gives of experience
and gold, long messages. It reads as the shared quest-event bank; which
events point into it is `unknown`.

### Opcode 7 opens a chest

Chests are equally distinctive — opcode 7 is theirs, 429 uses against a handful
elsewhere — and its argument is an index into the event file's fixed **20-slot**
chest array. The largest value across all 65 scripts is **19**, and on 37 of
them the values used are exactly `1..N` with no gaps. `observed`

The shipped chests are empty: every one of the twenty slots on every map reads
`-1`, because the original fills them on first visit from the map's own
`Tres 0-6` treasure level. So an engine has to roll the contents, which
StarHaven does through the same generator the rest of the game's loot uses.
How many things a chest holds is `inferred`.

### The conditional machinery

Seven more opcodes fall together as one machine. Reproduce with
`evt_info --variables`.

| Opcode | Uses | Shape | Reading |
| ---: | ---: | --- | --- |
| 14 | 1,951 | `[type u8][value u32][step u8]` | check, jump on pass |
| 16 | 1,090 | `[type u8][value u32]` | give / add |
| 17 | 330 | `[type u8][value u32]` | take / subtract |
| 18 | 948 | `[type u8][value u32]` | set |
| 36 | 368 | `[step u8]` | goto |
| 1 | 1,653 | `[u8 0..2]` | end of event |
| 15 | 1,136 | `[door u8][state u8]` | open or shut a door |

The step bytes are what settles the control flow: opcode 14's trailing byte
is a sequence number of **its own event on 1,951 of 1,951 uses**, and opcode
36's byte on 368 of 368. Opcode 1 closes essentially every event and its byte
never passes 2. Opcode 15's first byte is **not** a step (97 of 1,142) — it
runs 1..64 with a second byte of 0..2, a door and a state. `observed`

The four typed opcodes share one type vocabulary, and three types are
established by their closed sets:

- **type 17 is an item id**: across 641 uses it never leaves 1..578, and 578
  is exactly the item table's last row. What opcode 16 gives spans the whole
  table; what opcode 17 takes is 433..578 — the quest-item rows, which is
  what a turn-in takes off you. `observed`
- **type 16 is a quest bit**: checked at 1..376 of the 512 bits `Quests.txt`
  holds, and the bits opcode 17 clears sit at 81..235, the same region whose
  rows carry journal text. `observed`
- **type 21 is gold**: round amounts, 5 to 250,000. `observed`
- **types 105 and up are numbered variables**, not one global enum: D01's
  five switches use five consecutive ids, one each. `observed`
- **type 25 is the temporary Might bonus** — named by the event that sets it
  (below); 26..31 as the remaining attributes is `inferred`, with the order
  settled by the prose join: Speed at 29 and Accuracy at 30, the reverse of
  the sheet's.

More types told their names by a **prose join**: a reward's give and the
number its event speaks sit side by side, and where the spoken number equals
the given value, the word after it names the type. Reproduce with
`evt_info --currencies`.

- **type 13 is experience**: "experience" beside a matching give on 7
  events, values 400..500,000 in big round numbers next to gold gives of the
  same magnitude — the Goblinwatch reward gives 2,000 gold and 2,000 of
  type 13 under "Here's your gold!". `observed` for the joins, `inferred`
  for the 58 unspoken uses.
- **type 23 is food**, 1..10 a give. **Type 3 is a cure's hit points and
  type 5 its spell points** — "restores 15 hit points" beside a give of 15.
- **Type 205 is the autonote chronicle**: its values are `Autonotes.txt`
  rows — the Seer writes 116 ("show the sixth letter to Andover
  Potbello") beside bit 81's stage and 115 (the Ironfist letter) beside
  bit 82's, the notes wording exactly the stages their events speak.
  `observed` for the Seer's ladder, `inferred` for the rest; verified in
  `evt_info --arc`.
- **A promotion event's two branches**: passing the class check jumps to
  the promotion — the class is set one rung up and the *lower* of the
  pair's two awards is given — while falling through gives the higher,
  the honorary one. `Awards.txt` names them in exactly that order
  ("Received Promotion to Hero" at 10, "...to Honorary Hero" at 11), and
  all six ladders follow the shape. `observed`, verified in
  `evt_info --arc`.
- **Type 2 is the character's class id, checked by equality**: the
  promotion events check it against the qualifying class and set it to
  the next rung — equality, not at-least, or their honorary branches
  could never fire and a promoted class would qualify again — the Crusader
  event turns 9 into 10 beside award 8, the Wizard 6 into 7 beside award
  12 — and **type 214 reads as "this person follows"**, checked against
  NPCdata row ids (the Prince of Thieves at 17, the Crusader's charge at
  11). `inferred`, verified by `evt_info --arc`.
- **types 32..38 are the seven attributes, given permanently** — "+2 Luck
  permanent" is a barrel's own string — in the same Speed-before-Accuracy
  order as the temporaries.
- **types 46..50 are the five resistances** in the monster table's element
  order, Fire, Electricity, Cold, Poison, Magic, given permanently.
- **type 12 is an award**: its value is a filled `Awards.txt` row on 193 of
  193 checks, gives and sets — and the one a known quest sets, Goblinwatch's
  53, reads "Solved the Goblinwatch Combination", the very quest whose
  reward event sets it. The engine wears them: the sheet lists the honors
  in the table's own words.
- **type 22 is found gold**: its gives sit where things are dug up rather
  than handed over — D05's dig events, headed by the map's own "Gold vein"
  string, pay 400..800 on the mine's random branches; the sewer's
  "Something's stashed here!" pays 1,000..2,000; D13's piles a rising
  1,000..3,500 — always round sums, never beside a spoken noun, in events
  that pay type 21 elsewhere. That it is gold in the purse is `inferred`
  from the finds' labels; it is the "gold you find" the Factor's and
  Banker's profession rows take their percent of, and the engine pays that
  percent on exactly these. Fame and reputation were tried first and are
  recorded refuted-in-range: both columns top out short of 3,500.

StarHaven applies the largest hired found-gold bonus once after each event
segment and publishes the resulting purse before another interaction or save.
The reward message includes that same bonus. Type-21 payments do not receive
it; fractional bonus gold rounds down. The bonus saturates at the engine's
integer purse limit. Choosing the largest benefit remains an `inferred`
stacking policy, exercised by `tests/test_party_event.cpp`.

Whole events confirm the readings. D01's switches all read: check my
variable, jump to the end if it is set, set it, throw four doors — two open,
two shut, mirrored between paired levers. New Sorpigal's fountain, event 150,
is a complete if-else: check the Might bonus at 10, jump past the work when
it is already on; else set it to 10, say `"+10 Might temporary."`, and goto
the end over the `"Refreshing!"` branch. And Castle Alamos's exit, event 47,
is a **gated door**: check quest bit 54 and jump to the travel step, or fall
into the end and go nowhere. `observed`

Whether a passing check means *at least* or *exactly* the value cannot be
told from flows that check against 1 and 10; the engine reads it as at least.
`inferred`

The engine walks all of it — src/game/script_walk.hpp — so a gated exit
stays shut until its bit is set, a switch throws once, and a turn-in takes
the quest item and pays.

### StarHaven acting-character context

The live adapter initializes class checks from the selected character-sheet
member, or member zero when no sheet is selected. `walk_party_event` in
`src/game/party_event.cpp` captures that actor and class at interaction entry.
Class values written by the script remain available through modal-message and
riddle continuations, even if the displayed member changes. Each fresh
interaction reads the live character again; a previous event's class variable
does not determine the next event's branch.

This follows StarHaven's existing acting-member policy. Original selector
opcodes, per-character awards, and direct application of every class write
remain separate compatibility work; promotions still use the award-driven
adapter. Synthetic coverage lives in `tests/test_party_event.cpp`.

### Opcode 11 repaints a face

Its arguments are a u32 and a NUL-terminated name, and the names decide it:
across all 83 scripts, **215 of the 215 named uses are `BITMAPS.LOD`
entries**, and the vocabulary is state — `t1swdu` and `T1swDd` are the
T-set's switch drawn down, `T3S1ON`/`T3S1OFF` a thing on and off, `lavatyl`
and `orwtrtyl` lava and water tiles, `SKY_NIT1` a night sky. So a thrown
lever is drawn thrown: the u32 names the face and the name its new texture.
`observed` for the names; the u32 as a face index is `inferred` from its
range (to 5,290, within the indoor maps' face counts). A first reading of
the name as a sound effect was tested against `DSOUNDS.BIN` and failed —
0 of 215 names are sound names. Reproduce with `evt_info --textures`.

The engine applies it indoors: the walker collects repaints in order with
opcode-23 attribute changes, and the face wears its new texture. Version-6 saves
preserve both results. If the animation bit is already set, the original resolves
the name through DTFT first; a failed lookup clears that bit and resolves a static
bitmap (`0x43e530`–`0x43e60c`, `observed`). The engine follows that named lookup.
The few outdoor uses are not applied yet.

### Opcode 13 changes a placed decoration

Opcode 13 selects a zero-based decoration in the currently loaded map, changes
its descriptor by name, and sets its visibility. The old “set/compare variable”
label was an inference and is superseded by the handler trace. This operation
changes world presentation within quest events; it does not directly mutate
quest or NPC variables. `observed`

| Argument offset | Type | Meaning |
| ---: | --- | --- |
| 0 | i32 LE | Placed-decoration index; negative or out-of-range indices do nothing |
| 4 | u8 | Zero hides; any nonzero value shows |
| 5 | NUL-terminated string | Case-insensitive DDECLIST descriptor name; `"0"` keeps the existing descriptor |

The minimum bounded layout is six bytes (an empty name). The name must
terminate inside the record. StarHaven rejects short or unterminated records
without changing state. An empty or unknown name resolves to descriptor zero,
matching the original lookup's fallback. Index validation precedes all world
changes. The operation continues to the next event step. `observed` for the
original lookup, index bounds and effects; bounded rejection is engine policy.

The original handler at VA `0x43db94` reads the index and visibility, indexes
28-byte placed records, and uses the 80-byte DDECLIST table for names.
VA `0x43dc38` replaces the descriptor ID and prepares its sprite; VA
`0x43dc49` clears or sets placement flag `0x20`, preserving other flags.
The comparison at VA `0x4af370` is case-insensitive. These addresses apply only
to the user-owned GOG executable, 857,720 bytes, SHA-256
`28d2b83e75db45134d161da1da767afcbdb3e381921d3de61c2784ac85cd00ce`.
Analysis: radare2 6.2.2, 2026-09-10; static evidence, without running the original.

Reproduce the data and engine integration check:

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/evt_info --decorations
```

On the icons.lod sample pinned in the coverage audit, **97 of 110 records**
decode completely and apply to valid indices in **22 loaded maps**. There are
75 descriptor replacements and 22 retain-descriptor operations; 81 show and
16 hide. All replacement names resolve. The other 13 records have zero or one
argument byte, in DBM1–5, DDB1 and OUT templates. They are rejected, not counted
as successfully executed instructions. `observed`

The quest-associated examples `CD1 / 59 / 2` and `CD1 / 60 / 3` both update
decoration 394; `OUTE3 / 226 / 7` updates decoration 339. These joins replace
the earlier variable hypothesis. The install check applies each decoded
operation to disposable engine sessions, not whole quest events; it is not
an original-game trace or a full-playthrough claim.

The walker emits ordered decoration changes. Both map and global event callers
apply them to the current map. Descriptor replacement refreshes the name,
ambient sound, collision radius and descriptor flags. Drawing resolves the
descriptor's numeric DSFT frame reference to its animation group; descriptor
and animation names are not interchangeable. Seven shipped updates need this
join, which a name-only renderer misses. Invisible placements
are omitted from rendering, ambient sources and the nearby decoration index;
the mixer silences voices whose last source disappears. Unrelated placement
flags, position and array order remain intact, so IDs stay stable.

Version-3 engine saves persist resulting descriptor IDs and visibility per
map and decoration index. Map revisits restore them; the existing map-refill
policy discards them when that map's remembered state expires. Version-1/2
saves load with no decoration overrides. This persistence/refill integration
is engine policy; exact original refill and dynamic decoration-light behavior
remain outside this slice. Synthetic tests cover quest/NPC continuation,
case-insensitive replacement, retain-name operations, unknown names, bounds,
visibility and save/reload isolation.

### Opcodes 39 and 40 move the quest chain's people

Both read as NPC mutations and both verify whole against the NPC table's own
spaces. Opcode 39 is `[npc u32][slot u8][topic u32]`: on 132 of 132 uses the
NPC is a 1-based `NPCdata.txt` row, the slot is 0..2 — the table's own three
topic columns — and the topic resolves in `npctopic.txt` or is zero, which
clears the slot. Opcode 40 is `[npc u32][place u32]`: 29 of 29 name an NPC,
and the place is a `2DEvents.txt` row or zero for away. `observed` Reproduce
with `evt_info --npc-mutations`.

Andover Potbello's letter event ends with opcode 39 setting his first topic
slot to topic 2 — pay him, and what he offers moves on. The engine applies
the topic rewrites when the party talks and keeps them with the quest bits;
the moves are applied too — a person moved away is gone from their counter,
and one moved within the same map stands at the new one. A move across maps
waits: the mover's record lives in the other map's session.

### Opcode 19 summons from the map's own encounter table

Its shape resolved into a name: `[slot u8][variant u8][count u8][x i32]
[y i32][z i32]`. The slot stays within its own map's **filled encounter
slots on 272 of 272** resolvable uses (the remaining 12 sit in scripts with
no `MapStats.txt` row), the variant is 1..3 on **284 of 284** — the monster
table's own A/B/C triples — and the count runs 1..6. Castle Darkmoor rings
a room with (1,1,1) points: one ghost each, on a circle. `observed`
Reproduce with `evt_info --catalog 19`.

The engine summons: the monster comes from the encounter slot the way spawn
points draw on it, shifted to the named variant, and the group spreads
around the point like any spawn group. The new arrivals join the fight at
full health without resetting anyone's wounds.

### Opcode 9 hurts the party

`[target u8][element u8][amount u32]`, 128 full uses. The element indexes
the resistance columns' own order — 0 physical, 1 fire, 2 electricity,
3 cold, 4 poison, 5 magic — and the maps vouch for it: the Pyramid's trap
rooms sweep all six at amount 5, poison rides the sewer's "Ouch!" and
Sweet Water's wells, electricity the Control Center's panels, and the
haunted spiral lands a physical 1,000. Targets 0..3 are the four
characters — the Hall of the Fire Lord's event 27 addresses 0, 1, 2 and 3
in four consecutive steps — and 4, 5 and 6 read as the user, the whole
party and one at random, `inferred` from 5 riding every "Cave-in!". The
engine deals it, answered by each victim's own resistance the way the
fight's blows are. Reproduce with `evt_info --catalog 9`.

### Opcode 21 casts a spell

`[spell u8][mastery u8][level u8][from i32×3][to i32×3]`, 154 uses. The old
`animation u16` reading combined the spell and mastery bytes; those numbers
often happened to name plausible DSFT groups because spell effects use those
animations downstream. The third byte is spell level, explaining the
spiral's 1, 5, and 9 values. `observed`

The spell is launched from the first point toward the second. When the second
point is all zero, the engine substitutes the party's current position plus
eye height. Thus the 83 apparently aimless casts target the party rather than
carrying an omitted distance or cadence. `observed`

### Opcode 25 rolls a step

`[step u8 x6, zero-padded]`, 100 uses of which 88 parse full. Every one of
the **452 nonzero entries is a step of its own event**. `observed` The mine
at D05 rolls the same six-slot tuple on nine dig sites — 400, 600 or 800
gold, or one of two "Cave-in!" steps, with a favoured step listed twice —
and D16's teleporter pads each roll among four exit teleports. Uniform
choice over the six slots reproduces the repeats-as-weights reading; a zero
slot falls through. That the choice is uniform is `inferred`; the slots and
their targets are the file's. Reproduce with `evt_info --asks`.

### Opcode 26 asks for a typed answer

`[prompt u32][answer u32][answer u32][step u8]`, 39 uses of which 19 parse
full (the rest sit in the misframed shared scripts). On all 19: the three
u32s are the map's own string indices, the two answers are one word in two
spellings, and the byte is a step of its own event. `observed` The strings
say what it is outright:

| Map | Prompt | Answers |
| --- | --- | --- |
| CD1 | "What's the password?" | "JBARD" / "jbard" |
| CD2 | "Steal (Yes/No)?" | "Yes" / "Y" |
| D08 | "Answer?" | "dark" / "darkness", "arrow" / "an arrow", "time", "fish" / "a fish" |
| Pyramid | "Answer?" | "kriK", "kcopS", "uluS", "aruhU", "yttocS", "yoccM" |

The Pyramid's answers are Star Trek's bridge crew reversed, and its plaque
event's long message carries the riddle. A match jumps to the named step —
"Ok!", ""Who told you!  Alright, you may pass!"" — and a miss falls through
to what follows, which in every shipped event is the "Wrong!"/"Incorrect."
branch. Matching ignoring case is `inferred` from the pairs being case
variants. The engine walks it: the question stops the walk, the message
line collects the typing, and Enter resumes the event at whichever step the
answer earned.

### Opcode 32 turns an event on or off

`[event u32][on/off u8]`, 312 uses. The byte is 0 or 1 on all 312, and the
id resolves through the same lookup order a face's event id takes: an event
of its own script on **175**, of `GLOBAL.EVT` on another **106**, zero on 7.
Of the remaining 24, half are the Oracle's, naming events of the Control
Center next door — its sibling map — and half resolve nowhere: 389..391
are defined by no script of the 83, checked exhaustively, and 75 by
eighteen scripts with no principled pick among them, so they stay dangling
like the 22 face ids. `observed` Pyramid's event 14 switches its riddle events
33..43 off and back on wholesale, and the Oracle throws its ids in matched
on/off sets of four beside a door — which is what reads 0 as disable and 1
as enable. `inferred` for that polarity. Reproduce with `evt_info --asks`.

The earlier Autonote and Award readings are out: the ids run past both
tables' ends, and the uses concentrate in `ORACLE.EVT` rather than where
notes are earned.

The other **62 opcodes are `unknown`.** An early test read opcode 4 as a
string index over *all* its uses and saw the argument leave the range 522
times — those were the establishment events, whose headers are `2DEvents.txt`
rows; split by kind, the reading holds (see "Opcode 4 opens an event").


## A face names an event

`BlvFaceExtra`'s field at +0x1A, which this project carried as unknown, is the
event id: **1,408 of the 1,441** non-zero values across the 52 indoor maps name
an event that map's own script defines. `observed` So a door, a switch or a
sign is a face that points at a script.

The ones that do not resolve are mostly not dangling: counted per face, 66 of
the 88 unresolved name events of `GLOBAL.EVT`, the shared quest script — see
"Faces fall back to the shared script" below. The remainder is `unknown`.

## The strings

`.STR` is NUL-terminated strings end to end, and the first is a single space on
every map examined. `observed` `D01.STR` reads: `"Exit Door"`, `"Chest"`,
`"Switch"`, `"Empty"`, `"Door"`, then single letters; `OutE3.STR` names
buildings and speaks: `"Welcome to New Sorpigal"`, `"Refreshing!"`.

## Outdoors, a model facet names one

The `.odm` payload is accounted for byte for byte, so the outdoor trigger had
to be inside a record already decoded but not fully read. It is: **+0x124** of
the 308-byte model facet, as a `u16`. **1,718 of the 1,719** non-zero values
across the fifteen outdoor maps name an event that map's own script defines.
`observed`

New Sorpigal's `FountainW` model carries event 150, whose message is
`"+10 Might temporary."` — a fountain you drink from, which is what the model
is called.

## What that buys, measured

Across the 52 indoor maps, **5,560 faces carry an event id**. Of those, 206
name themselves through opcode 35 and **1,567 say something** through opcode 29
or 30 when used. Outdoors, **1,758 facets** carry one across the fifteen maps,
73 naming themselves and 415 speaking. `observed` So the name is the rare case and the message the
common one: a door mostly has nothing to call itself and plenty to say when you
try it.

The engine reads both. A face with a name shows it under the crosshair, and
using one prints its message.

## What the scripts do not carry: sound

No opcode names a sound. A sweep of every unnamed opcode, at every u32
offset inside its arguments, against the sound table's 1,355 ids produced no
candidate: nothing resolves near-totally, and the partial hits land on the
dense id regions with names that read as noise — interface clicks, monster
attack fragments. The working of a door or a lever must be sounded by the
engine's own choice, which is what this engine does, from the archive's own
names. `observed` for the sweep; reproduce with `evt_info --soundsweep`.

## The rival branches

Five global events check more than one item, and they are not all
kindness: the Seer's ladder reads the blasters and the Ritual (46), the
Hourglass event also answers about Gharik's key (52, a hint line), the
charm-seer trades any of Lodestone, Harpy Feather or Four Leaf Clover
(105), the musician takes Flute or Harp (304) — and **Slicker
Silvertongue's own topic (102) is the dark turn-in**: shown the Zenofex
letter he swaps bit 200 for 201 — the traitor warned — and keeps nothing;
approached empty-handed he hands over a Cloak of Baa. `observed`,
verified in `evt_info --arc`.

## The award ledger, audited

Every award row swept against every script's gives (`evt_info --ledger`):
**58 awards are granted by scripts, 28 by nothing in any event file.**
The orphans fall into four families that explain themselves: the
seventeen guild memberships (joined at the guild counter, a service the
executable — and this engine — performs outside the scripts), the seven
`%u` counters (bounties, deaths, prison terms, arena ranks — running
tallies no script could keep; the arena itself *ships*: `zarena.blv` in
`Games.lod` names itself "The Arena", renders under this engine, and the
executable references it beside `Hive.blv` — the tournament that fills
those counters is executable code around a real level), Freed Archibald (see the King's Library
in [`text-tables.md`](text-tables.md)), and three story honors whose granting
mechanism was `unknown`. The obelisk's has since been read: all fifteen
outdoor obelisks give autonotes 79..93, one fragment per map, and the
executable grants award 62 for the completed set — StarHaven grants it
the same way, on the fifteenth fragment. `observed` for the gives,
`inferred` for the grant's timing. The last two — Returned the Prince
(1) and Gave Bat Guano to Barad (44) — are **cut content**: the Pouch of
Bat Guano ships as item 554 but no event in any script touches it,
"Barad" appears in no table, no prose and no executable string, and the
Prince's only script contact is the capture that grants award 5. They
join the Quest column as rows the shipped game cannot reach. `observed`

## Binary anchors: the event-loading machinery

Read-only radare2 reconnaissance of `MM6.exe` locates the script-loading half
of the system, anchored on the strings `global.evt` (`0x4bf524`) and `%s.evt`
(`0x4bf56c`). All RVAs below are virtual addresses; the binary is image-based at
`0x400000`. `observed`

| Address | Role | Evidence |
| --- | --- | --- |
| `0x4397b0` | loads `global.evt` | references `str.global.evt` at `0x4397ba` |
| `0x439ea0` | loads `<map>.evt` | references `str._s.evt` at `0x439eae` |
| `0x439870` | indexes the parsed event text | builds a 500-entry table over the text block at `0x54d044`; asserts `MAX_EVENT_TEXT_LENGTH` against a cap of 800 (`0x320`) |
| `0x54d044` | event-text block (data) | the array `0x439870` indexes |
| `0x54d03c` | "current event/NPC" id (data) | written by the loaders and triggers, read by the presenter |
| `0x43c7c0` | **event executor + dialogue presenter** (the runtime interpreter) | a 43-case switch over opcodes 1..43 via the jump table at `0x43e3c8`; also renders the line and word-wraps at 450 (`0x1c2`) px. See "The runtime executor is found" below |
| `0x43ab00` | dialogue trigger | sets `0x54d03c`, calls the executor `0x43c7c0`, then clears it |

The **opcode interpreter** that executes map-event steps (door, chest, give,
take, summon, …) is `0x43c7c0` — located via the door-state call site
`0x43dd14` (opcode 15's handler). Its dispatch hub is `0x43c948`; the full
opcode→handler table is at `0x43e3c8`. See "The runtime executor is found"
below for the decoded opcodes.

### The parser is found, and the record framing is confirmed

The hypothetical step 1 above is done. The raw-byte parser is `fcn.00439940`
(called from the loader `0x439ea0`); it reads the raw EVT stream at `0x54fce4`,
builds an index of 12-byte entries at `0x552f60`, and a 32-byte parsed-record
array at `0x54f060`, capped at 3000 records (`0xbb8`). It asserts against
`D:\MM6Src\code\EVENTS.CPP` line 588 on overflow. `observed`

Its field reads confirm the record framing byte-for-byte against this project's
own parser (`map_script.cpp`), relative to the record's leading size byte:

| Offset | Field | Engine read | Project read |
| ---: | --- | --- | --- |
| +0 | size | `0x54fce4`, advance `pos += size + 1` | `at += 1 + size` |
| +1..+2 | event_id | `0x54fce5` low, `0x54fce6` high | `payload[at+1] | payload[at+2]<<8` |
| +3 | sequence | `0x54fce7` | `payload[at+3]` |
| +4 | opcode | `0x54fce8` | `payload[at+4]` |
| +5.. | arguments | `0x54fce9`.. | `payload[at+5]..` |

The two agree exactly, so the project's parser is faithful to the original on
all 83 shipped scripts. `observed`

The parser's own dispatch (opcodes 3..38 via a case table at `0x439e74` and
jump table at `0x439e64`, four cases) handles only the few opcodes that need
special *parsing* — most fall to its default and are stored verbatim. It is not
the runtime executor.

### The runtime executor is found

The runtime opcode interpreter **is** `fcn.0043c7c0` — the same function the
earlier note flagged as the "dialogue presenter." It is both: a large switch
that runs event steps *and* renders their text. The dispatch hub at `0x43c948`
reads the opcode (`mov al, byte [edx+ebp+4]`, offset +4 — matching the parser),
subtracts 1, bounds-checks against 42 (`cmp eax, 0x2a`), and jumps through a
**43-entry table at `0x43e3c8`** covering opcodes 1..43. `esi` is the current
step; `[esi+5]` onward are its arguments. `observed`

The door-state call at `0x43dd14` (opcode 15, `case 15`) was the anchor that
settled it. Each opcode's handler reads its arguments from `[esi+N]` and acts;
e.g. opcode 15 reads door id `[esi+5]` and state `[esi+6]`, opcode 7 (chest)
reads `[esi+5]`.

### Opcode 33 displays a message and suspends the event

Opcode 33 (`ShowMessage`) displays the previously selected message and waits
for dismissal before continuing the event. It is not a variable mutation or a
mode argument: **the handler reads no argument bytes**. Both the 82 one-byte
records and the six empty records are valid for this operation. `observed`

The evidence uses the same user-owned GOG executable identified in the
[decoration specification](#opcode-13-changes-a-placed-decoration): 857,720 bytes,
SHA-256 `28d2b83e75db45134d161da1da767afcbdb3e381921d3de61c2784ac85cd00ce`.
Addresses below are virtual addresses in the PE32 image at base `0x400000`.
Static analysis used radare2 6.2.2 on a disposable copy; the original executable
was not run.

| Behavior | Evidence | Reproduction |
| --- | --- | --- |
| No payload fields are read; an existing message window prevents another from opening. | `observed` | Handler VA `0x43e304`, through `0x43e37a`. |
| The engine pauses its timer, saves the event ID, next sequence and script-bank mode, then creates a window with kind 19 and operation 33. | `observed` | VA `0x43e34c..0x43e37a`, shared creation tail at `0x43e2df`; timer routine `0x420db0`. |
| Local opcode 30 selects the long-text buffer; global opcode 30 selects NPC text. In NPC dialogue mode, opcode 29 also selects NPC text. | `observed` | VA `0x43d9a6..0x43da19` and `0x43d855..0x43d897`. |
| The message renderer uses the long-text buffer, with selected NPC text as its fallback. | `observed` | VA `0x43a890..0x43a978`. |
| Dismissal destroys the window, restores the bank and next sequence, calls the event interpreter, and resumes the timer. | `observed` | VA `0x43aaa3..0x43aae8`, alternative close path `0x43ab00..0x43ab51`. |

The continuation names **sequence + 1**, not the next physical record. A missing
sequence ends execution. The implementation preserves 256 as an exhausted
sequence after 255, avoiding accidental byte wraparound and reward replay.
The selected text survives successive pauses in the same invocation. A fresh
invocation starts with no selected text in StarHaven; reuse of stale text across
unrelated original-game invocations remains `unknown` and is not emulated.

### Install-backed checks

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/evt_info --messages
```

This metadata-only mode examines all 83 scripts, probes suspension directly at
every opcode-33 location, and executes three full events against disposable
walker state. It does not write saves or print dialogue or argument bytes.
All counts are `observed` in the recorded GOG installation:

- 88 records across 69 events in 33 scripts; all 88 suspend correctly.
- 61 have a text-selection opcode at the preceding sequence; all 61 candidate
  indices resolve to nonempty text. These are syntactic joins, not branch proofs.
- 44 have an executable record at the following sequence; the others have no
  continuation at that sequence.
- `GLOBAL / 20`: selects NPC text 28 and changes one quest bit and one topic
  before pausing at sequence 6. Dismissal finishes without repeating them.
- `OUTE3 / 240`: displays local text 27 at sequence 1. The quest bit is absent
  while suspended and set after dismissal.
- `CD2 / 53`: displays local text 7 at sequence 1. Its door operation is absent
  before dismissal and emitted after dismissal.

The command fails on unreadable input, broken candidate joins, failed
suspension probes, or failed full-event expectations. The 27 records without an
immediate text candidate remain in the report; this is not a claim of missing
text. For example, GLOBAL event 20 selects its text earlier in the event.

### Runtime integration and limits

`WalkOutcome::message` returns a text index and continuation sequence;
`WalkPresentation` carries transient text selection across pauses. The UI uses
`ScriptMessage` to retain the map, event, script bank and NPC-dialogue context.
Local messages resolve through `.STR`, and global messages through `npctext.txt`.
NPC events and map interactions share the outcome application path.

The modal wraps text, supports scrolling, and accepts Enter, Escape or a left
click to continue. Movement, combat time, normal input and save/load shortcuts
are held while it is open. The dismissal consumes exactly one continuation,
and a map replacement clears pending interaction. These are implemented engine
behaviors; the panel layout and key bindings do not claim original UI fidelity.

No new persistent field is needed: opcode 33 itself changes no campaign state,
and saves cannot be requested halfway through its modal. Changes made before
and after the pause use the existing save model. Synthetic regressions cover
reward timing, NPC state, bank identity, repeated messages, empty payloads,
missing sequences, sequence 255, disabled events and stale-map cancellation.
Original window styling, audio restoration and nested external script calls
remain outside this slice.

## Opcode 34 spawns sprite objects

Opcode 34 requests sprite objects at a position. Its earlier “Move to
coordinates” reading was incorrect: the handler calls the object-spawn helper,
not the party-travel path. The operands occupy 22 bytes (`observed`):

| Argument offset | Type | Meaning |
| --- | --- | --- |
| 0 | u32 | Object ID; descriptor matching uses its low 16 bits |
| 4, 8, 12 | i32 each | X, Y, Z in original map coordinates |
| 16 | i32 | Initial speed, passed to the velocity initializer |
| 20 | u8 | Number of spawn attempts; zero creates none |
| 21 | u8 | Zero selects vertical launch; any nonzero value scatters |

The bounded parser rejects any wrong opcode or operand block shorter than
22 bytes. It accepts trailing bytes and preserves the signed coordinate and
speed values. These are engine parser rules, not a claim that the original
interpreter guarded short records.

### Evidence and resource joins

Static evidence comes from the same GOG executable identified above,
SHA-256 `28d2b83e75db45134d161da1da767afcbdb3e381921d3de61c2784ac85cd00ce`,
inspected read-only with radare2 6.2.2 on 2026-09-11.

| Fact | Status | Evidence |
| --- | --- | --- |
| The handler assembles the five little-endian words, zero-extends the two bytes, passes an additional zero, and continues after the call. | observed | VA `0x43cf90..0x43d055`, call to `0x42aa10` |
| The helper creates a 100-byte sprite-object record and resolves the first descriptor whose ID matches the request's low word. No match selects descriptor zero. | observed | VA `0x42aa10..0x42aab4`; 52-byte DOBJLIST stride and ID at `+0x20` |
| Contained loot uses the first compiled ITEMS row whose sprite byte equals the **full** requested ID. No match leaves item ID zero. | observed | VA `0x42aaf5..0x42ab18`; 40-byte ITEMS stride, byte at `+0x21` |
| Scatter draws two random values per attempted object; the other branch draws none. | observed | VA `0x42ab1c..0x42abb2` |
| The allocator selects the first unused descriptor-zero slot, with a capacity of 1,000. It initializes previous position from current position. | observed | VA `0x42a730..0x42a7a2` |
| Full turn is 2,048 units and quarter turn is 512. | observed | Trigonometry initializer `0x445060`, `0x4451f0..0x445232` |

Descriptor IDs, descriptor row indices, item IDs and sprite-frame indices are
separate spaces. The contained-item lookup compares against a byte, so an
object ID above 255 cannot become loot merely because its low byte matches an
item sprite. Text-table sprite values are narrowed to the compiled byte before
comparison. The audit reports missing or unrepresentable descriptor indices
explicitly; it does not silently substitute the original's unused slot zero.

For scatter, yaw is the first random result modulo 2,048. Pitch is
`256 + floor((second result modulo 512) / 2)`, hence 256..511 units
(45 degrees up to just below 90 degrees). Without scatter, yaw is zero and
pitch is 512, a vertical launch. Speed and those angles feed the shared
initializer, which stores signed 16-bit velocity components. The opcode does
not supply a target, spell ID, skill or mastery. Original shared random ordering,
fixed-point velocity rounding and downstream impact behavior are separate
runtime requirements; this parser does not implement them.

### Installed-record audit and runtime status

Run `STARHAVEN_GAME_DIR=/path/to/MM6 ./buildDir/evt_info --object-spawns`.
The tool reads every script, resolves DOBJLIST and DSFT joins, and reports
numeric metadata only. It does not walk events, create objects or write saves.
For the installation hashed in the coverage audit, it finds 55 records in
11 scripts and 22 events: 45 complete requests and 10 short records. Each of
`DBM1` through `DBM5` contains one empty and one one-byte request. They remain
visible in the report rather than being excluded as presumed templates.

| Requested ID | Complete records | Requested objects | Descriptor index | Flags | Contained item ID |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 2 | 2 | 2 | `0x0000` | 1 |
| 36 | 1 | 5 | missing | — | not resolved |
| 1000 | 10 | 20 | 135 | `0x0194` | 0 |
| 1050 | 23 | 85 | 139 | `0x0174` | 0 |
| 2081 | 2 | 2 | 158 | `0x013c` | 0 |
| 2100 | 1 | 3 | 159 | `0x0154` | 0 |
| 4070 | 3 | 45 | 178 | `0x0054` | 0 |
| 8080 | 3 | 3 | 209 | `0x0174` | 0 |

All 44 matched descriptors select DSFT group heads, including frame zero for
ID 1000. `ZNWC.EVT / 65 / 1` requests ID 36, absent from this DOBJLIST.
The original helper falls back to descriptor zero, which remains an unused
object slot; the audit exposes the mismatch instead of claiming five live
objects. The totals are **165 requested**, not 165 successfully spawned.
The command returns 1 for this unresolved join. Short records are counted
separately; missing input or other unresolved descriptor/frame joins also fail.

### Temporary-object lifecycle

The 2026-09-16 static trace uses the executable hash above. The following
facts guide the bounded temporary-object simulator (extended for ID 2081 on
2026-09-18):

| Fact | Status | Evidence (virtual addresses) |
| --- | --- | --- |
| Simulation time is 128 ticks per second; motion uses tick delta divided by 128. | observed | Timer `0x420ec0..0x420f49`; millisecond conversion and fixed-point seconds |
| Gravity subtracts five velocity units per elapsed tick unless descriptor bit `0x20` is set. | observed | Indoor motion `0x4624cc..0x4625cd` |
| A swept flat-floor contact under bit `0x80` reverses and halves vertical velocity, stopping it below 10. Wall contacts reflect velocity. The already-grounded branch also damps motion. | observed | `0x46293d..0x462a45`; grounded branch `0x4624ff..0x4625b1` |
| Temporary objects expire at their stored descriptor lifetime; signed age overflow also removes them. Bit `0x40` invokes impact on expiry. | observed | Updater `0x4638d0..0x4639fc` |
| ID 1050 becomes ID 1051 on impact, resets age and velocity, and queues an area effect with radius 512. Impact at displacement 5,020 or more from the stored origin removes it instead. | observed | `0x45c709..0x45c777`, `0x45d4dd..0x45d608` |
| Opcode 34 initializes source ownership to zero. The area-effect party and actor consumers do not apply damage for this source. | observed | Constructor `0x42aae0`; consumers `0x431e5e..0x431e99`, `0x431473..0x431490` |

ID 1000 uses descriptor 135, frame **zero**, flags `0x194` and lifetime
768 ticks. It falls, bounces and expires without detonation. Frame zero is the
zero-scale `null` group: it produces no billboard in this installation. Its
separate colored particle trail is implemented; the descriptor supplies RGB
**128, 128, 255** in the examined installation.

The indoor/outdoor motion paths select the ordinary particle emitter when
`0x100` is set and neither `0x200` nor `0x400` is set
(`0x462d07..0x462dbf`, `0x463809..0x4638bd`). The emitter stores current position,
color and **256 + rand()%64** lifetime ticks in a **100-slot circular pool**
(`0x4348e0..0x434976`). A particle can survive its source object. Each original
visual update moves it up by **4..8** units and independently by **-2..2** on
each horizontal axis, then subtracts elapsed ticks (`0x434dd0..0x434e2f`).
The renderer projects a full-color point, tests world depth and writes color
without modifying depth; it does not fade or use a texture
(`0x46a496..0x46a50e`, `0x46a7ee..0x46a866`). These are `observed` branches.

StarHaven emits and jitters these points at a fixed **32 Hz**, advancing their
lifetimes at 128 Hz, independently of frame batching. This cadence replaces
the original frame-coupled behavior and is an engine policy. The visual RNG
is separate from gameplay RNG and resets on map/load clear. Moving ID 1000
emits at its base position; settled objects stop emitting. Existing particles
continue until expiry or pool overwrite. Pause freezes them, and successful
map/load clear discards them. Rendering uses one internal-framebuffer pixel,
8-bit descriptor RGB and the existing camera/depth convention. Exact original
cadence, shared RNG ordering, color quantization and doubled-resolution mode
remain outside this implementation. IDs 1050/1051, 2081 and 2100/2101 use this
same particle path as described below; other families' trails remain open.

ID 1000 actor contact is implemented through the ordinary collision response.
Flag `0x40` is absent, so it bypasses the impact handler and its 5,020-unit
cutoff (`0x4628df..0x4628fd`). Actor target type 3 then selects the shared
horizontal redirection, all-axis **58500/65536** damping and resource-timed
hurt animation (`0x4628fd..0x462929`, `0x462b58..0x462cbe`). These are `observed`
branches, shared with ID 2081. There is no damage, resistance roll, replacement
or detonation. Gravity, bouncing, object ID, age and the original 768-tick
deadline remain intact. StarHaven uses the same timed wince, animation fallback,
remaining-movement bound and actor overlap latch described below for ID 2100.

ID 1000 party contact is also implemented. Party target type 4 (`0x460171`)
skips actor redirection and reaches only common **58500/65536** damping of
all velocity components (`0x4628fd..0x46291d`, `0x462c8c..0x462cbe`). These are
`observed` branches in the indoor caller. The object keeps its heading, ID,
clock, gravity, bounce response and original 768-tick deadline, without damage,
actor reaction, resistance RNG, replacement or detonation. The impact-handler
displacement cutoff is bypassed. StarHaven uses the same party-body sweep and
independent overlap latch as ID 2081: continued overlap does not slow it again,
but separation and re-entry permit another contact. Actor/party order and
same-tick movement use the existing geometry-first bounded solver.

A fully settled ID 1000 does **not** search for actor or party contacts: the
original grounded path zeros velocity and returns before body searches when
horizontal speed squared falls below 400 (`0x46254c..0x4625b1`). The engine skips body
searches while its settled object still has floor support, and resumes movement
if that support disappears. Original near-floor/sector arithmetic, full actor
state-8 AI, decorative contacts and exact particle presentation remain open.

ID 1050 uses descriptor 139, frame 26, flags `0x174` and lifetime 768 ticks; it has no
gravity. Its stationary replacement, ID 1051, uses descriptor 140, frame 32,
flags `0x13c` and lifetime **48 ticks**. The replacement expires without
another detonation. All three have radius and height 16 in the inspected
installation. These are resource observations, not hard-coded simulator values.

Actor and party contacts for event-created **ID 1050 are implemented**. Both
follow the 1051 transition: zero velocity and age, one source-zero radius-512
area notification, then stationary presentation for the replacement lifetime.
There is no resistance gate, direct actor-state update or actor/party damage.
The actor and party source-type exclusions do not apply to event source zero
(`0x45c6ab..0x45c6e7`). Dispatch at `0x45c909..0x45c917` reaches the replacement
branch directly; the branch at `0x45d4dd..0x45d608` has no character-target
exception. It returns zero after presentation, so the caller does not perform
its ordinary actor response. These are `observed` branch/return facts; the
source-zero area consumers are identified in the table above.

The common 5,020-unit displacement guard removes before replacement. The live
adapter uses the same living-actor candidates, optional movement-body party
cylinder and expanded sweeps as the other supported contact families. Geometry
wins equal-time contacts, then stable actor order, then the party. These are
StarHaven policies rather than original sector/trajectory parity. Neither a
magic-immune actor nor an absent resistance callback blocks this transition.
Removed/replaced flight objects do not react to the same contact again, and
1051's expiry emits no second notification. Existing launch validation rejects
missing or invalid 1051 resources before mutating state or consuming RNG.

**ID 1050/1051 particle trails are implemented.** During flight, 1050 emits
ordinary points using its descriptor RGB, including at zero launch speed.
A successful geometry, actor, party or timed-expiry transition emits an
immediate **5–10-point burst**, using the incoming **1050** descriptor's color
and flag `0x100`, before the stationary **1051** begins its own ordinary trail.
The impact handler retains the incoming descriptor (`0x45c68c..0x45c6a7`) and
reads its flag/color after replacement (`0x45d5b4..0x45d5de`). These are
`observed` branches, not an assumption that both descriptors have equal RGB.

The burst emitter (`0x434980..0x434a8e`) compares the emitted count with a fresh
**5 + rand()%6** threshold before every point and once after the final point;
it does not draw a single uniformly distributed count. Each particle gets
vertical offset **0..32**, two horizontal offsets **-16..16**, and its own
**256..319-tick** lifetime in the shared 100-slot ring. A transition skips
ordinary emission on that update (`0x4628df..0x4628f7`, `0x463934..0x463951`).
The no-gravity motion path permits zero velocity, so later 1051 updates emit
at the unchanged position using **1051's** RGB and ordinary trail flags.
Its age-zero animation and resource-defined 48-tick lifetime remain intact;
final expiry emits no additional burst. The 5,020-unit guard removes before
replacement or burst. Particles already emitted survive either transition
or final removal. Fixed cadence, independent visual RNG, pause and map-clear
policies are shared with ID 1000 above; original visual parity is unverified.

ID 2081, requested before the loot in CD2 events 35/36, uses descriptor 158,
frame 130, flags `0x13c` and lifetime **48 ticks** (0.375 seconds). Unlike the
stationary 1051 replacement, it retains its requested launch velocity: the
installed requests launch vertically at speed 1,000. Flag `0x20` bypasses
gravity, and the ordinary motion path still runs. It needs no replacement
resource. Flag `0x40` is absent, so contact and expiry do not invoke an impact
action; expiry simply removes it. These are `observed` in the same executable:
age/expiry at VA `0x463907..0x463990`, motion dispatch at
`0x463992..0x4639ae`, gravity bypass and velocity integration at
`0x4624cc..0x462741`, and contact gating at `0x4628df..0x4628fd`.
Actor contact is now implemented through the ordinary response, despite the
absent impact flag: the caller dispatches actor target type 3 at
`0x4628fd..0x462929` to `0x462b58`. It redirects horizontal velocity away from
the actor, damps all components by **58500/65536**, and starts the actor's
resource-timed hurt animation as detailed below for ID 2100. Object ID, age,
stored lifetime and gravity bypass remain unchanged. There is no replacement,
detonation, resistance draw or direct damage. The 5,020-unit displacement
cutoff belongs to the skipped impact handler (`0x45c709..0x45c777`), so it does
not suppress this response. StarHaven shares the timed wince, overlap latch,
remaining-movement policy and explicit animation fallback with ID 2100.
Party contact is also implemented: the party candidate is target type 4
(`0x460171`), which bypasses the actor-specific branch and reaches only common
all-axis damping at `0x462c8c..0x462cbe`. Direction, ID, age, gravity behavior
and the original expiry deadline are preserved; the party receives no direct
damage, and no actor animation, replacement, area notification or resistance
roll occurs. The skipped impact handler's displacement cutoff does not apply.
These are `observed` branches in the indoor motion caller. StarHaven continues
remaining movement and latches party overlap, clearing it at a later tick
after separation or omission of the party context. The party latch is
independent of the actor latch, so an actor and the party may each respond
within the same tick. Geometry wins ties,
then actors, then the party. These sweeps, overlap rules and float arithmetic
are engine policies, not exact original collision/repeated-contact parity.

ID 2081's ordinary colored trail is implemented. Its descriptor supplies
**RGB 255, 255, 0** in the inspected installation. The same indoor/outdoor
particle selectors used above accept its `0x100` flag and absent `0x200/0x400`
flags (`0x462d07..0x462dbf`, `0x463809..0x4638bd`). No-gravity bypasses the
grounded early return (`0x4624cc..0x4625d1`, `0x462e94..0x462ec3`), allowing
emission at zero velocity and after settling. Ordinary contacts resume motion
through the trail path (`0x462c8c..0x462d1a`); they add no burst. Expiry removes
2081 before motion or emission (`0x463934..0x463992`). These are `observed`
branches; the fixed cadence and collision policies remain StarHaven choices.

Points use the shared ring, descriptor color and independent 256–319-tick
lifetimes described for ID 1000. They survive the 48-tick object, pause with
simulation, and clear on successful map/load. Emission preserves animation age,
contact responses and gameplay RNG. Decorative contacts, sound, original
frame cadence and exact trajectory/visual agreement remain separate gaps.

ID 2100 uses descriptor 159, frame 136, flags `0x154` and lifetime 768 ticks.
Gravity applies. Geometry contact or expiry changes it into ID 2101, clears
velocity and age, and queues the same source-zero, radius-512 area effect.
The replacement uses descriptor 160, frame 142, flags `0x13c` and lifetime
48 ticks; it remains stationary and expires without another impact. The
common 5,020-unit displacement cutoff applies. These are `observed` in the
installed resources and the branch at VA `0x45c927..0x45ca84`; the source-zero
area consumers are identified in the evidence table above.

Actor contact is an implemented exception: target type 3 returns without
replacement at `0x45c984..0x45c995`. The indoor caller redirects the object's
horizontal velocity away from the actor's center, preserving horizontal speed
before damping (`0x462b58..0x462c2f`). Its common response then multiplies all
three velocity components by **58500/65536** (`0x462c8c..0x462cbe`). Object ID,
age and expiry remain unchanged; the common displacement guard still removes
a distant object before response. No resistance roll, actor damage or area
notification occurs at this contact.

The actor receives state 8, an action-clock reset and a duration of its hurt
animation's DSFT group length times eight simulation ticks
(`0x462c33..0x462c85`). State 8 selects graphic slot 4, the hurt animation
(table `0x44c390`, entry `0x44c3b0` to `0x44c191`). These are `observed` field
writes, not a call to the full damage handler. The live battle maps them to a
resource-timed wince, without modifying health, buffs or attack recovery.
The actor renderer starts that animation at zero and advances it on simulation
time, including pause and repeated-response resets. This does not implement
all original AI decisions involving state 8 or impose a new paralysis effect.

StarHaven continues the unused movement after deflection, retaining its
four-contact-per-tick bound. An overlap latch suppresses repeat contact with
the last actor until separation; replacing/clearing the object clears it.
Floating-point radial heading, a positive-X fallback for coincident centers,
the overlap policy and fixed actor positions within a call are engine choices,
not original sector/integer-trajectory parity. A zero horizontal speed remains
zero. Missing, zero-length or signed-word-overflowing hurt animation data uses
the battle's existing 0.4-second wince fallback and increments the exposed
fallback count. Valid durations come from DMONLIST's hurt animation joined to
DSFT, never from a hard-coded monster duration. Decorative contacts for 2100
remain unimplemented.

Party contact (target type 4) is implemented and takes the ordinary **2101
transition**, with zero velocity and age and a radius-512 source-zero area
notification. It does not take the actor exception, redirect the projectile,
reset actor animation, or consume resistance RNG. The 5,020-unit displacement
guard still removes before transition. These are `observed` in
`0x45c984..0x45ca84`: only target type 3 returns early. The party candidate is
identified as type 4 at `0x460171`, and source-zero objects reach that search
at `0x462818..0x462828`. After presentation the handler returns zero, so the
indoor caller skips ordinary contact response. Source zero does not reach the
party or actor area-damage paths identified above.

The live adapter reuses the party cylinder described below for 4070. Its
expanded sweep, fixed positions per call, and geometry/actor/party tie order
are engine policies. An earlier actor deflection can still reach the party
within the same tick; this transition also clears the actor-overlap latch.
Stationary 2101 effects have no character contacts and expire at their resource
lifetime without another notification. This does not establish original party
collision dimensions, integer trajectories or sector-selection parity.

ID 2100/2101's colored trails and impact burst are implemented. Both
installed descriptors provide **RGB 255, 255, 0**. Moving 2100 uses the ordinary
point emitter and retains it after actor deflection. Actor target type 3
returns before burst presentation (`0x45c984..0x45c995`); it adds no extra
particle or lifetime reset. A later party or geometry contact in the same tick
can still trigger the ordinary transition.

Geometry, party and timed transitions gate the common burst on the incoming
2100 descriptor's `0x100` bit (`0x45ca6d..0x45ca84`) and pass its color to the
shared emitter (`0x45d5c7..0x45d5de`). This is the same **5–10-point burst**
specified for 1050 above, including its resampled continuation threshold.
A transition skips ordinary emission on that update. Later stationary 2101
updates use the replacement descriptor's color and ordinary-trail flags.
These are `observed` branches. The original 5,020-unit guard removes before
presentation; final 2101 expiry adds no burst. Existing particles survive
transition/removal and retain their independent deadlines. Fixed visual
cadence, isolated RNG, pause and map-clear policies are shared with the other
implemented trails; original visual parity remains unverified.

ID 4070 uses descriptor 178, frame 246, flags `0x54` and lifetime **256 ticks**.
It falls under gravity, but its impact action returns to ordinary geometry
response for target types 6 (face), 5 (decoration) and 0 (none). It therefore
settles on flat floors without a vertical rebound (`0x80` is absent) and
reflects from walls; geometry contact does **not** change it into ID 4071. The common
5,020-unit cutoff still runs before this exception. Lifetime expiry supplies
target type 2 and does change it into ID 4071, resetting velocity and age and
queuing the source-zero radius-512 area effect. The stationary replacement
uses descriptor 179, frame 252, flags `0x3c` and lifetime **80 ticks**. These
are `observed`: dispatch tables at VA `0x45da5c`/`0x45da70`, geometry exceptions
and replacement at `0x45cc3f..0x45cd31`, expiry target at
`0x463939..0x463951`, and installed descriptor rows.

Actor (type 3) and party (type 4) contacts are implemented: both immediately
follow the same 4071 transition and radius-512 notification as expiry. No
resistance gate or direct target-state change occurs. Source-zero event effects
do not damage actors or the party through the area consumers identified above.
The handler returns zero after presentation (`0x45d974..0x45d9b9`), so the
indoor caller skips its ordinary actor response (`0x4628df..0x4628f7`).
Source-zero objects are eligible for both candidate searches in the indoor
caller (`0x462818..0x462853`); its actor helper excludes states 4, 5 and 11
(`0x45ee00..0x45ee40`). Exact sector and actor-state equivalence remains open.

StarHaven uses the same expanded-cylinder sweep as 8080, with the earliest
contact winning. Geometry wins a tie, then actors in stable order, then the
party. Settled 4070 objects also test body overlap each simulation tick. These
are engine policies. The live adapter supplies the party body at camera eye
position minus the existing eye height, using player movement's radius/height
constants. It does not invent a monster-stat row for the party. Living actors
with valid stat rows remain the actor candidates; magic immunity does not
prevent a 4070 contact. Missing party input disables party contacts for isolated
probes. Actor/party positions remain fixed within one simulation call. The
5,020-unit cutoff removes before transition, and neither target path consumes
resistance RNG. Stationary 4071 effects do not contact characters again.

ID 8080 uses descriptor 209, frame 377, flags `0x174` and lifetime **768 ticks**.
It retains launch velocity without gravity. Its impact handler removes it for
any target type other than actor type 3, including geometry and the type-2
expiry call. These paths produce **no replacement and no area detonation**.
The engine therefore validates only its flight descriptor/frame for this slice;
a missing ID-8081 resource does not prevent a non-actor launch. These are
`observed` in the installed resource and at VA `0x45cfe0..0x45cfe5`,
`0x45d7a2..0x45d7ad`, `0x45c760..0x45c769`, with expiry dispatch at
`0x463939..0x463951`.

Party contact is also implemented: target type 4 follows this immediate removal
path, without reaching the actor-only resistance gate, animation reset or buff
helper. It requires no 8081 resource, emits no area notification and consumes
no resistance RNG. The party candidate is type 4 at `0x460171`; event source
zero reaches the search at `0x462818..0x462828`. The removal branch returns
zero, so the indoor caller skips ordinary collision response. These are
`observed` branch/return facts, not a source-zero approximation of the actor
response.

StarHaven supplies the same optional movement-body cylinder described for 4070.
An earlier party hit removes before an actor callback; an earlier actor hit
retains its distinct resistance and replacement path. Geometry wins equal-time
contacts, then stable actor order, then the party. Those selection rules,
fixed bodies per call and party dimensions are engine policies, not recovered
original sector or moving-target parity. Missing party input disables party
contacts for isolated probes. Removed objects produce no repeat contact or
expiry notification on later ticks.

Actor contact takes a separate, implemented path (`observed` at
`0x45d7b3..0x45d8e6`). The gate at `0x421e90..0x421f17` uses the actor's
magic resistance and level. Immunity (resistance 200 or above) rejects without
a random draw; otherwise accept when `rand() % (level + resistance + 30) < 30`.
A rejected hit removes the object. An accepted hit writes actor state zero and
selects stand animation (`0x44c140`, table `0x44c390`, branch `0x44c15e`), then
attempts a buff update. Opcode 34 supplies slot zero, duration zero and skill
zero. The buff helper at `0x44a970` preserves an existing later expiry, so this
creates no lasting condition and does not erase an active buff. Acceptance does
not depend on that helper returning success.

Acceptance changes the object to ID 8081 and clears age and velocity, without
an area detonation. Installed descriptor 210 uses frame 383, flags `0x13c` and
lifetime **96 ticks**. Missing or invalid replacement resources remove the
object after its accepted actor response; they do not prevent launching 8080.
The common 5,020-unit displacement guard removes before the resistance draw.

StarHaven maps state zero to ending the battle's wince animation, preserving
health, recovery, hostility and all condition/buff timers. Only living actors
with valid monster-stat rows are candidates. A swept expanded vertical cylinder
uses DMONLIST radius/height, with the aiming system's 48/160 fallback when body
data is missing. Expansion uses flat end caps and is conservative at corners.
The earliest contact wins; geometry wins equal-time ties, followed by stable
actor order. These selection rules, malformed-stat clamping and fixed actor
positions within a simulation call are **StarHaven policies**, not proven
original sector/actor selection parity. Actor resistance draws use the saved
per-map object RNG; tick-first processing preserves their order across frame
batches for fixed actor positions. Ordering against MM6's process-wide RNG
remains unverified. Other object families' character paths, decorative
contacts, trails and sound remain open.

The binary descriptor already contains its effective lifetime. The text-table
builder derives animation lifetime from DSFT group length multiplied by eight;
the binary loader copies it unchanged. The simulator reads the stored value,
including for ID 1051; it does not rescale it from the renderer's animation clock.
See [DOBJLIST lifetime](dobjlist.md#lifetime-units).

`src/game/temporary_objects.cpp` accepts full request IDs 1000, 1050, 2081,
2100, 4070 and 8080.
It validates descriptor and frame joins before changing state, uses the request's
speed, and consumes two explicit RNG draws per scattered attempt even when its
1,000 slots are full. Inactive slots are reusable. Missing resources and other
IDs produce explicit errors. IDs 1050, 2100 and 4070 require valid replacement descriptors.
Zero elapsed ticks pause the simulation. The API exposes object state and
unowned detonation notifications; it does not invent spell damage.

The engine uses floating-point positions, one-tick integration, and a two-sided
swept sphere against map polygons, including edges and vertices. It resolves
initial penetration and permits up to four contacts per tick, discarding any
remaining movement at that bound. Floor response halves downward speed when
flag `0x80` is set, otherwise clears it, and damps horizontal motion; walls
reflect it. These are **StarHaven simulation policies**, not a reproduction of
original sector, actor, slope or integer collision arithmetic. Exact trigonometric rounding, shared process RNG order,
remaining trails, sound and original-runtime visual agreement remain unverified.

Outdoor motion also sweeps against the heightmap using the renderer's default
scale and triangle split. Only cells intersected by the sphere's swept bounds
are queried. Terrain and model facets compete for the earliest contact, with
model facets winning ties. A center starting below the landscape is recovered
vertically to the surface plus its radius. Resting objects check current
terrain support; no terrain pointer survives a simulation call or map change.
This path serves both temporary effects and persistent event loot. Indoor
objects receive no terrain. These are **StarHaven policies**: terrain ends at
the rendered grid boundary, and water tiles currently behave as solid ground.
Original water/material responses and precise outdoor collision remain open.
The player's separate bilinear height sampler is unchanged.

**Runtime support is partial.** The walker emits bounded opcode-34 requests;
`ScriptObjectEffects` applies persistent ID-1 loot and temporary IDs 1000/1050/2081/2100/4070/8080
in the live adapter. Other IDs report their script, event, sequence, object ID
and error instead of creating an inert substitute. Temporary objects move and
collide with map geometry, and their DSFT sprites join the existing billboard
renderer. Animation uses object age divided by eight; it pauses with simulation
and restarts at zero when an object switches to its replacement. Fractional 128 Hz ticks carry across
frames; one call processes at most one second. Invisible descriptor bit `0x01`
suppresses drawing. Placed objects, persistent event loot and live temporary
effects share the 1,000-object limit. Loot and temporary requests consume the
same saved per-map random sequence, including dropped scattered attempts.

Temporary effects follow the existing launch lifecycle: a successful map open
or save load clears them and their fractional clock. A failed destination
preparation leaves them intact. They are not serialized or recreated on return;
persistent loot keeps its version-7 save contract. This is an explicit
**StarHaven policy**, not established original-game save parity.

`evt_info --object-lifecycle` seeds the six D18 event-56 spawn branches through
the walker and the same live application helper, checks drawable sprite
resources, and advances against loaded geometry until all effects expire.
It does not prove natural branch reachability or original-runtime visual parity.
Decorative contacts, exact character-contact/AI parity, terrain-material responses,
other families' trails, sound, the remaining IDs and original temporary-object
persistence remain unresolved before opcode 34 can be called complete. See the
[event-script coverage audit](../explanation/event-script-coverage.md).

`evt_info --object-impact [PPM]` walks D01 event 47 from entry and applies its three
ID-2100 requests through the live helper. It checks the one-time counter,
three transitions, drawable 2101 frames starting at age zero, and removal after
the replacement lifetime. The installed geometry run removes all three by tick
253. It also checks flight particles, the incoming-color impact burst,
stationary replacement emission, visible points, independent particle expiry
and unchanged gameplay RNG. An optional PPM contains only the point scene.
The event's chest and monster-summon outcomes are not applied by this
object probe; it does not establish the whole event's player reachability or
actor-contact behavior. Synthetic regressions cover geometry and expiry
transitions, gravity, failed replacement joins, pause, clearing and far impacts.
Particle tests use differing flight/replacement colors and replacement lifetimes,
check actor-only and actor-then-party paths, and cover flag gating, cadence-aligned
transitions, shared-ring overwrite and batching.

`evt_info --object-reaction` applies D01 event 47's three objects with a
controlled actor at their launch point. It verifies three contacts and
redirects without immediate replacement or burst, continuing ordinary trail
emission, and damping against a no-actor
control. The selected installed actor's hurt animation lasts 80 ticks; all 80
samples resolve and use its reset simulation clock. Moving the actor aside
and adding a controlled floor then produces three ordinary 2101 transitions
and removals. Actor health, an active buff and the object RNG remain unchanged.
Chest/summon outcomes and natural actor placement are excluded. Synthetic tests
also cover angled deflection, a later wall in the same tick, overlap/re-entry,
far removal, missing resources, dead actors, pause and reset by ID 8080.

`evt_info --object-1000-reaction` seeds all ten ID-1000 instructions in D18
events 56–61 and applies their requests against controlled actor overlap.
Twenty contacts/deflections retain the original ID and 768-tick expiry, bounce
on a controlled floor and expire without detonation. Each branch checks an
80-tick resource-defined hurt animation, unchanged actor health and buff, and
unchanged post-launch RNG. The probe checks 15,320 post-contact zero-scale
samples, preserving the installed absence of a billboard. Synthetic tests
cover angled deflection, same-tick wall contact, overlap/re-entry, gravity
following a contact beyond 5,020 units, settled-object exclusion, exact expiry,
dead actors, missing hurt resources and pause. Event-entry timers, natural
placement, original trajectory rounding and trail presentation are excluded.

`evt_info --object-1000-party` exercises the same ten D18 spawn records with
the party at each launch point and the actor outside the path. The controlled
engine fixture produces 20 initial contacts and 24 later re-entry contacts
from vertical flight; scattered objects move away. All 20 objects retain their
animation age and expire at tick 768 after floor bounces, with no actor reaction
or detonation and unchanged post-launch RNG. The zero-scale null billboard is
retained. Synthetic coverage adds all-axis slowing with gravity, actor/party
ordering and ties, later geometry contact, far contact, overlap/context-loss
re-entry, settled-body exclusion, pause and slot reuse. Party dimensions,
contact selection and overlap latching are engine policies; these counts are
not original-runtime or natural-placement measurements.

`evt_info --object-1000-trail [PPM]` walks all ten D18 ID-1000 spawn records,
applies them above a controlled floor, and checks descriptor-colored visible
points, object expiry, final particle expiry and unchanged gameplay RNG. Its
optional PPM contains only the controlled point scene. Synthetic tests cover
pool overwrite, lifetime bounds, jitter, frame batching, settled/flag gating,
pause, surviving particles after object expiry, indoor/outdoor adapters and
map clear. Renderer tests check one-pixel output, full color, clipping, world
occlusion and unchanged depth. This establishes engine output, not original
runtime visual agreement or natural event reachability.

`evt_info --object-1050-trail [PPM]` walks all twenty D18 ID-1050 spawn
records, advances flight in a controlled scene, then places the party body
at the launch site to trigger both replacements. It checks flight points,
incoming-color bursts, unchanged gameplay RNG, stationary replacement
animation and ordinary emission, the 48-tick deadline, and eventual particle
expiry. The optional PPM contains the controlled points. Synthetic tests
add distinct flight/replacement colors, geometry/actor/party/timed transitions,
no double emission on a cadence-aligned transition, descriptor flag gating,
far removal, pool overwrite, batching, pause, clear and indoor/outdoor adapters.
These checks establish engine behavior, not original visual parity or natural
party placement and event reachability.

`evt_info --object-2081-reaction` walks CD2 events 35/36 and applies each
ID-2081 request against a controlled actor at launch. Each produces one
contact/deflection, a resource-timed 80-tick hurt animation, and removal at the
object's original 48-tick deadline without replacement or detonation. Effect
animation continues on its original clock. Actor health, an active buff and
object RNG remain unchanged. The companion loot is excluded here; the
`--object-loot` probe covers its combined lifecycle and persistence. Synthetic
regressions also cover angled deflection, a later wall bounce in the same tick,
overlap/re-entry, contact beyond 5,020 units, dead actors, missing animation
data and pause. These controlled checks do not establish natural actor
placement, original collision arithmetic or full AI-state parity.

`evt_info --object-2081-party` applies the same CD2 requests against controlled
party overlap. Each registers one contact, slows compared with a no-party
control and retains its effect animation and 48-tick expiry, without actor
reaction or detonation. Object RNG and the distant actor's health, buff and
animation remain unchanged. Synthetic tests cover angled and zero-speed
motion, remaining movement into geometry, contact beyond 5,020 units,
actor/party ordering, geometry ties, overlap/re-entry, missing party context,
pause, expiry and slot reuse. The movement-body cylinder comes from the live
party adapter; original collision dimensions and natural placement remain
unverified. Companion loot remains covered by `--object-loot`.

`evt_info --object-1050-contacts` seeds each of D18 event 56's four ID-1050
spawn branches and applies the resulting requests against controlled actor or
party overlap, with geometry excluded. Eight actor contacts and eight party
contacts produce 16 stationary 1051 replacements, 768 drawable samples over
the installed 48-tick lifetime, and 16 removals. Actor health, an active buff
and post-launch RNG remain unchanged. Synthetic tests additionally cover
contact ordering/ties, an ignored resistance callback, retained actor hurt
animation/conditions, distant removal, dead actors and pause. This is not
natural branch reachability, full-event acceptance or original-runtime visual
parity; the unsupported timer at event entry is not executed.

`evt_info --object-party` walks D01 event 47 from entry and applies only its
three objects with controlled party overlap and no map geometry or actors.
It checks three immediate 2101 transitions, age-zero stationary presentation,
144 drawable samples over the installed 48-tick replacement lifetime, three
removals and unchanged post-launch RNG. Companion chest/summon outcomes and
natural encounter placement are excluded. Synthetic tests also cover a swept
party hit, earlier actor deflection, contact ties, distant removal, the impact
flag, absent/out-of-range party input, pause, clearing and a different resource
lifetime. No user save is written by this probe.

`evt_info --object-expiry` seeds OUTE3 event 220 at sequence 4, applying the
three ID-4070 records through the live helper. It checks 45 timed transitions
at tick 256, animation age reset, drawable 4071 resources and 45 removals at
tick 336. It skips the preceding unsupported opcode 3 and the three ID-1050
records, so it is not whole-event or natural-reachability acceptance. With
terrain enabled, the inspected installation records 110 terrain contacts and
15,075 position samples different from a model-only control; all 45 objects
still follow the same transition/expiry clock. Character contacts are excluded
from this timing probe. Synthetic regressions separately verify fast falls, buried
starts, rendered slopes, model/terrain ordering, indoor isolation, support
changes, bounce/impact/settling behavior and the far-contact/expiry cutoff.

`evt_info --object-contacts` tests each OUTE3 request twice using controlled
initial overlap, once with an actor and once with the party body. It observes
45 actor transitions, 45 party transitions, 90 removals and 7,200 drawable
stationary replacement samples across the installed 80-tick lifetime. Health,
active actor buffs and the post-launch object RNG remain unchanged. Map geometry
and the earlier event records are excluded; this is not natural event activation
or original-runtime collision/visual parity. Synthetic tests additionally cover
geometry ties, actor/party ordering, dead-actor exclusion, immunity, settled
contacts, pause, clearing and the distant-contact cutoff.

`evt_info --object-removal` exercises OUTD3 event 200 from sequence 1 with
activation counter 105 seeded to 1. It checks the disabled/capped branches,
three live ID-8080 requests, valid flight sprites and removal without any
replacement or detonation. A second run of the same requests in empty geometry
checks expiry at the descriptor lifetime. A separate controlled-party pass
places the party at each launch origin without actors or geometry: all three
objects remove on their first tick, with no replacement, detonation or RNG
change. The same three requests remain active in no-party controls. The
unsupported timer record at sequence 0, companion monster summons, natural
party placement and actor contacts are excluded; this is not natural
player-reachability or whole-event acceptance.

`evt_info --object-actor` uses the first seeded OUTD3 event-200 request with
controlled actor overlap and no map geometry. It loads real monster statistics,
body dimensions, descriptors and sprite frames, checks accepted/resisted/immune
cases with deterministic seeds, preserves actor health and an active buff, and
verifies stationary ID-8081 drawing and expiry across its 96 ticks. It writes
no saves and does not establish natural event activation, moving-target
selection, or original-runtime visual parity.

### Persistent event loot

`src/game/script_loot.cpp` implements the two ID-1 requests in CD2 events 35
and 36. The existing descriptor/item joins are used, including descriptor zero
rejection and DSFT group validation. Descriptor flags must be zero for this
slice. Failed validation changes neither objects nor random state. Valid
requests preserve coordinates, signed speed, count, and scatter draw order;
capacity counts occupied placed objects and event loot against 1,000 slots.
Repeated activation makes another request, as the script instructs. `observed`
operand and lookup rules; the explicit resource rejection is engine policy.

Loot uses the shared 128 Hz swept-sphere motion, gravity and floor-rest response
without temporary expiration. Its sprite is drawn from the resolved DSFT group.
The existing party-pack policy supplies icon-sized automatic pickup, gated by
range, height, free space and an unobstructed collision ray. Full packs leave
the object in place. Newly created item instances have no bonuses or charges
and are unidentified. Automatic pickup, collision occlusion, separate random
state seeded at 1 per map, and float trajectories are **StarHaven policies**;
exact original cursor interaction and shared random ordering remain unverified.
A simulation call processes at most one second, retaining the fractional tick.

Version-7 saves append random state, fractional ticks and bounded object records
to each map's `recall` row. Records carry descriptor index, contained item ID,
position, velocity and resting state; resource names and art are re-resolved.
The current map and maps left behind share this snapshot. Picking an object up
removes it from subsequent snapshots; a full pack does not. Ordinary refill
expires the snapshot, while loading an older saved day restores it unchanged.
Versions 1–6 load with empty event-loot state. Invalid counts, missing fields,
non-finite numbers and invalid record values reject the save atomically;
missing resource joins reject destination preparation before replacing the world.
Existing placed-map loot remains outside this new persistence slice.

`evt_info --object-loot [PPM]` starts CD2 events 35/36 at entry with their default
counter state. It applies ID 2081 and ID 1 together, checks motion, drawable
animation frames and expiration without detonation. Each effect emits eleven
descriptor-colored points before its tick-48 deadline; the probe checks visible
points, unchanged gameplay RNG and particle survival followed by expiry. The
optional PPM contains only the point scene. It then checks full packs,
map memory, save/reload and one-time loot pickup without writing user slots.
Repeat activation creates both requests again; a seeded counter taking the
other branch emits neither. Temporary-effect clearing preserves the loot.
This witnesses these event paths in the engine, not natural player reachability
or original-runtime visual parity. Synthetic tests additionally cover modal
continuation, capacity, invalid resources, wall occlusion and refill. Trail
regressions add moving/zero-speed/grounded emission, ordinary actor/party contact,
descriptor flags, custom lifetime, pool overwrite, batching, pause, map clear
and both indoor/outdoor live adapters.

## Opcode 41 generates an item reward

The old inferred “Open panel/dialogue” label is superseded by the handler at
VA `0x43dffc`–`0x43e071`. It generates an item, optionally replaces its ID,
hands the complete instance to the party, and advances to the next sequence.
These semantics are `observed` in the same MM6 executable fingerprint used
above; no original-game runtime session was performed.

### Layout and generation order

Offsets below are relative to the argument payload, after the five framing
bytes. The minimum payload is six bytes; trailing bytes are ignored.

| Offset | Type | Meaning | Status |
| --- | --- | --- | --- |
| 0 | u8 | Treasure level, passed to the generator | observed |
| 1 | u8 | Item-generation selector | observed |
| 2 | u32 little-endian | Optional item ID override; zero keeps the generated ID | observed |

The handler clears a 28-byte item instance and calls `0x448790` with the
level and selector. This is the [existing item generator](items.md#generator-callers):
levels 1–6, selector 0 for unrestricted generation, selectors 1–19 for
equipment categories, and aliases 20–43. Only after this call does a nonzero
override replace the item ID. Standard bonus, strength, special bonus, charges
and flags remain as generated. In particular, a fixed reward must still consume
the generator's random draws and artifact-state changes. `observed`

The receipt routine at `0x487750` first calls `0x41fe70` to dispose of the
previous held item, then copies the complete new item to party offset
`+0x5bac` (VA `0x90e81c`) and updates the cursor. The previous item is tried
in the selected character's pack, then the party packs, with a world-drop
fallback if none accepts it. The new reward itself remains held on the cursor.
This delivery order is `observed`; the exact full-pack drop trajectory and
pixel-level cursor presentation remain outside this investigation.

### Shipped records and reproduction

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/evt_info --generated-items
```

On the audited installation, the metadata-only probe finds 86 records across
24 scripts and 61 events. Six short records occur in `DBM1.EVT` through
`DBM5.EVT` and `OUT.EVT`. All 80 complete records generate an item resolving
against `ITEMS.TXT`; ten apply explicit ID overrides. There are zero failures.
These counts and table joins are `observed`; the fixed-seed probe does not
establish reachability or reproduce original random timing.

An additional disposable walk of `GLOBAL.EVT / 426` generates one reward
and applies its subsequent variable operation. The flow verifies
opcode 42 at sequence 2 to select event 424 for the next interaction. A second
walk of 424 gives no further item reward.

### Engine integration and limits

`ScriptItemGenerator` supplies synchronous table-backed generation to the
walker. Later checks and takes in the same event see the generated item.
Missing generation data or invalid level, selector or override ID produces a
sequence-addressed diagnostic and leaves generator state unchanged. Short
records remain visible in the coverage report and are skipped by the walker.

StarHaven automatically places complete reward instances into the first pack
with space. Generated and ordinary grants/takes are applied in script order,
so a later take removes the expected instance even when their item IDs match.
Full packs retain rewards in a visible waiting list; opening space
allows automatic delivery. Waiting items remain available to event item checks
and takes. Version-4 saves preserve the waiting instances, generator seed and
artifact-found flags, with versions 1–3 still readable. This is an explicit
engine UI adaptation of the original cursor-item workflow, not a claim of
original inventory interaction fidelity.

The script generator has its own persistent random and artifact state. The
original process-wide ordering shared with loot, chests and other callers is
not reproduced by this slice. Synthetic tests cover override metadata and
random consumption, invalid input, item-check/take ordering, modal resumption,
full-pack retention, save continuity and malformed-save rejection.

## Opcode 23 changes indoor face attributes

Opcode 23 is a masked write to one indoor face, not a script-variable operation.
The handler at VA `0x43d6fb`–`0x43d7b6` reads these operands (`observed`):

| Argument offset | Type | Meaning |
| ---: | --- | --- |
| 0 | i32 little-endian | Zero-based indoor face index |
| 4 | u32 little-endian | Attribute mask |
| 8 | u8 | Zero clears the mask; any nonzero value sets it |

Execution only changes an indoor map (map kind 1), with an index in the signed
range `[0, face_count)`. Outdoor maps and invalid indices continue without a
write. The engine bounds the nine-byte operand and ignores trailing padding.
The original ORs the mask into attributes when setting, ANDs its complement
when clearing, and sets mask `0x2` in the shared word at `0x90e838`. It never jumps
or suspends; execution continues at the next sequence. `observed`

The count/base at `0x5f7d20`/`0x5f7d24` describe 80-byte BLV faces; the write
is at face offset `+0x1c`. The same base and stride are consumed by the indoor
renderer at `0x492a10`, and the adjacent texture helper uses the same indexed
face at `0x43e530`. This establishes the field, independently of the old
variable-operation inference. `observed`

All 45 records split into 32 complete operands and 13 short authoring/template
records. Complete operands join to valid faces in CD2, D06, D12 and D17:

| Mask | Complete records | Effect and evidence |
| --- | ---: | --- |
| `0x20000000` | 14 (12 set, two clear) | Pass-through geometry. Existing collision research identifies the bit; CD2 event 33 sequences 5/6 change faces 4522/4575. |
| `0x10` | 15 set | Alternate draw path, selected at `0x494f41`–`0x494f47`; its visual difference remains `unknown`. The engine stores/persists the bit. |
| `0x4000` | Three (two set, one clear) | Texture animation. The renderer tests this bit at `0x492a69` and uses DTFT selection at `0x492a80`; otherwise it resolves the static bitmap. |

The engine applies ordered face changes to the live map and rebuilds collision
when attributes change. Pass-through faces cease blocking movement and direct
face interaction. Texture selection is per face, using its animation flag and
DTFT loop; it no longer changes the shared cached bitmap. Thus a static face
using the same texture name stays static. D12 events 22/23 start and stop a
painting, and D17 event 55 starts another. `observed` in install-backed probes.

Version-6 engine saves remember attributes and texture names per normalized map
filename and face index. Returning to the map restores collision and presentation;
map refill clears this memory with other temporary world changes. Versions 1–5
remain readable with no invented historical face changes. Named texture storage
is an engine adaptation of the original numeric bitmap/DTFT indices; setting or
clearing animation without a matching retexture does not emulate every original
index-reinterpretation quirk. The original face-save layout is not established
by this handler trace.

Reproduce with `evt_info --face-bits`: every complete record joins and mutates
correctly; the CD2 passage and D12/D17 animation flows pass save/reload checks.
The pass-through mask has synthetic movement and aiming regressions, and parser
checks cover every short length, signed-index rejection, arbitrary masks and
non-boolean set bytes. The alternate draw-path appearance remains a separate
rendering gap, even though the mask operation itself is implemented.

## Opcode 42 changes the current decoration's event

The old inferred “Conditional check” label is superseded by the handler at
VA `0x43cb48`–`0x43cb98`. It consumes a u32 little-endian event value, changes
the currently used decoration, and advances normally. Four payload bytes are
required; extra bytes are ignored. It does not branch or change a quest bit.
`observed` in the MM6 executable fingerprint recorded above.

| Input | Immediate effect | Status |
| --- | --- | --- |
| Zero | Store event byte zero and set placement flag `0x20` (hidden/inactive) | observed |
| Nonzero | Store `(value + 112) & 255`, preserving placement visibility | observed |

The context pointer at `0x55bc00` identifies the current 28-byte decoration.
Its signed word at `+0x14` selects one byte in the 200-byte map-state array
at `0x5b22fc`. The normal implicit interaction reads that byte, adds 400,
and calls GLOBAL. Thus values 400–655 select the corresponding event; other
nonzero values wrap into that interval. A nonzero input encoding byte zero
leaves the decoration active immediately, but map restoration later hides a
zero-state implicit decoration. Setting a nonzero value never unhides one.
`observed` at `0x43cb48`, `0x420ac3`, `0x45bc12`, `0x4556c3`, `0x46e778`.

### Initial interactions and persistence

An explicit placement event at `+0x16` takes precedence and runs in the map's
local script. Otherwise fourteen descriptor IDs use implicit global events:
118–121, 146, 154, 155, 158, 162–164, 166–167 and 182. The first 124 eligible
placements receive state slots 75–198, in placement order. The initial-value
routine consumes a percentile draw even for a fixed result. `observed` at
`0x455220` and `0x455050`. Initial event IDs are:

| Descriptor IDs | Percentile bands and initial GLOBAL event |
| --- | --- |
| 118–121 | 0–49: 447; otherwise another draw modulo 10 selects 448–457 |
| 146 | 0–19: 437; otherwise 436 |
| 154 | 0–39: 431; 40–69: 432; 70–89: 433; 90–99: 434 |
| 155 | Four equal 25-percent bands: 443–446 |
| 158 | 0–49: 429; otherwise 430 |
| 162 | 435, still consuming the first draw |
| 163–164 | 0–29: 410; successive ten-percent bands select 411–417 |
| 166 | 0–39: 439; 40–69: 440; 70–89: 441; 90–99: 442 |
| 167 | 0–79: 425; 80–89: 426; 90–96: 427; 97–99: 428 |
| 182 | 0–19: 419; otherwise 423 |

These are observed distribution rules, not copied table resources. The original
saves and restores the complete 200-byte array with map state (`0x44fb65`,
`0x44fc97`, `0x46dd5f`, `0x48b4ed`). Its restoration assigns the decoration
slots again and hides zero-valued entries. Modal opcode 33 preserves and
restores the actual decoration context (`0x43e35e`, `0x43aaae`), not just the
choice of local versus global script bank.

### Engine behavior and acceptance

StarHaven now decodes explicit placement events in both BLV and ODM maps and
lets the player use active decorations within the usual reach and aim limits.
A segment check rejects targets behind collision polygons. Explicit events
stay local; implicit events use GLOBAL with a captured placement index.
That index survives modal dismissal. Missing context is diagnosed by script,
event and sequence, and cannot mutate another decoration.

Opcode 13 and 42 outcomes are applied in execution order. Event changes,
descriptor changes and visibility share per-map, per-placement save state.
Version-5 saves append the optional event byte to `decoration` records;
versions 1–4 remain readable. A missing old event byte uses the engine's
initial assignment. Zero-state implicit decorations are hidden on restoration.
Map refill discards overrides under the existing refill policy.

Initialization uses the observed distributions with a stable map-name seed
and the engine's MM6 random generator. This seed and per-placement persistence
are engine policies; original process-wide RNG timing, reindexing quirks after
descriptor changes, and exact picking distances are not claimed as matched.
Touch, monster and object proximity triggers remain outside this interaction
slice. Unused templates and partial script semantics still limit campaign coverage.

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/evt_info --decoration-events
./buildDir/evt_info --generated-items
```

The first read-only probe covers all 47 opcode-42 records: six short records
in DBM1–DBM5 and OUT, 17 hide operations and 24 event changes, with zero
failures. It loads all 67 map files; 788 active implicit interactions resolve
to existing GLOBAL events under the fixed engine seed. The reward probe checks
GLOBAL 426 selecting 424, whose repeat use produces no additional item reward.
Synthetic tests cover thresholds and random draws, placement limits, local/global
routing, modal context, ordered visibility changes, wrapping, wall occlusion,
map isolation, save round trips and malformed/older saves. No original-game
runtime session was performed.

## The complete opcode table

The dispatch hub at `0x43c948` reads the opcode at step offset +4, subtracts 1,
and bounds-checks against 42 (`cmp eax, 0x2a`): **only opcodes 1..43 are
dispatched**; opcode 0 and any opcode ≥44 fall to the default, which merely
advances the step counter. The shipped data carries 90 distinct values in
0..90 (88 is absent). Values 44..53 occur six times each; values 54..90 occur
37 times altogether, including two uses of 62, one with 35 argument bytes.
These are still original-default records rather than extra executable
handlers. Counts are `observed` by `evt_info --coverage`; the executable
vocabulary is 1..43, including the six default cases listed below.

Merging the names already established from the data with the handlers decoded
from the executable, every opcode 1..43 now has a reading:

| Op | Name | Handler | Status |
| ---: | --- | --- | --- |
| 1 | End | `0x43e3bd` | observed |
| 2 | Enter (establishment) | `0x43e076` | observed |
| 3 | Spawn sprite object (calls spawner `0x48eb40`) | `0x43df77` | observed |
| 4 | Event header / block-open (data: the interactable noun; handler: decrement block counter) | `0x43dd01` | observed |
| 5 | Title (shares handler with 4) | `0x43dd01` | observed |
| 6 | Travel (move party to a map) | `0x43dd35` | observed |
| 7 | Chest (open) | `0x43dd1e` | observed |
| 8 | Play effect/sound by category (sub-switch `[esi+5]` 0..5) | `0x43cebf` | observed |
| 9 | Harm the party | `0x43d575` | observed |
| 10 | Set boolean game-state flag (`[esi+6]`→`0x61a96c`) | `0x43d835` | observed |
| 11 | Retexture (repaint a face) | `0x43db10` | observed |
| 12 | Set variable (value + name pointer) | `0x43db40` | inferred |
| 13 | Set decoration descriptor and visibility | `0x43db94` | observed |
| 14 | Check (variable test) | `0x43d05a` | observed |
| 15 | Door (`[id][state]`; see event-tables.md) | `0x43dd0a` | observed |
| 16 | Give (item/gold) | `0x43d369` | observed |
| 17 | Take | `0x43d195` | observed |
| 18 | Set (variable) | `0x43d281` | observed |
| 19 | Summon (from encounter table) | `0x43dc7b` | observed |
| 20 | no-op (default) | `0x43e1e2` | observed |
| 21 | Launch (sprite) | `0x43da1e` | observed |
| 22 | Reset 20-slot dialogue/choice buffer | `0x43cab5` | inferred |
| 23 | Set or clear indoor face attribute bits | `0x43d6fb` | observed |
| 24 | Variable op (4-byte value) | `0x43d7bb` | inferred |
| 25 | RandomJump (roll a step) | `0x43d505` | observed |
| 26 | Ask (typed answer) | `0x43d451` | observed |
| 27 | no-op (default) | `0x43e1e2` | observed |
| 28 | no-op (default) | `0x43e1e2` | observed |
| 29 | Message (short) | `0x43d855` | observed |
| 30 | LongMessage (sign text) | `0x43d9a6` | observed |
| 31 | no-op (default) | `0x43e1e2` | observed |
| 32 | Switch (event on/off) | `0x43d666` | observed |
| 33 | ShowMessage (modal text, then resume) | `0x43e304` | observed |
| 34 | Spawn sprite objects by object ID | `0x43cf90` | observed |
| 35 | Name (the interactable noun) | `0x43cf82` | observed |
| 36 | Goto (jump) | `0x43cea8` | observed |
| 37 | no-op (default) | `0x43e1e2` | observed |
| 38 | no-op (default) | `0x43e1e2` | observed |
| 39 | SetTopic | `0x43cb9d` | observed |
| 40 | MoveNpc | `0x43cd61` | observed |
| 41 | Generate item reward | `0x43dffc` | observed |
| 42 | Set current decoration event | `0x43cb48` | observed |
| 43 | Read variable by type (sub-switch `[esi+5]` 0..5) | `0x43c94f` | inferred |

Opcode 0 (88 uses, up to 37 arg bytes, carries map filenames like
`sub03bz.blv`) is the **script/map identifier header**, not an executable
step — the executor skips it via the same default path. `observed`

The `observed` rows are grounded in the handler's own reads and calls or in
the data analysis above; the `inferred` rows name the operation from its shape
but require a trace of the called function to pin the exact argument semantics.
Opcode 4's two readings are complementary, not conflicting: in the
*data* it opens an event and carries the interactable-noun string index (see
"Opcode 4 opens an event" above); in the *handler* it records the structured
block's opening.

Do not treat the addresses above as stable across MM6 builds; they are pinned
to the recorded SHA-256.

## Historical question status

> Audited in the [open-question register](../open-questions.md); the register
> supersedes unresolved hypotheses below.

- The "rare tail of the 90 distinct opcodes" is now resolved: the executable
  dispatch covers **only opcodes 1..43** (see "The complete opcode table");
  opcodes 44..53 are a six-use template and 54..90 are single-use trailing
  junk, all of which the executor silently skips via its default case.
  `observed`
- Opcode 6's bytes 16..25 are pitch, vertical speed, house id, and exit-picture
  id. The former 8.8 scalar combined the two trailing byte ids.
- `OUT.EVT`'s 87 stubs are answered as far as the data reaches: every
  stub is the same two-step husk that does nothing, and exactly **three
  outdoor facets point into the shared script** — Sweet Water's at ids
  50 and 51, New Sorpigal's at 38 — facets whose event ids their own
  map's script does not define. The shared script reads as a **null
  sink**: a defined nothing for stray facet ids to land on. Why the
  other 84 husks exist when nothing shipped points at them stays
  `unknown`; `inferred` for the sink reading.
- Which of the shared scripts beyond `OUT.EVT` use the headerless framing,
  and what the misframed remainder of the unheaded class parses to under it.
  `unknown`
- The 128 topic ids with no global event beyond being plain prose, and what
  runs the global events no topic and no face points at. `unknown`
- The 22 face event ids that resolve in neither their map's script nor
  `GLOBAL.EVT`. `unknown`
- Opcode 15's door numbers are the ids of the indoor event files' own door
  records — see [`event-tables.md`](event-tables.md) — and the engine moves
  them. The open/shut argument (0/1/2) is the requested state passed to the
  door-state function; the state machine at `[door+0x4c]` is now decoded (see
  [`event-tables.md`](event-tables.md)): **0 = closed, 1 = open, 2 = in
  transition, 3 = at-target**. State 2 is the mid-motion state: re-triggering
  it snaps the door to state 3. `observed`
- Opcode 11's outdoor u32 indexes a model facet, not a terrain tile.
- Opcode 1's byte is consumed and ignored by the executor.
- Opcode 21 is spell id, mastery, level, source, and destination; a zero
  destination aims at the party's eye position.
- Variable ids are the `EvtVariable` enumeration linked from the
  [open-question register](../open-questions.md).
- Opcode 9's targets 4, 5, and 6 are active character, whole party, and random
  character respectively.
