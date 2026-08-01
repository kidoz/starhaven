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
