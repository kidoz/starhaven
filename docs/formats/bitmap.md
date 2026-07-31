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
- Fonts and 16-bit images. Out of scope here. PCX entries in the same
  container are read by `src/core/image/pcx.cpp` — see
  [`interface-panels.md`](interface-panels.md).

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
256 entries × 3 bytes (R, G, B), each 0–255. Palette index 0 is the
transparent color when flag `0x0200` (UI art) or `0x0001` (world textures)
is set.

## Flags

The shipped values are few. `BITMAPS.LOD`'s 1,958 entries carry exactly
three: `0x13` on 1,085, `0x12` on 607, and `0x0` on the 266 zero-size
non-image entries. `icons.lod`'s bitmaps carry `0x0`, `0x10`, `0x100`,
`0x200` and `0x210`. `observed`

| Bit | Meaning | Status |
| --- | --- | --- |
| 0x0001 | uses palette index 0 — the transparent color. Set on 1,085 of 1,085 world textures whose pixels contain index 0 and on 0 of the 607 that do not: a perfect split. | observed for the split; the transparency reading `inferred` |
| 0x0002 | a four-level mip chain follows: the pixel data is exactly `size + size/4 + size/16 + size/64` bytes on all 1,692 flagged entries, and the executable's loader stands pointers at those very offsets when it tests the bit. | observed |
| 0x0010 | set on every real image in both archives; not yet seen tested. | unknown |
| 0x0100 | `icons.lod` UI art only (233 entries); untraced. | unknown |
| 0x0200 | `icons.lod` UI art: palette entry 0 is transparent. | inferred |
| 0x0400 | never on disk — the executable's loader ORs it into the word after loading, a runtime marker. | observed |

**This corrects the previous census**, which reported `0x0100` on 578 and
`0x0400` on 1,084 of the 1,958; neither value occurs in the shipped
`BITMAPS.LOD` at header offset `0x2C`, and the counts are not reproducible
against either archive.

The decoder treats index 0 as transparent when `0x0200` (UI art) or
`0x0001` (world textures) is set.

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

- What bit `0x0010` selects (present on every real image in both archives)
  and what `0x0100` marks on `icons.lod` UI art. `unknown`
- Colorkey (non-index-0 transparency) handling — deferred.
- Whether `anotherPaletteId` is ever non-zero — not observed.
