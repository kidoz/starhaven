# The spell switch (`MM6.exe`, runtime)

Status: **six schools read, and the machinery around every case.** Not a file format: this is the
executable's own per-spell code, reached while chasing the recovery tick.
Every claim is tagged `observed` (read from an instruction) or `inferred`.

## How a spell reaches its own numbers

The spell queue is an array of **20-byte records**; the processor walks ten
of them from the pointer it is handed. Each record carries:

| Offset | Size | Field | Status |
| --- | --- | --- | --- |
| `+0` | 2 | spell id, 1..102, `0` for an empty slot | observed |
| `+2` | 2 | the caster's party index | observed |
| `+4` | 2 | the target's party index | observed |
| `+8` | 1 | flags, tested against `0xca` | inferred |

The party record it resolves them against is the array at **`0x908f34`**,
stride **`0x161c` = 5660 bytes**, four entries — Power Cure's case walks it
from `0x908f34` to `0x90e7a4`, which is exactly four strides. `observed`

Dispatch is one instruction, at `0x422c93`:

```
movsx eax, word [ebx]          ; the spell id
dec   eax
cmp   eax, 0x65                ; 101
ja    <done>
jmp   dword [eax*4 + 0x429c74] ; 102 entries, one per spell
```

So **case index = spell id − 1**, and `SPELLS.TXT`'s own numbering indexes
the table directly. Several ids share a body — `0x4230e1`, `0x423492` and
`0x42322d` are three copies of one projectile launcher, read below — and the
rest have one case each. `observed`

Each case is entered with two things and computes from nothing else:

- the caster's **skill points** in a local (`[esp+0x10]` at the Body cases),
- the **mastery** in `esi`, as **1 normal, 2 expert, 3 master**.

The shape is always the same: a three-way branch on `esi` writes the
spell's amount to a second local, an eligibility call at `0x421f30` decides
whether anything happens at all, the case plays its sound through
`0x4358f0` with the target's index plus 100 as the source, and then it
applies itself.

## The Body school, spells 67..77

| Id | Spell | Case | What the case computes |
| --- | --- | --- | --- |
| 67 | Cure Weakness | `0x427d8c` | window ×180 / ×10800 / ×259200 s a point; condition **1** |
| 68 | First Aid | `0x427e25` | heal **5 / 7 / 10** flat; then drains `points` off recovery |
| 69 | Protection from Poison | `0x427ebb` | **1 / 2 / 3** resistance a point, for **3600 s** a point |
| 70 | Harm | `0x4230e1` | the shared launcher; no number of its own |
| 71 | Cure Wounds | `0x427f79` | heal **2 × points + 5**, all three ranks alike |
| 72 | Cure Poison | `0x42800d` | the same window ladder; condition **6** |
| 73 | Speed | `0x428114` | **10 + 2 (expert 3) × points**, **3600 s** a point, party at master |
| 74 | Cure Disease | `0x428288` | the same window ladder; condition **7** |
| 75 | Power | `0x4283b8` | identical to Speed |
| 76 | Flying Fist | `0x42322d` | the shared launcher, second copy |
| 77 | Power Cure | `0x428591` | heal **2 × points + 10** to all four |

`observed` throughout.

## What the table's prose confirms, and where it is wrong

`SPELLS.TXT` states a number for six of these, and the executable agrees with
every one: First Aid's 5/7/10, Cure Wounds' "five plus 2 per point of skill",
Speed's and Power's "10 points plus 2 (expert 3) per point" with "master:
Spell affects entire party", and Protection from Poison's 1/2/3 points of
resistance a point. That agreement is the check on the reading of the
dispatch itself.

Two kinds of number the prose does **not** carry are recorded here as the
executable's alone:

- **The buff durations.** Protection from Poison, Speed and Power each last
  **one hour per point of skill**, at every rank. No line of the table says
  so. `observed`
- **The cure windows above normal rank.** The three "if you cast this spell
  in time" cures multiply the caster's points by **180, 10800 and 259200
  game seconds** and subtract the result from the world clock to make the
  cutoff the condition's timestamp is tested against — **three minutes,
  three hours, three days a point**. The prose agrees at normal ("3 minutes
  per point of skill") and under-states the two above it, calling them "1
  hour" and "1 day" where the code grants three of each. The same ladder
  appears in Mind's Cure Insanity at `0x427a05` and Spirit's Resurrection at
  `0x427282`. **It does not belong to every timed cure**, which is a
  narrowing of what was first written here: Spirit's Remove Curse
  (`0x426b0e`) and Raise Dead (`0x427016`) multiply by **180, 3600 and
  86400** instead — three minutes, one hour, one day, exactly the words
  their own rows use. So there are two ladders and the choice is per spell.
  StarHaven follows the executable for both. `observed`

## Spirit, Mind and Light

| Id | Spell | Case | What the case computes |
| --- | --- | --- | --- |
| 46 | Bless | `0x4266e4` | `+points`, for **64 min + 5 a point**, 15 at master |
| 47 | Healing Touch | `0x4268fa` | three branches; the amounts not decoded |
| 48 | Lucky Day | `0x42698d` | **10 + 2 (expert 3) × points**, party at master |
| 49 | Remove Curse | `0x426b01` | window **180 / 3600 / 86400 s** a point |
| 50 | Guardian Angel | `0x426b97` | **3600 s** a point |
| 51 | Heroism | `0x426c21` | identical to Bless |
| 52 | Turn Undead | `0x426e16` | **180 × points + 3** |
| 53 | Raise Dead | `0x427009` | window **180 / 3600 / 86400 s** a point |
| 54 | Shared Life | `0x4270ea` | **1 / 2 / 3 ×** points |
| 55 | Resurrection | `0x427275` | window **180 / 10800 / 259200 s** a point |
| 56 | Meditation | `0x427359` | **10 + 2 (3) × points**, **3600 s** a point |
| 57 | Remove Fear | `0x427534` | window **180 / 10800 / 259200 s** a point |
| 59 | Precision | `0x4275cd` | identical to Meditation |
| 60 | Cure Paralysis | `0x427741` | window **180 / 10800 / 259200 s** a point |
| 62 | Mass Fear | `0x427819` | **180 × points** |
| 64 | Cure Insanity | `0x4279fc` | window **180 / 10800 / 259200 s** a point |
| 66 | Telekinesis | `0x427b38` | **1 / 2 / 3 ×** points |
| 78 | Create Food | `0x4285f8` | **1 / 2 / 3 ×** points |
| 79 | Golden Touch | `0x4286bc` | **40 / 60 / 80** percent |
| 83 | Day of the Gods | `0x428a43` | **2 / 3 / 4 × points + 10**, for **2 / 3 / 4 hours** a point |
| 88 | Divine Intervention | `0x428fdc` | **1 / 2 / 3** casts a day |

`observed` throughout. Dispel Magic (80), Prismatic Light (84) and Sun Ray
(87) carry no mastery constants in their cases, and Healing Touch's three
branches were not decoded; those four are `unknown`.

**Every figure the rows state, the executable matches.** Golden Touch's
"40% gold value", Divine Intervention's "once per day", Telekinesis'
"1 point per point of skill", Create Food's "1 day per 10 points",
Meditation's and Precision's "10 points plus 2 per point" with the party at
master, Shared Life's "1 hit point per point to the pool", Bless's and
Heroism's "1 hour + 5 minutes per point" and "+ 15" at master, Day of the
Gods' "twice skill".

Three things the rows do not carry:

- **The hour a point** that Guardian Angel, Meditation and Precision each
  last — the same figure Body's and Fire's buffs keep.
- **The ten** Day of the Gods and Hour of Power add on top of their
  multiple of the skill.
- **Bless and Heroism's real base**, sixty-four minutes rather than the hour
  their rows round to — the same four-minute overrun Haste has.

One number is recorded with a doubt: Hour of Power's case sets 2 and 3 for
the lower ranks, matching its row, but **12** at master where the row says
four. What the twelve is for is `unknown`, and nothing has been built on it.

## Which cures use which ladder

With all the timed cures now read, the split is complete. **Remove Curse and
Raise Dead** use the plain ladder — three minutes, one hour, one day a
point, exactly their rows. **Cure Weakness, Cure Poison, Cure Disease,
Remove Fear, Cure Paralysis, Cure Insanity and Resurrection** use the
three-of-each ladder — three minutes, three hours, three days — where their
rows say one hour and one day. Seven against two, and the rows are wrong
about the seven. `observed`

## What First Aid gave away

First Aid's line reads "Recovery is reduced by an amount equal to the
caster's skill in Body Magic", and its case is the **only caller** of the
standalone recovery drain at `0x482bb0` — passing the raw skill points. That
is how the drain was found to be a spell's helper rather than the per-frame
tick, and it fixes the unit of both: the reduction is in the world clock's
own units, 128 to the real second. See
[`player-record.md`](player-record.md).

## The sounds

Each case pushes its own sound id. Across Body they run from **7000 in tens,
one step per spell** — Cure Weakness 7000, First Aid 7010, Cure Poison 7050,
Cure Disease 7070, Power Cure 7100. `observed` for the five measured, and
`inferred` that the six between them fill the gaps.

## What a cast costs, and where the guard is

`0x421f30`, the call every case body opens with, is the **spell-point
purse**. It takes the cost, compares it against the caster's points at
**`+0x1418`**, and either plays sound **209** and returns zero — the case
then does nothing at all — or subtracts and returns one. `observed` The
three shared bodies perform the same subtraction inline instead of calling
it, which is why they enter through a `cmp`/`jl` on the same field.

## The three shared bodies are one launcher, not a damage table

This was the expectation going in and it is wrong, so it is recorded rather
than quietly dropped. `0x4230e1` (twenty-one ids), `0x42322d` (four) and
`0x423492` (seven) are three near-identical copies of a single routine that
**carries no damage numbers whatever**. Each one:

- spends the cost against `+0x1418` inline;
- looks the spell's sprite up in the descriptor list at `[0x5f6df4]`,
  records of **52 bytes** whose id sits at `+0x2e`;
- takes the party's position out of the globals at `0x908c70`, `0x908c98`,
  `0x908c9c` and `0x908ca0`;
- stamps the projectile with the **spell id**, the caster's **raw skill
  points**, and a packed **owner handle**, then jumps to a common spawn tail
  at `0x426550`. `observed`

So the damage of a Fire Bolt is not in its case. It is decided on impact, by
a separate dispatcher at `0x43102e` that switches on the *object's* own kind
byte at `+0x134` (values 4..46) rather than on the spell — which is why
reading the three bodies converts no prose-guessed numbers at all. That
dispatcher is the next thing to read, and it is `unknown`.

## The object handle

The launcher packs its caster as `(index << 3) | 4`, and the queued-message
handler that fills both recovery counters unpacks exactly that shape, taking
kind 4 as a party slot and kind 3 as an actor. So the engine has **one
universal object handle**, an index over a three-bit kind, and it is the same
one in the spell code and the message queue. `observed`

## Which spells are thrown at the world

`0x421f70` is a complete partition of the spell list. It maps ids 2..99
through a byte table at `0x421f98` onto two cases that return one and zero,
and **exactly 47 answer yes**: every direct-damage spell, plus Stun, Turn to
Stone, Charm, Mass Fear, Feeblemind, Dispel Magic, Slow, Destroy Undead,
Paralyze, Mass Curse and Shrinking Ray. Every id served by the three shared
launchers is on the list, along with fifteen more that have cases of their
own.

Its single caller, `0x420b56`, reads the active character's **readied spell
id at `+0x152f`** and, when the answer is yes, queues action **25** on a
click — so the predicate is what decides whether clicking on the world fires
what you have readied. Nothing in `SPELLS.TXT` states this partition; the
byte table is its only source, and StarHaven now carries the list.
`observed`

## The Fire school, spells 1..11

| Id | Spell | Case | What the case computes |
| --- | --- | --- | --- |
| 1 | Torch Light | `0x423021` | brightness **2 / 3 / 4**, for **3600 s** a point |
| 2 | Flame Arrow | `0x4230e1` | the shared launcher |
| 3 | Protection from Fire | `0x4236e3` | **1 / 2 / 3** resistance a point, for 3600 s a point |
| 4 | Fire Bolt | `0x4230e1` | the shared launcher |
| 5 | Haste | `0x42379f` | **(points + 64) × 60 s**, and **3840 + 180 × points** at master |
| 6 | Fireball | `0x4230e1` | the shared launcher |
| 7 | Ring of Fire | `0x42398e` | radius **512**, and **1024** at expert and master |
| 8 | Fire Blast | `0x423b4f` | **3 / 5 / 7** shots |
| 9 | Meteor Shower | `0x423d39` | **8 / 12 / 16** meteors |
| 10 | Inferno | `0x424142` | no number; it branches on where the party is |
| 11 | Incinerate | `0x4230e1` | the shared launcher |

## The Dark school, spells 89..99

| Id | Spell | Case | What the case computes |
| --- | --- | --- | --- |
| 89 | Reanimate | `0x429104` | **10 / 20 / 30** hit points a point |
| 90 | Toxic Cloud | `0x42322d` | the shared launcher |
| 91 | Mass Curse | `0x42928b` | **120 / 180 / 240 s** a point |
| 92 | Shrapmetal | `0x429445` | **3 / 5 / 7** fragments |
| 93 | Shrinking Ray | `0x423492` | the shared launcher |
| 94 | Day of Protection | `0x4295fe` | everything cast at **2 / 3 / 4 ×** the skill |
| 95 | Finger of Death | `0x423492` | the shared launcher |
| 96 | Moon Ray | `0x4297a1` | no number; it branches on where the party is |
| 97 | Dragon Breath | `0x4230e1` | the shared launcher |
| 98 | Armageddon | `0x4299e1` | **1 / 2 / 3** casts a day, counted at `+0x1619` |
| 99 | Dark Containment | `0x4230e1` | the shared launcher |

`observed` throughout.

### What the two schools confirm, and what they add

`SPELLS.TXT` states a figure for eleven of the thirteen cases that have one,
and the executable agrees with **every one**: Fire Blast's three, five and
seven shots, Meteor Shower's eight, twelve and sixteen meteors, Shrapmetal's
three, five and seven fragments, Reanimate's ten, twenty and thirty hit
points a point, Mass Curse's two, three and four minutes a point, Day of
Protection's two, three and four times skill, Armageddon's one, two and
three casts a day, Torch Light's and Protection from Fire's hour a point,
and Protection from Fire's one, two and three points of resistance.

Three numbers the prose does not carry:

- **Torch Light's brightness.** "Brighter light" and "brightest light" are
  **3** and **4** against a normal **2**. `observed`
- **Ring of Fire's radius.** "Small radius of effect around party" is
  **512** of the world's units, and "larger radius" is **1024** — the only
  blast the executable measures, and the number this engine had already
  guessed at for every blast. `observed`
- **Haste's true base.** Both lower ranks compute `(points + 64) × 60`
  seconds and master `3840 + 180 × points`, so the base is **sixty-four
  minutes** where the table says "1 hour", and the step is a minute a point
  below master and three at it — which is exactly what the prose says apart
  from the four extra minutes. `observed`

## The dispatcher at `0x43102e` was not the spell's — a second correction

Chased on the expectation that it decided a thrown spell's damage, it turned
out to be something else entirely, and something worth having. It is not
keyed on an *object* at all: the enclosing loop at `0x430f87` walks the
**striker's own equipment slots** after a blow lands, and `+0x134` is the
item record's special-enchantment dword at `+0xc`. So the switch is over
**weapon enchantments, 4..46**, and each case adds its own damage to the
target's hit points at `+0x28`. It is written up in full in
[`weapon-specials.md`](weapon-specials.md).

Where a thrown spell's damage is decided therefore remains open, and the
honest position is that two guesses at it have now missed.

## The projectile object, laid out

The launcher's spawn tail hands its block to `0x42a730`, which is the
insert: the objects live in an array at **`0x5c9ad8`**, **100 bytes** a
record, **1000** of them, ending at `0x5e217a`. A free slot is a zero word
at `+2`; the routine takes the first, copies the record in and returns its
index, or −1 when the array is full or the scan runs off the end.
`observed`

Two offsets fix the base from outside the launcher, which is what makes the
rest trustworthy rather than counted-off-by-eye:

- **`+0x18` is a flags word.** The AI clears bit 2 of it across the whole
  array at `0x405c1e`, striding by the same 100.
- **`+0x4a` is the owner handle.** The collision handler at `0x431e64`
  reads `[index × 100 + 0x5c9b24]`, takes `& 7` as the kind and `>> 3` as
  the index, and branches: **kind 3** multiplies by 548 into the actor
  record, kinds 2 and 4 go elsewhere. That is the same packed handle the
  launcher writes and the recovery queue unpacks.

With those two, the launcher's own writes read off:

| Offset | Size | Field | Status |
| --- | --- | --- | --- |
| `+0x00` | 2 | the sprite's index in the descriptor list | observed |
| `+0x02` | 4 each | position, from the party globals | observed |
| `+0x18` | 2 | flags | observed |
| `+0x3e` | 4 | **the spell id** | observed |
| `+0x42` | 4 | **the caster's skill points** | observed |
| `+0x4a` | 4 | the owner handle | observed |

## The damage, found — and two bytes to correct first

The layout above was written off the array base `0x5c9ada`. It is
**`0x5c9ad8`**: the insert scans for a free word at `+2`, not at `+0`. So
every offset in that table is two low, and the corrected ones are **flags
`+0x1a`**, **spell id `+0x40`**, **caster's skill `+0x44`**, a third word at
**`+0x48`**, and the **owner handle `+0x4c`**.

That two-byte slip is also why the search for readers came up empty. With
the right offsets the answer is one instruction: the collision handler's
**kind-4** branch — the party's own projectiles — opens with

```
mov edx, [ebp + 0x48]
mov ecx, [ebp + 0x40]      ; the spell id
push edx
mov edx, [ebp + 0x44]      ; the caster's skill
call 0x432ad0
```

**`0x432ad0` is the spell-damage routine.** It masks the skill to its low
six bits, switches on **spell id minus two** through a 98-entry byte
selector at `0x432f84` into a jump table at `0x432ef8`, and **34 spells have
a case** while the other 62 share a return of zero. Each case is a few
instructions in one of three shapes: a lone die, a die rolled once per point
of skill, or no dice at all and the skill plus a constant. `observed`

The full table lives in `src/game/spell_damage.hpp`. A sample of its shape:
Flame Arrow **1d8**, Fire Bolt **skill d4**, Fireball **skill d6**,
Incinerate **skill d15 + 15**, Lightning Bolt **skill d8**, Implosion
**skill d10 + 10**, Harm **skill d2 + 8**, Sun Ray **skill d20 + 20**,
Dragon Breath **skill d25**, Ring of Fire **skill + 6**, Armageddon and Dark
Containment **skill + 50**.

The mapping onto this engine's own strike is exact — a flat part rolled
once plus a part rolled per point of skill is precisely the shape the cases
take — so the prose parser is now the fallback rather than the source.

## Why three attempts missed it

Worth recording, because the pattern is the lesson. The first two leapt at
switches that looked like the right shape and were not. The third laid the
object out properly but read the collision handler's **kind-3** branch —
which is a *monster's* missile landing on the party, and rolls a monster's
attack out of its own table. The party's spells are **kind 4**, forty lines
further down, and that branch calls the damage routine in its first four
instructions.

## What the other four routines turned out to be

Named while checking them off, and worth keeping: `0x4219b0` picks a party
member out of an eight-bit mask; `0x421d50` is a saving throw, rolling
against the target's level at `+0x35`; `0x446c20` rolls a **monster's**
attack from the sub-structure at `+0x2c` of its record, which holds a spell
id at `+0x22` and a packed skill at `+0x23`; and `0x47d5a0` is the general
"does this character wear an item with special N", walking the sixteen
anchors. `observed`

## Still unread
- The other eight schools' own case bodies. The addresses are all in the
  dispatch table; only Body has been read. `unknown`
