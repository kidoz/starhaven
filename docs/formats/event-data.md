# Event-data format (.ddm / .dlv) (Might and Magic VI)

Status: **draft, evidence-backed — outer format only.** This documents the file
envelope of the outdoor (`.ddm`) and indoor (`.dlv`) event-data files stored in
`Games.lod`. The decompressed payload's internal event-record layout is a
follow-up. Each claim is tagged `observed`, `inferred`, or `unknown`.

## Scope

This document covers the **zlib wrapper** surrounding `.ddm` (outdoor map events)
and `.dlv` (indoor map events) entries — the same envelope used by `.odm` map
files. It documents decompression and the payload's gross structure. The
internal event-record tables (triggers, actions, NPC spawns, etc.) are deferred.

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Container | `data/Games.lod`, entries `outa1.ddm` (187 B), `outb2.ddm` (808 B), `d01.dlv` (3070 B) |
| Wrapper | identical 8-byte form to `.odm` (see [`odm.md`](odm.md)) |

## Outer structure

A `.ddm`/`.dlv` entry uses the **same 8-byte zlib wrapper** as `.odm`:

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| 0x00 | 4 | u32 | streamSize | observed | size of the zlib stream that follows (= stored size − 8) |
| 0x04 | 4 | u32 | decompressedSize | observed | length of the decompressed payload |
| 0x08 | … | bytes | zlibData | observed | a standard zlib stream (`0x78 0x9c` header) |

`observed` on `outa1.ddm` (stored 187 B): u32@0x00 = 179 = stored − 8;
u32@0x04 = 86292; `zlib.decompress(bytes[8:])` yields exactly 86292 bytes. The
same identity holds for `outb2.ddm` (→ 97552 B) and `d01.dlv` (→ 107019 B).

## Decompressed payload

The decompressed payload is a **large, mostly-zero fixed-size structure**
(~86 KB outdoor, ~107 KB indoor) with event data sparsely populated:

- `outa1.ddm`: 86292 bytes, only ~handful nonzero (a nearly-empty starting map).
- `outb2.ddm`: 97552 bytes, 1244 nonzero bytes (a populated town region).

The payload holds the map's event tables (tile triggers, scripted actions,
monster spawns, NPC placements, chest contents, etc.). The exact sectioning and
record layouts are `unknown` in this slice and are the subject of follow-up
research (they are large, fixed-offset tables referenced by the engine's
`MapInfo`/event structures).

## Decoding (this slice)

1. Read the 8-byte wrapper (streamSize, decompressedSize).
2. Inflate the zlib stream at offset 8.
3. Verify the inflated length equals decompressedSize (if nonzero).
4. Expose the decompressed payload as a blob for later event-table decoding.

The engine reuses the same zlib helper as the `.odm` parser — the wrapper is
byte-identical.

## Invalid-input behavior

The decoder rejects, deterministically and without reading out of bounds:

- fewer than 8 bytes (cannot hold the wrapper);
- zlib failure;
- an inflated length not matching decompressedSize (when nonzero).

## Open questions (next slice)

- The internal layout of the event tables beyond the first one's type/name
  fields (see [`event-tables.md`](event-tables.md)): the 548-byte record body,
  the other tables (spawn points, chests, objects), and the `.dlv` indoor
  differences. These need the `MM6.exe` `.ddm`-loader trace.
- The distinction between `.ddm` and `.dlv` payloads beyond size (indoor vs
  outdoor event sets).
