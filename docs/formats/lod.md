---
title: "LOD archive format"
summary: "Binary layout, compression, and validation rules for standard Might and Magic VI resource archives."
doc_type: reference
status: partial
last_updated: 2026-08-01
tags:
  - mm6
  - lod
  - archive
  - binary-format
---
# LOD archive format (Might and Magic VI: Mandate of Heaven)

Status: **draft, evidence-backed.** Field semantics are verified against a
user-supplied legal GOG.com installation. Each claim is tagged
`observed`, `inferred`, or `unknown`. The implementation in
`src/core/lod/lod_archive.*` is written from this document, not from any
proprietary code.

## Scope

This document covers the **MM6+ standard `.LOD`** format used by
`BITMAPS.LOD`, `SPRITES.LOD`, and `icons.lod` — the resource containers the
engine reads. It does **not** cover:

- `Games.lod`, which is the separate live-game-data / chapter LOD with a
  different directory layout (different `lod_type`, games-header variant); see
  [`games-lod.md`](games-lod.md).
- Entry-specific payload formats. Images and sprites have their own headers;
  a generic compressed entry instead starts with the zlib envelope described
  below.
- MM7/MM8 (76-byte entries, 64-byte names) and Heroes III (92-byte header).
  These are out of scope for MM6 but noted where they diverge.

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Archive | `data/BITMAPS.LOD` (also `SPRITES.LOD`, `icons.lod`) |
| Magic | `4C 4F 44 00` ("LOD\0") `observed` |
| Version string | `"MMVI"` at offset 4 `observed` |
| Header size | 288 bytes (0x120) `observed` |
| Example `count` (`BITMAPS.LOD`) | 1958 entries `observed` |

The semantics below were cross-checked against the public reference parser in
`might-and-magic/mmarch` (MIT) as a hypothesis, then confirmed against the
binary's own bytes before being recorded here. The implementation does not
reproduce that parser's code.

## Byte order

All multi-byte integers are **little-endian**. The reader in
`src/core/io/byte_reader.*` is the single chokepoint and never maps raw bytes
onto C++ structs.

## File header (288 bytes / 0x120)

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| 0x00 | 4 | char[4] | magic | observed | `"LOD\0"` |
| 0x04 | 80 | char[80] | version string | observed | `"MMVI"`, NUL-padded |
| 0x54 | 172 | — | reserved | observed | all zero in observed files |
| 0x100 | 16 | char[16] | lod_type | observed | `"bitmaps"`, `"sprites"`, `"icons"`, `"game"` |
| 0x110 | 4 | u32 | archive_start | observed | absolute offset of the directory table; equals 288 (0x120) in observed files |
| 0x114 | 8 | — | reserved | inferred | not used for parsing |
| 0x11C | 2 | u16 | count | observed | number of directory entries |
| 0x11E | 2 | — | reserved | inferred | |

Note the version discriminator: at offset 4 the parser treats the first u32 as
a version code. If it decodes to a value `<= 0xFFFF` the file is the Heroes III
92-byte-header format; otherwise it is MM6+ and uses this 288-byte layout. In
observed MM6 files offset 4 is the ASCII `"MMVI"` = `0x49564D4D`, well above
0xFFFF. `observed`.

## Directory entry record (32 bytes)

Entries are stored back-to-back starting at `archive_start`. Entry `i` is at
`archive_start + i * 32`.

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| +0x00 | 16 | char[16] | name | observed | NUL-padded ASCII |
| +0x10 | 4 | u32 | addr | observed | offset of this entry's data, **relative to `archive_start`** |
| +0x14 | 4 | u32 | size_field | observed | authoritative stored byte size |
| +0x18 | 4 | u32 | reserved | observed | zero in MM6 leaf entries |
| +0x1C | 2 | u16 | child_count | observed | zero for a leaf entry |
| +0x1E | 2 | u16 | priority | observed | zero in examined leaf entries; exact directory policy unknown |

### Absolute data offset

An entry's data begins at the absolute file offset
`archive_start + addr` (i.e. `addr` is relative to the start of the directory
table). `observed`.

### Sizing data

The `size_field` at +0x14 is the **authoritative stored size** (in bytes) of
the entry's data in the archive. `observed`.

Verified on `BITMAPS.LOD` (1958 entries): every entry's
`archive_start + addr + size_field` stays within the file, and the maximum such
end offset equals the file size exactly — i.e. the data is packed with no slack
and `size_field` is reliable for every entry. `observed`.

Note: the entry table is **not** sorted by `addr` and entries are not packed in
directory order, so a "gap to the next entry" heuristic is **not** reliable.
Earlier drafts of this spec tried gap-based sizing; that was incorrect.

### Compression

Compression is not declared by either trailing directory word. Generic
compressed payloads carry their own 16-byte envelope:

```text
u32 version = 91969 (0x00016741)
char signature[4] = "mvii"
u32 stored_size
u32 unpacked_size
u8 zlib_data[stored_size]
```

Image and sprite payloads declare compression in their own headers instead.
All three paths use zlib; there is no LZ algorithm selected by directory
`+0x18`. `observed` for the MM6 structure, corroborated by the public
compatibility references linked from the
[open-question register](../open-questions.md).

## Invalid-input behavior (required by the engine)

The reader must reject, deterministically and without reading out of bounds:

- fewer than 288 bytes (cannot hold a header);
- magic other than `"LOD\0"`;
- version code `<= 0xFFFF` at offset 4 (Heroes III format — not supported here);
- `archive_start` outside `[288, file_size]`;
- `count` such that the directory table would extend beyond the file;
- an entry whose `archive_start + addr` exceeds the file size;
- an entry whose `size_field` would extend past the file;
- a compressed envelope whose signature or declared bounds are invalid.

The reader reports a structured status; it never throws and never reads past
the supplied buffer.

## Historical question status

> Audited in the [open-question register](../open-questions.md); the register
> supersedes unresolved hypotheses below.

- The meaning of the u32 at entry +0x1C (`unknown`).
- Whether `size_field` for sprites is authoritative; verify on `SPRITES.LOD`.
- The `Games.lod` directory layout is canonical in
  [`games-lod.md`](games-lod.md).
- The LZ decompression scheme remains `unknown`.
