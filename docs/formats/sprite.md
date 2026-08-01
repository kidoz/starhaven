---
title: "Sprite format for Might and Magic VI LOD entries"
summary: "Binary layout, scanline compression, palette lookup, and RGBA decoding rules for Might and Magic VI sprites."
doc_type: reference
status: partial
last_updated: 2026-08-01
tags:
  - mm6
  - sprite
  - lod
  - image-format
---
# Sprite format (Might and Magic VI `.LOD` sprite entries)

Status: **draft, evidence-backed.** Field layout is verified against a
user-supplied legal GOG.com installation. Each claim is tagged `observed`,
`inferred`, or `unknown`. The implementation in `src/core/image/sprite.*` is
written from this document.

## Scope

This document covers the **NWC sprite** format stored as entries inside
`SPRITES.LOD` (and used for characters, monsters, items, UI elements in other
archives): the `LodSpriteHeader_MM6` structure followed by per-scanline line
records and zlib-compressed palette-index pixel data. It does **not** cover:

- The `.LOD` container — see [`lod.md`](lod.md).
- Paletted images (`BITMAPS.LOD`) — see [`bitmap.md`](bitmap.md). Those embed
  their own palette; sprites reference a shared `palXXX` palette instead.
- Palettes themselves are palette-only image entries (see "Palette lookup"
  below).

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Container | `data/SPRITES.LOD` (`lod_type` `"sprites08"`) |
| Example entries | `3DAGGER` (218 B), `3AMULET` (318 B) |
| Header size | 32 bytes `observed` |
| Line record size | 8 bytes `observed` |

## Byte order

All integers little-endian. The engine's `ByteReader` is the single chokepoint.

## Sprite header (32 bytes)

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| 0x00 | 12 | char[12] | name | observed | NUL-padded ASCII |
| 0x0C | 4 | u32 | dataSize | observed | bytes of (possibly compressed) pixel data following the line table |
| 0x10 | 2 | u16 | width | observed | sprite width in pixels |
| 0x12 | 2 | u16 | height | observed | sprite height in pixels; also the number of line records that follow |
| 0x14 | 2 | u16 | paletteId | observed | references `palXXX` (zero-padded 3 digits) in `BITMAPS.LOD` |
| 0x16 | 2 | u16 | unk_0 | observed | appears always 0 |
| 0x18 | 2 | u16 | emptyBottomLines | observed | number of clear lines at the bottom (redundant; lines still present) |
| 0x1A | 2 | u16 | flags | observed | runtime-only? |
| 0x1C | 4 | u32 | decompressedSize | observed | decompressed pixel-data size; **0 if uncompressed** |

### Header size invariant

For a valid entry: `32 + height*8 + dataSize == entry size`. `observed` on
`3DAGGER` (32 + 4×8 + 154 = 218 ✓) and `3AMULET` (32 + 7×8 + 230 = 318 ✓).

## Line records (8 bytes each, `height` of them)

Immediately after the 32-byte header come `height` line records, one per
scanline (top to bottom).

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| +0x00 | 2 | i16 | begin | observed | first visible column (inclusive) |
| +0x02 | 2 | i16 | end | observed | last visible column + 1 (exclusive) |
| +0x04 | 4 | u32 | offset | observed | byte offset into the **decompressed** pixel data for this line's pixels |

For a scanline, columns in `[begin, end)` are visible; their palette indices are
`decompressed[offset + (x - begin)]` for `x` in `[begin, end)`. Columns outside
`[begin, end)` are fully transparent. If `begin >= end`, the line is empty
(fully transparent) and consumes no pixel bytes.

### Scanline bounds invariant

The maximum `offset + (end - begin)` over all non-empty lines is less than the
decompressed pixel-data length. `observed` on `3AMULET`: max = 337 < 338.

## Pixel data

After the line table come `dataSize` bytes of pixel data. Each byte is a palette
index. The data is **zlib-compressed** when `decompressedSize != 0`
(`0x78 0x9c` header), decompressing to exactly `decompressedSize` bytes.
`observed`. When `decompressedSize == 0` the pixel data is stored uncompressed.

Offsets in the line records index into the **decompressed** pixel data.

## Palette lookup

Sprites do **not** embed a palette. They reference a shared palette by
`paletteId`:

- The palette is a `palXXX` entry in `BITMAPS.LOD`, where `XXX` is `paletteId`
  zero-padded to 3 digits (e.g. `paletteId=2` → `pal002`). `observed`:
  `3DAGGER`, `3AMULET` etc. all have `paletteId=2`, and `pal002` exists.
- A palette entry is a palette-only image: a 48-byte `LodImageHeader_MM6` with
  all-zero image fields, followed by a 768-byte RGB palette (256 × 3).
  `observed` on `pal002` (816 = 48 + 768 bytes).

To render a sprite, the engine loads the referenced `palXXX` palette and maps
each visible pixel's index to RGB. Index 0 is treated as transparent for
sprites (this is how the empty/transparent areas are encoded).

## Decoding to RGBA

1. Read and validate the 32-byte header (`32 + height*8 + dataSize == size`).
2. Read `height` line records.
3. Obtain the pixel buffer: zlib-decompress if `decompressedSize != 0`, else
   use the raw `dataSize` bytes.
4. Allocate a `width × height` RGBA buffer, fully transparent.
5. For each line `y`, for each column `x` in `[begin, end)`:
   - index = `pixels[offset_y + (x - begin)]`
   - if index == 0 → transparent (alpha 0)
   - else RGBA = palette[index] with alpha 255.

## Invalid-input behavior

The decoder rejects, deterministically and without reading out of bounds:

- fewer than 32 bytes (cannot hold a header);
- `width == 0` or `height == 0`;
- the line table would extend past the buffer (`32 + height*8 > size`);
- `dataSize` inconsistent with the total size;
- a truncated pixel region;
- zlib failure, or a decompressed length not matching `decompressedSize`
  (when compressed);
- a line whose `offset + (end - begin)` exceeds the decompressed length.

## Historical question status

> Audited in the [open-question register](../open-questions.md); the register
> supersedes unresolved hypotheses below.

- Exact meaning of `flags`. `unk_0` is `0` on all 299 sampled sprites and
  `emptyBottomLines` is the redundant clear-line count; both are now `observed`
  constants, treated as opaque by the decoder.
- Whether any sprite uses a non-zero `decompressedSize == 0` (uncompressed)
  path — none observed, but the decoder supports it.
- Non-index-0 transparency or colorkey behavior has not been observed and
  remains `unknown`.
