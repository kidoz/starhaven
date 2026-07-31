# The player record (`MM6.exe`, runtime)

Status: **partially mapped, from traced routines only.** Not a file format:
this is the in-memory character structure the executable's own combat and
skill code addresses. It is recorded because three separate traces kept
re-deriving the same offsets, and because the fields StarHaven still guesses
at — recovery, the attack bonus's parts — will be found here. Each claim is
tagged `observed` (read from an instruction) or `inferred`.

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
| `+0x50`..`+0x55` | 6 | the five resistances and one more, one byte each, in the design table's own order; **200 or above means immune** | observed |
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

- **The attribute run**, eight fields sixteen bytes apart: `+0x12b0` id 9,
  `+0x12c0` id 6, `+0x12d0` id 1, `+0x12e0` id 2, `+0x12f0` id 4, `+0x1300`
  id 5, `+0x1310` id 0, `+0x1320` id 3. Seven of the eight are the sheet's
  attributes in `stats.txt`'s order — **0 Might, 1 Intellect, 2 Personality,
  3 Endurance, 4 Accuracy, 5 Speed, 6 Luck** — which the routines confirm
  from the other side: hit points ask for 3, spell points for 2, recovery
  and armour for 5, the attack bonus for 4. **Id 9 is an eighth field in the
  same block with no name.** `unknown`
- **The resistances, and they are the party's rather than the character's.**
  Ids 10, 12, 11, 13 and 23 read five *globals* at `0x908e3c`, `0x908e4c`,
  `0x908e5c`, `0x908e6c` and `0x908e7c` — sixteen bytes apart, one block,
  outside the player record entirely. That "of Protection" adds ten to ids
  10..13 and nothing to 23 is the confirmation: four of the five elements
  and not the fifth.
- **A pair of stored words**: ids 15 and 19 both read `+0x1270`, and id 16
  reads `+0x1280`.
- **Eight ids with no base at all.** Ids 7, 8, 14, 17, 18, 20, 21 and 22
  fall to the common `ret` with zero in `eax`, so whatever they are worth is
  built entirely by the bonused getter from gear and level. **Id 14 is the
  character's level** — both the hit-point and spell-point routines multiply
  their class number by it — and the rest are unnamed.

So the gamble half paid. The resistance block is named and its home moved
out of the player record; the attribute run is confirmed from two sides; and
the three specials that point at ids 7, 8 and 9 still point at numbers with
no names, which was the risk. Those three rows in
[`weapon-specials.md`](weapon-specials.md) stay `unknown`, and nothing more
has been hung on them.

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
which of this engine's skill names is not yet joined. `unknown`

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

**What a level costs was hunted and not found.** The training hall's two
lines — "Train to level %d for %d gold" and "You need %d more experience to
train to level %d" — are fetched by an index the code computes rather than
pushes, so neither string leads anywhere, and no ladder of experience values
sits in `.rdata` or `.data` either. StarHaven's triangular staircase,
1000·L·(L−1)/2, is its own. `inferred`

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
