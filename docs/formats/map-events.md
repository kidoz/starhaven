# Map event scripts (Might and Magic VI)

Status: **verified** for the container and the record structure; twenty-four
opcodes are named and the rest are undecoded. Each claim is tagged `observed`,
`inferred`, or `unknown`.

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

The engine applies it indoors: the walker collects the repaints and the face
wears its new texture. The few outdoor uses are not applied yet.

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

### The complete opcode table

The dispatch hub at `0x43c948` reads the opcode at step offset +4, subtracts 1,
and bounds-checks against 42 (`cmp eax, 0x2a`): **only opcodes 1..43 are
dispatched**; opcode 0 and any opcode ≥44 fall to the default, which merely
advances the step counter. The shipped data carries opcodes 0..90, but
54..90 each appear once with zero arguments (trailing junk) and 44..53 are a
six-use template, so the executable vocabulary is 1..43. `observed`

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
| 13 | Set/compare variable by id | `0x43db94` | inferred |
| 14 | Check (variable test) | `0x43d05a` | observed |
| 15 | Door (`[id][state]`; see event-tables.md) | `0x43dd0a` | observed |
| 16 | Give (item/gold) | `0x43d369` | observed |
| 17 | Take | `0x43d195` | observed |
| 18 | Set (variable) | `0x43d281` | observed |
| 19 | Summon (from encounter table) | `0x43dc7b` | observed |
| 20 | no-op (default) | `0x43e1e2` | observed |
| 21 | Launch (sprite) | `0x43da1e` | observed |
| 22 | Reset 20-slot dialogue/choice buffer | `0x43cab5` | inferred |
| 23 | Variable op (4-byte value) | `0x43d6fb` | inferred |
| 24 | Variable op (4-byte value) | `0x43d7bb` | inferred |
| 25 | RandomJump (roll a step) | `0x43d505` | observed |
| 26 | Ask (typed answer) | `0x43d451` | observed |
| 27 | no-op (default) | `0x43e1e2` | observed |
| 28 | no-op (default) | `0x43e1e2` | observed |
| 29 | Message (short) | `0x43d855` | observed |
| 30 | LongMessage (sign text) | `0x43d9a6` | observed |
| 31 | no-op (default) | `0x43e1e2` | observed |
| 32 | Switch (event on/off) | `0x43d666` | observed |
| 33 | Mode-dependent sub-screen enter/exit | `0x43e304` | inferred |
| 34 | Move to coordinates | `0x43cf90` | inferred |
| 35 | Name (the interactable noun) | `0x43cf82` | observed |
| 36 | Goto (jump) | `0x43cea8` | observed |
| 37 | no-op (default) | `0x43e1e2` | observed |
| 38 | no-op (default) | `0x43e1e2` | observed |
| 39 | SetTopic | `0x43cb9d` | observed |
| 40 | MoveNpc | `0x43cd61` | observed |
| 41 | Open panel/dialogue | `0x43dffc` | inferred |
| 42 | Conditional check | `0x43cb48` | inferred |
| 43 | Read variable by type (sub-switch `[esi+5]` 0..5) | `0x43c94f` | inferred |

Opcode 0 (88 uses, up to 37 arg bytes, carries map filenames like
`sub03bz.blv`) is the **script/map identifier header**, not an executable
step — the executor skips it via the same default path. `observed`

The `observed` rows are grounded in the handler's own reads and calls or in
the data analysis above; the `inferred` rows name the operation from its shape
but pin the exact argument semantics to a follow-up trace of the called
function. Opcode 4's two readings are complementary, not conflicting: in the
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
