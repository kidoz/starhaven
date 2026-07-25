# Bitmap format (Might and Magic VI `.LOD` image entries)

Status: **draft, evidence-backed.** Field layout is verified against a
user-supplied legal GOG.com installation. Each claim is tagged `observed`,
`inferred`, or `unknown`. The implementation in `src/core/image/bitmap.*` is
written from this document.

## Scope

This document covers the **NWC paletted image** format stored as entries inside
`BITMAPS.LOD` (and `icons.lod`): the `LodImageHeader_MM6` structure followed by
optionally-compressed pixel data and a 256-entry RGB palette. It does **not**
cover:

- The `.LOD` container itself — see [`lod.md`](lod.md).
- Sprites (`SPRITES.LOD`), which use a separate `LodSpriteHeader_MM6` layout
  with per-line offsets. Deferred to a later slice.
- Fonts, PCX images in `Games.lod`, and 16-bit images. Out of scope here.

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Container | `data/BITMAPS.LOD` |
| Example entry | `apothmid` (4903 bytes), `BCSCTR` (33764 bytes) |
| Header size | 48 bytes `observed` |
| Palette | 768 bytes = 256 × RGB, appended after pixel data `observed` |

## Byte order

All integers little-endian. The engine's `ByteReader` is the single chokepoint.

## Image header (48 bytes)

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| 0x00 | 16 | char[16] | name | observed | NUL-padded ASCII; often embeds a `TGA` marker at byte ~8 |
| 0x10 | 4 | u32 | size | observed | `width * height` of the first (largest) mipmap |
| 0x14 | 4 | u32 | dataSize | observed | bytes of pixel data following the header, **before** the palette |
| 0x18 | 2 | u16 | width | observed | image width in pixels |
| 0x1A | 2 | u16 | height | observed | image height in pixels |
| 0x1C | 2 | i16 | widthLn2 | observed | log2(width) for power-of-two sizes, else 0 |
| 0x1E | 2 | i16 | heightLn2 | observed | log2(height) for power-of-two sizes, else 0 |
| 0x20 | 2 | i16 | widthMinus1 | observed | width−1 for power-of-two sizes; may be garbage otherwise |
| 0x22 | 2 | i16 | heightMinus1 | observed | height−1 for power-of-two sizes; may be garbage otherwise |
| 0x24 | 2 | i16 | paletteId | observed | palette id (palette is stored with the image; field not needed to decode) |
| 0x26 | 2 | i16 | anotherPaletteId | observed | appears always 0 |
| 0x28 | 4 | u32 | decompressedSize | observed | decompressed size of pixel data; **0 if pixel data is uncompressed** |
| 0x2C | 4 | u32 | flags | observed | bitmask — see below |

### Verified invariants

On `apothmid` (4903 bytes): `48 + dataSize(4087) + 768 == 4903` ✓ and
`size(4096) == width(64) * height(64)` ✓. Same identity holds for `BCSCTR`.
`observed`.

## Pixel data

Immediately after the 48-byte header come `dataSize` bytes of pixel data. The
pixel data is one byte per pixel (palette indices).

### Compression

If `decompressedSize == 0`, the pixel data is stored **uncompressed** and is
exactly `size` bytes long (`width * height`).

If `decompressedSize != 0`, the pixel data is **zlib-compressed** (RFC 1950,
with the `0x78 0x9c` zlib header). `observed` — confirmed by feeding the raw
`apothmid` pixel block to a standard zlib decoder and obtaining exactly
`decompressedSize` (5440) bytes.

This is **standard zlib**, not a proprietary scheme. The engine uses the
system `zlib` library to decompress.

### Mipmaps

When `decompressedSize > size`, the decompressed pixel data contains a **mipmap
chain**: the first `size` bytes are the full-resolution image, followed by
successively halved mip levels (down to 16×16). The `0x0002` flag indicates
mipmaps are present.

For rendering the base image, the decoder uses only the first `size` bytes.

## Palette

After the pixel data (`offset 48 + dataSize`) comes a **768-byte palette**:
256 entries × 3 bytes (R, G, B), each 0–255. Palette index 0 may be transparent
when flag `0x0200` is set.

## Flags

| Bit | Meaning | Status |
| --- | --- | --- |
| 0x0002 | has mipmaps | observed |
| 0x0100 | not an image (text file?) | inferred |
| 0x0200 | palette entry 0 is transparent (alpha key); else colorkey | inferred |
| 0x0400 | don't free buffers (runtime hint) | inferred |

This slice treats index 0 as transparent when `0x0200` is set; full colorkey
handling is deferred.

## Decoding to RGBA

1. Read and validate the 48-byte header (`size == width*height`; total length
   `== 48 + dataSize + 768`).
2. Obtain the pixel buffer: if `decompressedSize == 0`, use the raw `dataSize`
   bytes; else zlib-decompress to `decompressedSize` bytes.
3. Read the 768-byte palette.
4. For each of the first `size` pixels, map the index to RGBA via the palette.
   Index 0 is fully transparent when flag `0x0200` is set; otherwise opaque.

## Invalid-input behavior

The decoder rejects, deterministically and without reading out of bounds:

- fewer than 48 bytes (cannot hold a header);
- `width == 0` or `height == 0` or `size != width * height`;
- `dataSize` larger than the remaining buffer;
- truncated palette (fewer than 768 bytes after pixel data);
- zlib failure or a decompressed length not matching `decompressedSize`
  (or, when uncompressed, not matching `size`).

## Unknown / open questions

- Exact semantics of flags `0x0100`, `0x0400` (`inferred`).
- Colorkey (non-index-0 transparency) handling — deferred.
- Whether `anotherPaletteId` is ever non-zero — not observed.
