# Map event scripts (Might and Magic VI)

Status: **verified** for the container and the record structure; the opcodes
themselves are undecoded. Each claim is tagged `observed`, `inferred`, or
`unknown`.

## Scope

Covers where a map's script lives, how its records are framed, and how a face
names one. Does not cover what any opcode does.

## They are not in `Games.lod`

`Games.lod` holds only geometry and saved state — 52 `.blv`, 52 `.dlv`, 15
`.odm`, 15 `.ddm`, and nothing else. `observed` The scripts are in
**`icons.lod`**, alongside the design tables: **83 `.EVT`** entries and
**76 `.STR`**, one pair per map, 738 to about 2,000 stored bytes each.
`observed`

That is worth stating because two earlier searches for map events looked in the
wrong file. The `.odm` payload is now accounted for byte for byte with no room
for a script, and the `.ddm`'s undecoded prefix is 1,947 zero bytes out of
1,948 on New Sorpigal — saved state that starts empty, not code. `observed`

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product and edition | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Archive | `data/icons.lod` |
| Scripts | 83 `.EVT`, 15,504 records `observed` |
| Strings | 76 `.STR`, 1,274 strings `observed` |

Reproduce with:

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/evt_info D01          # events and strings
./buildDir/evt_info OutE3 5      # one event, step by step
```

## The container

The same 48-byte container the design tables use, with the unpacked size at
`0x28` and zlib after it; zero there means the entry is stored as it is. See
[`text-tables.md`](text-tables.md). `observed`

## A script is a run of size-prefixed records

| Offset | Size | Type | Field |
| --- | --- | --- | --- |
| +0x00 | 1 | u8 | bytes that follow |
| +0x01 | 2 | u16 | event id |
| +0x03 | 1 | u8 | sequence within the event |
| +0x04 | 1 | u8 | opcode |
| +0x05 | n | | arguments |

**All 83 scripts are consumed exactly** by this walk — 15,504 records, no bytes
left over on any map — and event ids are non-decreasing, so an event's steps
are contiguous. `observed`

Sequence numbers count from zero within an event: 5,176 records are step 0,
1,407 are step 1, 1,115 step 2, and so on down. `observed`

**90 distinct opcodes** appear. The most common are 4 (2,192 uses), 14, 1, 15,
16, 18, 29 and 30. What any of them does is `unknown`: argument lengths are
fixed per opcode, but testing whether an argument indexes the string table is
not decisive, since most small numbers are valid indices either way.

## A face names an event

`BlvFaceExtra`'s field at +0x1A, which this project carried as unknown, is the
event id: **1,408 of the 1,441** non-zero values across the 52 indoor maps name
an event that map's own script defines. `observed` So a door, a switch or a
sign is a face that points at a script.

The 33 that do not resolve are `unknown`; they may name events in another map's
script, or the field may carry something else where the referenced event is
absent.

## The strings

`.STR` is NUL-terminated strings end to end, and the first is a single space on
every map examined. `observed` `D01.STR` reads: `"Exit Door"`, `"Chest"`,
`"Switch"`, `"Empty"`, `"Door"`, then single letters; `OutE3.STR` names
buildings and speaks: `"Welcome to New Sorpigal"`, `"Refreshing!"`.

## Open questions

- Every opcode. `unknown`
- Which argument, if any, indexes the string table. `unknown`
- How an outdoor map's buildings name their events — the indoor case is the
  face field above, and the outdoor equivalent is not yet located. `unknown`
- The 33 face event ids with no matching event. `unknown`
