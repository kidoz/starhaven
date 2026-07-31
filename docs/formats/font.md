# Bitmap fonts (`.FNT`, Might and Magic VI)

Status: **verified.** The game's interface fonts. Each claim is tagged
`observed`, `inferred`, or `unknown`.

## Scope

Covers the fourteen `.FNT` entries of `icons.lod` and the three pixel values
their glyphs use. Does not cover `fontpal`, a separate 769-byte entry stored
uncompressed whose relationship to the fonts is `unknown`.

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product and edition | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Archive | `data/icons.lod` |
| Fonts | 14 entries ending `.FNT`, 13 of which decode `observed` |

Reproduce with:

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/font_info --list
./buildDir/font_info Lucida.fnt "Goblinwatch"
```

The second command draws the string as ASCII art. That is the verification:
either the glyphs spell the word or they do not.

## Layout

The same 48-byte-header + zlib container the rest of `icons.lod` uses (see
[`dtile.md`](dtile.md)). The decompressed block is four regions of fixed size:

| Offset | Size | Contents |
| --- | ---: | --- |
| 0x00 | 32 | header |
| 0x20 | 3,072 | 256 × 12-byte glyph metrics |
| 0xC20 | 1,024 | 256 × `u32` pixel offsets |
| 0x1020 | rest | glyph pixels |

### Header

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| +0x00 | 1 | u8 | firstChar | observed | 31 in twelve fonts, 30 in `COMIC.FNT` |
| +0x01 | 1 | u8 | lastChar | observed | 255 in all |
| +0x02 | 2 | u16 | — | observed | 8 on all 14 fonts; unused |
| +0x04 | 1 | — | zero | observed | |
| +0x05 | 1 | u8 | height | observed | 14 to 30 |
| +0x06 | 26 | — | zero | observed | |

### Metrics (12 bytes per character)

Three `i32`s: left spacing, width, right spacing. A character the font does
not define has all three zero. The pen advances by their sum, which is not the
width — `W` in `ARRUS.FNT` is 17 wide with a right spacing of −3. `observed`

### Pixels

`u32` offsets into the pixel region, one per character, and each glyph is
exactly `width × height` bytes, one per pixel, row-major from the top.

The glyphs **tile the region in character order** — no gaps, nothing left
over. On thirteen fonts every consecutive pair satisfies
`offset[next] − offset[this] == width[this] × height`, and the last glyph ends
exactly at the region's end. `observed`

Only three byte values ever appear: **0** leaves the background alone, **1** is
the glyph body, **255** its outline. The font says nothing about what colours
those are. `observed`

## Two traps

**The first glyph is at offset 0.** Treating a zero offset as "undefined"
looks right — 224 of the 256 offsets are non-zero — and silently drops one
glyph from every font. The width is what says whether a character exists.
`observed`

**The first-character byte is the character.** `ARRUS.FNT` stores 31 and
defines characters 31 to 255, so 225 glyphs, not the 224 a reader would get by
counting non-zero offsets.

Both were caught the same way: a synthetic font whose first glyph sat at
offset 0 crashed the decoder.

## `calig.fnt` does not decode

One of the fourteen fails the tiling check on every glyph pair, and its offset
table contains values like `0xFFFFFF00`. It inflates to 84,040 bytes and has
no monotonic offset table at the expected place. Whatever it is, it is not
this format, and the decoder rejects it rather than slicing glyphs out of the
wrong bytes. `observed`

## Invalid-input behavior

The decoder rejects, deterministically and without reading out of bounds:

- an entry too small for the 48-byte container header;
- a body that is not a zlib stream;
- a block too small for the metric and offset tables, or a height of zero;
- a glyph running past the pixel region, or glyphs that do not tile it.

## Historical question status

> Audited in the [open-question register](../open-questions.md); the register
> supersedes unresolved hypotheses below.

- What `fontpal` is for, and whether the body and outline colours come from it
  or are chosen per call site. StarHaven currently chooses them. `unknown`
- Which font the original interface used where. `unknown`
