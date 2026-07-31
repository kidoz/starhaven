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

### The tail: a fixed block, then trimmed state

What follows the chest array is 16,256 to 30,434 bytes. It is saved runtime
state — it contains values such as `0x0307CDA0` in long runs, which are heap
pointers from the process that wrote the file — but it is not shapeless.

**It begins with a fixed 200-entry array of 80-byte records.** The evidence is
exact: across the 52 maps, all 56,000 pointer-shaped values in the first 16,000
bytes fall in fields 9 to 16 of an 80-byte stride, and each of those eight
fields holds exactly 7,000 of them — 200 records × the 35 maps whose state
carries pointers at all. Not one lands anywhere else. `observed`

`200 × 80 + 256 = 16,256`, which is exactly the smallest tail any map has, and
ten of the small `zddb*` maps have precisely that. So a minimal indoor payload
is the fixed block plus the same 256-byte trailer the outdoor layout ends with.
`observed`

The decoder requires room for both. Without that the indoor test would accept
any chain that merely fits, since — unlike outdoor — the payload does not end
on a trailer at a computable offset.

### The 200-slot block is the door array

The suspicion recorded below turned out right, and the whole mechanism is in
this one file. Each 80-byte record is a door:

| Offset | Size | Field | Evidence |
| --- | --- | --- | --- |
| +0x00 | u32 | attributes | bit 0: starts open; zero on 754 of 795 doors |
| +0x04 | u32 | id | **the id opcode 15 throws** |
| +0x08 | u32 | timer/state | zero on disk |
| +0x0C | 3×i32 | direction, 16.16 | unit axes: (-1,0,0), (0,0,+1)… |
| +0x18 | u32 | distance | 8..504 world units |
| +0x1C, +0x20 | u32 | open and close speeds | 50..150; read as world units a second — Goblinwatch's 250-unit doors at 85 take three seconds, which is what a stone door sounds like. `inferred` |
| +0x24 | 8×u32 | heap pointers | meaningless on disk |
| +0x44 | u16, u16 | vertex count, face count | |
| +0x48 | u16 | sector count | 0 on every D01 door |
| +0x4C | u16 | state | runtime; 0 closed, 1 open, 2 in transition, 3 at-target |

### The door state machine (`+0x4C`)

The runtime state word at `+0x4C` is decoded from the door-state function in
`MM6.exe` (anchored on its `"Unable to find Door ID: %i!"` assertion and the
200-door / 80-byte-stride scan of the array at `0x5f7d44`). Opcode 15's second
byte is the **requested state** passed to that function. `observed`

| State | Meaning |
| ---: | --- |
| 0 | closed |
| 1 | open |
| 2 | in transition (mid-motion); re-triggering snaps to 3 |
| 3 | at-target / idle (the "already there" skip) |

The transitions are direct in the code: requesting a state already held is a
no-op; requesting the opposite of state 2 snaps it to 3 and zeroes the timer
at `+0x08`. The timer integrates against the open/close speeds at `+0x1C/+0x20`
(`imul` of speed × elapsed), and `0x3c00` is a fixed-point threshold the timer
saturates against. The per-frame vertex translation that actually moves the
door is in the door-update function (`0x44f320`/`0x4552e0`), which walks the
same array. `observed`


The region between the block and the trailer — the part whose size the
paired level declares — is each door's id arrays in slot order, in exactly
the order the eight runtime pointers walk: vertex ids, face ids, sector ids,
per-face texture deltas U and V, then the vertices' X, Y and Z. Per door
that is `4V + 3F + S` u16s, and the sum across a map's doors **is the
declared size to the byte**. `observed`

The X/Y/Z arrays are the shut position: across all 52 maps, **795 doors**
carry 4,067 vertex entries, and **4,067 of 4,067** equal the position the
level ships that vertex at. Opening a door is therefore moving its vertices
from the base along the direction by the distance, which is what the engine
now does — and rebuilds collision, so the doorway really opens. `observed`
for the bases; the snap rather than a timed slide is the engine's.
Reproduce with `ddm_info <map>.dlv --doors`.

### The tail's size is declared — in the other file

The state between the fixed block and the trailer is **exactly as many bytes as
the paired `.blv` says**, in the `u32` at header offset `+0x74`. It agrees on
**all 52 maps**, from 0 bytes on the small `zddb*` levels to 8,028 on the
largest. `observed`

Reproduce with `ddm_info <map>.dlv`, which opens the level and compares:

```text
layout: indoor  sections at 887, tail 22044 bytes (16000 of fixed state, 6044 of the rest)
state bytes: 5788 here, 5788 declared by D01.blv  (agree)
```

**This corrects an earlier revision of this document**, which concluded that the
size was not structural — that "the writer trims the buffer to its populated
extent" — on the evidence that every payload ends 256 or 257 zero bytes after
its last nonzero byte. That observation is correct and the conclusion drawn
from it was wrong. The trailing zeros are the trailer; the size that precedes
them is declared. The mistake was looking for the declaration inside the file
that uses it, and concluding it was absent from both.

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
u16 appearance           // DCHEST.BIN row, 0..7
u16 flags                // bit 0: trapped; bit 1: items placed (runtime)
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
because generation happens when the map is populated. The two leading words
and the grid's fill are decoded below. `observed`

## The chest flags word (`+0x02`)

Both used bits are read and written by the chest-open routine at `0x41e4f0`
in `MM6.exe` (the sole caller sits in the interaction dispatcher; opcode 7 is
the chest opener). The routine addresses the 20-slot chest array at
`0x5e2580` with the 4204-byte stride — `0x5e2580 + 20 × 4204 = 0x5f6df0` is
the loop bound — which is how the record layout above was anchored in the
first place. `observed`

**Bit 0 is the trap.** Set on 1,191 of the 1,340 shipped chests, clear on
149. On open with the bit set, the engine looks up the current map's compiled
`MapStats.txt` row — the lookup at `0x446bd0` walks the 56-byte rows
comparing the **File name** column against the runtime map-name buffer — and
reads the byte the parser filled from the column headed **`Lock` `0-10`**
(not the `Trap` column; the parser's per-column jump table at `0x446b6c`
settles which write feeds the read at `0x41e5bf`). The check, in full:

- The acting character's Disarm Traps is fetched by `0x4853e0`, whose only
  callers are this check. The skill byte lives at player `+0x7d`: low six
  bits the level, bit 6 expert, bit 7 master. The level is doubled by an
  equipped, unbroken **Pendragon** (item 410), **Hades** (item 415), or any
  equipped item bearing special bonus 35 — **"of Thievery"**, whose prose
  says only "Increases chance of Disarming" and turns out to mean ×2. Then
  +4, +6, +8 apply for party checks on ids 25, 26, 51 — `npcprof.txt`'s
  **Tinker, Locksmith and Burglar** hirelings, whose prose promises exactly
  those points to Disarm Traps. The sum is finally multiplied by 2, 3 or 4
  for normal, expert or master. `observed`
- The roll: that value plus `rand % 10` must **exceed five times the map's
  lock difficulty**. Success clears bit 0 and the chest opens with no
  ceremony. A value of zero skips the roll and fails outright. `observed`
- Failure fires the trap: the engine picks uniformly among four
  `DOBJLIST.BIN` entries — 811 `firetrap`, 813 `electrap`, 812 `coldtrap`,
  814 `poistrap` — and spawns the chosen object at the chest's world
  position, derived from the clicked target (decoration, indoor face, or
  outdoor model face), with a sound and a portrait reaction. Bit 0 is
  cleared here too: a chest trap fires once. `observed`
- The detonation is traced to the end: when the spawned object's animation
  expires, the per-frame updater special-cases the four trap ids into the
  detonation routine at `0x4309d0`. Within 1,024 units of the party it rolls
  **once, for everyone: 5 plus the map's `Trap` column of d20s** — the
  column `MapStats.txt`'s own header annotates `"D20's"` — with the element
  by id (fire 811 → type 2, electric 813 → 3, cold 812 → 4, poison 814 → 5).
  Each member then rolls a Perception dodge — the packed skill byte `P` read
  raw, dodging when `rand % (P + 20) > 20`, speaking line 33 on the leap —
  and the rest take the roll through their resistance inside the
  receive-damage routine. See [`text-tables.md`](text-tables.md), "The lock
  and trap columns, cashed". `observed`
- A map with no `MapStats.txt` row skips the check entirely; the chest opens
  quietly with the bit still set. `observed`

**This corrects the previous revision's reading** of the flag as a
"per-chest variant/placement toggle": the clustering of flag=0 on the
metal-chest appearances is just which chests the designers left untrapped.

**Bit 1 is "items placed", and ships zero.** On an open with bit 1 clear the
routine at `0x41e3b0` runs first (its assertion cites `button.cpp`, so this
is original chest code): it reads the appearance word, asserts it below 8,
takes the grid dimensions from two 8-entry tables at `0x4bd18c`/`0x4bd1ac` —
**every entry is 9, so every chest is a 9×9 grid** and the 140-cell array is
capacity — shuffles the cell order, lays each nonzero item id into free
cells, and sets bit 1 so placement never reruns. That is why all 187,600
grid cells ship zero. `observed`

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

- The two fixed 968-byte blocks (`0x008`, `0x3D0`) and the 256-byte trailer
  are **entirely zero on every shipped map** (verified across all sampled
  `.ddm` payloads) — they are runtime state buffers shipped empty, the same
  pattern as the monsters' zero tails. `968` is an allocation size, not a
  record count; the engine fills these at runtime. `observed`
- The chest's first u16 is read: 0..7 on every shipped chest, the
  `DCHEST.BIN` row whose last field numbers the `CHEST01`..`CHEST08`
  screens — the chest's appearance. The 140 i16 grid entries are read
  too, and the answer is silence: **zero on all 187,600 cells across the
  1,340 shipped chests** — the runtime loot layout, filled on first open
  by the placement routine (see the flags-word section). Reproduce with
  `ddm_info <map>`, which prints the nonzero count.
- The indoor prefix's 883 bytes are **entirely zero on every shipped map**
  (verified across D01, D07, D11, zddb01, CD1) — a fixed runtime scratch
  buffer shipped empty, like the outdoor 968-byte blocks. The "unaligned"
  oddity is simply that **883 is prime**: it is an allocation constant, not a
  `count × stride` array, so no record stride divides it. `observed`
- The door attribute word is resolved. Measured across all 795 doors of
  the 52 indoor maps it is **zero on 754, one on 41**, and bit 0 is
  **"starts open"**: the executable's indoor loader (the same function
  that asserts `"No map info for %s found in 'Map Stats.txt'"`) walks the
  door array and, where bit 0 is set, writes state 1 (open) with the timer
  saturated at `0x3c00` — the door stands fully open before the first
  frame. Every other door maps shut to at-target, and every door's
  attribute word is then overwritten with 2 in memory, a runtime marker.
  This also explains the vertex census below: the geometry ships at the
  shut position on 4,067 of 4,067 vertices, and the 41 marked doors are
  swung open by the loader, not by the file. D07's concentration — 20 of
  the 41, half its own doors — is level design: half its doorways begin
  open. `observed` The earlier refutations (not texture, not travel axis,
  not one map's quirk) stand consistent with this reading. The second
  count word's high half is closed: it **duplicates the vertex count**,
  the low half of the word before it, on 795 of 795 doors. `observed`
  (`ddm_info <map> --doors` prints each door's attribute word.)
- The chest's remaining u16 (`+0x02`, beside the appearance) is now traced:
  it is the flags word — bit 0 the trap, bit 1 runtime "items placed" — and
  has its own section above. The census that read it as a
  "variant/placement toggle" was measuring which chests ship untrapped.
  `observed`
