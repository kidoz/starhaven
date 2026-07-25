# Items and item instances

Status: **draft, evidence-backed.** `ITEMS.TXT` defines the stable item ids
stored in placed outdoor objects and chest item slots.

## `ITEMS.TXT`

The table contains 581 item ids, exactly the contiguous range 0–580. Three
rows are unnamed placeholders, so 578 have names. Unlike monster ids, item ids
are direct and zero-based: id *N* selects table entry *N*. `observed`

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

Reproduce a typed lookup with:

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/data_info --items 160
```

## Serialized item instance

An item instance is 28 bytes. Its leading `u32` is the direct `ITEMS.TXT` id;
the meanings of the remaining six words are not yet independently verified.

| Offset | Size | Type | Field | Status |
| --- | ---: | --- | --- | --- |
| `+0x00` | 4 | u32 | `item_id` | observed |
| `+0x04` | 24 | u32[6] | item state | unknown |

Three independent observations establish the size and id:

- The executable walks chest items with a 28-byte stride. `observed`
- A chest is exactly a 4-byte field, 140 item instances, then 140 signed
  16-bit grid entries: `4 + 140 × 28 + 140 × 2 = 4204`. `observed`
- When an outdoor sprite object loads, the executable reads its item instance
  at object `+0x24`, multiplies the leading value by the 40-byte compiled item
  descriptor stride, and derives the object's type from that descriptor.
  `observed`

## Join from placed outdoor objects

All 129 serialized sprite objects across the 15 shipped outdoor maps have a
valid direct join:

```text
ITEMS[object.contained_item_id]
```

The objects use six ids: 160–162 (the three herb pictures), 447 (`tarot`), 463
(`horsshu`) and 465 (`thybone`). No `+1` or `-1` adjustment is involved.
The other 24 item-state bytes are zero in all 129 serialized instances; this
does not establish their runtime meanings. `observed`

`ddm_info` reports `item_joins` so the relationship can be checked against a
user-owned install.

## Open questions

- Meanings of the six item-state words at `+0x04..+0x1B`.
- How the table's abbreviated modifier and material columns drive gameplay.
- How random-item and enchantment tables construct an item instance.
