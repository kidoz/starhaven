# Games.lod container format (Might and Magic VI)

Status: **draft, evidence-backed.** Field layout is verified against a
user-supplied legal GOG.com installation. Each claim is tagged `observed`,
`inferred`, or `unknown`. The implementation in
`src/core/lod/game_lod_archive.*` is written from this document.

## Scope

This document covers the **`Games.lod`** container — the live-game-data archive
holding the game's maps and event data. It is a distinct variant from the
standard `.LOD` resource archives (`BITMAPS.LOD`/`SPRITES.LOD`/`icons.lod`),
which are documented in [`lod.md`](lod.md).

This slice covers **only the container** — how to enumerate entries and locate
their raw bytes. It does **not** cover the internals of the contained files:

- `.blv` / `.odm` — indoor / outdoor map geometry and layout.
- `.dlv` / `.ddm` — indoor / outdoor event scripts.

Decoding those is a separate, larger research question.

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Archive | `data/Games.lod` (9,650,199 bytes) |
| Header magic | `4C 4F 44 00` ("LOD\0") `observed` |
| Version string | `"GameMMVI"` at offset 4 `observed` |
| Header size | 256 bytes `observed` |
| Root directory entry size | 32 bytes `observed` |
| File entry size | 32 bytes `observed` |
| Entry count | 134 (52 `.blv`, 15 `.odm`, 52 `.dlv`, 15 `.ddm`) `observed` |

## Byte order

All integers little-endian. The engine's `ByteReader` is the single chokepoint.

## How Games.lod differs from the standard `.LOD` archives

| Aspect | Standard (`BITMAPS.LOD` etc.) | `Games.lod` |
| --- | --- | --- |
| Version string (offset 4) | `"MMVI"` | `"GameMMVI"` |
| Header size | 288 bytes (256 header + 32 root entry, see below) | 256 bytes + 32-byte root entry |
| Directory table | located via `archive_start` u32 at 0x110 | located at root entry's `dataOffset` |
| Per-entry compression | zlib inside image/sprite payloads | none at the container level |

The 288-byte "header" used by the standard-archive reader is actually this
256-byte `LodHeader_MM6` followed by the 32-byte root directory entry. The two
variants share the same `LodHeader_MM6` + `LodEntry_MM6` building blocks; they
differ in the version string and in how the directory is interpreted.

## File header — `LodHeader_MM6` (256 bytes)

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| 0x00 | 4 | char[4] | signature | observed | `"LOD\0"` |
| 0x04 | 80 | char[80] | version | observed | `"GameMMVI"` for Games.lod (NUL-padded) |
| 0x54 | 80 | char[80] | description | observed | `"Maps for MMVI"` |
| 0xA4 | 4 | u32 | size | observed | always 100 (legacy/unused) |
| 0xA8 | 4 | u32 | unk_0 | observed | always 0 |
| 0xAC | 4 | u32 | numDirectories | observed | always 1 for MM games |
| 0xB0 | 80 | char[80] | unk_1 | observed | unused / garbage |

## Root directory entry — `LodEntry_MM6` (32 bytes, at offset 256)

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| +0x00 | 16 | char[16] | name | observed | `"maps"` |
| +0x10 | 4 | u32 | dataOffset | observed | absolute file offset of the directory table (288) |
| +0x14 | 4 | u32 | dataSize | observed | bytes from dataOffset to end of file |
| +0x18 | 4 | u32 | unk_0 | observed | 0 |
| +0x1C | 2 | u16 | numItems | observed | number of file entries that follow |
| +0x1E | 2 | u16 | priority | observed | 0 |

## File entries — `LodEntry_MM6` (32 bytes each, `numItems` of them)

Starting at the root entry's `dataOffset` (288). Each:

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| +0x00 | 16 | char[16] | name | observed | NUL-padded ASCII, e.g. `"d01.blv"`, `"out01.odm"` |
| +0x10 | 4 | u32 | dataOffset | observed | offset of this file's data, **relative to the root directory's dataOffset** |
| +0x14 | 4 | u32 | dataSize | observed | raw stored size of this file |
| +0x18 | 4 | u32 | unk_0 | observed | 0 |
| +0x1C | 2 | u16 | numItems | observed | 0 for files (non-zero would mean a subdirectory) |
| +0x1E | 2 | u16 | priority | observed | 0 |

### Absolute file-data offset

A file's bytes begin at `root.dataOffset + entry.dataOffset`
(i.e. the entry's offset is relative to the root directory's data offset).
`observed`.

### Verified invariants

On `Games.lod`:
- `256 + 32 + numItems*32 == root.dataOffset` (the file-entry table ends exactly
  where file data begins). `observed`: `256 + 32 + 134*32 = 4576 = root.dataOffset` ✓
- `root.dataOffset + root.dataSize == filesize` (the root covers the whole tail).
  `observed`: `288 + 9649911 = 9650199` ✓
- Every entry's `root.dataOffset + entry.dataOffset + entry.dataSize <= filesize`.
  `observed` across all 134 entries.

## Compression

**None at the container level.** `observed`: scanning all 134 entries, none
begins with the Games7 compression signature (`0x00016741` / `0x6969766D`).
File data is stored raw; `dataSize` is the true byte count. (The contained
`.blv`/`.odm`/`.ddm`/`.dlv` files may have their own internal structure, but
that is outside this slice.)

## Invalid-input behavior

The reader rejects, deterministically and without reading out of bounds:

- fewer than 288 bytes (cannot hold header + root entry);
- signature other than `"LOD\0"`;
- version that is not a recognized `Game*` form (e.g. `"GameMMVI"`);
- `numDirectories != 1`;
- root `dataOffset` outside `[288, filesize]`;
- `numItems` such that the file-entry table would extend past the root's data;
- an entry whose `dataOffset + dataSize` would exceed the root's region.

## Historical question status

> Audited in the [open-question register](../open-questions.md); the register
> supersedes unresolved hypotheses below.

- Meaning of the header `size` field (always 100) and `unk_1[80]` — unused.
- The internals of `.blv` / `.odm` / `.dlv` / `.ddm` files — deferred to a
  dedicated slice (the largest remaining format).
- Whether any MM6 Games.lod uses per-entry Games7 compression — none observed
  in this edition.
