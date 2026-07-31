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

## What is still missing

The recovery field — the one that would settle the party's swing rate and
the monster table's `Rec` units — has not been located. The routines mapped
here do not touch it; the search that failed is recorded in
[`text-tables.md`](text-tables.md). `unknown`
