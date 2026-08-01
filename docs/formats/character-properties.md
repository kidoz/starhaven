# The character property setter at `0x4412b0`

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

```
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

```
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
immediately below it at `0x5b22f8` is read eighty-one times, most of them in
the AI. `unknown` what that one holds.

**So the hundred property ids write over a display table.** They index the
same array from its base — id 105 lands on `0x5b22fc` exactly — which cannot
be a property of a character. Either the numbering is dead in the shipped
game, or the original scribbles on its own sheet text. Nothing in this engine
should implement them.
