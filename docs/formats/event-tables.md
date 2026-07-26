# Event sections (`.ddm` / `.dlv`)

Status: **draft, evidence-backed.** This documents the counted actor,
sprite-object, and chest arrays in a decompressed event payload, outdoor and
indoor. It supersedes the earlier interpretation of `0x798` as an actor record.

## Scope

Both layouts: the 15 outdoor `.ddm` files and the 52 indoor `.dlv` files.

## Layout

| Payload offset | Size | Field | Status |
| --- | ---: | --- | --- |
| `0x000` | 8 | saved outdoor state | observed |
| `0x008` | 968 | fixed block | observed |
| `0x3D0` | 968 | fixed block | observed |
| `0x798` | 4 | `actor_count` | observed |
| `0x79C` | `actor_count × 548` | actor records | observed |
| next | 4 | `sprite_object_count` | observed |
| next | `sprite_object_count × 100` | sprite-object records | observed |
| next | 4 | `chest_count` | observed |
| next | `chest_count × 4204` | chest records | observed |
| next | 256 | fixed trailer | observed |

The counts, strides, and trailer account for every byte in all 15 shipped
outdoor payloads. Actor counts range from 0 to 58, sprite-object counts from 0
to 42, and `chest_count` is 20 in every examined map.

The executable's outdoor loader reads the count at `0x798`, copies
`actor_count × 548`, then repeats the same count-and-copy sequence for
100-byte sprite objects and 4204-byte chests. `observed`

## Indoor layout (`.dlv`)

Indoor files carry **the same three sections, in the same order, with the same
strides**. Only the fixed prefix differs, and it is shorter:

| Payload offset | Size | Field | Status |
| --- | ---: | --- | --- |
| `0x000` | 883 | fixed prefix | observed |
| `0x373` | 4 | `actor_count` | observed |
| `0x377` | `actor_count × 548` | actor records | observed |
| next | 4 | `sprite_object_count` | observed |
| next | `sprite_object_count × 100` | sprite-object records | observed |
| next | 4 | `chest_count` | observed |
| next | `chest_count × 4204` | chest records | observed |
| next | rest of payload | saved runtime state | observed |

The prefix is **unaligned** — the count sits at 883, not a multiple of four —
which is unusual enough to be worth stating twice. `observed`

All 52 indoor payloads decode: 76 actors, 219 sprite objects, and `chest_count`
of exactly 20 on every map, matching the outdoor files. `observed`

### Why the sections are believed

Arithmetic alone does not settle the offset, because the indoor payload does
not end on a fixed trailer. Content does:

- **All 76 actor positions fall inside the paired `.blv`'s own vertex
  extents.** `observed`
- Actor names read as designers wrote them: `"Snergle"` in Snergle's Caverns,
  `"Queen Spider"` in the Abandoned Temple, and the developers' own names —
  `"Trip Hawkins"`, `"Lisa Whitman"` — in `znwc.blv`, the New World Computing
  easter-egg level. `observed`
- **Every one of the 219 sprite objects carries an item id that resolves in
  `ITEMS.TXT`**, and 269 of 269 fixed chest items resolve too, with no invalid
  negative placeholder. `observed`

### The tail is state, not a section

What follows the chest array is 16,256 to 30,434 bytes and does **not** close
arithmetically. It is saved runtime state: it contains values such as
`0x0307CDA0` in long runs — heap pointers from the process that wrote the file.
Ten of the small `zddb*` maps share a floor value of exactly 16,256 bytes. No
model of its size is offered, and the decoder records its length rather than
requiring anything of it. `observed`

### Distinguishing the two layouts

An outdoor payload also chains from the indoor offset, but reads three zero
counts there. The reverse never happens: **no indoor payload satisfies the
outdoor layout**, because that test demands an exact 256-byte trailer, and
0 of 52 provide one. So a reader must try outdoor first; trying indoor first
would silently report a populated outdoor map as an empty indoor one.
`observed`

### Two maps disagree with the object table

Sprite objects repeat their descriptor's object id, and the pair agrees for 203
of the 219 indoor objects. The 16 exceptions are all in `zddb05.blv` and
`zddb07.blv` — two of the unfinished maps `MapStats.txt` marks `"pending"` —
and they are wrong in one uniform way: the descriptor index is **exactly one
too high**. Where `DOBJLIST.BIN` pairs descriptor 106 with object id 128,
`zddb07` writes descriptor 107 with object id 128. Their object ids and item
ids remain mutually consistent, so a reader should prefer the object id when
the two disagree. `observed`

## Chest record boundary

The chest body is now structurally bounded:

```text
u32 unknown
ItemInstance items[140]  // 28 bytes each
i16 grid[140]
```

This accounts for all 4204 bytes exactly. The executable addresses the item
array at chest `+4` with a 28-byte stride and the grid at chest `+3924`.
The leading item word is a direct `ITEMS.TXT` id; see
[`items.md`](items.md) when positive. Values −1…−6 instead request deferred
random generation in placeholder classes 1…6. At population time the
executable combines that class with the map's 0…6 treasure class through the
documented 6×7 level-range table. Across the 15 shipped outdoor maps, 191 of
202 nonempty chest slots are such placeholders; all 11 fixed positive ids join
to `ITEMS.TXT`. The item-state fields are zero in these serialized templates
because generation happens when the map is populated. Meanings of the chest's
first word and grid values remain open. `observed`

## Correction to the earlier interpretation

The `u32` at `0x798` is the actor count, not a field in actor record zero. The
first actor begins four bytes later at `0x79C`. Treating the count as a record
field produced an apparent first-record “type” equal to the map's actor count
and shifted every decoded actor field by four bytes.

## Invalid-input behavior

The decoder rejects an outdoor layout when:

- the payload cannot reach the actor count;
- any count-times-stride section exceeds the payload or overflows;
- a following count cannot be read;
- the bytes after the chest array are not exactly the 256-byte trailer.

## Open questions

- Meanings of the two fixed 968-byte blocks and the 256-byte trailer.
- The first chest word and meanings of its 140 grid entries.
- The indoor prefix's 883 bytes, and why the count lands unaligned.
- The structure of the indoor tail beyond "saved runtime state".
