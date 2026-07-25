# SND sound archive (Might and Magic VI)

Status: **verified.** The container holding MM6's sound effects, and the wave
format its entries carry. Each claim is tagged `observed`, `inferred`, or
`unknown`.

## Scope

Covers `Sounds/Audio.snd` and the RIFF/WAVE payloads inside it, and — briefly —
the music beside it.

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product and edition | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Archive | `Sounds/Audio.snd`, 18,106,810 bytes, 1,526 entries `observed` |

Reproduce with:

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/play_sound --list
./buildDir/play_sound 01archerA_attack
```

## Layout

| Offset | Size | Type | Field | Status |
| --- | --- | --- | --- | --- |
| 0x00 | 4 | u32 | count | observed |
| 0x04 | count × 52 | Entry[] | directory | observed |

### Entry (52 bytes)

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| +0x00 | 40 | char[40] | name | observed | NUL-padded; 57 names contain spaces |
| +0x28 | 4 | u32 | offset | observed | absolute file offset |
| +0x2C | 4 | u32 | packed_size | observed | bytes stored |
| +0x30 | 4 | u32 | unpacked_size | observed | bytes after inflation |

The directory ends at `4 + 1526 × 52 = 79,356`, exactly the first entry's
offset, and the entries **tile the file with no gaps and no overlaps**, the
last ending precisely at the file's final byte. `observed`

### Compression

An entry whose two sizes are equal is **stored uncompressed**; otherwise the
bytes are a zlib stream. Exactly one of MM6's 1,526 entries
(`StartMainChoice02`, 328 bytes) is stored, and treating it as zlib fails
outright with a header-check error. `observed`

## Payloads

Every entry inflates to a RIFF/WAVE file. All 1,526 are wave format tag **17
(IMA/DVI ADPCM)** at **22050 Hz**; 1,489 are mono and 36 stereo. `observed`

The `fmt ` chunk is 20 bytes — the standard 16 plus a 2-byte extension giving
samples per block — and a `fact` chunk sits between `fmt ` and `data`, so a
decoder must walk the chunk list rather than assume fixed offsets. `observed`

### The `fact` chunk matters

ADPCM data is blocked, and the final block is padded out to a whole group of
eight samples. Decoding blocks alone therefore yields **up to seven frames more
than the sound actually has**: across all 1,526 entries the block-derived count
exceeds the `fact` count by 0 to 7, and never falls short. `observed`

`fact` gives the exact frame count and the decoder trims to it.

### IMA ADPCM

The encoding is the published IMA/DVI standard, not anything specific to this
game: a 4-byte per-channel block header (initial predictor as `i16`, step index
as `u8`, one pad byte), then 4-bit nibbles in groups of four bytes per channel,
low nibble first. The block's first output frame is the predictor itself.

For stereo the nibble groups alternate per channel — eight samples of one, then
eight of the other — so the decoder re-interleaves them.

## Invalid-input behavior

The reader and decoder reject, deterministically and without reading out of
bounds:

- a file too short for the count, a zero count, or a directory that will not fit;
- an entry lying inside the directory or past the end of the file;
- stored bytes that do not inflate;
- a buffer that is not RIFF/WAVE, a chunk running past the end, a missing
  `fmt ` or `data` chunk;
- a format tag other than PCM or IMA ADPCM;
- an ADPCM block size too small to hold its own header.

## Verification

All 1,526 entries decode. Sampled effects show lag-1 sample correlation of 0.76
to 0.985 — monster roars and movement sounds are broadband, so lower than
speech, but far from the near-zero a broken decode produces. `observed`

## Music

The game's music needs no reverse engineering: fifteen ordinary **MP3** files
sit beside the archive in `Sounds/`, named `2.mp3` through `16.mp3`, all
44,100 Hz stereo and totalling about 51 minutes. `observed`

Because MP3 is a standard format rather than anything of MM6's, StarHaven
decodes it with a third-party library instead of writing one: **minimp3**,
a public-domain (CC0) single-header decoder, pinned to a commit in
`subprojects/minimp3.wrap`. It is the project's only decoding dependency that
is not either the standard library, zlib, or SDL.

Verification is the same statistic used for the Smacker and ADPCM paths:
decoded tracks show lag-1 sample correlation of 0.982 to 0.998, which is what
music looks like and noise does not.

Two notes on the tracks:

- Their names sort as text in a way that reads wrongly — `10.mp3` before
  `2.mp3` — so discovery sorts numeric stems numerically. `observed`
- There is no `1.mp3`; the set begins at 2. `observed`

## Open questions

- Whether any other MM6 edition ships entries with a different wave format.
  `unknown`
- The sound-name conventions (`01archerA_attack`, `Party Start`) and how the
  game maps events to them. `unknown`
