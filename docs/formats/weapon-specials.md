---
title: "Weapon special effects"
summary: "Observed on-hit and statistic effects for Might and Magic VI special weapon enchantments."
doc_type: reference
status: verified
last_updated: 2026-08-01
tags:
  - mm6
  - runtime
  - weapons
  - enchantments
---
# Weapon specials (`MM6.exe`, runtime)

This runtime reference documents what the executable does with a special
enchantment after a blow lands and when a statistic is requested. Every claim
is tagged `observed` (read from an instruction) or `inferred`. The game's
special-bonus table gives each row a name, class letter, and price but no
effect, so the executable branches are the only source for the numbers below.

## Scope

This page covers special-enchantment identifiers 4 through 46, three named
artifact branches, on-hit effects, and statistic adjustments observed in the
Might and Magic VI executable. It does not define base weapon damage or general
item generation.

## The walk

After a hit, the loop at `0x430f87` steps through the striker's equipment
slots — a slot index in a local, a count beside it — and for each one:

- skips the piece whose flag byte at `+0x13c + 28n` has bit 1, the broken
  mark;
- tests the item's **id** against three artifacts by name;
- otherwise switches on the item's **special-enchantment dword at `+0xc`**,
  through a byte selector at `0x431b48` into a jump table at `0x431b0c`,
  covering enchantment ids **4..46**.

Every case does the same three things: it writes a **damage-type id** into a
list the caller later shows, bumps that list's count, and subtracts its roll
from the target's hit points at `+0x28`. `observed`

The damage-type ids are **5 cold, 6 electricity, 7 poison, 8 fire**, by which
specials write which. `observed`

## What each one adds

| Id | Name | Element | Damage | Case |
| --- | --- | --- | --- | --- |
| 4 | of Cold | cold | 3–4 | `0x4310a3` |
| 5 | of Frost | cold | 6–8 | `0x4310ce` |
| 6 | of Ice | cold | 9–12 | `0x4310ef` |
| 7 | of Sparks | electricity | 2–5 | `0x43111a` |
| 8 | of Lightning | electricity | 4–10 | `0x431145` |
| 9 | of Thunderbolts | electricity | 6–15 | `0x431166` |
| 10 | of Fire | fire | 1–5 | `0x431187` |
| 11 | of Flame | fire | 2–12 | `0x4311a6` |
| 12 | of Infernos | fire | 16–18 | `0x4311c4` |
| 13 | of Poison | poison | 5 flat | `0x4311e2` |
| 14 | of Venom | poison | 8 flat | `0x4311f3` |
| 15 | of Acid | poison | 12 flat | `0x431204` |
| 46 | of The Dragon | fire | 10–20 | `0x431215` |

`observed` throughout. The bands are written two ways — a mask for the powers
of two, an `idiv` remainder otherwise — but they come to the same inclusive
range. The three poison rows are the only flat ones.

Each ladder of three climbs in the table's own order, and the three tiers of
a school never overlap: cold runs 3–4, 6–8, 9–12; electricity 2–5, 4–10,
6–15; fire 1–5, 2–12, 16–18. `observed`

## The ones that take instead of give

**16 Vampiric** and **41 of Darkness** share a case with the artifact **400,
Mordred**, and none of them adds damage. They give the striker back a fifth
of the blow — the dealt damage times `0x66666667`, shifted down 33, which is
a divide by five — capped at the striker's own maximum. `observed` at
`0x430ff2`.

## The two artifacts named by id

The routine recognises two weapons before it looks at any enchantment, and
gives each a flat amount: **415 Hades** twenty points, **416 Ares** thirty.
`observed` at `0x430fc9` and `0x430fda`.

## Where the other forty-six are applied

Thirteen specials add damage above, and three more had turned up one at a
time in unrelated routines — "of Recovery" inside the recovery drain, "of
Swiftness" inside the strike. Whether the rest are scattered the same way or
gathered into one walk has a clean answer: **both, deliberately.**

**There is one walk**, in the bonused stat getter. At `0x4831df` it steps the
equipped items, reads each one's special at `+0xc`, and dispatches on ids
**1..57** through a byte selector at `0x4837b8` into a jump table at
`0x48376c`. `observed`

**And it handles only eighteen of them.** Ids **3..41 all fall to the
do-nothing case** — which is exactly the run holding the twelve elemental
riders, Vampiric, of Recovery and of Darkness, every one of which is answered
somewhere else. The specials that reach a stat are 1, 2 and the run 42..57,
and each case is the same three instructions: test which stat is being asked
for, add a fixed amount, done.

| Id | Name | Adds | To |
| --- | --- | --- | --- |
| 1 | of Protection | 10 | stats 10–13, the resistances |
| 2 | of The Gods | 10 | every attribute, 0–6 |
| 42 | of Doom | 1 | everything, with no test at all |
| 43 | of Earth | 10 | Endurance and stat 7 |
| 44 | of Life | 10 | stat 7 |
| 45 | Rogues | 5 | Accuracy and Speed |
| 46 | of The Dragon | 25 | Might |
| 47 | of The Eclipse | 10 | stat 8 |
| 48 | of The Golem | 5 | stat 9, and 15 to Endurance |
| 49 | of The Moon | 10 | Luck and Intellect |
| 50 | of The Phoenix | 10 | stat 8 |
| 51 | of The Sky | 10 | Speed, Intellect and stat 8 |
| 52 | of The Stars | 10 | Endurance and Accuracy |
| 53 | of The Sun | 10 | Might and Personality |
| 54 | of The Troll | 15 | Endurance |
| 55 | of The Unicorn | 15 | Luck |
| 56 | Warriors | 5 | Might and Endurance |
| 57 | Wizards | 5 | Intellect and Personality |

`observed` throughout. The stat ids 0..6 are `stats.txt`'s own order —
Might, Intellect, Personality, Endurance, Accuracy, Speed, Luck — and 10..13
are the resistances of Protection answers for.

**Ids 7, 8 and 9 are now named, from four agreeing directions.**
`stats.txt`'s next three rows after Luck are Hit Points, Armor Class and
Spell Points, and the numbering continues into them; which of the three
takes which id is settled by what uses them. The max-hit-point routine folds
in stat **7** and the max-spell-point routine stat **8**. "of Life" and "of
Earth" give 7, which is what those names should give. "of The Eclipse", "of
The Phoenix" and "of The Sky" give 8. And "of The Golem" gives 9 beside
fifteen points of Endurance, which is what an armoured thing should give.
So **7 is Hit Points, 8 is Spell Points, 9 is Armor Class**. `inferred` —
strongly, from four independent alignments, but no routine that names them
on the sheet has been read.

Nothing in the game's tables carries any of this either: the special-bonus
rows give a name, a class and a price, and the sheet is left to say what
changed. StarHaven now adds these on top of the prose it was parsing.

## How this was found, and what it is not

It was reached by chasing what was believed to be the projectile-impact
dispatcher — the place a thrown spell's damage would be decided. It is not
that. The switch is over a worn item's enchantment, not over an object kind,
and the loop belongs to the melee blow rather than to any projectile. The
mistake is recorded in [`spell-switch.md`](spell-switch.md) beside the
earlier one it followed, and where a spell's damage is actually rolled is
still `unknown`.
