# Design data tables (Might and Magic VI)

Status: **verified.** Thirty tab-separated tables sit inside `icons.lod`
carrying the game's design data: maps, monsters, items, spells, quests, classes
and NPC dialogue. Each claim is tagged `observed`, `inferred`, or `unknown`.

## Scope

Covers the container, the text format, and the six tables parsed into typed
rows — `MapStats.txt`, `MONSTERS.TXT`, `ITEMS.TXT`, `RNDITEMS.TXT`,
`STDITEMS.TXT`, and `SPCITEMS.TXT`. The other 24 are readable through the same
reader but not yet given typed views.

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
- The 24 tables without typed views: spells, quests, NPC dialogue and shop
  behaviour are readable but not yet modelled. `unknown`
- Where `MONSTERS.TXT`'s treasure codes (`"5%6D20+L2Bow"`) are interpreted, and
  how they index `ITEMS.TXT`. `unknown`
