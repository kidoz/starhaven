# Items and item instances

Status: **draft, evidence-backed.** `ITEMS.TXT` defines concrete item ids,
while `RNDITEMS.TXT`, `STDITEMS.TXT`, and `SPCITEMS.TXT` describe generation
weights and enchantments stored in a 28-byte item instance.

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
| `+0x00` | 4 | i32 | `item_id` | observed; 0 empty, positive concrete id, chest values −1…−6 request random treasure levels 1…6 |
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

A negative chest item id is a deferred-generation placeholder:

```text
-1 => treasure level 1
-2 => treasure level 2
...
-6 => treasure level 6
```

The executable selects a positive base item using the corresponding
`RNDITEMS.TXT` weight column when the chest is populated at runtime.
`observed`

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

## Special bonuses: `SPCITEMS.TXT`

The 59 special-bonus rows likewise use 1-based selectors in table order.
Each row stores an effect, a name affix, weights for twelve item types, a
value expression, treasure class, and description. The item types add
one-handed, two-handed, and missile weapons to the nine equipment categories
used by standard bonuses. `observed`

## Joins in shipped outdoor maps

All 129 sprite objects across the 15 outdoor maps have a valid concrete join:

```text
ITEMS[object.contained_item.item_id]
```

They use ids 160–162 (the three herb pictures), 447 (`tarot`), 463 (`horsshu`)
and 465 (`thybone`). No adjustment is involved, and the remaining item-state
fields are zero in these placed instances. `observed`

The shipped chest templates contain 202 nonempty slots:

- 191 are random placeholders −1…−6;
- 11 are fixed positive ids, all of which join directly to `ITEMS.TXT`;
- every other state field in these template slots is zero because random
  generation is deferred until the map is populated.

The chest boundary is exact: a 4-byte field, 140 item instances, and 140
signed 16-bit grid entries total 4204 bytes. `observed`

## Reproducing the lookups

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/data_info --items 160
./buildDir/data_info --random-items 160
./buildDir/data_info --standard-bonuses 1
./buildDir/data_info --special-bonuses 16
./buildDir/ddm_info outb2.ddm
```

`ddm_info` reports both sprite-object item joins and fixed/random chest totals.

## Open questions

- Meanings of flag bits above bit 1 and direct MM6 evidence for `+0x18`.
- The complete item-type-dependent interpretation of overloaded fields.
- How the generation algorithm combines the table footer probabilities with
  base-item and enchantment weights.
- How the abbreviated modifier and material columns in `ITEMS.TXT` drive
  gameplay.
