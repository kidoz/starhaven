# Decoration frame table (`DIFT.BIN`, Might and Magic VI)

Status: **decoded, layout verified.** The glows the torches and braziers wear,
in the same shape `DSFT.BIN` uses for sprites. Each claim is tagged `observed`,
`inferred`, or `unknown`.

## Scope

Covers all of `DIFT.BIN`: the record layout, the group structure, and the
self-check that ties a group's first frame to its length. The sprite names do
not resolve directly in `SPRITES.LOD` or `BITMAPS.LOD`; how the engine reaches
their art is not established here.

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product and edition | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Container | `data/icons.lod`, SHA-256 `2e8f2c0d…p813bd18` |
| Entry | `DIFT.BIN`, stored 272 bytes, inflating to 1,956 `observed` |
| Records | 61 in 7 groups `observed` |

## Container

The 48-byte-header + zlib container the other `D*.BIN` tables use (see
[`dtile.md`](dtile.md)). The decompressed block is a `u32` count (61) followed
by sixty-one 32-byte records; `4 + 61 × 32 = 1,956`, the declared length
exactly. `observed`

## Record (32 bytes)

| Offset | Size | Type | Field | Status | Notes |
| --- | ---: | --- | --- | --- | --- |
| +0x00 | 12 | char[12] | group_name | observed | set on a group's first frame; empty after |
| +0x0C | 12 | char[12] | sprite_name | observed | a per-frame sprite reference |
| +0x18 | 2 | u16 | flags | observed | 0 or 1 on every shipped frame |
| +0x1A | 2 | u16 | frame_count | observed | on a group's first frame, equals the group's length |
| +0x1C | 2 | u16 | duration | observed | per-frame, in the frame tables' shared unit |
| +0x1E | 2 | u16 | — | observed | zero on every shipped frame |

This is the `DSFT.BIN` shape (name, sprite, flags, duration) with `frame_count`
standing in for DSFT's group length. See [`dsft.md`](dsft.md).

## Groups

A record with a non-empty `group_name` starts a group; the records after it, up
to the next named one, belong to it.

| Group | Frames | frame_count on first | Sum of durations |
| --- | ---: | ---: | ---: |
| *(unnamed, sprite `pending`)* | 1 | 0 | 4 |
| `glow01` | 6 | 6 | 9 |
| `glow02` | 6 | 6 | 9 |
| `glow03` | 6 | 6 | 9 |
| `glow04` | 6 | 6 | 9 |
| `glow05` | 6 | 6 | 9 |
| `fire` | 30 | 30 | 33 |

The self-check holds on every named group: **the first frame's `frame_count`
equals the group's length**, 6 of 6 for the glows and 30 of 30 for `fire`.
`observed` The leading unnamed group (the `pending` sprite) carries
`frame_count` 0, which is consistent with a single-frame still rather than a
breach of the rule.

`flags` is 1 on every frame of the six named groups and 0 on the lone unnamed
one; `inferred` that bit marks a member of a multi-frame group, though unlike
DSFT's "another follows" bit it is set on the last frame too.

## The sprite names

The frame sprite names — `glow01a`..`glow01f`, `fr1`..`fr30` — are **not** entries
of `SPRITES.LOD` or `BITMAPS.LOD` (none of the 61 resolves in either). They are
internal animation references the engine resolves through an indirection this
table does not itself name. `observed` for the names; `unknown` for the join.

## Invalid-input behavior

The parser rejects, deterministically and without reading out of bounds:

- an entry shorter than the 48-byte container header;
- a body that is not a zlib stream;
- a record count whose 32-byte records do not account for the inflated block
  exactly.

## Open questions

- How the sprite names resolve to art — they are entries of neither LOD.
  `unknown`
- The flag bit's exact meaning (it is set on the last frame too, unlike DSFT).
  `inferred`
