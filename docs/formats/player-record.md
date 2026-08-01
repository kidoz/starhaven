---
title: "Player runtime record"
summary: "Observed field offsets, skills, statistics, and remaining unknowns in the Might and Magic VI player record."
doc_type: reference
status: partial
last_updated: 2026-08-01
tags:
  - mm6
  - runtime
  - player
  - memory-layout
---
# The player record (`MM6.exe`, runtime)

Status: **partially mapped, from traced routines only.** Not a file format:
this is the in-memory character structure the executable's own combat and
skill code addresses. It is recorded because three separate traces kept
re-deriving the same offsets, and because the fields StarHaven still guesses
at — recovery, the attack bonus's parts — will be found here. Each claim is
tagged `observed` (read from an instruction) or `inferred`.

## Scope

This page covers the `0x161c`-byte in-memory player record fields established
through traced combat, skill, condition, equipment, and training routines. It
does not claim a complete save-file schema; unresolved offsets remain
`unknown`.

## A verification pass, and what it caught

After two retractions in one sitting, the load-bearing claims were each
re-checked from an angle they were not derived from. The results, in order
of how much weight they carry:

- **The spell dice — three of thirty-four were wrong.** Checked against the
  bands `SPELLS.TXT` prints in words, which the tracing had not used.
  Flame Arrow's "1-8", Lightning Bolt's "1-8 per point", Harm's "8 plus 1-2
  per point", Sun Ray's "20 plus 1-20", Dragon Breath's "1-25 per point" all
  matched. **Static Charge, Cold Beam and Magic Arrow did not**, and the
  cause was a decoding error in two forms: a case that computes
  `flat + rand() % sides` had been read as `flat + 1d(sides)`, one point
  high, and a case with a fixed die *count* had been read as a single die.
  All three are corrected, and the roller now distinguishes the four shapes
  the cases actually take.
- **The buff slot map — confirmed structurally.** Each slot was read from
  one instruction, so the check is that the map is *consistent*: across the
  whole executable no two spells write the same slot, no spell writes two
  unrelated ones, and Day of Protection's seven are exactly seven slots that
  each also have a single owner. A wrong reading would have shown up as a
  collision or an orphan and did not. Slots 6 and 7 have a second writer
  outside the spell dispatcher, which is noted and unread.
- **The attribute ladder — confirmed overwhelmingly.** The pair at
  `0x4c2860`/`0x4c289c` is referenced from **73 and 37 sites** across dozens
  of routines, not just the recovery one it was found in. It is the game's
  universal parameter curve beyond doubt.
- **The class tables — confirmed five ways from the prose.** `0x4c2640` is
  read from ten sites, `0x4c2654` from four, so neither is a one-routine
  reading. And `Class.txt` states five checkable things, all of which hold:
  "Champions enjoy the benefit of an extra four hit points per level"
  (Knight 4 → Champion 8), "Cavaliers ... an extra two" (4 → 6), "High
  Priests ... an extra two hit points and spell points" (2 → 4 and 3 → 5),
  "Arch Mages ... an extra two" (2 → 4 and 3 → 5), "Knights begin with the
  greatest number of hit points" (base 30, the highest of the six).
- **The weapon-recovery table — the weakest of the four, and it stands,
  with one hole found.** `0x4c2750` is read from six sites and every one is
  inside `0x481a80`, so no second routine corroborates it. What does
  corroborate it is `SkillDes.txt`: daggers are "very quick" and carry the
  lowest number of any weapon at 60, axes are "rather slow on the attack"
  and carry the highest at 100, and the three skills whose expert line
  promises "a quicker attack" are exactly the three the routine tests for.

  Checking it a second way — every equippable row `ITEMS.TXT` ships, tallied
  by skill group — turned up a real hole. The table names twelve groups; the
  file uses **thirteen**, giving three weapons the group **"Club"**, which is
  no skill in the game. The routine copes because of its own shape: it loads
  entry 0, the bare-hand 100, and only overwrites it when the item's skill
  byte names a row, so an unknown group leaves the default standing.
  StarHaven was reading the empty lookup as a recovery of zero, which made a
  club strike instantly; it now keeps the default, as the routine does.

## Where the offsets came from

| Routine | What it does | Documented in |
| --- | --- | --- |
| `0x41e4f0` | opens a chest, rolls Disarm | [`event-tables.md`](event-tables.md) |
| `0x4853e0` | fetches Disarm Traps with its doublings | same |
| `0x421cb0` | rolls whether a blow lands | [`text-tables.md`](text-tables.md) |
| `0x421dc0` | applies a resistance to a blow | same |
| `0x482e80` | fetches any of 23 stats, with bonuses | this file |

## Fields read by those routines

| Offset | Size | Field | Status |
| --- | --- | --- | --- |
| `+0x28` | 2 | a word the attack-bonus path adds in | inferred |
| `+0x36` | 2 | a word the same path adds in | inferred |
| `+0x50`..`+0x55` | 6 | six resistance bytes; **200 or above means immune**. The element id does **not** index them directly — see the audit at the end | observed |
| `+0x60` | 4 | armour class, as the to-hit roll reads it off the target | observed |
| `+0x7d` | 1 | packed skill byte: low six bits the level, bit 6 expert, bit 7 master | observed |
| `+0x128` + n×28 | 4 | the equipped-item ids the trap and attack code walk (`Pendragon` 410, `Hades` 415 are compared here) | observed |
| `+0x13c` + n×28 | 1 | that item's flag byte; **bit 1 is "broken"**, tested before an item counts | observed |
| `+0x144` + n×28 | 4 | a second per-item word the stat getter reads (special bonus id: 399 and 401..429 are compared) | inferred |
| `+0x141c` | 4 | a party-level word the attack-bonus path reads | inferred |
| `+0x1428` | 4 | the slot index a stat's case walks from | observed |
| `+0x142c` | 4 | the weapon slot index — the trap, attack and stat code all use it | observed |
| `+0x1430` | 4 | a third slot index, used by the last four stat cases | observed |
| `+0x1440` | 4 | the slot index the Disarm getter uses | observed |

## The stat dispatcher

`0x482e80` takes a stat id 0..22 and returns its value with every bonus
applied. A 23-byte index table at `0x4836b0` maps the id to one of eight
cases through a jump table at `0x483690`:

- **ids 0..13 share one case** — the attributes and the derived numbers,
  which differ only by the id the case then switches on inside (it compares
  against 3, 7, 8, 9, 10, 11 and 13 while summing worn items' bonuses).
- **id 14** walks the slots from `+0x1428`; **15..18** from `+0x142c`;
  **19..22** from `+0x1430`. `observed`

Each case walks the same shape: for every equipped slot, skip it when the
item's flag byte at `+0x13c` has bit 1 set (broken), then add what the
item's id at `+0x128` and its bonus word at `+0x144` grant. That is the
same walk the Disarm getter performs, which is how "of Thievery" was
identified in the chest trace.

## The stat block, read straight off the base getter

`0x483800`'s jump table at `0x4838d0` gives one case per id, and the whole
block reads off at once. It splits four ways. `observed`

- **The "attribute run"**, eight fields sixteen bytes apart — *now known to
  be the buff records' power words, not attributes*: `+0x12b0` id 9,
  `+0x12c0` id 6, `+0x12d0` id 1, `+0x12e0` id 2, `+0x12f0` id 4, `+0x1300`
  id 5, `+0x1310` id 0, `+0x1320` id 3. Seven of the eight are the sheet's
  attributes in `stats.txt`'s order — **0 Might, 1 Intellect, 2 Personality,
  3 Endurance, 4 Accuracy, 5 Speed, 6 Luck** — which the routines confirm
  from the other side: hit points ask for 3, spell points for 2, recovery
  and armour for 5, the attack bonus for 4. **Id 9 is an eighth field in the
  same block with no name.** `unknown`
- **Five globals, and they are not resistances — the earlier reading is
  retracted.** Ids 10, 12, 11, 13 and 23 read five words at `0x908e3c`,
  `0x908e4c`, `0x908e5c`, `0x908e6c` and `0x908e7c`, sixteen bytes apart,
  and last time this was written up as the party's resistance store. It is
  not. `0x47d170` clears the whole block and shows its shape: **sixteen
  records of sixteen bytes** starting at `0x908e34`, each an eight-byte
  expiry, a **power** word at `+8`, a skill word at `+0xa` and two flag
  bytes. That is the party's **spell-buff array**, and the five ids read the
  *power* of its first five slots — the five "Protection from" spells. A
  character's own resistances are still the six bytes at `+0x50`, exactly
  where the chest trace found them, so this engine's per-character
  resistances were right all along. `observed`
- **A pair of stored words**: ids 15 and 19 both read `+0x1270`, and id 16
  reads `+0x1280`.
- **Eight ids with no base at all.** Ids 7, 8, 14, 17, 18, 20, 21 and 22
  fall to the common `ret` with zero in `eax`, so whatever they are worth is
  built entirely by the bonused getter from gear and level. **Id 14 is the
  character's level** — both the hit-point and spell-point routines multiply
  their class number by it — and the rest are unnamed.

So the block splits cleanly: the character's own numbers in the record, the
party's protections in a buff array outside it, and eight ids that are pure
derivation. Ids 7, 8 and 9 are named in
[`weapon-specials.md`](weapon-specials.md).

## The attribute store, settled — and the getter renamed

The question left open when the character's buff array was found is closed,
and it closed against the earlier reading.

The script-variable table names the attribute fields outright. Variables
**32 to 37** write `+0x18`, `+0x1c`, `+0x20`, `+0x24`, `+0x28` and `+0x2c`;
variables **38 to 44** write `+0x16`, `+0x1a`, `+0x1e`, `+0x22`, `+0x26`,
`+0x2a` and `+0x2e`. So the seven attributes live as **seven pairs of words,
four bytes apart, in the run `+0x16` to `+0x30`** — a stored value and a
modifier each — and a map script sets either half by id. `observed` at
`0x440d7e`..`0x440e94`.

**They are not at `+0x12b0 + 16k`.** Which settles the other half: the words
the stat getter returns for the attribute ids are the **power fields of the
character's buff records**, at `+8` of each. Meditation writing slots 6 and 7
lands on the ids for Intellect and Personality, and Power writing 10 and 11
lands on Might and Endurance — exactly the attributes their rows name, which
is now a consequence rather than a coincidence.

**So `0x483800` is not the base getter.** It returns a character's **spell
bonus** to a stat, beside `0x482e80`'s gear bonus. The name used in the
earlier notes is withdrawn, and with it the description of `+0x12b0`..
`+0x1320` as "the attribute block".

**And how the four parts compose, settled.** The re-opened question is
closed in the same sitting. The stored attributes run from **`+0x14`**, four
bytes apart, each an even word holding the value and the odd word after it
holding a modifier — variables **31 to 37** and **38 to 44** respectively,
which also corrects this engine's own reading of the run as starting at 32.
The two formulas confirm the mapping from the other side, each reading the
pair for the stat it asks the getters about: **max hit points** asks for id
3 and reads `+0x20`/`+0x22`, **max spell points** asks for id 2 and reads
`+0x1c`/`+0x1e`. `observed`

So an attribute a formula uses is

> **stored value + stored modifier + spell bonus (`0x483800`) + gear bonus
> (`0x482e80`)**

and the ladder is applied to that sum. Nothing here changes the class tables
or the per-level numbers, which were read from their own tables and confirmed
five ways from `Class.txt`.

## The stat ids, and where each base value lives

`0x483800` is the *base* getter beside `0x482e80`'s bonused one, and its
own 24-entry jump table at `0x4838d0` names a field per id. Eight of them
land on a 16-byte-spaced run — `+0x12b0`, `+0x12c0`, `+0x12d0`, `+0x12e0`,
`+0x12f0`, `+0x1300`, `+0x1310`, `+0x1320` for ids 9, 6, 1, 2, 4, 5, 0 and
3 — which is the attribute block; two more sit at `+0x1270` (ids 15 and
19) and `+0x1280` (id 16). `observed`

**The attack bonus is stat 4.** The getter at `0x47e270` asks both the
base and bonused routines for id 4 and then adds the weapon skill scaled
by a percentage: the weapon's kind selects a skill id through the table
at `0x4c276c` (16, 15, 14, 13, 2, 12) and that skill's percentage comes
from the byte table at `0x4c27fc` — 100, 100, 100, 50, 10, 100, 75, 60,
50, 30, 25, 10. So the attribute contributes **raw**, not through the
sheet's bonus curve, and a weapon whose skill is worth only 10% barely
gains from training. `observed` for both tables; which percentage answers
which StarHaven skill name corresponds to the id remains `unknown`.

## The class and skill numbering, closed

`GLOBAL.TXT` holds both runs, and the executable indexes them by id:

- **Class id = row 253 + id** — Knight 0, Cavalier 1, Champion 2, Cleric
  3, Priest 4, High Priest 5, Sorcerer 6, Wizard 7, Archmage 8, Paladin
  9, Crusader 10, Hero 11, Archer 12, Battle Mage 13, Warrior Mage 14,
  Druid 15, Greater Druid 16, Arch Druid 17. This confirms every
  promotion beat walked in `evt_info --arc`: 9→10 is Paladin→Crusader,
  6→7 Sorcerer→Wizard, 7→8 Wizard→Archmage. `observed`
- **Skill id = row 271 + id** — Staff 0, Sword 1, Dagger 2, Axe 3, Spear
  4, Bow 5, Mace 6, Blaster 7, Shield 8, Leather 9, Chain 10, Plate 11,
  the nine schools 12..20, then Identify Item, Merchant, Repair Item.
  `observed`

With the names in hand, the percentage table at `0x4c27fc` reads:
Staff/Sword/Dagger/Bow 100, Mace 75, Blaster 60, **Shield 50, Leather
30, Chain 25, Plate 10**, every school 100, Identify Item and Repair
Item 120, Merchant 20. And the priority list at `0x4c276c` is Light,
Earth, Water, Air, **Dagger**, Fire, Plate, Chain, Leather, Shield,
Blaster, Mace, Bow, Spear.

**The doubt is settled: the getter serves the striker.** Reading the
to-hit routine's three callers decides it. Each pushes four arguments,
and in every one the *second* is the monster's runtime record — the same
548-byte array at `0x56f478` the AI walks — while the routine reads
armour from that second argument at `+0x60`. The first argument is
whoever is striking, and it is the object the getter is called on. So
`0x47e270` and `0x47e810` return the **attacker's bonus**, and StarHaven
now assembles it their way: an attribute read raw plus the first skill of
the priority order the character holds, weighted by that skill's own
percentage. `observed`

What remains odd is filed rather than smoothed over: an attack bonus that
weighs Plate at 10% and Shield at 50% — and searches armour skills before
weapons — reads strangely for an offensive number, and the engine follows
the code rather than the intuition. `unknown` why the table is shaped
that way.

## The weapon path, read from the other side

`0x47e810` is the getter the to-hit routine calls whenever a weapon is
involved, and it confirms the shape read at `0x47e270` rather than
replacing it: it takes the equipped **weapon slot index at `+0x142c`**,
reads that slot's item id at `+0x128 + n × 28`, and — for anything but
the two blaster ids 64 and 65 — computes from the same pair of tables,
asking the stat dispatcher for **stat 4** (base and bonused) and scaling
the skill by the byte at `0x4c27fc`. The blaster branch at `0x47e9cc`
takes a different road: it adds the word at `+0x36` and bands the result
against a four-entry threshold table at `0x4c2834` — **50, 100, 150,
65535** — before applying the same percentage. `observed`

The skill chosen is not the weapon's: the helper at `0x482d30` walks the
same fourteen-entry priority list at `0x4c276c` and returns the **first
skill the character actually has**, reading each from `+0x1380 + id × 8`.
So the id puzzle stands — the walk is over a fixed priority order, not a
weapon-to-skill map — but the formula's shape is now confirmed from both
callers: **stat 4, plus a skill scaled by a per-skill percentage.**
`observed`

## The skill block, and a join that did not land

The attack-bonus getter walks a list of skill ids and reads each one out
of the character at **`+0x1380 + id × 8`** — so the skills are an
eight-byte-per-skill block, not the single packed byte the chest trace
found at `+0x7d` (that byte is the acting character's Disarm, copied for
the check). `observed`

Two tables drive the walk: a fourteen-entry priority list at `0x4c276c`
holding ids `16, 15, 14, 13, 2, 12, 11, 10, 9, 8, 7, 6, 5, 4` — the first
one the character actually has wins, with id 17 as the fallback — and a
per-skill percentage table at `0x4c27fc`: `100, 100, 100, 50, 10, 100,
75, 60, 50, 30, 25, 10, 100 ×8, 120, 20, 120`. `observed`

**The join to skill names failed.** Read against `SkillDes.txt`'s rows
either 0- or 1-based, the priority list interleaves armour skills and
magic schools in an order no weapon-bonus reading explains (1-based it
begins Earth, Water, Air, Fire, Sword; 0-based Spirit, Earth, Water, Air,
Dagger). Either the executable numbers its skills by a third order, or
this getter is not the melee attack bonus at all but its armour-class
sibling — the weapon path at `0x47e810`, which reads the equipped weapon
slot at `+0x142c`, is the better candidate for the melee bonus and is not
yet read. `unknown`

## The recovery counter, found

**`+0x137c` is the recovery counter**, a `u16`, and it is the field four
earlier searches missed. Every write to it in the executable is one of
seven:

- two tick-downs (`0x482c2c`/`0x482c63` and `0x488664`/`0x488693`) that
  subtract an elapsed amount scaled by **0.01** (the double at
  `0x4b93f8`) and clamp at zero, setting a global flag at `0x52d29c`
  when a character comes free;
- one clear in the party-reset loop at `0x48624c`, which then walks
  `+0x1380` to `+0x1410` in eight-byte steps — the skill block, cleared
  beside it;
- one set from `AI.CPP` (the assertion at `0x405c64` names the file and
  line 2546) that multiplies its input by **32/15 = 2.1333…** (the double
  at `0x4b9318`) before storing.

`observed` Both ends are now read further:

**The setter is the party's own.** The `AI.CPP` site handles a queued
message whose word at `+0x1c` packs a kind in its low three bits and an
index above them; kind **4 with index 0..3 is a party member**, and the
message's second parameter — the dword at `+0x20` — is multiplied by
**32/15** and stored as that character's recovery. So an action queues
its own recovery amount and the handler scales it. `observed` The monster
table's `Rec` column does **not** reach this field; monsters carry their
own counter elsewhere. `observed` by exclusion — this is the only setter,
and it writes only to party slots.

**What speeds the burn is an item, not a spell — the earlier reading is
retracted.** The routine does not walk an effect list. It walks the
**sixteen equipment anchors at `+0x1428`**, and for each one that holds an
item it skips the piece when the flag byte at `+0x13c + 28n` has bit 1
(broken) and otherwise compares the item's dword at **`+0xc`** — the
special-enchantment id, in a 28-byte item record whose id sits at `+0`,
enchantment at `+4`, strength at `+8`, special at `+0xc`, charges at
`+0x10` and flags at `+0x14`. The value it wants is **17**, which the
game's own special-bonus table names **"of Recovery"** (class B, value
200). When such a piece is worn the burn takes a percentage of **50**, so
the amount subtracted is `elapsed × 50 / 100 + elapsed` — **elapsed ×
1.5**. `observed` The earlier claim that this was effect 17 / Haste was
wrong and is withdrawn; StarHaven now drains at 1.5 for a worn "of
Recovery" and keeps the Haste spell on the same figure as its own
extension.

## The elapsed unit, closed

**A point of `Rec` is one sixtieth of a real second.** Three measurements
close it.

*The tick's caller was misread, and that too is retracted.* `0x482bb0` is
not the per-frame drain: its only caller is `0x427ea9`, which sits in the
spell-queue processor's jump table at `0x429c74` as case 67 — spell id
68, **First Aid**, whose block plays sound 7010, heals 5/7/10 by mastery
and then calls it. The real per-frame drain is the *same code inlined*, at
`0x4885d7`..`0x488693`, inside the time-advance routine at **`0x4880a0`**.
`observed`

*That routine names the unit.* It adds the dword at `0x4d519c` — the
frame's elapsed — to the 64-bit world clock at `0x908d08`, then makes a
calendar of the clock by multiplying it by the float at `0x4b9374`,
**0.234375 = 30/128**, and dividing the result by 60, 60, 24 and 7 for
minutes, hours, days and weeks. A second of *real* time is **128 units**:
the sound code at `0x488d79` turns a table of plain seconds
(0.0, 0.11, 0.22 … at `0x4be38c`) into the same units by multiplying by
128.0, and an animation timer at `0x48f5fd` reloads with `0x80`. So one
unit is 1/128 of a real second, the world clock runs at **thirty times
real time**, and the drain subtracts exactly the units the clock gained
— halved first when bit 2 of the byte at `0x908dec` is set. `observed`

*The setter's constant then does the arithmetic.* A queued amount is
multiplied by **32/15** (the double at `0x4b9318`) before it is stored, so
a point of `Rec` costs 32/15 units and 128 units pass a second:
`128 ÷ 32/15` = **60 points a second**, or half a *world* second a point.
`observed` StarHaven spends the `Rec` column at that rate.

## Where a strike's own recovery comes from

**`0x481a80` is the attack-recovery routine**, and it answers the amount the
queued message carries. It takes one flag — whether the blow is a shot —
and builds a `Rec` number out of a **fourteen-word table at `0x4c2750`**:

| 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 100 | 100 | 90 | 60 | 100 | 80 | 100 | 80 | 30 | 10 | 10 | 20 | 30 | 0 |

The routine loads entry **0** first and keeps it when nothing is in hand, so
**a bare fist recovers at 100** — 1⅔ seconds at the traced rate. Otherwise
it indexes by the equipped item's **skill-group byte**, read from the item
row's `+0x15` in a 40-byte-per-item table at `0x560c14`, at **skill id plus
one**: Staff 100, Sword 90, Dagger 60, Axe 100, Spear 80, Bow 100, Mace 80,
Blaster 30, Shield 10, Leather 10, Chain 20, Plate 30. `observed`

What it walks, in order:

- a shot takes the **missile slot at `+0x1430`**; a blow takes the **weapon
  slot at `+0x142c`**;
- the **other hand at `+0x1428`** then overrides the number **only when its
  own is larger** — the slower hand sets the pace;
- **worn armour at `+0x1434`** adds its own entry on top, and a **shield**
  in the off hand (equip-type byte `+0x14` of the item row equal to 4) adds
  its entry too;
- each of those two is **halved when the wearer's packed skill byte at
  `+0x5f + id` has bit `0x40`, and dropped to zero on bit `0x80`** — which
  is exactly `SKILLDES.TXT`'s "Recovery penalty reduced" and "eliminated"
  lines, now with numbers. Every piece is skipped when its flag byte at
  `+0x13c` marks it broken. `observed`

What the routine then takes back off that total, read to its return at
`0x481e97`:

- **the Speed bonus**, and the ladder that produces it turned out to be the
  game's whole attribute curve — see below;
- **the level of a Sword, Axe or Bow held at expert or better.** The routine
  tests the item's skill byte against 2, 4 and 6 and, when either rank bit is
  set in the packed skill byte, subtracts the skill's level outright.
  `SkillDes.txt` says the same in words for exactly those three: "expert
  swordsmen gain a quicker attack", "expert axe fighters gain a little more
  speed on their attacks", "expert archers gain a speed increase";
- **a flat 20** for a worn item whose special enchantment is **59, "of
  Swiftness"**, or which is one of two artifacts named by id — **404
  Merlin** and **405 Percival**;
- one further term, 25, gated on the dword at `+0x128c`. `unknown` what that
  field is.

The sum is floored at zero and returned. `observed` StarHaven now spends
that on a strike, less the `+0x128c` term and a day-of-week term the routine
also folds in, both of which are left out rather than guessed at.

## What a class is worth

`0x481ea0` builds max hit points and `0x482090` max spell points, and both
open the same way: `al = byte [esi + 0x12]`, **the class id**. Each then
indexes two tables with it — one **by class**, holding what a level is
worth, and one **by class family**, the id divided by three, holding the
base.

| | Knight | Cavalier | Champion | Cleric | Priest | High Priest | Sorcerer | Wizard | Archmage |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| hit points a level | 4 | 6 | 8 | 2 | 3 | 4 | 2 | 3 | 4 |
| spell points a level | 0 | 0 | 0 | 3 | 4 | 5 | 3 | 4 | 5 |

| | Paladin | Crusader | Hero | Archer | Battle Mage | Warrior Mage | Druid | Greater Druid | Arch Druid |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| hit points a level | 3 | 4 | 5 | 3 | 4 | 5 | 2 | 3 | 4 |
| spell points a level | 1 | 2 | 3 | 1 | 2 | 3 | 3 | 4 | 5 |

The bases, by family: hit points **30** for the Knight line, **25** for
Paladins and Archers, **20** for Clerics, Sorcerers and Druids; spell points
**0**, **5**, **5**, **10**, **10**, **10** in the same order. `observed` at
`0x4c2640`/`0x4c2630` and `0x4c2654`/`0x4c2638`.

`Class.txt`'s prose is the check on the reading: "Cavaliers enjoy the
benefit of an extra two hit points per level", and the table gives Knight 4
against Cavalier 6.

The shape is `base + (the effective level + the attribute's bonus) × the
class's number`, floored at one — hit points asking the getter for stat 3
(Endurance), spell points for stat 2 (Personality).

**Correction: stat 14 is not the level.** That was written here last and it
is wrong. Reading the bonused getter's case for id 14 (`0x48332e`) settles
it: the id has no stored base at all, and its case walks the sixteen
equipment anchors adding **five for a worn item whose special is 25, "of
Power"**, and nothing else. So stat 14 is a gear bonus.

**The level is the word at `+0x32`.** What the two routines multiply the
class number by is `stat 14 + word[+0x32] + word[+0x34]`, and the same three
are summed at `0x487daa` and `0x484e1a`. So `+0x32` is the stored level,
`+0x34` a modifier laid on top of it, and stat 14 the gear's contribution —
which makes an item "of Power" worth five levels of hit points and spell
points. `observed`

Two further terms each routine folds in — a stat 7 for hit points, a stat 8
for spell points, and a byte at `+0x1578` — are read but not named, and
StarHaven leaves them out rather than guessing.

**What a level costs was hunted twice and not found — but the hunt changed
the shape of the answer.** The second attempt went at the field rather than
the strings: every instruction in the executable that writes the level word
at `+0x32`. There are **two**, and both are generic — the "set a character
field" and "add to a character field" that map scripts use, each writing
whichever of `+0x30`, `+0x32`, `+0x34`, `+0x36` an argument names, and each
capping at **255**. `observed`

So **nothing raises a level by itself.** No routine watches experience and
promotes; the only door is a script, which is to say the training hall.
StarHaven follows that: experience is banked, and a level is bought at a hall
or not at all.

A third attempt went one step further and named the door. Both routines
dispatch on a **script variable id**, through a 225-entry selector at
`0x4411c0`, and ids **7, 8, 9, 10 and 11** land on `+0x30`, `+0x32`,
`+0x34`, `+0x36` and one more. So the level is **script variable 8**, beside
experience at 13, and a level is gained by a script adding to it. `observed`

The threshold itself is still unfound, and three approaches have now missed
it — the strings are fetched by a computed index, no ladder of experience
values sits in the data, and the field is written only by a generic script
verb that carries no condition. It is recorded as absent from what can be
read, and the triangular staircase StarHaven offers at, 1000·L·(L−1)/2,
remains this engine's own. `inferred`

## The attribute curve, found inside the recovery routine

The ladder the Speed term walks is not a recovery table at all: it is **the
bonus every attribute reads through**. `0x481dd0` compares the value against
a descending run of 29 words at **`0x4c2860`** — 500, 400, 350, 300, 275,
250, 225, 200, 175, 150, 125, 100, 75, 50, 40, 35, 30, 25, 21, 19, 17, 15,
13, 11, 9, 7, 5, 3, 0 — and takes the signed byte at the matching step of
**`0x4c289c`** — 30, 25, 20, 19 … 1, 0, −1 … −6.

So **13 is the pivot at no bonus**, 15 gives one, 17 two, 19 three, 21 four,
25 five, 100 eleven, and 500 or more the maximum 30; below the pivot, 11
costs one, 9 two, down to a floor of −6. `observed` StarHaven's own guessed
curve — one point either side of 15, widening by fives — is replaced by
this, which changes hit points, spell points, armour class, melee damage and
now recovery all at once.

## The monsters' counter, found at last

Five hunts failed and the negative stood; it is now retired. **The
monster's recovery counter is the dword at `+0x6c`** of the 548-byte
runtime record at `0x56f478`, and it is filled and drained exactly the way
the party's is.

- **The setter is shared.** The queued-message handler that fills a
  character's `+0x137c` has a second branch beside it: where **kind 4 with
  index 0..3** means a party member, **kind 3 with index 0..499** means an
  actor, and the same parameter is multiplied by the same **32/15** before
  being stored at `0x56f4e4 + 548 × index` — which is `+0x6c` of the
  record. `observed` at `0x405cdd`..`0x405cf5`.
- **The drain is the same shape.** At `0x401b5d`: subtract an elapsed while
  the counter is above zero, then clamp at zero. The accumulator at `+0xa8`
  beside it takes the same elapsed added rather than subtracted. `observed`

**Why five hunts missed it.** They enumerated every write into a monster's
record *from the AI cluster*, and the only write to `+0x6c` does not come
from there — it comes from the message handler, which addresses the field
absolutely (`mov [edx*4 + 0x56f4e4], eax`) rather than through a record
pointer, so no scan keyed on a base register could see it. The field also
sits below the `+0xe0`..`+0xfc` position-and-velocity band those hunts had
already catalogued, which is where the searching had concentrated.

**And the last inference is now a measurement.** The elapsed the countdown
subtracts is the global at **`0x4d51c4`**, not the `0x4d519c` that advances
the world clock, and the two were only assumed to agree. They do:

- Neither is written absolutely because both are **fields of an object**.
  Every absolute reference in the neighbourhood falls on two runs of the
  same shape, `0x4d5180` and `0x4d51a8` — **`0x28` apart**, each touched at
  `+0`, `+4`, `+0xc`, `+0x1c` and `+0x24`, and each loaded into `ecx` before
  a call. `observed`
- The two share **three methods** — `0x420db0`, `0x420df0` and `0x420ec0` —
  out of a cluster at `0x420d70`..`0x420f50`. Two instances, one class.
  `observed`
- `0x420ec0` is the one that fills the field. It samples
  **`GetTickCount()`**, shifts it left seven and divides by 1000 — the
  reciprocal multiply by `0x10624dd3` with a `shr 6` — guards a wrap, and
  stores `now − last` at **`+0x1c`**, spinning until the difference is
  positive. `observed`

So the unit is settled twice over: `ms × 128 ÷ 1000` is **exactly 128 units
to the real second**, which the sound table's `× 128.0` had only implied,
and the two globals are the same field of the same class filled by the same
method. **A monster's `Rec` is spent at the same sixty points a real second
the party's is**, and this engine's reading of it is now `observed` rather
than assumed.

The class itself, so far as it is used here: `+0x04` a running flag, `+0x0c`
the last sample, `+0x10` a saved sample, `+0x1c` the elapsed, forty bytes
long.

## Conditions: how they begin, and what was not found

Sought: what poison and disease do between being caught and being cured,
since the engine invents both the damage and the interval. Two things came
out and the third did not.

**The attribute modifiers are per-tick scratch.** The time-advance routine
calls `0x484520` on every character each tick, and that routine zeroes the
odd words of the attribute pairs — `+0x16`, `+0x1a`, `+0x1e`, `+0x22`,
`+0x26`, `+0x2a`, `+0x2e` — along with the resistance dwords at `+0x38` to
`+0x58` and three byte runs from `+0x1570`. So the stored value in each pair
is durable and the modifier beside it is rebuilt from gear and spells every
frame, which is the other half of the composition settled earlier.
`observed`

**Conditions begin from fatigue.** In the same per-character pass, the
routine rolls `rand() % 100` against the byte at **`0x908d6c`** times five,
and again against it times ten, and on success stamps a condition's 64-bit
timestamp with the current clock. That byte is **zeroed by the rest
routine** (`0x42c874`), so it is a fatigue counter that grows while the party
stays awake and makes conditions steadily likelier — a mechanic the engine
has no equivalent of. `observed`

**What a condition then does was not found.** The conditions are
*timestamps*, not counters, so nothing ticks them down and any damage must
be applied wherever the timestamp is read. The routines that read them —
`0x47fb60` checks `+0x13f0`/`+0x1400` before healing, and the attack-bonus
walk tests the run from `+0x1380` — only ever ask whether a condition is
set, never how long it has been. Where the harm is done is `unknown`, and
StarHaven's invented drain stays marked as its own.

## What a condition costs — found, and a retraction with it

Two batches looked for this on the clock and found nothing, because it is
not charged over time at all. **A condition is a percentage multiplier on
the character's numbers**, applied wherever they are computed.

The recovery routine, the hit-point routine and the attack-bonus getter each
walk a fourteen-entry list at **`0x4c276c`**, reading the **64-bit value at
`+0x1380 + 8 × id`** for each id in turn and taking the first that is not
zero — then index a per-condition byte table by that id and scale. `observed`

Read as condition ids, that list is **exactly "worst first"**: Eradicated,
Stoned, Dead, Unconscious, Asleep, Paralyzed, 11, 10, 9, 8, Diseased,
Poisoned, Insane, Drunk — which is what `stats.txt` says the sheet's
Condition row shows. And `0x4c27fc`, indexed by condition id, gives
**Poisoned 75, Diseased 60, Afraid 50, Drunk 10**, with the four unnamed ids
at 50, 30, 25 and 10 and everything else at 100.

### The retraction

That list and that table were written up here as a **skill** priority order
and a table of **per-skill percentages**, with `Identify Item` at 120,
`Merchant` at 20 and `Repair Item` at 120, and three of this engine's counter
services were wired on them. All of that is withdrawn.

The instruction settles it and should have settled it the first time: the
walk reads **eight bytes and ORs the halves to test for non-zero**. A count
of skill points is a small integer; only a timestamp is read that way. The
run at `+0x1380` is independently confirmed as the condition run three other
ways — the cure spells push ids 1, 6 and 7 into it, the heal refuses on 14
and 16, the AI treats 2, 12, 13, 14, 15 and 16 as absent.

What was built on the misreading is undone: haggling and the two counter
services are back to one percent a point, marked as this engine's own, and
the attack bonus keeps only what survives — a raw attribute, scaled by the
worst condition. What it adds between the two is `unknown` again.

The fit that made the wrong reading persuasive is worth recording as a
caution: ids 9, 10 and 11 carry 30, 25 and 10, which read beautifully as
Leather, Chain and Plate descending. It was a coincidence.

## An audit of claims named from a fit

Four retractions, the last persuasive because the numbers told a good story
that turned out to be chance. The failure mode is specific — asserting a
*name* from how values look rather than from what an instruction does — so
the load-bearing combat claims were re-derived from their instructions.
Three results.

**The to-hit roll: exact.** `0x421cb0` reads the target's armour at `+0x60`,
calls one of the two attack-bonus getters, and rolls
`rand() % (armour + 2 × attack + 30)`. The bar is `armour + 15` by default
and `2 × armour + 30` for kind 3. Every part of the earlier reading survives
unchanged. `observed`

**Immunity at 200: exact.** `0x421dc0` compares the byte against **200** and
returns zero damage at or above it, for every element. `observed`

**The resistance byte order: wrong, and corrected.** This document said the
six bytes at `+0x50`..`+0x55` hold the resistances "in the design table's own
order", which was an assumption dressed as a measurement. The routine's jump
table settles it, and the mapping is **rotated by two**:

| element id | 0 | 1 | 2 | 3 | 4 | 5 |
| --- | --- | --- | --- | --- | --- | --- |
| byte | `+0x54` | `+0x55` | `+0x50` | `+0x51` | `+0x52` | `+0x53` |

`observed` at `0x421dd0`..`0x421e1a`. Nothing in StarHaven depended on it —
the engine reads resistances from `MONSTERS.TXT`'s columns by name, not from
this record — so the correction is to the record's description alone.

**The rule this leaves.** A claim may be tagged `observed` only for what an
instruction does. Where a *name* follows from how the numbers read, the name
is `inferred` however good the fit, and the two are stated separately.

## The attack bonus's middle term: age

The retraction re-opened what the attack-bonus getter adds between the raw
attribute and the condition scaling. Reading `0x47e270` from the top answers
it, and answers two older questions with it.

The getter, in order:

1. Turns the world clock into years, adds the word at **`+0x36`**, subtracts
   the party word at **`+0x141c`**, and adds **1165**.
2. Bands that against `{50, 100, 150, 65535}` at `0x4c2834`, taking a
   percentage from the four-byte row at `0x4c2854`.
3. Walks the condition priority list and takes the worst set.
4. Asks the two getters for stat **4**.
5. Multiplies the word at **`+0x28`** — the stored **Speed** — by the band's
   percentage, and adds it.

**That banded term is age.** It is the same arithmetic the recovery routine
and the max-hit-point routine perform, and this project recorded it twice as
an unexplained "day-of-week" contribution taken off the clock. It is not: a
count of years plus **1165**, banded at fifty, a hundred and a hundred and
fifty, with the percentage falling as it climbs — and `stats.txt` has an Age
row that says exactly what such a number would be for.

The three curves differ, and sensibly:

| band | under 50 | 50–99 | 100–149 | 150+ |
| --- | --- | --- | --- | --- |
| spell points (`0x4c284c`) | 100 | **150** | 100 | 10 |
| hit points (`0x4c2850`) | 100 | **75** | 40 | 10 |
| attack bonus (`0x4c2854`) | 100 | 100 | 40 | 10 |
| recovery (`0x4c2858`) | 100 | 100 | 40 | 10 |

**Spell points are the odd one**: they *rise* by half through the fifties,
eighties and nineties, hold at full through the hundreds, and only fail past
a hundred and fifty with the rest. The old grow wiser before they fail.

Hit points feel age first; the other two hold until a hundred; past a
hundred and fifty a character keeps a tenth of all three. `observed` for the
bands and the curves; `inferred` that the banded number is age, from the
arithmetic and that row rather than from a routine that names it.

**And how it composes, settled.** Read to the getter's return at
`0x47e403`, the whole of it is:

> ladder( stat 4's spell bonus + stat 4's gear bonus + stored `+0x28` ×
> age% × condition% + stored `+0x2a` ) + stat 15's award, spell and gear
> contributions + the byte at `+0x1570`

`observed` at `0x47e354`..`0x47e3fd`, where `ladder` is the sheet's own
attribute curve.

**It really is lopsided.** The getter asks the bonus getters for stat **4**
and reads the *stored* pair at `+0x28`/`+0x2a` — which the two anchors fixing
the stored run make stat **5**'s, since max hit points asks id 3 and reads
`+0x20` and max spell points asks id 2 and reads `+0x1c`. So one attribute's
bonuses are summed with another's stored value. That is what the
instructions do; that the two are Accuracy and Speed is `inferred` from
`stats.txt`'s order rather than from anything that names them.

## Tallying the record — and why the method does not transfer

The party record yielded to a tally of absolute references because it lives
at a fixed address and nothing else does. The character record does not
yield the same way, and the reason is worth recording.

**Absolute references reach only character zero.** Counting them gives 48
offsets and 214 references — enough to have found the buff array and Haste's
slot in earlier sittings, but sparse, because almost every routine takes a
character *pointer* and reaches its fields by displacement.

**Displacements are contaminated.** Tallying `[reg + disp]` across the image
gives 319 offsets in the record's range, but a displacement belongs to no
particular structure: `+0x0a00`, `+0x04fc` and `+0x07d0` all score highly and
all fall inside ranges that other structures use too. The tally cannot tell
which. So the number that looks like a ranking is not one, and it is recorded
here as a caution rather than a result.

What the absolute references did name:

- **`+0x1618` is cleared every tick.** The time-advance routine walks the
  four records from `0x90a54c` in strides of `0x161c` writing zero, bounded
  at `0x90fdbc`. It is the record's last dword and it is frame scratch.
  `observed`
- **`+0x60` is the base of a byte array inside the record.** `0x43012c`
  writes `byte [reg + index*4 + 0x908f94]`, and `0x908f94` is exactly
  `+0x60`. The array has the two hundred bytes between it and the item
  records at `+0x128` to live in. What it holds is `unknown`. `observed`
  for the base and the indexed write.

## The `+0x1570` run: read by six getters, written by nothing

The attack bonus ends by adding the signed byte at `+0x1570`, the per-tick
clear routine zeroes a run starting there, and the recovery routine reads
`+0x1578`. Following the field gives an answer that is neither of the two
expected.

**The eight bytes `+0x1570`..`+0x1577` are a per-stat scratch, and they are
always zero.** Six sites read them, all in the stat-getter cluster at
`0x47exxx` and all as `movsx` of a signed byte: `+0x1570` twice (the attack
bonus and one other), `+0x1572` three times, `+0x1574` once, `+0x1576`
twice. Every one adds the byte to its total.

**Nothing writes them.** Across the whole disassembly the only instruction
that touches those eight offsets other than the six reads is the per-tick
clear at `0x484520`, which zeroes them. So whatever they were for, in the
shipped executable they contribute nothing to any stat, every tick, forever.
`observed`, with one caveat stated rather than glossed: a write through a
computed pointer or a block store would not appear in a scan by displacement.

**The four bytes above them do have writers.** `+0x1578` and `+0x1579` are
zeroed at `0x44154c` immediately after a character's hit points are set to
their maximum, and `+0x157a`/`+0x157b` in the same routine family. So those
four are tied to hit and spell points rather than to the stat getters, and
are not part of the vestigial run.

So the attack bonus's last term is **structurally present and always zero** —
a vestige, not a UI artefact and not a live contribution.

## `+0x60`: the skills

Two hundred bytes sat between the resistance bytes and the item records with
nothing said about them. Thirty-one are the skills.

| what | where | evidence |
| --- | --- | --- |
| the array | `+0x60`, one byte a slot | `observed` |
| how many slots | 31 (`0`..`0x1e`) | `observed` at 0x484899, `cmp eax, 0x1e; jle` |
| the points | low six bits, `& 0x3f` | `observed` at 0x42d0e4 |
| the mastery | the top two bits | `inferred` — the mask leaves exactly two, and four rungs is what the game has |
| not learned | the whole byte is zero | `observed` at 0x484173 and 0x484899 |

**Training**, out of `0x42d0d8`, is three rules and no more:

- raising a skill from `n` costs **`n + 1`**, and the routine refuses when the
  pool holds less (`cmp esi, edx; jb`);
- the points stop at **60** (`cmp dl, 0x3c; jae` refuses at or above it);
- the pool is a **dword at `+0x1410`**, four bytes below the hit points, and
  `0x42d10e` writes back what the cost left of it.

The raise is `inc al` on the packed byte, which is safe because 60 is well
under the mask — the mastery bits ride along untouched. `observed`

**What the array is for.** `0x484890` is the check that lets a made party
start: it walks all four characters at stride `0x161c` from `0x908f34` to
`0x90e804`, counts the non-zero slots in each, and returns false unless every
one of them has **at least four**. `observed` — and it is why the array's
length is knowable at all, since that walk states its own bound.

`0x484150` is the rank getter. It takes a small argument (0..12, through a
byte selector at `0x484258` into a jump table at `0x48424c`), reads the
character's byte at `+0x60 + index`, and — if it is non-zero — divides the
class at `+0x12` by three to get the class family. `observed` for the reads;
`inferred` that the family is what caps the rung.

Script opcodes reach the array too: `0x43c9a0` and `0x43c9bf` read a slot
whose index is an instruction's argument byte at `+5`, and `0x430216` writes
**1** — a skill granted outright rather than bought.

## The thirty-one slots, and who may hold them

`SKILLDES.TXT` ships **exactly thirty-one rows**, and the array is exactly
thirty-one bytes, so slot `n` is row `n`:

| 0..7 | 8..15 | 16..23 | 24..30 |
| --- | --- | --- | --- |
| Staff, Sword, Dagger, Axe, Spear, Bow, Mace, Blaster | Shield, Leather, Chain, Plate, Fire, Air, Water, Earth | Spirit, Mind, Body, Light, Dark, Identify, Merchant, Repair | Bodybuilding, Meditation, Perception, Diplomacy, Thievery, Disarm Traps, Learning |

**A correction.** An earlier twenty-four-name list put Light and Dark at 16
and 17, ahead of the three self schools. That was wrong. What caught it is
the class table below: on the old order the Cleric begins with Spirit rather
than Body and may pick Light at character creation, and no Sorcerer may ever
learn Dark. On `SKILLDES.TXT`'s order every one of the six rows comes out
exactly as the game plays. The first twelve names did not move, so nothing
that indexed the weapon groups changes.

## `0x4c2694`: which class may hold which skill

A **six by thirty-one byte table**, stride thirty-one, indexed by the class
family and the slot. The family is the class over three (`0x484181`) — which
is why eighteen classes need six rows, and why the hit-point bases have six
entries too. It ends where the weapon-recovery table begins at `0x4c2750`,
which is what fixes its length. `observed`

```text
Knight    3 1 2 2 2 2 3 3 2 1 2 3 0 0 0 0 0 0 0 0 0 3 3 3 2 0 2 3 0 2 3
Cleric    2 0 0 0 0 3 1 3 2 2 3 0 0 0 0 0 2 2 1 3 3 2 3 2 3 2 3 2 0 3 3
Sorcerer  2 0 1 0 0 3 0 3 0 2 0 0 1 2 2 2 0 0 0 3 3 2 3 2 3 2 3 2 0 3 3
Paladin   3 1 2 3 2 3 2 3 2 2 2 3 0 0 0 0 1 3 3 0 0 3 3 3 3 3 2 2 0 2 3
Archer    3 2 2 2 3 1 3 3 0 2 3 0 2 1 3 3 0 0 0 0 0 2 3 3 3 3 2 2 0 2 3
Druid     1 0 3 0 0 3 2 3 3 2 0 0 3 3 2 1 2 3 2 0 0 2 3 2 3 2 3 3 0 3 2
```

The trainer's list at `0x49c864` and `0x49ca81` tests the byte for
**non-zero** and nothing finer, then skips any skill the character already
holds. So **zero means the class may never learn it**, and that much is
`observed` — a Knight has no magic and no Meditation, a Sorcerer no plate and
no shield, a Druid no sword, no axe and no mail.

What separates 1, 2 and 3 is `inferred`, from three readings agreeing.
`0x484150`'s first body walks the slots whose byte is **1** and hands back the
first or the second of them; every family has exactly two, and in all six
cases they are the pair that class is known to begin play with — Knight sword
and leather, Cleric mace and body, Sorcerer dagger and fire, Paladin sword and
spirit, Archer bow and air, Druid staff and earth. Its other two bodies walk
the slots whose byte is **2**, which is the list a new character chooses from.
So **1 granted, 2 offered at creation, 3 learnable only from a trainer, 0
never**.

One column is zero in all six rows: **Thievery**. No class in the game may
hold it.

`0x484150` returns **31** when the walk runs off the end, which is why
`0x4301fa` compares its result against `0x1f` before using it. `observed`

## The two bits above the points

Every skill is one byte: the points in the low six bits, **the rank in the top
two**. Three traced sites agree and none of them looks at the number:

- the training routine at `0x42d0e4` masks the points with `0x3f` and raises
  them with `inc al`, which leaves `0xc0` standing — a number and a rank
  sharing a byte;
- the armour-recovery routine tests **`0x40` at `0x481c1e`** and halves the
  penalty, then **`0x80` at `0x481c84`** and drops it entirely, which is
  `SKILLDES.TXT`'s "Recovery penalty reduced" and "eliminated" lines with the
  bits behind them;
- the property setter's skill case at `0x441dad` masks the incoming byte with
  `0xc0` before writing, preserving whatever rank was there.

`observed`, all three.

## Who sets the bits, and what a rank costs

The teacher is at `0x4969e4`. It takes the character from the pointer array at
`0x944c64`, the skill from `0x9ddd88`, and does this:

```asm
0x004969f3  mov  cl, byte [edx + eax + 0x60]
0x004969fb  and  cl, 0x3f              ; the old rank goes first
0x004969fe  mov  byte [eax], cl
0x00496a12  mov  ecx, dword [0x9ddd98] ; 0 expert, 1 master
0x00496a18  neg  ecx
0x00496a1a  sbb  cl, cl                ; 0x00 or 0xff
0x00496a20  and  cl, 0x40              ; 0x00 or 0x40
0x00496a25  add  cl, 0x40              ; 0x40 or 0x80
0x00496a28  or   dl, cl
0x00496a2a  mov  byte [eax], dl
```

So **`0xc0` never occurs**: the bits are cleared and exactly one is set, and
the two bits hold one of three states rather than four. That closes what stood
as `unknown`. `observed`

The price is built by the same trick three instructions later, at `0x496cdb`,
from the same flag:

```asm
neg eax ; sbb eax, eax ; and eax, 0xbb8 ; add eax, 0x7d0
```

— **2000 gold for expert, 5000 for master**. `observed`

The same packing shows up in the game's own text: `MONSTERS.TXT`'s parser at
`0x448320` masks a cell's number with `0x3f` and then compares the rest of the
cell against `"E"` and `"M"`, ORing `0x40` and `0x80`. A monster's spell skill
is written the same way a character's is stored.

**Retracted.** This engine placed expert at four points and master at seven,
invented because no table states where the ranks begin. No table states it
because the rank is not in the points at all — a teacher sets the bits. A
character with thirty points and no bit set is a novice; one with two points
and `0x80` is a master. Every reader of a skill in this engine now takes the
byte and splits it, rather than guessing a rank from a count.

## Whose resistances are whose

Two claims stood in this document and both could not be right: six bytes at
`+0x50`..`+0x55`, and ten words at `+0x1254`..`+0x1267` in base-and-modifier
pairs. Reading who calls what settles it.

`0x421dc0` — the routine that turns an element id into a resistance — reads
the six bytes, and **its argument is an actor**:

- `0x401917` reaches the record as `lea ecx, [esi - 0xa0]`, from the AI's own
  state pointer;
- `0x430f36` and `0x431915` test `dword [esi + 0x114]` and `[esi + 0x118]` —
  the actor's 64-bit pair — in the same breath as the call, on the same
  register they pass;
- and `0x4318d4` in that same routine holds a *character* in a different
  register entirely, reading its condition array at `+0x1388`.

So the caster is one record and the thing being resisted is another. **The six
bytes at `+0x50`, with 200 meaning immune, are the monster's**, and the
element order rotated by two belongs to that jump table alone. `observed`

**A character's five resistances are the words at `+0x1254`..`+0x1267`**, base
and modifier in pairs, sitting immediately below the buff array at `+0x1268` —
the property setter's ten case bodies write them (ids 46..50 and 51..55) and
the property getter's five read them. Nothing in a damage path reads them,
because a character's resistance is applied by the same `0x421dc0` only when
the character *is* the target, and that path takes the actor form. `observed`
for the offsets and the pairs.

### What the dead pair would have done

`0x430f36` is worth its own line, because it names the timers the last batch
buried. Immediately before the resistance call:

```asm
0x00430f36  cmp dword [esi + 0x118], edi
0x00430f40  cmp dword [esi + 0x114], edi
0x00430f48  xor eax, eax        ; the damage becomes nothing
```

While the pair is greater than zero the blow does no damage at all — so it was
a **damage-shield window on the actor**. Since nothing ever writes it and every
actor on disk carries zero, the shield never comes up. `observed`

## The experience field, and what Learning is worth

`+0x1420` is the experience, and it is **64-bit**: the property adder does a
`cdq`/`add`/`adc` pair across `+0x1420` and `+0x1424` and clamps the result at
`0xee6b2800` — four billion — while the sheet at `0x41498a` tests the pair
against **9,999,999** to choose which of two fonts prints it. `observed`

A new character is not created with none of it. `0x483e2b` sets the level word
at `+0x32` to 1 and, in the same breath, writes `rand() % 100 + 251` to
`+0x1420` — so every character starts somewhere between **251 and 350**.
`observed`

**Learning has no implementation in the executable.** That is not a guess
about how hard the search was: `+0x1420` is touched by **eighteen instructions
in the whole image**, of which five write —

| where | what |
| --- | --- |
| `0x440c12` | the script property setter, id 13 |
| `0x4416e3`, `0x441707` | the script property adder, id 13, clamped at four billion |
| `0x4425bb` | the script property taker, as a 64-bit subtract |
| `0x483e41` | character creation, `rand() % 100 + 251` |

— and every read of the skill array in the image is likewise accounted for:
the training routine, the trainer's list, the creation chooser, the teacher,
and two display paths. **The two sets never meet.** No instruction multiplies
experience by anything, and nothing reads slot 30.

That leaves an honest open end worth stating plainly: **the award for a kill
is not among those eighteen references either.** Whatever grants experience
for a dead monster does not write `+0x1420` directly, and it has not been
found. `unknown`

So what a point of Learning is worth here is this engine's own — one percent
more experience a point, doubled and tripled by the rung the way every other
silent row is, applied per character because that is where the skill lives.
`inferred`

## `+0x30` and `+0x11`, the last two unnamed fields of the dense part

**`+0x30` is an armour-class term.** The getter at `0x482860` builds the
armour class from four things and nothing else:

```
0x00482866  call 0x483800          ; the gear getter, stat id 9
0x00482875  call 0x482e80          ; the other getter, stat id 9
0x0048287a  movsx ecx, word [esi + 0x30]
0x00482880  movsx eax, byte [ebx + 0x4c289c]   ; the ladder, Speed's row
0x0048288d  cmp edi, 1
0x00482890  setl al ; dec eax ; and eax, edi   ; a negative becomes nothing
```

So: both stat getters for **stat 9**, plus the stored word at `+0x30`, plus
the Speed row of the attribute ladder, **floored at zero**. `observed` — and
it is the only place `+0x30` is read, which makes property id 8 the one way a
script can change a character's armour class directly.

**`+0x11` is a boolean beside the class.** Three sites read it:

- `0x42b073` does `neg al; sbb eax, eax; add eax, 0x7db` — **2011 when the
  byte is zero and 2010 when it is not**, handed to the sound object at
  `0x9cf598`;
- `0x421a68` chooses between `byte [eax + 0x12]` and `byte [eax + 0x11]` by a
  sign test and compares the result the same way, so the two are alternative
  identifiers for one lookup;
- `0x43dc8c` reads it beside `+0x10` and packs the two together.

`observed` that it is a boolean, because a two-way select on zero is all any
reader does with it. `inferred` that the boolean is the character's **sex**:
two adjacent voice ids chosen per character is what a male and a female line
look like, and the field sits immediately before the class. Property id 1
writes it, one below the class's id 2.

## The `+0x1570` run: not a vestige, a row of stored terms

This document filed the run as vestigial — six getters read it and nothing
wrote it — and then the property setter turned up clearing four of its bytes
on a full restore. **The negative is withdrawn.** The run is the same thing
`+0x30` is, one row down.

Every getter that reads it has the identical shape: ask both stat getters for
one stat id, add the stored byte, add the attribute ladder's byte, sum.

```
0x0047e3da  call 0x483800                     ; the gear getter
0x0047e3e5  call 0x482e80                     ; the other getter
0x0047e3ea  movsx ecx, byte [esi + 0x1570]    ; the stored term
0x0047e3f3  movsx eax, byte [edi + 0x4c289c]  ; the ladder
0x0047e3fd  add ebx, ecx
```

Two of them are certain from the class tables they read:

| offset | which getter | how it is known |
| --- | --- | --- |
| `+0x1578` | **maximum hit points** | `0x482060` reads the class hit-point base at `0x4c2630` and floors the answer at 1 |
| `+0x157a` | **maximum spell points** | `0x4821c8` reads the class spell-point base at `0x4c2638` |
| `+0x1570`, `+0x1572`, `+0x1574`, `+0x1576` | four more derived stats | each getter pushes its own stat id — 15, 16, 19 and 20 are among them |

`observed` throughout. And it explains the clears the last pass found without
being able to place them: setting hit points to their maximum clears
`+0x1578`/`+0x1579`, setting spell points to theirs clears `+0x157a`/`+0x157b`
— a temporary addition to a ceiling goes when the pool behind it is refilled.

`+0x157c` and `+0x157e` are not part of the run. They are words, written with
`0x21` and `1` at `0x47fe46` and `0x451383`, and read forty and eighteen times.
`unknown`.

## `+0x157c` is the quick spell

The word just past the row of stored terms is read forty times and was the
most-referenced unnamed field left anywhere on the record. `0x47fde9` says
what it is in five instructions:

```
0x0047fde9  mov  ax, word [esi + 0x157c]
0x0047fdf0  add  eax, 0xfffffffe        ; -2
0x0047fdf3  cmp  eax, 0x61              ; 97
0x0047fdf6  ja   0x47fe07
0x0047fdfa  mov  dl, byte [eax + 0x47fe78]
0x0047fe00  jmp  dword [edx*4 + 0x47fe70]
```

A value bounded to **2..99** dispatching a 98-way switch is a **spell id**, and
`stats.txt` has a row for it: **Quick Spell**. It is written with **33** at
`0x47fe46` and tested against **21** — the Fly spell — at `0x41a760`, which is
a movement path asking whether the bound spell is the one that flies.
`observed`

It is not the readied spell at `+0x152f`. That is what the cast key throws
now; this is what stays bound. The two words after it, `+0x157e` and
`+0x1580`, are zeroed on the same path and are scratch for the cast.

So the gamble's stated risk — an interface index, real but dull — half landed:
it *is* an interface binding, but one the game's own stat table names.

## Filed: no instruction awards experience for a kill

Four approaches have now been run to the end. The counts are here so the next
pass starts somewhere else.

**From the character's field.** `+0x1420`, with its high half at `+0x1424`, is
touched by **eighteen instructions in the image**. Five write, and all five are
accounted for: the script property setter (`0x440c12`), the adder (`0x4416e3`,
`0x441707`), the taker as a 64-bit subtract (`0x4425bb`), and character
creation (`0x483e41`, `rand() % 100 + 251`). No `lea` anywhere takes the
field's address, so there is no write through a pointer either.

**From the monster's table.** The runtime rows are **72 bytes from
`0x56c188`**, indexed `9 × id` then `× 8`. Five columns are referenced by
absolute base — `+0x00`, `+0x09`, `+0x12`, `+0x38`, `+0x40` — and two more
through the pointer at `0x55dd94`, `+0x18` and `+0x36`. **None of the seven
feeds an accumulator.**

**From the death handler.** `0x403730` sets the death bit `0x20000`, sets the
death animation `+0x3e = 4`, and takes fifty off the reputation. It computes no
other number. The two callers of the property routines that pass a computed id
both read it out of a script instruction's argument byte at `+5`.

**From the turn-based side.** Party `+0x1c0` is `0x908e30`, and it has four
writers: the toggle at `0x42b142` and `0x42b15c`, and the map-load pair at
`0x45395a` and `0x453e32`. The routine that leaves turn-based combat,
`0x405810`, clears a flag, plays a sound and returns — no accumulator, no
distribution.

So: **no instruction in MM6.exe awards experience for a kill**, by any of the
four routes. Either it arrives entirely through the event system, or the
mechanism lies somewhere none of these reaches. `unknown` — and this engine's
own split of a fight's worth among whoever is standing stays marked as its
own.

## The stat id space, counted

Every `push` that immediately precedes a call to one of the three stat getters
— `0x483800`, `0x483930` and `0x482e80` — was collected. The answer is
**ids 0 through 21 and 23, and nothing else**; **id 22 is never asked for
anywhere in the image**. `observed`

| ids | what | how it is known |
| --- | --- | --- |
| 0..6 | the seven attributes, Might..Luck | the property setter writes `+0x14 + 4k` for the matching run |
| 7 | maximum hit points | its getter indexes the class base at `0x4c2630` |
| 8 | maximum spell points | its getter indexes `0x4c2638` |
| 9 | armour class | its getter adds the stored word at `+0x30` and the Speed ladder |
| 10, 11, 12, 13, **23** | the five resistances | one routine asks for all five in a single evenly spaced run at `0x47f6a3`..`0x47f7a8`, and five is how many columns `MONSTERS.TXT` carries |
| 14 | shown beside the attributes | `0x47d915` continues the same 32-byte stride that spaces ids 0..6 |
| 15, 16, 19, 20 | the derived combat figures that carry a stored term | `+0x1570`..`+0x1577` |
| 17, 18, 21 | asked for only from inside those getters | ingredients rather than figures |
| 22 | never asked | — |

**A correction.** `special_stats.hpp` had "ids 10..13 are the resistances".
There are five, and the fifth is **23** — it trails the other four rather than
continuing them, which is why the earlier reading stopped at 13.

The gamble's stated risk was that the gaps would be display-only rows with no
getter to find. Half of that landed: 14, 17, 18 and 21 have getters but no
name, and what they are stays `unknown`. What did not land is the fear that
there would be nothing to count — the id space itself is now closed, with its
one hole named.

## Withdrawn: the experience award was there all along

This document filed a four-count negative saying no instruction in MM6.exe
awards experience for a kill. **It is wrong, and it is withdrawn.** The award
is `0x421520`, and every one of the four searches missed it for the same
reason: it reaches the experience field through the **absolute address
`0x90a354`** — character zero's `+0x1420` — and steps by `0x161c`, rather than
through a `+ 0x1420]` displacement that a scan for the offset would catch. It
is the same computed-pointer blind spot that hid half the monster row's
columns.

What led to it was naming a monster column. The death path at `0x431a85` hands
`0x421520` the dword at the monster row's `+0x38`, and this document called
that the death sound. The parser says otherwise: **case 6 writes `+0x38` from
the `EXP` column**. So the value handed over is the monster's experience, and
the routine receiving it is the award.

What the routine does, in order:

1. walks the four characters testing the condition slots **13 through 17** —
   Unconscious, Dead, Stoned, Eradicated, Zombie — and counts who is eligible;
2. **divides the experience by that count** (`idiv ebx`);
3. for each eligible character, reads the **Learning** byte at `+0x7e`, which
   is skill slot 30, and forms `points x (rank + 1)`;
4. asks `0x467f30` for professions **13** and **14** — the Teacher and the
   Instructor — taking **10** for the first;
5. multiplies the share by those terms plus nine, over a hundred.

`observed` for the count, the division, the Learning read and its arithmetic,
and the two profession ids.

**And so the second negative goes too.** "Learning has no implementation" was
filed on the strength of the same failed searches. It has one, here, and its
rung multiplies as **`rank + 1`** rather than through a table — which is the
doubling and tripling its row promises, reached a different way.

## The award, read to the end

Two things were left open when `0x421520` was found: what the constant nine
does, and what `0x467f30` is.

**The terms add to the share; they do not scale it.** `0x42161a` is the tail:

```
mov ecx, dword [esi]          ; the character's experience, low half
add eax, ebp                  ; the bonus, plus the share
add ecx, eax
mov dword [esi], ecx
mov eax, dword [esi + 4]
adc eax, edx                  ; carried into the high half
cmp dword [esi], 0xee6b2800   ; and clamped at four billion
add esi, 0x161c               ; on to the next character
```

So the whole shape is

```
share = experience / eligible
bonus = share x (learning + hireling% + 9) / 100
each  = share + bonus,  64-bit, clamped at 4,000,000,000
```

and **the nine is a flat nine percent** every character collects over the plain
share whether or not it has Learning or a Teacher. `observed`

**`0x467f30` is "is somebody of profession *n* in the party".** It walks the
roster at stride 60 from `0x6aef28`, testing a record's `+0x18` against the
profession id and the `0x80` bit at `+0x08` — the flag property id 214 sets —
and falls back to comparing the party global at `0x90e7bc` when no record
matches:

```
mov  esi, dword [0x6ba534]     ; the count
mov  eax, 0x6aef30             ; the flag field of record 0
cmp  dword [eax + 0x10], ecx   ; the profession id, at +0x18 of the record
test byte [eax], 0x80          ; hired?
add  eax, 0x3c                 ; the next record
```

That names two more of the roster's fields — **`+0x08` the flags, `+0x18` the
profession** — and confirms the stride from a third place. `observed`
