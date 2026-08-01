---
title: "Character property setter at 0x4412b0"
summary: "Observed dispatch cases and runtime fields handled by the Might and Magic VI character-property setter."
doc_type: reference
status: partial
last_updated: 2026-08-01
tags:
  - mm6
  - runtime
  - character
  - properties
---
# The character property setter at `0x4412b0`

This runtime reference maps the 225-way character-property dispatch used by
Might and Magic VI event instructions. The mapped cases settle character,
party, script-clock, and global-table fields while retaining unresolved fields
as `unknown`; this page does not define the complete player or party record.

## Scope

This page covers the dispatch tables and observed case bodies at `0x4412b0`
and related getter and adder routines. The canonical record-level context is in
[`player-record.md`](player-record.md) and [`party-record.md`](party-record.md).

## What the gamble was after, and what it found instead

The question was what a level grants: `0x441314` adds an amount to the skill
pool at `+0x1410`, and the amount came from further up than had been read.

Following it up settles that question with a **negative**: there is no
level-up table behind it. `0x441314` is one case of a general
**"add this much to that property of this character"** routine, and the amount
is whatever the caller passes. The routine has only **four callers** in the
whole image — two that read the property id out of an event instruction's
argument byte at `+5`, and two that hand it a fixed id 17. None of them is a
level-up, and none of them passes 225. So what a level is worth is still
`unknown`, and this is the second approach from the writing side to end that
way.

## What it found on the way

The routine is a switch over **225 property ids**, through a byte selector at
`0x441ff0` into a jump table at `0x441f1c`, and it has **53 distinct case
bodies**. `observed`

Each body computes its own offset from the id, which is what makes the
mapping readable without inference:

| ids | what the body does | offset it lands on |
| --- | --- | --- |
| 25..31 | `add word [character + 0x16 + 4k]`, capped at 255 | the seven attribute **modifiers** |
| 32..38 | `add word [character + 0x14 + 4k]`, capped at 255 | the seven attribute **bases** |
| 39..45 | the same seven bodies as 25..31 | a second name for the modifiers |
| 46..55 | ten bodies of their own | the resistances and their neighbours |
| 56..86 | `byte [character + id + 0x28]`, masked `0x3f`, `0xc0` kept, stopped at 60 | the **thirty-one skills**, `+0x60`..`+0x7e` |
| 87..103 | `[character + 8 × id + 0x10c8] = ` the world clock | the **seventeen conditions** at `+0x1380` |
| 105..204 | `byte [id + 0x5b2293] +=`, capped at 255 | a hundred global byte counters, `unknown` |
| 205 | its own body | the autonote |
| 225 | `[character + 0x1410] +=` | the **skill pool** |

Three of those rows are worth their own line, because each independently
re-confirms something read a different way:

- **The skills.** Id 56 lands on `+0x60` and id 86 on `+0x7e` — thirty-one
  slots. The body masks the byte with `0x3f`, preserves `0xc0`, and clamps at
  sixty. That is the trainer's arithmetic exactly, arrived at from a second
  direction.
- **The conditions.** Id 87 lands on `+0x1380`, and the body stamps the
  **world clock read straight out of the party record** at `0x908d08`. So the
  condition array really is timestamps, and it really is seventeen long.
- **Id 17 is an item.** The two hard-coded callers pass 17 with amounts 544
  and a word from a table at `0x4c3dae`, and the body hands the party record
  to `0x487750`. `kVarItem = 17` was already in the engine; here it is from
  the other side.

## A correction to the script variable numbering

This engine had the two attribute runs at 31..37 and 38..44. The case bodies
say otherwise, with fixed offsets that leave nothing to interpret:

```text
id 25 -> add word [esi + 0x16]     Might modifier
id 31 -> add word [esi + 0x2e]     Luck modifier
id 32 -> add word [esi + 0x14]     Might base
id 38 -> add word [esi + 0x2c]     Luck base
```

So **the modifiers begin at 25 and the bases at 32**, and ids 39..45 land on
the modifier bodies a second time. `observed` at `0x4418ce`, `0x44199a`,
`0x4419bc` and `0x441ace`.

## All fifty-three bodies

Every case names its own field in its first two instructions. Read whole, the
table settles nine constants, corrects two more, and names four fields that
had no name.

| id | what the body does |
| --- | --- |
| 1 | `byte [+0x11] =` — `unknown` which field that is |
| 2 | `byte [+0x12] =` — the **class** |
| 3 | `dword [+0x1414] +=` — **hit points** |
| 4 | `[+0x1414] =` the maximum getter at `0x481ea0`, and clears `+0x1578`/`+0x1579` |
| 5 | `dword [+0x1418] +=` — **spell points** |
| 6 | `[+0x1418] =` the maximum getter at `0x482090`, and clears `+0x157a`/`+0x157b` |
| 8..11 | the four words `+0x30`, `+0x32`, `+0x34`, `+0x36` — an unnamed word, the **level**, its modifier, the birth word |
| 12 | a bit array, indexed `id >> 3` — the **award** |
| 13 | `dword [+0x1420] +=`, clamped at 4,000,000,000 — the **experience** |
| 16 | a bit array with `0x80000007` — the **quest bit** |
| 17 | builds a record and hands the party to `0x487750` — an **item** |
| 21 | `0x41ede0(amount)` — **gold** |
| 22, 24 | `rand() % amount`, and `rand() % amount + 1` |
| 23 | `0x4875f0`, then the table at `0x56c008` — **food** |
| 25..31, 39..45 | the seven attribute **modifiers**, `+0x16 + 4k` |
| 32..38 | the seven attribute **bases**, `+0x14 + 4k` |
| 46..50 | the five resistance **bases**, words at `+0x1254` |
| 51..55 | the five resistance **modifiers**, the words beside them |
| 56..86 | the thirty-one **skills** |
| 87..103 | the seventeen **conditions**, stamped with the world clock |
| 104 | `rep stosd` of 36 dwords from `+0x1380` — **clears all eighteen conditions** |
| 105..204 | a hundred global bytes at `0x5b2293` |
| 205 | the **autonote** |
| 213..215 | a bit array, and the party globals `0x908d48` and `0x908d05` |
| 216..221 | six 8-byte world-clock stamps at `0x90e19c` |
| 7, 14, 15, 18..20, 206..212, 222..224 | the do-nothing case |

### What it corrects

**The level is variable 9, not 8.** Both routines share one selector — the
225 bytes at `0x4411c0` and at `0x441ff0` are identical — and both index it
with `id - 1`. Following the selector *through* to the case, id 9 lands on the
body that writes `word [esi + 0x32]`; the earlier reading took the jump-table
entry number for the id and came out one low. The max-hit-point getter at
`0x481ebf` settles which word is the level from the other side: it adds
`+0x32` and `+0x34` together, the base-and-modifier shape everything else on
this record keeps. `observed`

**The `+0x1570` run is not written by nothing.** That was filed as a negative:
six getters read it and no instruction wrote it. Cases 4 and 6 write it —
`+0x1578` and `+0x1579` are cleared when hit points are set to the maximum,
`+0x157a` and `+0x157b` when spell points are. So the run is what a full
restore resets, which is the first thing anything has said about it.

### What it names

- **`+0x1420` is the experience**, a dword, clamped at 4,000,000,000.
- **`+0x1254`..`+0x1267` are the five resistances**, base and modifier words in
  pairs, sitting immediately below the buff array at `+0x1268`. Their ids and
  their offsets do not run in step: by offset the order is 46, 48, 47, 49, 50.
- **The condition array is eighteen slots**, not seventeen: case 104 clears
  `0x24` dwords from `+0x1380`, which is 144 bytes, and only seventeen of them
  have a setter id.
- **`+0x12` is the class** and `+0x11` the byte before it, which nothing names.

## `0x5b22fc`: not a hundred counters

Property ids 105..204 add to a hundred bytes from `0x5b22fc`, capped at 255,
and being global rather than per-character they looked like a run worth
naming. Following the array from its own side settles what it is, and it is
**not a counter and not a character's anything**.

Three places read it, and all three read it the same way:

```asm
0x0041f726  movsx eax, word [ecx + 0x14]        ; an attribute's value
0x0041f72c  mov cl, byte [eax + 0x5b22fc]       ; -> a small band number
0x0041f732  mov ecx, dword [ecx*8 + 0x6a82f8]
```

and at `0x420ad0` and `0x427d3b` the band is added to **400** and handed to the
string routine at `0x43c7c0`. So the array is indexed by an attribute's raw
value and yields the row of the word printed beside it — **the table that
turns a number into "Good" or "Average" on the sheet**. `observed` for the
indexing and the string call; `inferred` that the rows are the descriptive
words, since 400 is not that row in `GLOBAL.TXT` and whichever table it is has
not been identified.

It is uninitialised in the image, so it is filled at run time, and the dword
immediately below it at `0x5b22f8` is **the actor count** — the save writer
at `0x46cd8b` passes it straight to `fwrite` beside the actor array's address
and its 548-byte stride, which is why the AI reads it eighty-one times.

**So the hundred property ids write over a display table.** They index the
same array from its base — id 105 lands on `0x5b22fc` exactly — which cannot
be a property of a character. Either the numbering is dead in the shipped
game, or the original scribbles on its own sheet text. Nothing in this engine
should implement them.

## `0x90e19c`: six clocks that belong to the scripts alone

Property ids 216..221 stamp eight-byte world-clock values into a fixed array
at `0x90e19c + 8 × id` — `0x90e85c` through `0x90e88b`, just past the end of
the party record at `0x90e7a4`.

Four instructions in the whole executable touch that array, and all four are
property plumbing:

| where | what |
| --- | --- |
| `0x441095` | the setter writes a stamp |
| `0x441ec7` | the adder writes a stamp |
| `0x4429ca` | a clear |
| `0x44036c` | the getter reads one back |

The getter is the interesting one:

```asm
0x0044036c  fild  qword [eax*8 + 0x90e19c]
0x00440373  fmul  dword [0x4b9374]        ; the 30/128 calendar float
0x00440379  call  0x4ae24c                ; back to an integer
0x00440380  push  0x3c                    ; 60
```

— the **same conversion the world clock uses for the calendar**, then divided
by sixty. So a script stamps a moment and reads it back in game time.

**Nothing else reads them.** That is the finding: these six are neither
interface state nor engine state. The game keeps them purely so that a map
script can mark when something happened and later ask how long ago it was, and
no part of the engine ever looks. The gamble's stated risk was that they would
turn out to be screen cooldowns with no bearing on the game; they are the
opposite — they bear on nothing *but* the game's own scripts.

## The last three ids before the clocks

**213 — a bit on the character, and nobody but a script cares.** `0x4417e7`
indexes an array at `+0x1530` the way every bit array here is indexed:
`byte[base + (bit >> 3)]`, mask `0x80 >> (bit & 7)`. The array begins just
after the readied spell at `+0x152f` and runs to where the stored terms start
at `+0x1570` — **sixty-four bytes, five hundred and twelve bits**. That is far
more room than the game's ninety-nine spells would need, so the obvious name
was refused.

Following its readers settles it. **Four instructions in the whole image reach
the array**, and all four are property plumbing:

| where | what |
| --- | --- |
| `0x44018a` | the getter tests the bit and answers whether it is set |
| `0x440cf2` | the setter |
| `0x4417fe` | the adder |
| `0x442664` | the taker |

There is no absolute reference into any character's copy anywhere, and the
getter does nothing with a set bit but report it. So these are **512 flags a
character that the game itself never consults** — the same shape as the six
clocks at `0x90e19c`, one row down. A map script marks a character and later
asks whether it is marked; no part of the engine looks. `observed`

**214 — a mark on somebody, and two party bytes cleared.** The body zeroes
party `+0x95` and `+0x96`, multiplies its amount by fifteen, and sets bit
`0x80` on `dword [0x6aef30 + 60 × amount]` — a global array of **sixty-byte
records** based at `0x6aef28` and counted by `0x6ba534`. It then walks that
array against `0x90e7e0` and the party record's end at `0x90e7a4`. `observed`;
that the two cleared bytes are the party's hireling slots is `inferred`, from
their going blank exactly when a record in that array is flagged.

**215 — the reputation.** `0x441326` masks its amount to a byte and adds it to
`0x908d48`, which is party `+0xd8`. `observed` — a second door onto the
standing the death handler moves by fifty.

So the gamble's stated risk was that what remained would be save-file
bookkeeping with no game meaning. It is not bookkeeping — a bit array, an NPC
mark and the reputation — but two of the three keep their names to themselves.
