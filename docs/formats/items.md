---
title: "Items and item instances"
summary: "Design-table joins, serialized item layout, and generation behavior for Might and Magic VI items."
doc_type: reference
status: partial
last_updated: 2026-08-01
tags:
  - mm6
  - items
  - inventory
  - text-tables
---
# Items and item instances

Status: **draft, evidence-backed.** `ITEMS.TXT` defines concrete item ids,
while `RNDITEMS.TXT`, `STDITEMS.TXT`, and `SPCITEMS.TXT` describe generation
weights and enchantments stored in a 28-byte item instance.

## Scope

This page covers item-definition tables, serialized item instances, random
generation weights, standard and special bonuses, equipment types, and the
shared generation sequence. Inventory presentation and paperdoll placement are
covered by [`paperdoll.md`](paperdoll.md).

## `ITEMS.TXT`

The table contains 581 item ids, exactly the contiguous range 0–580. Three
rows are unnamed placeholders, so 578 have names. Concrete ids are direct and
zero-based: id *N* selects table entry *N*. Id 0 is an empty item instance.
`observed`

| Column | Shipped heading | Typed field | Status |
| ---: | --- | --- | --- |
| 0 | `Item #` | id | observed |
| 1 | `Pic File` | picture | observed |
| 2 | `Name` | name | observed |
| 3 | `Value` | value | observed |
| 4 | `Equip Stat` | equip stat | observed |
| 5 | `Skill Group` | skill group | observed |
| 6–7 | `Mod1`, `Mod2` | modifiers 1 and 2 | observed |
| 8 | `material` | material | observed; numeric codes, `Artifact`, or `Relic` |
| 9 | `ID/Rep/St` | `id_rep_st` | observed |
| 10 | `Not identified name` | unidentified name | observed |
| 11–14 | `Sprite Index`, `Shape`, `Equip X`, `Equip Y` | display fields | observed |
| 15 | `Notes` | notes | observed |

The parser keeps ambiguous headings such as `ID/Rep/St` neutral instead of
assigning gameplay semantics that the table alone does not prove.

## Serialized item instance

Sprite objects and chests use the same 28-byte item instance. The original
generator clears seven 32-bit words before filling these fields. `observed`

| Offset | Size | Type | Field | Status |
| --- | ---: | --- | --- | --- |
| `+0x00` | 4 | i32 | `item_id` | observed; 0 empty, positive concrete id, chest values −1…−6 request random placeholder classes 1…6 |
| `+0x04` | 4 | i32 | standard-bonus selector or potion power | observed; standard selector is 1-based, alternate potion meaning corroborated |
| `+0x08` | 4 | i32 | standard-bonus strength | observed |
| `+0x0C` | 4 | i32 | special-bonus selector or gold amount | observed; special selector is 1-based, alternate gold meaning corroborated |
| `+0x10` | 4 | i32 | charges | observed; filled for generated wands |
| `+0x14` | 4 | u32 | flags | observed; bit 0 identified, bit 1 broken |
| `+0x18` | 1 | u8 | equipped/body slot | inferred |
| `+0x19` | 3 | bytes | reserved | observed zero in serialized outdoor data |

The field meanings come from independent executable behavior:

- the item generator allocates and clears exactly 28 bytes, then writes the
  selectors, bonus strength, charges, and initial flags at the offsets above;
- the value calculation reads the standard and special selector fields;
- the item tooltip reads the selectors, strength, and charges;
- identify and broken-item paths respectively test flag bits 0 and 1.

The `+0x18` byte is initialized to zero by the examined MM6 paths. Its
equipment-slot meaning agrees with the compatible MM7/8 structure but has not
yet been isolated in MM6 executable behavior, so it remains `inferred`.

## Random base-item weights: `RNDITEMS.TXT`

`RNDITEMS.TXT` has 400 typed entries with direct ids 0–399. Each entry carries
a picture label and six weights, one for every treasure level. The id is the
join key; picture labels are descriptive and do not consistently match
`ITEMS.TXT` spelling or even selection. `observed`

A negative chest item id is a deferred-generation **class**, not a final
treasure level. The executable combines its absolute value (1…6) with the
map's compiled `MapStats` treasure class (0…6), then makes one uniform draw
over this inclusive range:

| Placeholder class | Map 0 | Map 1 | Map 2 | Map 3 | Map 4 | Map 5 | Map 6 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 |
| 2 | 1 | 1–2 | 2 | 2 | 2 | 2 | 2 |
| 3 | 1–2 | 2 | 2–3 | 3 | 3 | 3 | 3 |
| 4 | 2 | 2–3 | 3 | 3–4 | 4 | 4 | 4 |
| 5 | 2–3 | 3 | 3–4 | 4 | 4–5 | 5 | 5 |
| 6 | 3 | 3–4 | 4 | 4–5 | 5 | 5–6 | 6 |

The resulting level selects the corresponding `RNDITEMS.TXT` weight column.
`chest_treasure_level_range` and `roll_chest_treasure_level` expose this
mapping directly. `observed`

The weight totals recomputed from ids 1–399 are:

| Treasure level | Total weight |
| ---: | ---: |
| 1 | 437 |
| 2 | 827 |
| 3 | 959 |
| 4 | 970 |
| 5 | 928 |
| 6 | 805 |

Id 0 is excluded as the empty item. Selection takes the shared random
function's remainder modulo the total and walks cumulative weights. The base
item path compares `cumulative >= remainder` without first converting the
remainder to 1-based form. Consequently the first candidate receives the
roll-zero outcome in addition to its ordinary weight, and the last candidate
loses one outcome. A zero-weight first candidate can win roll zero. This
off-by-one behavior is preserved by the deterministic selection helper.
`observed`

## Bonus probability branches

The `RNDITEMS.TXT` footer supplies three percentage arrays:

| Treasure level | Equipment standard | Equipment special | Weapon special |
| ---: | ---: | ---: | ---: |
| 1 | 0% | 0% | 0% |
| 2 | 40% | 0% | 0% |
| 3 | 40% | 10% | 10% |
| 4 | 40% | 15% | 20% |
| 5 | 40% | 20% | 30% |
| 6 | 75% | 25% | 50% |

The loader reads these three rows into separate six-element arrays.
The generator then branches by the base item's compiled equipment type:

- weapon types 0–2 roll only against `Weapon special`;
- equipment types 3–11 make one percentile roll: values below `standard`
  choose a standard bonus, and the immediately following `special` interval
  chooses a special bonus;
- type 12 is a wand and receives charges instead of an enchantment roll;
- other types take no standard or special bonus in this path.

For example, treasure-level 3 equipment rolls 0–39 standard, 40–49 special,
and 50–99 no bonus. Treasure-level 3 weapons roll 0–9 special and 10–99 no
bonus. `observed`

## Standard bonuses: `STDITEMS.TXT`

The 14 standard-bonus rows have no explicit numeric column. Their serialized
selectors are 1-based in table order. Every row contains a stat, a name
suffix, and weights for nine item types: armour, shield, helm, belt, cape,
gauntlets, boots, ring, and amulet. `observed`

The table also defines the inclusive standard-bonus strength range for each
treasure level:

| Treasure level | Minimum | Maximum |
| ---: | ---: | ---: |
| 1 | 0 | 0 |
| 2 | 1 | 5 |
| 3 | 3 | 8 |
| 4 | 6 | 12 |
| 5 | 10 | 17 |
| 6 | 15 | 25 |

The generator sums each item-type column before selection. The shipped totals,
in table-column order, are `115, 70, 115, 70, 140, 155, 110, 140, 140`.
Like base-item selection, standard selection uses a zero-based remainder with
an inclusive cumulative comparison. This gives the first standard-bonus row
the roll-zero outcome even when its weight for that item type is zero.
`observed`

## Special bonuses: `SPCITEMS.TXT`

The 59 special-bonus rows likewise use 1-based selectors in table order.
Each row stores an effect, a name affix, weights for twelve item types, a
value expression, treasure class, and description. The item types add
one-handed, two-handed, and missile weapons to the nine equipment categories
used by standard bonuses. `observed`

The treasure-class footer determines which rows participate:

| Treasure level | Eligible classes |
| ---: | --- |
| 1–2 | none |
| 3 | A, B |
| 4 | A, B, C |
| 5 | B, C, D |
| 6 | D |

Special selection first removes ineligible and zero-weight rows, sums the
remaining item-type weights, and uses a one-based cumulative draw. Unlike base
and standard selection, it therefore has no roll-zero off-by-one quirk.
`observed`

## Compiled equipment types

The `Equip Stat` strings in `ITEMS.TXT` are compiled case-insensitively into
the numeric types used by generation:

| Value | Table labels | Generation behavior |
| ---: | --- | --- |
| 0 | `Weapon`, `Weapon1or2` | one-handed/special-weapon column |
| 1 | `Weapon2` | two-handed/special-weapon column |
| 2 | `Missile` | missile/special-weapon column |
| 3–11 | `Armor`, `Shield`, `Helm`, `Belt`, `Cloak`, `Gauntlets`, `Boots`, `Ring`, `Amulet` | standard and special equipment columns |
| 12 | `WeaponW` | wand charges |
| 13–18 | `Herb`, `Bottle`, `Sscroll`, `Book`, `Mscroll`, `Gold` | no enchantment through this branch |
| 19 | anything else | other/unknown |

Values 0–11 index `SPCITEMS.TXT` directly. Values 3–11 index
`STDITEMS.TXT` after subtracting three. `observed`

The `Skill Group` strings compile case-insensitively into a second byte used
by restricted generation:

| Value | Skill group |
| ---: | --- |
| 0–7 | Club, Staff, Sword, Dagger, Axe, Spear, Bow, Mace |
| 8–12 | Blaster, Shield, Leather, Chain, Plate |
| 13 | any other label (`Misc`) |

The compiled `Mod2` byte supplies a wand's base charges. Generation adds
`random % 6`, so a wand receives `Mod2…Mod2+5` charges. The compiled
`ID/Rep/St` byte also controls the initial identified flag: zero starts
identified, while a nonzero value starts unidentified. `observed`

## Shared random sequence

MM6 owns one process-wide 32-bit random state. Startup calls Windows
`GetTickCount` once and passes that millisecond value to the only state setter.
Every draw then performs unsigned 32-bit wraparound:

```text
state = state * 0x343fd + 0x269ec3
result = (state >> 16) & 0x7fff
```

The result is therefore in `0…32767`. This is the familiar Microsoft C
runtime LCG formula, but the executable carries its own state and functions.
`observed`

Starhaven exposes this as `Mm6Random(seed)`. It deliberately requires an
explicit seed: choosing wall-clock time is application policy, while explicit
state makes saved games, tests, and compatibility traces reproducible.

### Unrestricted item-generation choreography

The deferred chest-placeholder path uses the following calls in exact order:

1. Draw `random % 30` for an artifact candidate at **every** treasure level.
   Levels 1–5 ignore this candidate but still consume the draw.
2. At treasure level 6 only, draw `random % 100`. Values 0–4 award candidate
   item id `400 + candidate` if that artifact has not appeared and fewer than
   13 of the 30 candidates have appeared. Success marks it found and returns
   immediately.
3. Otherwise draw `random % baseWeightTotal` and select the base item.
4. Continue according to its compiled equipment type:

| Selected type/path | Additional draws |
| --- | --- |
| weapon, zero special chance | none |
| weapon, nonzero special chance | percentile; special selector only on success |
| equipment, zero standard chance | none |
| equipment, no bonus | one percentile |
| equipment, standard bonus | percentile, standard selector, strength |
| equipment, special bonus | percentile, special selector |
| wand | charges modulo 6 |
| other | none |

Consequently an ordinary non-level-6 item consumes at least two calls, not
one. A failed level-6 artifact attempt consumes three calls before any
type-specific work; a successful artifact consumes exactly two. Zero chance
entries skip their percentile draw. All modulo operations use the 15-bit
result above. `observed`

`generate_random_item` reproduces this unrestricted path with explicit random
and artifact state. It is safe on malformed synthetic tables: instead of the
original executable's possible divide-by-zero, it returns a typed error when a
required weight total or strength range is unavailable.

### Restricted generation

The generator's second argument is a selector. Zero takes the unrestricted
path above. Values 1…19 match compiled equipment type `selector − 1`; values
20…43 are named aliases:

| Selector | Filter | Selector | Filter |
| ---: | --- | ---: | --- |
| 20 | equipment: weapon | 32 | skill: chain |
| 21 | equipment: armor | 33 | skill: plate |
| 22 | skill: misc | 34 | equipment: shield |
| 23 | skill: sword | 35 | equipment: helm |
| 24 | skill: dagger | 36 | equipment: belt |
| 25 | skill: axe | 37 | equipment: cloak |
| 26 | skill: spear | 38 | equipment: gauntlets |
| 27 | skill: bow | 39 | equipment: boots |
| 28 | skill: mace | 40 | equipment: ring |
| 29 | skill: club | 41 | equipment: amulet |
| 30 | skill: staff | 42 | equipment: wand |
| 31 | skill: leather | 43 | equipment: spell scroll |

Restricted generation considers ids 1…399 that match the filter and keeps
zero-weight matches in the candidate list. It skips both artifact calls. A
positive total consumes one weighted-selector draw and preserves the same
inclusive comparison as unrestricted base selection. A zero total consumes no
selector draw and chooses the first matching row; if no row matches, the
original zero-initialized candidate yields item id 0. Enchantment, wand-charge,
and initial-identification behavior is then shared with the unrestricted path.
`observed`

### Generator callers

All nine executable call sites agree on the three-argument signature
`(treasureLevel, selector, outputItem)`:

| Call address | Role | Inputs before the call |
| --- | --- | --- |
| `0x4218e0` | monster loot | monster-configured treasure level and selector after its loot-chance roll |
| `0x43e055` | give-item event | two event operand bytes; result goes to party inventory |
| `0x4528c2`, `0x4857d3` | mirrored character setup | level 2, ring-category selector 40 |
| `0x456197` | placed-object population | level from its record and a prior random selector in 20…43 |
| `0x4564f7`, `0x456651` | chest population | resolved level and unrestricted selector 0 |
| `0x49fce2`, `0x49fee2` | mirrored inventory/restock paths | caller level and a prior random choice of cloak 37 or boots 39 |

This establishes both non-chest consumers of restricted generation and the
random calls that occur before the generator receives control. `observed`

### Chest population

For each negative chest placeholder, population first draws `random % 5 + 1`
attempts and resolves the final treasure level through the 6×7 table above.
Additional attempts scan forward for an empty slot. Each has a fresh
percentile: 0…19 skips the slot, 20…59 generates an unrestricted item, and
60…99 generates gold. Gold uses one further draw:

| Level | Gold item id | Amount |
| ---: | ---: | --- |
| 1 | 197 | 50…100 |
| 2 | 197 | 100…200 |
| 3 | 198 | 200…500 |
| 4 | 198 | 500…1000 |
| 5 | 199 | 1000…2000 |
| 6 | 199 | 2000…5000 |

The function also consumes one apparently unused percentile draw on entry.
The original placeholder's primary item/gold/clear decision reads a stack
local that static analysis does not show initialized before its first use; on
later iterations it can retain the latest additional-attempt percentile. This
quirk prevents a defensible deterministic implementation of the complete
chest-population loop until an isolated runtime trace establishes the actual
incoming stack behavior. The level resolver and all generator paths after that
decision are independently specified. `observed`, primary decision `unknown`

## Joins in shipped outdoor maps

All 129 sprite objects across the 15 outdoor maps have a valid concrete join:

```text
ITEMS[object.contained_item.item_id]
```

They use ids 160–162 (the three herb pictures), 447 (`tarot`), 463 (`horsshu`)
and 465 (`thybone`). No adjustment is involved, and the remaining item-state
fields are zero in these placed instances. `observed`

The shipped chest templates contain 202 nonempty slots:

- 191 are random placeholder classes −1…−6;
- 11 are fixed positive ids, all of which join directly to `ITEMS.TXT`;
- every other state field in these template slots is zero because random
  generation is deferred until the map is populated.

The chest boundary is exact: a 4-byte field, 140 item instances, and 140
signed 16-bit grid entries total 4204 bytes. `observed`

## Reproducing the lookups

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/data_info --items 160
./buildDir/data_info --random-items       # weights and footer chances
./buildDir/data_info --standard-bonuses  # ranges and item-type totals
./buildDir/data_info --special-bonuses   # class-filtered totals
./buildDir/data_info --generate-item 6:1    # unrestricted level:seed probe
./buildDir/data_info --generate-item 3:1:23 # restricted level:seed:type probe
./buildDir/ddm_info outb2.ddm
```

`ddm_info` reports sprite-object joins, fixed chest items, and placeholder
counts by class.

## Historical question status

> Audited in the [open-question register](../open-questions.md); the register
> supersedes unresolved hypotheses below.

- Meanings of flag bits above bit 1 and direct MM6 evidence for `+0x18`.
- The complete item-type-dependent interpretation of overloaded fields.
- Runtime provenance of the stale/uninitialized primary chest percentile
  stack local and its first-iteration behavior.
- The higher-level purpose of the mirrored cloak/boots inventory paths.
- How the abbreviated modifier and material columns in `ITEMS.TXT` drive
  gameplay.

## Inventory icons

Every item row names a picture, and all **229** distinct picture names resolve
to an entry in `icons.lod` — the 583 items share them. `observed`

The icons are **9 to 140 pixels wide and 12 to 289 tall**, and neither
dimension is a multiple of anything. `observed` So how many cells of an
inventory grid an item occupies is not something the art states, and no table
states it either: the `Shape`, `Equip X` and `Equip Y` columns do not give a
width and a height in cells. `observed`

StarHaven uses a 14 x 9 grid of 32-pixel cells and lets an icon overhang a cell
by up to 14 pixels before it claims the next one. That threshold is chosen, not
read: it is what makes all 229 icons fit a pack that size, where rounding up
outright leaves one that could never be carried. `inferred`

## What the generator takes from the tables, and what it does not

Worth stating plainly, because the two are easy to confuse.

**From the tables.** `RNDITEMS.TXT` carries the percentile split itself —
three rows giving, per treasure level, how often a rolled item takes a
standard bonus, a special one, or neither, with a separate row for weapons.
The engine reads all of it. So "how often does a magic item appear" was never
invented.

**Not from the tables.** Which *class letter* a special bonus may be drawn
from at a given treasure level — level 3 from A and B, level 4 from A, B and
C, level 5 from B, C and D, level 6 from D alone — is this engine's ladder
and nothing else's. The special-bonus table gives every row a class letter
and a price and says nothing about levels.

That ladder was hunted in the executable and **not found**: neither a bitmask
of four class bits per level nor a low/high pair per level sits in either
data section, and the generator routine that would hold the comparisons
inline was not located. It stays `inferred`, and is now marked as such where
it lives.

This is the third consecutive "find the one routine that does X" search to
come back empty — after the experience threshold and the AI's decision
switch — and the pattern is worth naming: the searches that have succeeded
lately all started from a *field* or a *table* and worked outward to the code
that touches it, while the ones that failed started from a behaviour and went
looking for the code that implements it.

## The runtime item row, and a negative on the economy

The way into the economy was to start from `2DEvents.txt`'s **`Val`** column —
the price factor the engine reads — and find what multiplies by it. That did
not converge, and this is the **fourth** "find the one routine that does X"
search to come back empty. It is recorded rather than retried.

What was ruled out: no routine in the executable multiplies an item's value
by a runtime float in anything resembling a counter. Every register-relative
float multiply in the image belongs to geometry — distances, velocities,
projections. If a price is computed from the factor at all, it is not
computed that way.

What the search did establish, field-first, is a partial map of the
**runtime item table at `0x560c14`, 40 bytes a row** — the table the AI, the
recovery routine and the special-damage walk all index by an item id:

| Offset | Refs | What touches it |
| --- | --- | --- |
| `+0x00` | 36 | a **pointer to the item's name** |
| `+0x08` | 8 | a second string pointer, passed to `sprintf` |
| `+0x14` | 49 | the **equip type**, tested against 4 for a shield |
| `+0x15` | 25 | the **skill group**, indexing the recovery table |
| `+0x16`, `+0x17`, `+0x18` | 11, 7, 16 | further bytes the stat getters read |
| `+0x21`, `+0x22`, `+0x24` | 9, 7, 7 | unread |

`observed` for the offsets and their reference counts; `+0x14` and `+0x15`
are named from the routines that use them, the rest are `unknown`.

**The pattern, restated.** Four searches that began from a *behaviour* — the
experience threshold, the AI's decision switch, the item generator's class
ladder, and now the economy — have all come back empty. Four that began from
a *field or a table* — the buff arrays, the class tables, the condition run,
the party record — all landed. The economy should be attempted again only
from a field: find where a shop's parsed row lives at runtime, and read
outward from it.

## The economy, attempted from the field — a specific negative

The rule said to attempt the economy again from a field rather than a
behaviour, by finding where a shop's parsed row lives at runtime. Done, and
it fails for a reason that is worth stating precisely rather than as "not
found".

- **The table cannot be reached by name.** The strings `2DEvents`,
  `2devents`, `events.lod` and `Events.lod` **do not appear anywhere in the
  executable**. Whatever opens the file builds its name at runtime, so the
  usual way in — follow the filename to its reader, follow the reader to the
  table — is closed.
- **It cannot be reached by its indexing.** The runtime tables that *can* be
  found that way are enumerable: items at `0x560c14` with a stride of 40,
  monsters at `0x56c1c0` with 72, and two more at `0x55f628` (stride 8) and
  `0x55e5d0` (scale 4, 290 references — a lookup, not a row table). None has
  the shape of `2DEvents`' rows.
- **It cannot be reached by its arithmetic.** The earlier attempt already
  established that no routine multiplies an item's value by a runtime float.

So the negative is now specific: **the shop table is not findable by name,
by shape, or by arithmetic**, and the only remaining route — find the parser
— is behaviour-first, which is the approach that has failed four times. The
economy stays this engine's own, and is recorded as blocked rather than
merely unattempted.
