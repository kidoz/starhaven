# The spell switch (`MM6.exe`, runtime)

Status: **one school read, in full.** Not a file format: this is the
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
the table directly. Several ids share a body — `0x4230e1` collects the
plain damage spells, `0x423492` and `0x42322d` two more families — and the
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
| 70 | Harm | `0x4230e1` | the shared damage body |
| 71 | Cure Wounds | `0x427f79` | heal **2 × points + 5**, all three ranks alike |
| 72 | Cure Poison | `0x42800d` | the same window ladder; condition **6** |
| 73 | Speed | `0x428114` | **10 + 2 (expert 3) × points**, **3600 s** a point, party at master |
| 74 | Cure Disease | `0x428288` | the same window ladder; condition **7** |
| 75 | Power | `0x4283b8` | identical to Speed |
| 76 | Flying Fist | `0x42322d` | a shared body |
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

## Still unread

- The three shared bodies (`0x4230e1`, `0x423492`, `0x42322d`) and what
  distinguishes the families that use them. `unknown`
- The eligibility call at `0x421f30`, which every case guards itself with.
  `unknown`
- The other eight schools' cases. The addresses are all in the table above's
  source; only Body has been read. `unknown`
