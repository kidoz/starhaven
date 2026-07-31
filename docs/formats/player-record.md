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

**The tick is Haste-aware.** The burn is not a flat elapsed: the routine
first walks a sixteen-entry effect list on the character and, when it
finds effect **17**, takes a percentage of **50**; the amount subtracted
is then `elapsed × percent / 100 + elapsed` — that is, **elapsed × 1.5
while the effect is on**. `observed` StarHaven now shortens a hasted
character's recovery by exactly that figure.

What the elapsed unit counts is still unread: the tick has exactly one
caller (`0x427ea9`, in the time-advance path) which passes an integer
whose own origin lies further up. `unknown`
