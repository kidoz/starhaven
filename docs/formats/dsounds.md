# Sound table (`DSOUNDS.BIN`, Might and Magic VI)

Status: **verified.** The table that turns a sound id in the game's data into
an entry of the sound archive. Each claim is tagged `observed`, `inferred`, or
`unknown`.

## Scope

Covers `DSOUNDS.BIN`, its join to `Sounds/Audio.snd`, the numbering of monster
sound sets, and the field in `DDECLIST.BIN` that names a decoration's ambient
sound.

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product and edition | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Entry | `DSOUNDS.BIN` in `data/icons.lod`, stored 8,991 bytes, inflating to 151,764 `observed` |
| Records | 1,355, of which 1,345 are named `observed` |
| Archive | `Sounds/Audio.snd`, 1,526 entries (see [`snd.md`](snd.md)) `observed` |

Reproduce with:

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/play_sound --table
./buildDir/play_sound --id 4        # resolves the id, then plays it
```

## Layout

The same 48-byte-header + zlib container the other `icons.lod` tables use (see
[`dtile.md`](dtile.md)). The decompressed block is a `u32` count followed by
fixed 112-byte records; `4 + 1355 × 112 = 151,764`, the declared length
exactly. `observed`

### Record (112 bytes)

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| +0x00 | 32 | char[32] | name | observed | an `Audio.snd` entry |
| +0x20 | 4 | u32 | id | observed | what the rest of the data refers to |
| +0x24 | 4 | u32 | group | inferred | small code; see below |
| +0x28 | 72 | — | runtime | observed | zero in all 1,355 records |

## The join to the sound archive

**1,338 of the 1,345 named records are entries of `Audio.snd`.** `observed`

The seven that are not are individually explicable rather than a systematic
gap: `StartMainChoice02` is the archive's single stored-uncompressed entry, and
the rest (`09meteorshowwe03`, `58elemwaterA_charge`, `58elemwaterA_fidget`,
`136PeasentM2A_Charge`, `GirlC37a`, `MaleG37a`, `14_05`) name effects this
installation does not ship.

Note when measuring this yourself: 57 archive names contain spaces, so a shell
pipeline that splits on whitespace will under-count the join. It reported
1,302 here before the names were read whole.

## Ids

Ids run 0 to 36,501, and 1,352 of the 1,355 are distinct — `5556`, `6032` and
`5382` each appear twice. They are not in ascending record order. `observed`

### Monster sound sets

One region is regular. **Monster sounds start at id 1000 and advance ten ids
per monster**, with the action as the offset within the block:

| Offset | Action |
| --- | --- |
| 0 | attack |
| 1 | die |
| 2 | charge |
| 3 | fidget |

Of the 210 records named `<id><monster>_<action>`, **204 sit at
`1000 + 10k + action`**, spread over 57 blocks ending at id 1569. `observed`

The six exceptions are all the same kind of thing: a monster missing a sound
has its attack sound repeated into the empty slot, so `49elemairA_attack`
appears at both 1162 and 1163. `observed`

The leading number in the name is the monster's `MONSTERS.TXT` id (see
[`text-tables.md`](text-tables.md)), and it increases with the block — but the
block index is **not** that id, because only 57 of the 173 monsters have a
sound set at all. `observed`

How a monster reaches its set is now pinned: **`DMONLIST.BIN` carries the
base id at +0x08 of its record** — equal to the named set's base on 31 of 31
monsters whose names carry their id, a block id on all 173 (see
[`dmonlist.md`](dmonlist.md)). Reproduce with `sft_info --sounds`.

### The group field

Four values, and the names in each are consistent enough to be worth recording
even though the engine's use of them is `unknown`:

| Group | Count | Names include |
| --- | ---: | --- |
| 0 | 288 | `campfire`, `fountain`, `boat`, `RunBadlands`, `RunCarpet`, and every monster sound |
| 1 | 57 | `enter`, `wooddrclose`, `fireBall`, `ClickIn`, `ClickMinus` |
| 2 | 986 | `tip`, `heal`, `fizzle`, `TurnPageU`, `openchest0101` |
| 4 | 24 | `GirlA34a`, `MaleG37a` — the party's spoken lines |

Group 0 holds the continuous and world-positioned sounds; group 2 is the bulk
of one-shot effects. `inferred`

## Ambient sounds: `DDECLIST.BIN` +0x4C

A decoration type names the sound it makes at offset `+0x4C` of its
[`DDECLIST.BIN`](odm-decorations.md) record, as an id into this table.

**Seven of the 230 decoration types carry one, and all seven resolve** —
and to the sound each name would lead you to expect: `observed`

| Decoration | Id | Sound |
| --- | ---: | --- |
| `CampfireOn` | 4 | `campfire` |
| `CookFire` | 4 | `campfire` |
| `Statue` | 10 | `fountain` |
| `Shp` | 18 | `boat` |
| `Cauldron` | 228 | `bubbling cauldron01` |
| `mcryst01` | 229 | `memorycrystal` |
| `crysdisc` | 229 | `memorycrystal` |

That the other 223 are zero is the expected result, not a shortfall: a tree
makes no noise.

The neighbouring `u16` at `+0x4A` is **not** a second sound id. Five records
carry a nonzero value there and four of them do resolve as ids, but to
nonsense — the three torches would play `fireBall`. It is something else.
`observed`

## Invalid-input behavior

The parser rejects, deterministically and without reading out of bounds:

- an entry too small for the 48-byte container header;
- a body that is not a zlib stream;
- a record count that does not account for the inflated block exactly.

A repeated id keeps the first record, so a lookup is deterministic rather than
dependent on iteration order. An unnamed record is kept in the array but not
indexed by name.

## Open questions

- What the group field selects — looping, attenuation, or channel priority are
  all consistent with the names. `unknown`
- The 72 zero bytes at `+0x28`. `unknown`
- How ids outside the monster block are allocated; they are neither dense nor
  ordered. `unknown`
- What `DDECLIST.BIN` `+0x4A` holds. `unknown`
- The audible radius of an ambient sound, which appears nowhere in the data.
  StarHaven uses 2,048 world units — four terrain cells — chosen by ear.
  `inferred`
