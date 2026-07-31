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

**The monsters' own counter was hunted and not found.** Every write into
a monster's runtime record across the AI cluster (`0x430000`..`0x433000`)
was enumerated: the fields at `+0xe0`..`+0xfc` are the position and
velocity triples the mover writes, the bytes at `+0xfe`/`+0xff` are
flags, and the only other run — `+0x84`, `+0x86`, `+0x88` — is a
velocity scaled by 50, not a countdown. Nothing recovery-shaped writes
into the record from the AI, so either the monsters' cooldown lives in a
parallel array keyed by actor index (the way the door and light state
do) or the AI re-derives it each frame from `MONSTERS.TXT`'s `Rec`
column. StarHaven keeps its own hundredths-of-a-second reading in the
meantime. `unknown`
