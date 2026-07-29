# Design data tables (Might and Magic VI)

Status: **verified.** Thirty tab-separated tables sit inside `icons.lod`
carrying the game's design data: maps, monsters, items, spells, quests, classes
and NPC dialogue. Each claim is tagged `observed`, `inferred`, or `unknown`.

## Scope

Covers the container, the text format, and the nineteen tables parsed into
typed rows — `MapStats.txt`, `MONSTERS.TXT`, `ITEMS.TXT`, `RNDITEMS.TXT`,
`STDITEMS.TXT`, `SPCITEMS.TXT`, `Spells.txt`, `Class.txt`, `stats.txt` and
`SkillDes.txt`, `2DEvents.txt`, `NPCdata.txt`, `npcprof.txt`, `npctopic.txt`,
`npctext.txt`, `NPCNews.txt`, `npcbtb.txt`, `GLOBAL.TXT`, `Quests.txt`,
`Awards.txt` and `Autonote.txt`.
The other 11 are readable through the same reader but not yet given typed
views.

There is little to reverse engineer here, and that is the point: these are
spreadsheet exports the developers left in the archive. The work is unwrapping
the container, parsing the text correctly, and establishing how the tables join
to the binary data already decoded.

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product and edition | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Archive | `data/icons.lod`, 32,772,165 bytes, 2703 entries `observed` |
| Tables | 30 entries whose name ends in `.txt` `observed` |

Reproduce with:

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/data_info --list
./buildDir/data_info --maps
./buildDir/data_info --monsters ArcherB
./buildDir/data_info --items 160
./buildDir/data_info --random-items 160
./buildDir/data_info --standard-bonuses 1
./buildDir/data_info --special-bonuses 16
./buildDir/data_info --generate-item 6:1
./buildDir/data_info --spells Fireball
./buildDir/data_info --classes
./buildDir/data_info --buildings OutE3.Odm
./buildDir/data_info --npcs
./buildDir/data_info --professions
./buildDir/data_info --dialogue 296
./buildDir/data_info --news
./buildDir/data_info --quests 81
./buildDir/data_info --autonotes
./buildDir/data_info --check          # joins MONSTERS.TXT to DMONLIST.BIN
./buildDir/data_info Spells.txt --rows 10
```

## The container

Each entry is the same 48-byte-header + zlib container the image entries and
`DTILE.BIN` use (see [`dtile.md`](dtile.md)), even though the LOD directory
marks it container-uncompressed.

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| 0x00 | 16 | char[16] | name | observed | e.g. `"MAPSTATS.TXT"`; cased differently from the archive entry |
| 0x14 | 4 | u32 | packedSize | observed | stored size − 48 |
| 0x28 | 4 | u32 | unpackedSize | observed | text length after inflation |
| 0x2C | 4 | u32 | unknown | unknown | 256 on every observed table |
| 0x30 | … | bytes | zlibData | observed | standard zlib stream (`78 9c`) |

**An `unpackedSize` of zero means the bytes are stored as-is.** Exactly one
table is like this: `errorlog.txt`, a leftover developer log reading
`"CSpriteFrameTable::load - Unable to open file: sft.def."`. Treating a zero as
an error would reject valid data. `observed`

## The text format

Ordinary Excel-style tab-separated text, with three rules that matter:

- **Rows end with CRLF.** Every table uses `\r\n` between rows. `observed`
- **Fields are tab-separated and may be quoted.** A field opening with `"` runs
  to its closing quote, `""` stands for one literal quote, and a quoted field
  **may contain tabs and bare newlines**. Three tables rely on this:
  `PROFTEXT.txt`, `Scroll.txt` and `npctext.txt` hold multi-paragraph prose
  inside single cells. Splitting on `\n` instead of `\r\n` corrupts them.
  `observed`
- **The encoding is Windows-1252, not UTF-8 or ASCII.** Curly quotes, en dashes
  and accented letters appear in the prose tables, so anything bound for a
  terminal or a UTF-8 interface must be converted. `observed`

Two more properties shape the reader:

- **Rows are ragged.** Trailing columns are present on some rows and absent on
  others — `MapStats.txt` has 28 columns on its first 16 data rows and 27 on
  the rest — so a cell accessor must be bounds-safe rather than assume a
  rectangle. `observed`
- **Headers are merged spreadsheet cells spread over several rows,** and blank
  rows appear both between sections and in a block at the end. The parsers find
  the real header by matching its text rather than counting rows. `observed`

### Numbers

Numeric cells carry padding (`"  93  "`) and thousands separators
(`" 1,300 "`), both spreadsheet artifacts. The reader trims and strips commas.
Anything else — a range (`"2-4"`), a dice code (`"3D6+2"`), a treasure code
(`"5%6D20+L2Bow"`) — is deliberately **not** read as a number, because a
partial read would turn `"2-4"` into 2.

## The tables

All 30 parse. `observed`

| Table | Rows | Cols | Contents |
| --- | --- | --- | --- |
| `MapStats.txt` | 79 | 28 | the 67 maps: names, encounters, music |
| `MONSTERS.TXT` | 177 | 33 | the 173 monsters: stats, attacks, resistances |
| `ITEMS.TXT` | 583 | 16 | 578 named items |
| `Spells.txt` | 116 | 16 | 99 spells across the schools |
| `Class.txt`, `stats.txt`, `SKILLDES.TXT` | 19–32 | 2–5 | character classes, statistics, skills |
| `Quests.txt`, `Awards.txt`, `Autonote.txt` | 101–513 | 3–4 | quest, award and journal text |
| `NPCdata.txt`, `npcnames.txt`, `npctext.txt`, `npctopic.txt`, `NPCNews.txt`, `npcprof.txt`, `npcbtb.txt`, `PROFTEXT.txt` | 28–521 | 3–21 | NPCs, their professions and their dialogue |
| `2DEvents.txt`, `Trans.txt`, `Merchant.txt` | 7–559 | 2–25 | shops, travel and merchant behaviour |
| `RNDITEMS.TXT`, `STDITEMS.TXT`, `SPCITEMS.TXT`, `USEITEMS.TXT`, `Scroll.txt` | 31–413 | 4–35 | item generation and enchantment |
| `GLOBAL.TXT`, `passwords.txt`, `errorlog.txt`, `Autonotes.txt` | 1–598 | 1–3 | interface strings and leftovers |

## `MapStats.txt`

| Column | Field | Status | Notes |
| --- | --- | --- | --- |
| 0 | id | observed | 1-based |
| 1 | name | observed | display name, e.g. `"Sweet Water"` |
| 2 | file name | observed | the Games.lod entry, e.g. `"OutA1.Odm"` |
| 3 | reset count | observed | |
| 4 | first visit day | observed | |
| 5 | refill days | observed | 168, 224, 672 — 24-hour multiples |
| 6–8 | lock, trap, treasure difficulty | observed | headed `0-10`, `0-10`, `0-6` |
| 9 | encounter percent | observed | |
| 10–12 | group percentages | observed | how the three encounter slots share the roll |
| 13–24 | three encounter slots | observed | picture, name, difficulty, count range |
| 25 | Redbook track | observed | see below |
| 26 | map designer | observed | blank on the unfinished maps |

### It accounts for every map, exactly

The 67 file names in the table and the 67 `.odm`/`.blv` entries in `Games.lod`
are **the same set** — no map is listed that the archive lacks, and no map ships
that the table omits. `observed`

Fourteen of the 67 carry the placeholder name `"pending"` and no designer:
unfinished maps that shipped anyway. `observed`

### The music column

"Redbook" is the CD-audio term the original used. The value is the stem of a
file in `Sounds/`: track 5 means `Sounds/5.mp3` (see [`snd.md`](snd.md)).
**Every one of the 12 distinct tracks the maps reference exists** among the 15
shipped files; tracks 3, 4 and 15 are referenced by no map and presumably play
elsewhere. `observed`

This is the join that lets a walker play the right music for the map it just
loaded, which both walkers now do.

## `MONSTERS.TXT`

| Column | Field | Status |
| --- | --- | --- |
| 0–2 | id, picture, name | observed |
| 3–6 | level, hit points, armour class, experience | observed |
| 7–8 | treasure code, quest flag | observed |
| 9–11 | flying (`Y`/`N`), movement range, AI type | observed |
| 12–14 | hostility, speed, recovery | observed |
| 15–16 | preference, on-hit bonus (`BrkItem`, `DrainSP`, `Uncon`) | inferred |
| 17–24 | two attack slots: type, damage dice, missile, chance | observed |
| 25 | spell list, as `"Spell,Mastery,Skill"` | observed |
| 26–31 | resistances: fire, electricity, cold, poison, physical, magic | observed |
| 32 | miscellaneous special | unknown |

A resistance of `"Imm"` means immune rather than a number. `observed`

### The join to `DMONLIST.BIN`

The `Picture` column is the same name space as the binary monster table an
actor record's monster id indexes (see [`dmonlist.md`](dmonlist.md)).

Both hold **173 records**, and text row *N* names binary record *N−1*: 168 of
the 173 names match **exactly**, and the remaining 5 differ only in
typography — `"DragonCave A"` against `"DragonCaveA"`, `"PeasantF1C"` against
`"Peasantf1C"`, `"zDemonqueen"` against `"zDemonQueen"`. Nothing disagrees on
substance, so the alignment is established. `observed`

Because names join case- and space-insensitively, lookups normalize both sides.
Run `data_info --check` to reproduce the count.

This also settles an earlier open question: the A/B/C variants of a monster are
byte-identical in `DMONLIST.BIN` apart from their names because the difference
between them is not in the binary table at all — **it is here**. `ArcherA` is a
level-9 Archer with 35 hit points; `ArcherC` is a level-29 Fire Archer with
171, a fire attack and a Fireball spell. `observed`

### Encounters name a family, not a variant

`MapStats.txt` writes encounter pictures without the variant letter: `"Demon"`,
`"DragonCave"`, `"PeasantM3"`. Appending `A`, `B` or `C` resolves **137 of the
138** encounter slots in the shipped maps; the exception is `"DemonQueen"`,
whose row is named `zDemonqueen`. `observed`

Which variant an encounter means is **not** established. The slot's display
name identifies one exactly in 124 of the 138 cases, and the accompanying
`Dif 1-5` column does not correlate with the letter — difficulty 5 picks
variant A 34 times out of 34. `unknown`

## `ITEMS.TXT`

The 581 item ids form the exact contiguous range 0–580 and are addressable
directly by id. Placed outdoor objects and chest slots embed a 28-byte item
instance whose leading positive `i32` selects the row without an offset;
negative chest values −1…−6 request deferred random generation classes.
`RNDITEMS.TXT` exposes 400 direct-id base-item weight rows, while
`STDITEMS.TXT` and `SPCITEMS.TXT` expose 14 and 59 one-based bonus selectors.
Its footer also supplies the three six-level bonus-chance arrays; the typed
views expose standard strength ranges, special treasure-class eligibility,
compiled equipment and skill types, restricted selectors, the chest
class-to-level matrix, and deterministic generation using the shared random
sequence. The complete column maps, binary joins, probability branches, and
item-instance layout are documented in
[`items.md`](items.md). `observed`

## `Spells.txt`

Nine sections of eleven spells, 99 in all. Each section opens with a **heading
row** naming its school in the column the spell rows use for the number within
that school — `Fire Spells`, `Air Spells`, … `Dark Spells`. The school is not
repeated on the spell rows, so a reader has to carry it down from the heading.
`observed`

| Column | Field | Status |
| --- | --- | --- |
| 0 | id, 1..99, unique across schools | observed |
| 1 | number within the school, 1..11 | observed |
| 2 | name | observed |
| 3 | element it is resisted as (`Fire`, `Elec`, `none`) | observed |
| 4 | short name, for a narrow interface | observed |
| 5–7 | headed `A`, `X`, `M` — see below | inferred |
| 8 | description | observed |
| 9–11 | what it does at normal, expert and master | observed |

### The three numbers are a cost per mastery

Columns 5 to 7 are **non-increasing on all 99 rows**, and 94 rows have all
three equal. They also track the spell's rank: the first spell of every school
costs 1, the second 2, the tenth 25 and the eleventh 30. That is a spell point
cost that never rises with mastery, and five spells get cheaper. `inferred`

The letters `A`, `X`, `M` are the table's own; the mastery names in columns 9
to 11 are Normal, Expert and Master.

## `Class.txt`, `stats.txt` and `SkillDes.txt`

Three tables of the same shape: a name and one or more columns of prose. 18
classes, 25 statistics and 31 skills. Each opens with a heading row of labels
built exactly like a data row, so it is skipped by position rather than by
inspecting it. `observed`

## `2DEvents.txt`

556 establishments — shops, temples, taverns, guilds, training halls, stables —
each with a type, a proprietor and a title, three columns of stock or service,
and opening and closing hours. `observed`

| Column | Field | Status |
| --- | --- | --- |
| 0 | id, 1-based | observed |
| 1 | a second number, restarting within each type | observed |
| 2 | type — "Weapon Shop", "Tavern", "Temple" | observed |
| 3 | map code | observed |
| 5–7 | name, proprietor, title | observed |
| 13–15 | what it stocks, at three levels | inferred |
| 16 | notes | observed |
| 18–19 | hour it opens and closes | observed |

### The map column

A code of a letter and a digit — `A1` through `E3` — naming one of the fifteen
outdoor maps: `E3` is `OutE3.Odm`, New Sorpigal. **536 of the 556 rows carry
exactly one such code**; Free Haven has 95 establishments, Silver Cove 69, New
Sorpigal 46. `observed`

The other 20 do not. Some name several maps (`D2,C3,B1`), some are prose
(`From 153 (D3)`, `Oracle1 (C2)`), one is `na`. **Which map those belong to is
`unknown`**, so they are returned for none of them rather than for a guessed
one.

Nothing in the table places a building anywhere *within* its map. The interiors
are a separate screen the original drew in two dimensions, which is what the
file's name refers to.

## `NPCdata.txt` and `npcprof.txt`

396 named people and 77 professions. Two columns of `NPCdata.txt` are
references, and both resolve cleanly:

| Reference | Resolves |
| --- | ---: |
| "2D Location" → a `2DEvents.txt` row | **367 of 367** |
| "Profession" → an `npcprof.txt` row | **332 of 332** |

`data_info --npcs` reports both counts.

### Two traps in the reference columns

**A location of −1 is not a row id.** Eighteen rows carry it, and it means the
person stands in no establishment. Read as an id it produces eighteen
references that can never resolve; read as zero it hides that the table said
something specific. The value is kept as written and `placed()` asks the
question. `observed`

**One establishment has no type.** Row 479 of `2DEvents.txt` — a house in New
Sorpigal — leaves the type column blank but does have a name, and eighteen
people live in it. A reader that requires a type drops the building and orphans
all eighteen. `observed`

Both were found by measuring the joins rather than assuming them: the first
attempt reported 367 of 385 and 310 of 332, and neither shortfall was in the
data.

### `NPCdata.txt` columns

| Column | Field | Status |
| --- | --- | --- |
| 0–1 | id, name | observed |
| 2 | picture | observed |
| 3–5 | state, fame, reputation | inferred |
| 6 | establishment, or −1 | observed |
| 7 | profession | observed |
| 8–9 | whether they join, whether they carry news — 1 or 0, despite the heading reading `Y / N` | observed |
| 10–12 | three event ids | inferred |
| 13 | the designers' notes | observed |

### `npcprof.txt` columns

| Column | Field | Status |
| --- | --- | --- |
| 0–1 | id, name | observed |
| 2 | chance of appearing on a random NPC | inferred |
| 3 | weekly hire cost | observed |
| 4 | personality — "Merchant", "Sorcerer", "Scholar" | observed |
| 5–7 | action text, benefit while in the party, join text | observed |

The prose carries `%01`-style placeholders the engine substitutes; what each
stands for is `unknown`. The recurring benefit tail "%01 takes %17 percent of
all gold you find" names a share **no column of the table carries** — the
eight columns above are all there are — so the gold share stays `unknown`
and this engine's hirelings take none.

The 51 filled benefit cells use a small set of phrasings, and the engine
reads their numbers out — "Ten percent bonus on all experience learned",
"All boat travel 2 days faster (minimum one day)", "Makes one day of food
per day (maximum of 14 days)", the healers' three rungs, "Unlimited weapon
repair", "Five point bonus to Luck statistic", "Increases protection from
the four elements by 20", the dawn casts of Bless and Heroism at their
written durations. A benefit whose mechanic the engine lacks (skill points,
reputation, the travel spells) is carried as prose only. `observed` for the
phrasings; that the best bonus speaks for the party rather than stacking,
and the two-seat limit, are the engine's own. See src/game/hire.hpp.

## `npctopic.txt`, `npctext.txt` and `NPCNews.txt`

### The topics and the words share one numbering

Both files head their first column "Text Number From NPC Events.Doc", and they
are the same numbering: topic *N* is the label and text *N* is the words.
StarHaven merges them into one table for that reason. `observed`

473 topics, of which **19 have a label and nothing to say**. They are kept:
dropping them would leave gaps in the numbering the NPC event columns index.
`observed`

**All 298 event references from `NPCdata.txt` resolve, and all 298 have words.**
`observed`

### `NPCNews.txt`

279 regional rumours, each with a topic and a passage. The second column is
headed "Map" and **is not a map id**: it takes only sixteen distinct values —
1 and 26 through 40 — and they do not line up with the subjects. Rumour 1 is
about Goblinwatch, which is map 16, and its value is 40; rumour about Lair of
the Wolf, map 32, has value 30. `observed`

Reading it as a `MapStats.txt` id would appear to succeed, because every value
falls inside 1..67. That is the trap: the range check passes and the meaning is
still wrong. The value is kept as written and what it indexes is `unknown`.

## `PROFTEXT.txt`: what a hire says, day by day

79 rows and 16 columns: an id, a profession, and then a **topic and a line for
each of the seven days**, Sunday first. It is the largest table in the archive
and the last one of any size to be read.

- The id is the one `npcprof.txt` gives the profession: **77 of 77** hireable
  professions resolve here. `observed`
- **539 of 539** profession-days are filled in — seven for every one of the 77.
  `observed`
- 76 of the 77 names match `npcprof.txt` outright; the odd one is spelled
  `Stonecutter` here and `Stone Cutter` there. `observed`

The prose carries the same `%01`-style placeholders as the rest of the NPC
tables.

Reproduce with `data_info --proftext` and `data_info --proftext 1`.

## What `MapStats.txt` says about time

Three columns count days, and all three are per map: **Reset #**, **First
Visit Day** and **Refil Days**. The refill interval is 168, 224 or 672 days
across the 79 maps — twenty-four, thirty-two or ninety-six weeks. `observed`
What resets on the reset count, and what the first-visit day is measured from,
are `unknown`.

The tables know about time in two other places: `2DEvents.txt` gives every
establishment an opening and a closing hour, some of them across midnight, and
`PROFTEXT.txt` gives a hired NPC a different thing to say on each of seven
named days. Together they establish hours, a seven-day week, and days that
count. **How fast time passes is in none of them.** `observed`

## `MapStats.txt`'s encounter slots

Each map row carries three encounter slots — a picture, a monster name, a
difficulty and an appearance range written `"2-4"` — and the outdoor maps'
spawn points reference them by number (see
[`odm-tile-index.md`](odm-tile-index.md)).

### The picture names a triple, and the slot means its first

Monsters come in A/B/C triples sharing one picture stem: `RatA`, `RatB` and
`RatC` are Common, Large and Giant Rat. A slot gives the stem, `"Rat"`, and its
name column is the **A** variant's. **138 of 138** filled slots across all 79
maps resolve this way. `observed`

The one that has no triple is the Demon Queen, a unique; `MONSTERS.TXT` carries
uniques with a `z` in front of the picture instead — `zDemonqueen`. `observed`

The slot's own name column agrees with the resolved row on 113 of the 138. The
disagreements are spelling — the slot says `"Bloodsucker"` where the row says
`"Blood Sucker"` — so the picture is the join and the name is not. `observed`

The `Dif 1-5` column does **not** select the variant: 111 of the resolved slots
name the A variant at every difficulty from 1 to 5. What it drives is
`unknown`. `observed`

Reproduce with `data_info --encounters`.

## `npcbtb.txt`: what works on whom, and how they say it

The name is the file's own shorthand: **b**eg, **b**ribe, **t**hreat, the three
ways a party can lean on an NPC. Twenty-eight rows, thirteen personality
columns.

The first column is either an approach name or a message number:

| Rows | First cell | What the personality columns hold |
| --- | --- | --- |
| 1–3 | `Beg`, `Bribe`, `Threat` | `1` or `0` — whether that approach works |
| 4–27 | `1`–`24` | that personality's wording for that message |

The second column is a designers' note naming what the message is for — `"Rep
ok, 1st greet"`, `"Beg return"`, `"I don't like threats"` — and is shared by
every personality. `observed`

### The header repeats the matrix, and the matrix repeats the messages

The columns are headed `Peasant BTB`, `Thief BT`, `Sorcerer TB`, `Paladin Be` —
a name and a reminder of which approaches work, spelled with the initials. The
suffix is dropped; the three rows below state the same thing exactly.

The messages say it a third time. Six of the twenty-four are an acceptance and
a refusal for each approach, and a personality has the acceptance exactly where
the matrix allows the approach and the refusal exactly where it does not; the
impossible cell is written `n/a`. All **39 of 39** personality-and-approach
pairs agree. `observed` — `data_info --personalities` prints the count.

### Twelve personalities, and a thirteenth for monsters

The professions in `npcprof.txt` name twelve personalities. All twelve are
described here once `Evil Fanatic` is read as the professions' `Fanatic` —
`12 of 12`, a suffix match on the one name the two files spell differently.
The thirteenth column, `Monster`, no profession names; its wording is the
hostile register (`"%02, eh?  I'll remember that."`). `observed`

The prose carries the same `%01`-style placeholders as `npcprof.txt`.

## `MONSTERS.TXT`'s treasure codes

A kill's leavings are written as one cell: `"5%6D20+L2Bow"` is a five percent
chance of six twenty-sided dice of gold and an item of treasure level two that
is a bow. Any part may be absent — `"4D6"` is gold that always drops, `"5%L2Ring"`
a chance at a ring and no gold. **145 of the 145** coded rows parse. `observed`

The 117 coded items name 17 distinct kinds — `Misc`, `Ring`, `Amulet`, the
weapon skills, the armour skills, `Shield`, `Cape`, and the broad `Weapon` and
`Armor` — or none, and an unnamed kind is any kind. Every named kind is a
selector the random-item generator already takes (see [`items.md`](items.md)),
so a kill's item is generated the way a chest's is rather than invented; the
word-to-selector map itself is `inferred`. Reproduce with
`data_info --treasure`.

## The damage notation

`MONSTERS.TXT`'s two attack columns and `ITEMS.TXT`'s weapon modifier write
damage the same way, in one of exactly two forms: `NdN` and `NdN+N`. The letter
is written both ways — `3d3` and `1D6+1`. `observed`

- **212 of 212** monster attack damages parse. `observed`
- **78 of 78** weapons — the things whose equip type is a weapon, a two-handed
  weapon or a missile — carry dice. `observed`
- Everything else's modifier is not dice: armour's is a flat number, a wand's
  is charges. A cell reading `0` means no attack, and must not be read as a
  roll of nothing that still pays its bonus. `observed`

Reproduce with `data_info --dice`.

## What a shop's row says it sells

`2DEvents.txt` gives every establishment a price multiplier — the column headed
`Val`, **1.5 or 2** on the shipped rows — and two stock specifications written
the designers' way: `"L1 Weap"`, `"L2 Sword,Dagger"`, `"L2 Bows"`, plus a
count. `observed`

The `Ln` is a treasure level and the words after it name a kind, which is
exactly what the random-item generator takes (see [`items.md`](items.md)), so a
shop's shelves can be generated rather than invented. The generator's kinds are
equipment types, not weapon skills, so `"Sword,Dagger"` narrows only as far as
*weapon*. `inferred`

## The skills' own effect lines

`SKILLDES.TXT` lists 31 skills, and beside each skill's prose its columns
state what the skill does, one line per rank: `"Skill added to Attack
Bonus"`, `"Skill added to Attack Damage"`, `"Skill added to Armor Class"`,
`"Skill adds to Hit Points"`, `"Skill adds to Spell Points"`, `"Skill
adjusts shop prices in your favor"`, and the nine magic schools' `"Effects
vary per spell"`. `observed` The engine applies exactly what the first
(normal) line names: a weapon skill's points ride on the to-hit roll, and on
damage where its line grants damage; armour and shield points join the
armour class; Merchant bends counter prices; a school's points are the
per-skill dice multiplier its spells scale by. The expert and master lines —
doublings, stuns, dual wielding, the second arrow — are beyond this slice.
What a point is numerically worth where the line names no number, the
raising staircase (the next point costs its own number), five points a
level, and each class's one starting skill are the engine's own and say so.
`inferred` for those. The hired masters' bonuses — Arms Master, Squire, the
merchants — add their rows' points to the same reads.

## The spells' own numbers

`Spells.txt` states what a spell does in prose, and the damage and healing
among it follow few enough phrasings to read exactly: `"does 2-6 points of
damage"` flat, `"Damage is 1-4 points of damage per point of skill"` pure
scaling, `"does 9 points of damage plus 1-9 per point of skill"` both, and
the heals as `"Cures 5 hit points"` per mastery cell or `"heals a single
character of 3-7 hit points"` in the description. **25 of the 99** parse a
number this way — every direct-damage spell in the game and both heals; the
74 that state none are buffs, cures of conditions, and utility, honestly
beyond a numbers parser. `observed` Reproduce with
`data_info --spell-effects`.

The engine casts from it: a spell scroll (its spell the S-number in its own
`ITEMS.TXT` row) reads once at normal mastery — heals to the most wounded,
damage at what the party aims at, answered by the resistance of the spell's
own element. What the prose does not state is the engine's and says so: the
reader is the first character standing, their level stands in for the skill,
and First Aid is the one spell casters cast from their own points.

Four of the unnumbered 74 are conditions, and their prose is exact enough to
act on: **Charm** (61) "removes any hostile feelings ... If this creature
takes any damage, it will immediately become hostile again"; **Mass Fear**
(62) makes "all creatures in the caster's sight ... flee", broken by damage,
"will not work on Undead"; **Slow** (81) "doubles the recovery rate of a
single monster"; **Paralyze** (86) "prevents a monster from moving or
attacking ... cannot retaliate". Their durations sit in the *description* —
"3 minutes per point of skill" — where the buffs' sit in the mastery cells,
so the duration parser reads both places, trusting running prose only when
the scaling phrase is present. The engine lays all four through scrolls;
which creatures count as Undead is read off their names and marked the
engine's own. `observed` for the phrasings.

## The monsters' spell column, cast

`MONSTERS.TXT` column 25 writes `"Fireball,N,5"` — the spell's display name,
a mastery letter `N`/`M` (no shipped row says `E`), and a skill value — with
column 24's percent saying how often. All 55 coded cells parse; the names
resolve in `Spells.txt` except two shipped typos, `"Dispell Magic"` and
`"Psychic Shockt"`, which a two-character prefix tolerance absorbs.
`observed` The skill value is exactly the number the spell prose's per-skill
dice scale by, so a Fire Archer's Fireball rolls its written 1-6 five times
— every number in the chain is the tables' own.

## The stables' and docks' timetables

The nine `Stables` and twelve `Boats` rows of `2DEvents.txt` use the three
stock columns for **routes**, up to three each, written the designers' way:
`"Castle Ironfist D3,M,W,F,2"` is the destination with its area code, the
weekdays a ride leaves, and the days the ride takes. The sheet's own margin
notes name the parts — `"Destination,Area"`, `"Days leaving,"`,
`"Days Travel Time"`, `"Upto 3 Destinations"`, `"Teleport to Other Stable"`.
Tuesday is written both `T` and `Tu`, Sunday both `Su` and `Sun`, and a
destination may drop its area code — `"Bootleg Bay East"` — and resolve only
through `MapStats.txt`'s display names. One cell is a designer note rather
than a route (`"Any outdoor area..."`, on the Enterprise). `observed`

Two things the sheet does not say, which the engine supplies and marks as its
own: where you stand on arrival, and what a point of the `Val` column costs
in gold.

## `USEITEMS.TXT` is the alchemy

The last of the item-machinery family holds the herbs and potions, items
160..188. Each row carries its effect in the designers' prose — `"Cure 10
Hit points"`, `"Set Haste to 6 Hrs"`, `"Might +15 Perm, Int -5 perm 1 time
only"` — what becomes of the item (`"remove Item"`, or `"Change Item to
163"`, the empty bottle), and a full **mixing matrix** against every other
potion: 50 pairs yield a new potion and 390 blow up, graded `E1`..`E4`, whose
meanings the sheet's own header writes out — from 10-20 fire damage up to
Eradicated with everything broken. All 29 rows parse. `observed` Reproduce
with `data_info --use-items`.

Spell scrolls join elsewhere: their `ITEMS.TXT` first modifier is an
S-number — `Healing Touch`'s scroll writes `S47`, and `Spells.txt` id 47 is
Healing Touch. `observed` And `Scroll.txt` is the **message scrolls'** prose,
86 rows keyed by item id — row 505 is Sulman's letter itself, the full text
of the thing Andover pays for. `observed`

The engine drinks: `U` in a pack applies the cures, says the effect in the
table's words, and the bottle empties into item 163 exactly as written.
Mixing and the scroll spells wait on systems not yet built.

## The entrances' trailing columns

`2DEvents.txt`'s last columns — group-headed `Schedules`, `Other Exits`,
`Questbit`, `Enter` — are typed by the kind of row. Shops keep opening hours
there, already read. Castle entrances chain to a second screen: row 153's
exit cell reads `"2D 154"`, and row 154 is the Throne Room, whose own map
cell answers `"From 153 (D3)"`. Dungeon and temple entrances carry a
restriction word and a refusal — GoblinWatch's row says `"Need Key"` and
*"The doors to this keep are locked."* `observed`

The restriction is documentation of a gate the scripts enforce themselves:
OutE3's GoblinWatch event checks quest bit 300, else checks for item 489 and
refuses with that same locked-door line; with the key it **takes it**, sets
the bit, and travels into `D01.blv` — and its opcode-2 argument is **171**,
the entrance's own `2DEvents.txt` row. The sheet and the script describe one
system from two sides. `observed`

The remaining numeric cells on entrance rows — 16..37 running with the row
order, 50 and 169 on the Oracle — join to nothing tested yet. `unknown`

## The guilds' shelves, written outright

The eighteen magic-guild rows put their stock in plain words: `"Type = Fire,
Spells 1-7"` — the school, and the within-school spell numbers `Spells.txt`
already carries — with 1..7 at every Initiate guild, 1..11 at every Adept,
and `Val` of 2 or 3 as the price multiplier over the books' own values. The
margin notes say what the counter does: `"Buy Spells"`. Since each of the 99
spells has exactly one book carrying its S-number, a guild's shelf resolves
without generation or invention. `observed`

## The banks' two verbs, and the town halls' silence

The six `Bank` rows carry margin notes naming the counter's actions —
`"Deposit"` and `"Withdraw"` — and no column anywhere carries an interest
rate, so the vault only keeps what it is given; the engine's counter does
exactly that. `observed` The three `Town Hall` and `City Council` rows carry
nothing behind them at all: no bounty table ships in the thirty, so the
original's monthly bounty hunts must be generated in the executable, and an
engine that wants them must invent them. Scouted and left alone. `observed`

## `Trans.txt` is the transitions' prose

Not routes: 234 rows keyed by a `2D#` column, each an atmospheric
description — `"A large dragon obviously makes his residence in these
caves..."` — and most of them blank. What the key indexes is `unknown`;
the obvious candidate, `2DEvents.txt` rows, is untested. `observed` for the
shape.

## The `%01` placeholders

Every table of NPC prose carries them — `npcbtb.txt`, `npcprof.txt`,
`PROFTEXT.txt`, `Merchant.txt`, `npctext.txt`. Counting across all five, the
commonest are `%17` (100), `%02` (85), `%01` (63), `%06` (30), `%11` (28) and
`%10` (23). `observed`

Four are readable from the lines that use them, and two of those resolve into
`GLOBAL.TXT`:

| Code | Stands for | Evidence |
| --- | --- | --- |
| `%01` | the speaker's name | `"I'm %01."`, `"Name's %01."` |
| `%02` | the person addressed | `"%06 %02."`, `"%02, eh?"` |
| `%05` | the time of day | `"Good %05!"` with `GLOBAL.TXT` 395 `morning`, 396 `day`, 397 `evening` — **consecutive** |
| `%06` | an honorific | `"I'm sorry, %06,"` with 385 `sir`, 387 `lady` |

The consecutive run at 395–397 is what makes `%05` more than a guess: `"Good
%05!"` can only be one of three words, and exactly three sit together in the
string table. `inferred`

Four more are readable from `Merchant.txt`'s own lines, which name what they
stand for as they use them: `%24` the item (`"This %24 is of the finest
quality"`), `%25` the ordinary price and `%27` the one actually named
(`"Ordinarily I sell things like this %24 for %25 gold... I'll sell it to you
for %27"`), and `%28` the shopkeeper's trade (`"Sorry, I am a %28"`).
`inferred`

The rest are `unknown`. `%17` is plainly a percentage a hireling takes —
`"%01 takes %17 percent of all gold you find"` — but no column in `npcprof.txt`
carries such a number, so where its value comes from is not established. StarHaven leaves an unread code in the text
rather than blanking it, so nothing is silently swallowed.

## `Merchant.txt`: what the shopkeeper says

Six situations by four actions — buy, sell, repair, identify — and **21 of the
24 cells** are filled; the other three say `n/a`, which is where the situation
cannot arise for that action. A merchant of the wrong type has nothing to say
about your buying, only about your selling. `observed`

The rows are: not enough gold, no merchant skill, regular merchant skill, good
merchant skill, wrong type of merchant, and unnecessary. So haggling is a skill
with three bands, and the prose carries the same `%01`-style placeholders as
the NPC tables — `%24` for the item and `%25` for a price. `observed`

## `stats.txt` is the character sheet's field list

Twenty-five rows, each a field and the designers' description of it, in the
order a sheet shows them: the seven attributes — Might, Intellect, Personality,
Endurance, Accuracy, Speed, Luck — then Hit Points, Armor Class, Spell Points,
Condition, Quick Spell, Age, Level, Experience, the four attack and shoot
bonuses, five resistances and Skill Points. `observed`

The table gives the names and the prose; it gives no numbers. What a character
starts with, and what an attribute's bonus is, are in the original's
executable, not in any shipped table. `observed`

## `npcnames.txt`: two columns of given names

540 names in two columns headed `Male` and `Female`. The columns are not the
same length, so a blank cell is the end of that column rather than of the row.
`observed` The game draws on this when it needs to name somebody, and so does
this engine when it makes a party.

## `GLOBAL.TXT`: the interface's vocabulary

596 numbered strings, ids **0 to 595** with no gaps: `AC`, `Accuracy`, `Add to
Stat`, `Adventurer`, and on through every word the original drew on its panels
and menus. Two columns, the id and the string. Ids start at zero, so a reader
that treats a missing number as zero swallows the first string. `observed`

What indexes them is `unknown` — nothing decoded so far references the table by
id — but it is the label vocabulary any faithful interface needs.

## The journal: `Quests.txt`, `Awards.txt`, `Autonote.txt`

Three tables keyed the same way. The game sets bit *N* when something happens
and the journal shows line *N*, so the numbering is the interface and a line
with nothing written is still a line.

| Table | Bits | With text | Columns |
| --- | ---: | ---: | --- |
| `Quests.txt` | 512 | **52** | bit, note text, designers' notes, an older wording |
| `Awards.txt` | 100 | 86 | bit, award, designers' notes |
| `Autonote.txt` | 128 | 125 | bit, note text, category |

### Most quest bits have no quest text

Only 52 of the 512 carry player-facing text. All 512 carry a designers' note —
`" 1 D09, key to open D05"` — and 238 carry an "Old Quest Note Text", of which
43 are just the designers' note repeated. Where the other 460 bits get their
wording from is `unknown`; it is not in this table. `observed`

Autonotes are categorised: `Stat`, `Teacher`, and others, which is presumably
how the journal groups them. `inferred`

### `Autonotes.txt` is an older copy

The archive holds both `Autonote.txt` (128 entries) and `Autonotes.txt` (116).
They share 116 bits, 114 of them word for word; the two that differ are less
specific in the shorter file — `"10 Hit points cured by the right side pool."`
against `"…by the right side pool in Snergle's Iron Mines."`. `Autonote.txt` is
the later one and the one StarHaven reads. `observed`

## Invalid-input behavior

The reader rejects, deterministically and without reading out of bounds:

- an entry too short for the 48-byte container header;
- a zlib stream that will not inflate, or one whose output length disagrees
  with the declared size;
- a quoted field that runs to the end of the buffer without closing.

A cell request outside the table returns an empty string rather than failing,
because ragged rows make out-of-range reads normal rather than exceptional.

## Open questions

- The container's `unknown` field at 0x2C (256 on every table). `unknown`
- Which A/B/C variant an encounter slot selects, and what `Dif 1-5` drives.
  `unknown`
- The semantics of the monster columns tagged `inferred` above — `Pref`,
  `Bonus`, `Hst`, `Rec` — and of the `Misc Special` column. `unknown`
- The `%01` codes other than 1, 2, 5 and 6. `unknown`
- What indexes `GLOBAL.TXT` by id. `unknown`
- The remaining tables without typed views — `Merchant.txt`, `USEITEMS.TXT`,
  `Trans.txt`, `passwords.txt` — are readable but not yet modelled. `unknown`
- Where `MONSTERS.TXT`'s treasure codes (`"5%6D20+L2Bow"`) are interpreted, and
  how they index `ITEMS.TXT`. `unknown`
