# The spell switch (`MM6.exe`, runtime)

Status: **one school read in full, and the machinery around every case.** Not a file format: this is the
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
  appears outside the school — Mind's Cure Insanity at `0x427a05` — so it
  belongs to every timed cure. StarHaven follows the executable. `observed`

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

## Still unread

- The impact dispatcher at `0x43102e` and its 43 object kinds, which is
  where the damage of every thrown spell is actually decided. `unknown`
- The other eight schools' own case bodies. The addresses are all in the
  dispatch table; only Body has been read. `unknown`
