# Object descriptor table (`DOBJLIST.BIN`)

Status: **decoded, evidence-backed.** `DOBJLIST.BIN` is the global descriptor
table for loot sprites, projectiles, spell effects, and other sprite objects.
Each claim is tagged `observed`, `inferred`, or `unknown`.

## Container

The `icons.lod` entry uses a 48-byte archive-entry header followed by zlib
data. The inflated block is:

```text
u32 count
ObjectDescriptor records[count]  // 52 bytes each
```

In the examined table, `count = 232` and `4 + 232 × 52 = 12068`, accounting
for the inflated block exactly. `observed`

## Record

| Offset | Size | Type | Field | Status |
| --- | ---: | --- | --- | --- |
| `+0x00` | 32 | char[] | name | observed |
| `+0x20` | 2 | u16 | object id | observed |
| `+0x22` | 2 | u16 | radius | observed | the pick/touch box scan reaches this far |
| `+0x24` | 2 | u16 | height | observed | fed to the collision probe beside the radius |
| `+0x26` | 2 | u16 | flags | observed | see the flag bits below |
| `+0x28` | 2 | u16 | DSFT frame index | observed |
| `+0x2A` | 2 | u16 | lifetime | observed | the expiry compare in the per-frame updater |
| `+0x2C` | 2 | u16 | — | observed | zero in all 232 records |
| `+0x2E` | 2 | u16 | default speed | observed |
| `+0x30` | 3 | u8[3] | trail RGB | observed | nonzero on 64 of 232 records |
| `+0x33` | 1 | u8 | padding | observed |

Every nonzero frame index at `+0x28` selects the first frame of a
`DSFT.BIN` animation group. `observed`

## Flag bits

The flags at `+0x26` are sparse — 112 of 232 records carry 0 — and the union of
every set bit is `0x07fc`. The values that occur, with their counts:

| Flags | Count |
| ---: | ---: |
| `0x0000` | 112 |
| `0x0010` | 1 |
| `0x0014` | 1 |
| `0x003c` | 26 |
| `0x0054` | 2 |
| `0x0074` | 26 |
| `0x013c` | 30 |
| `0x0150` | 1 |
| `0x0154` | 3 |
| `0x0174` | 25 |
| `0x0194` | 1 |
| `0x01d4` | 1 |
| `0x0374` | 1 |
| `0x0574` | 2 |

Most of the recurring bits are now traced through `MM6.exe`'s sprite-object
code — the pick scan, the per-frame lifetime updater, and the indoor
physics routine:

| Bit | Meaning | Evidence |
| ---: | --- | --- |
| `0x01` | invisible: no sprite is drawn | compatibility structure and renderer consumer. `observed` |
| `0x02` | intangible: the pick/touch box scan skips the object | the scan tests it before reaching for the radius. `observed` |
| `0x04` | temporary: the object accumulates elapsed time and expires at the descriptor lifetime | the updater's lifetime block runs only under this bit. `observed` |
| `0x08` | lifetime comes from the selected SFT animation | lifetime setup reads the frame group. `observed` |
| `0x10` | cannot be picked up | pickup consumer rejects the descriptor. `observed` |
| `0x20` | no gravity: skips the fall-and-land block for level flight | the physics routine branches past landing straight to the collision probe. `observed` |
| `0x40` | detonates: the impact handler runs on collision or landing, and again at lifetime expiry | tested in both the landing path and the expiry path. `observed` |
| `0x80` | bounces on landing: vertical speed negated and halved, damped to rest below 10 | the landing path's own arithmetic. `observed` |
| `0x100` | emits the colored trail at `+0x30` | coincides exactly with a nonzero trail RGB — 64 of 64 records, no exceptions either way. `observed` for the coincidence, `inferred` for the naming |
| `0x200` | emits a fire trail | effect consumer. `observed` |
| `0x400` | emits a line trail | effect consumer. `observed` |

The value census reads cleanly under these names: the 112 zero-flag records
are the loot — persistent, tangible, falling, inert — while `0x74`
(temporary, floating, detonating) is a projectile and `0x174` the same with
a trail. The remaining bits distinguish animation lifetime, pickup behavior,
and particle, fire, or line trails.

A map sprite object selects this table by `descriptor_index` and repeats the
descriptor's object id. This two-field join succeeds for all 129 objects
serialized in the 15 outdoor maps. `observed`

Four records have a demonstrated runtime role reached by constant object id
rather than through a map: `firetrap` (811), `coldtrap` (812), `electrap`
(813) and `poistrap` (814) are the chest-trap explosions — the chest-open
path in `MM6.exe` picks one of the four ids uniformly at random and spawns
it at the chest when a trapped chest's disarm roll fails. The object then
lives out its animation, and at expiry the per-frame updater routes exactly
these four ids into the detonation routine that damages the party (see
[`event-tables.md`](event-tables.md), the chest flags word). `observed`

## Invalid-input behavior

The decoder rejects a missing 48-byte header, invalid zlib data, and any count
whose 52-byte records do not account for the inflated block exactly.

## Historical question status

> Audited in the [open-question register](../open-questions.md); the register
> supersedes unresolved hypotheses below.

Both questions are closed by the descriptor consumer names and flag table
above.
