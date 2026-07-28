# Map event scripts (Might and Magic VI)

Status: **verified** for the container and the record structure; the opcodes
themselves are undecoded. Each claim is tagged `observed`, `inferred`, or
`unknown`.

## Scope

Covers where a map's script lives, how its records are framed, and how a face
names one. Does not cover what any opcode does.

## They are not in `Games.lod`

`Games.lod` holds only geometry and saved state — 52 `.blv`, 52 `.dlv`, 15
`.odm`, 15 `.ddm`, and nothing else. `observed` The scripts are in
**`icons.lod`**, alongside the design tables: **83 `.EVT`** entries and
**76 `.STR`**, one pair per map, 738 to about 2,000 stored bytes each.
`observed`

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

**90 distinct opcodes** appear. The most common are 4 (2,192 uses), 14, 1, 15,
16, 18, 29 and 30. Argument lengths are fixed per opcode.

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

What it declares is `unknown`. Four readings have been tested and fail, and are
recorded so they are not tried again:

### Events without a header are a different kind

Of the 3,332 events, 2,182 open with opcode 4 and **1,150 do not**, and the two
groups do not use the same opcodes. `observed`

| Group | Events | Opcodes, most used first |
| --- | ---: | --- |
| headed | 2,182 | 4, 14, 15, 1, 2, 18, 29 |
| unheaded | 1,150 | 1, 14, **30**, **16**, 18, **32**, **36** |

An unheaded event opens with 30, 14, 15 or 6 rather than 4, and its id sits far
higher: median 45 against 27, and up to **808** where a headed event never
passes 262. `observed`

So the header is not merely optional decoration — it marks a class. What the
unheaded class is remains `unknown`.

The one lead has been tested and closed: their id range overlaps the event
columns of `NPCdata.txt`, but taking every NPC an establishment holds and
checking their event ids against that map's own script gives **15 of 129** —
5 of 44 on New Sorpigal, 0 of 22 on Silver Cove. `observed` Those columns are
`npctopic.txt` and `npctext.txt` ids, which resolve 298 of 298 (see
[`text-tables.md`](text-tables.md)); the overlap with script event ids is a
coincidence of range. The unheaded events are not NPC dialogue.

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

The other **86 opcodes are `unknown`.** Opcode 4, the commonest, is not a
string index: its argument leaves the range 522 times.

## A face names an event

`BlvFaceExtra`'s field at +0x1A, which this project carried as unknown, is the
event id: **1,408 of the 1,441** non-zero values across the 52 indoor maps name
an event that map's own script defines. `observed` So a door, a switch or a
sign is a face that points at a script.

The 33 that do not resolve are `unknown`; they may name events in another map's
script, or the field may carry something else where the referenced event is
absent.

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

## Open questions

- Every opcode. `unknown`
- Which argument, if any, indexes the string table. `unknown`
- The 33 face event ids with no matching event. `unknown`
