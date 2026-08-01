# The party record (`MM6.exe`, runtime)

Status: **structure settled, fields partly named.** Not a file format: this
is the in-memory party the executable keeps at **`0x908c70`**. Every claim is
tagged `observed` (read from an instruction) or `inferred`.

## It is one record, not scattered globals

Tallying every absolute reference into `0x908c00`..`0x909100` gives **123
distinct offsets** in a dense, regular run from `0x908c70` upward, with no
gaps large enough to separate one structure from another. The arithmetic
closes it:

- header of **`0x2c4` = 708 bytes** from `0x908c70`,
- then the **four character records** of `0x161c` = 5660 bytes each, starting
  at `0x908f34` — which is exactly where the character array is known to
  begin,
- ending at **`0x90e7a4`**, which is exactly the bound Power Cure's case
  loops to when it heals the whole party.

Two independently-found numbers meeting at both ends is the confirmation.
`observed`

## The fields named so far

| Offset | Refs | Field | Where it was read |
| --- | --- | --- | --- |
| `+0x000` | 124 | the record's own base | passed as `this` throughout |
| `+0x028` | 155 | position **x** | the spell launcher, the AI's reach tests |
| `+0x02c` | 155 | position **y** | the same |
| `+0x030` | 120 | position **z** | the same |
| `+0x034` | 71 | facing | the launcher aims with it |
| `+0x038` | 37 | a second angle | beside the facing |
| `+0x098` | 239 | the **world clock**, low half | the calendar routine |
| `+0x09c` | 180 | the world clock, high half | the same |
| `+0x0d8` | 28 | a counter the AI subtracts 100 from | `0x403086` |
| `+0x0fc` | 11 | the **fatigue byte** | incremented hourly, zeroed by rest |
| `+0x1c4` | — | the **spell-buff array**, 16 records of 16 bytes | `0x47d170` clears it |
| `+0x2c4` | 99 | the **four character records** begin | everywhere |

`observed` throughout. The buff array running from `+0x1c4` for exactly 256
bytes and ending precisely where the characters begin is a second structural
check on both.

The world clock at `+0x098` is the most-referenced field in the executable's
whole data segment at 239 sites, which is what a clock every routine consults
should look like.

## Four of the dense ones, named

Taking the offsets by reference count and reading what touches them.

**`+0x1c0` (73 references) is the turn-based flag.** Every site compares it
against **1**, and the branch is decisive: at `0x403f27`, when it is 1 the
actor's recovery counter at `+0x6c` is set directly, and otherwise the value
is scaled by a float and the double **−32/15** — the same constant the
real-time recovery uses. So one branch is the turn-based clock and the other
the running one. `observed`

**`+0x0fd` (57 references) is the quest-bit array.** `0x4031bf` loads it as
`this` and calls `0x43fdf0` with a number and a set/clear flag; that routine
shifts the number right three for a byte index and masks the low three bits
for the bit — a plain bit array. The site sets **bit 202**. `observed`

**`+0x0e0` (41 references) is script variable 20**, a party-level dword. The
script-variable getter's own jump table lands variable 20 on it, and the
interface formats it with `%lu`. Which quantity variable 20 is remains
`unknown` — the engine names 16, 17, 21, 22, 12, 3, 5, 13 and 23, and 20 is
not among them.

**`+0x074` (44 references) travels with the position.** At `0x425e9e` it is
written in one group with `+0x030`, `+0x034` and `+0x038` — the z, the facing
and the second angle — which makes it part of a position-and-heading
snapshot rather than a field of its own. `observed` for the grouping;
`inferred` that it is a snapshot.

## What is measured but unnamed

Ninety-odd further offsets carry references and no name yet. The densest of the
remainder, worth a later sitting: `+0x078` (29), `+0x008` (22), and the run
of four at `+0x0ac`, `+0x0b0`, `+0x0b4`, `+0x0bc` (22–24 each).
`unknown` for all of them; the reference counts are recorded so the next
attempt can start with the ones that matter.
