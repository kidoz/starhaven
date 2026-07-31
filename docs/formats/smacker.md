# Smacker video (`.smk`) as used by Might and Magic VI

Status: **video decoding verified; audio not decoded.** Each claim is tagged
`observed`, `inferred`, or `unknown`.

## Provenance of this specification

Smacker is RAD Game Tools' video format, used by many games and publicly
documented on MultimediaWiki. **This document and the decoder were not derived
by reverse engineering `MM6.exe`.** The implementation was ported from the
project maintainer's own MIT-licensed MM7 engine code, which cites public
format documentation, and was then corrected and verified against MM6's own
videos. The observations below are ours; the layout is public knowledge.

## Scope

Covers the header, frame tables, palette records, the block-coded video stream,
and the DPCM audio tracks — everything MM6 uses. The Bink-audio variant some
Smacker files carry is detected and reported, not decoded; **no MM6 track uses
it**.

## Source provenance (non-expressive)

| Artifact | Value |
| --- | --- |
| Videos | the 127 entries of `Anims1.vid` / `Anims2.vid` |
| Version | all `"SMK2"`; no `"SMK4"` present `observed` |
| Verification | all 127 decode every frame without error `observed` |

## Header (104 bytes)

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| 0x00 | 4 | char[4] | magic | observed | `"SMK2"` or `"SMK4"` |
| 0x04 | 4 | u32 | width | observed | |
| 0x08 | 4 | u32 | height | observed | coded height, before y scaling |
| 0x0C | 4 | u32 | frame_count | observed | excludes the ring frame |
| 0x10 | 4 | i32 | frame_rate | observed | signed; see below |
| 0x14 | 4 | u32 | flags | observed | see below |
| 0x18 | 28 | u32[7] | audio_size | observed | per-track unpacked size |
| 0x34 | 4 | u32 | trees_size | observed | bytes of the Huffman tree block |
| 0x38 | 4 | u32 | mmap_size | observed | allocation hints for the four trees |
| 0x3C | 4 | u32 | mclr_size | observed | |
| 0x40 | 4 | u32 | full_size | observed | |
| 0x44 | 4 | u32 | type_size | observed | |
| 0x48 | 28 | u32[7] | audio_rate | observed | sample rate + flags per track; see below |
| 0x64 | 4 | u32 | dummy | observed | zero on all 127 MM6 videos; unused |

### Frame rate

Both signs occur in MM6, and they use **different units**:

| Sign | Meaning | fps |
| --- | --- | --- |
| positive | milliseconds per frame | `1000 / rate` |
| negative | ten-microsecond units per frame | `100000 / -rate` |
| zero | default | 10 |

`observed`: the values present are −10000 (79 videos), −6666 (18), −12500 (9),
−16666 (4), −20000 (3) and **100 (14)**. Reading the positive form as
microseconds — a plausible-looking mistake — yields 10000 fps for those 14.
Under the rule above every video lands on a sane rate: 10, 15.0015, 8, 6 and 5
fps. `observed`

### Flags

| Bit | Name | Meaning | Status |
| --- | --- | --- | --- |
| 0x01 | ring frame | a trailing frame exists for seamless looping | observed |
| 0x02 | y-interlace | coded row r paints output row 2r only | inferred |
| 0x04 | y-double | coded row r paints output rows 2r and 2r+1 | inferred |

Bit 0 is set in 120 of the 127 MM6 videos, and it is **not** a picture-size
flag. It adds one entry to the frame tables. The proof is arithmetic: for each
video, `104 + 4N + N + trees_size + Σ(size & ~3)` equals the entry's byte
length exactly when `N = frame_count + 1` for all 120 videos that set the bit,
and when `N = frame_count` for the 7 that do not. `observed`

Treating bit 0 as "double height" instead — as some implementations do —
doubles the reported picture height and reads one frame-table entry too few,
which desynchronizes every frame offset. That is the single most consequential
error in this format; MM6's `Bank` is 460×344, not 460×688.

Bits 1 and 2 are `inferred` from public documentation: no MM6 video sets
either, so the vertical-scaling paths are exercised only by synthetic tests.

### Audio track flags

Each `audio_rate` word packs flags above a 24-bit sample rate:

| Bit | Meaning | Status |
| --- | --- | --- |
| 31 | compressed (DPCM rather than raw) | inferred |
| 30 | track present | inferred |
| 29 | 16-bit samples | observed |
| 28 | stereo | observed |
| 27 | Bink audio | inferred |
| 0–23 | sample rate | observed |

Bits 29 and 28 are `observed` because each compressed chunk **restates them**
in its own bitstream, which makes the header checkable rather than assumed:
across all 77 MM6 tracks the header's pair and the chunk's pair agree, with
zero mismatches.

Bits 31 and 30 are `inferred`: both are set on every MM6 track, so this data
cannot say which is "compressed" and which is "present". Nothing in the decoder
depends on telling them apart, since the chunk announces its own compression.

Getting this wrong is easy and quiet. Reading bit 28 as "compressed" — a
plausible-looking assignment — makes every mono track decode as raw PCM and
come out as full-scale noise.

MM6's tracks are 64 stereo 8-bit, 7 stereo 16-bit and 6 mono 8-bit, all DPCM
at 22050 Hz. `observed`

## Audio chunks

A frame's audio chunk begins with a `u32` whose low three bytes are the chunk
length **including that field**, so the payload is `length - 4` bytes.
`observed`

A compressed payload is:

| Order | Contents |
| --- | --- |
| 1 | `u32` unpacked length, in bytes |
| 2 | one bit: data present |
| 3 | one bit: stereo |
| 4 | one bit: 16-bit |
| 5 | one 8-bit Huffman tree per byte per channel — two for 16-bit, doubled again for stereo |
| 6 | base sample per channel, **right channel first** |
| 7 | the delta stream |

The audio trees are written **without a presence bit**: a dummy bit stands in
its place. `observed`

Each sample is the previous sample of that channel plus a delta drawn from that
channel's tree, and the arithmetic **wraps** rather than clamping — 8-bit deltas
wrap in 8-bit space, 16-bit deltas in 16-bit space. Eight-bit samples are
unsigned, so 128 is silence. `observed`

Verification without listening: decoded tracks were checked for lag-1 sample
correlation, which is near 1 for speech and music and near 0 for noise. Of the
43 tracks above an RMS of 600, 37 exceed 0.9 — the cinematics land at 0.94 to
0.998. The rest are ambience so quiet that the signal sits at the 8-bit least
significant bit, where low correlation is expected rather than a fault.

## Frame tables

Immediately after the header, with `N` = `frame_count` plus the ring frame:

| Array | Element | Count |
| --- | --- | --- |
| frame sizes | u32 | N |
| frame types | u8 | N |

A stored size carries flags in its low two bits; the byte length is
`size & ~3`, and bit 0 marks a keyframe. `observed`

The Huffman tree block (`trees_size` bytes) follows, then the frames back to
back in table order. `observed`

## Huffman trees

The tree block holds four trees, built in order: **mmap** (mono-block
bitmaps), **mclr** (mono-block color pairs), **full** (full-block pixels) and
**type** (block descriptors). Each is a 16-bit tree, and each is preceded by a
presence bit; an absent tree is a single leaf decoding to 0. `observed`

A 16-bit tree is itself built from two 8-bit trees supplying the low and high
byte of each leaf, followed by three 16-bit cache values. A leaf whose value
equals a cache slot is stored as a reference to that slot. Every decode moves
its result to the front of the three-entry cache, and the cache is cleared at
the start of each frame. `observed`

Bits are packed **LSB-first within each byte**. `observed`

## Frame payload

The frame type byte selects what precedes the video data:

- bit 0: a palette record. Its first byte is the record length in four-byte
  units, counting itself.
- bits 1..7: one audio chunk per track, each prefixed by a u32 whose low three
  bytes are its length.

Whatever remains is the video bit stream. `observed`

### Palette records

Three opcodes, applied against the palette as it stood **before** the record:

| Opcode | Meaning |
| --- | --- |
| `0x80 \| n` | keep the next `n + 1` entries unchanged |
| `0x40 \| n`, `src` | copy `n + 1` entries starting at old index `src` |
| `r`, `g`, `b` | a literal entry; each channel is 6-bit |

Six-bit channels widen to eight as `(v << 2) | (v >> 4)`, so 0x3F maps to 0xFF
rather than 0xFC. `observed`

Copies read the *old* palette, which matters when a record both writes an entry
and copies from its index in the same pass. `observed`

### Video stream

The picture is coded in 4×4 blocks, left to right and top to bottom. Each
descriptor decoded from the **type** tree carries:

| Bits | Meaning |
| --- | --- |
| 0–1 | block type: 0 mono, 1 full, 2 skip, 3 fill |
| 2–7 | index into a run-length table |
| 8–15 | for fill blocks, the color |

The run table counts 1..59 for indices 0..58, then jumps to 128, 256, 512,
1024 and 2048 so one descriptor can cover a large run. `observed`

| Type | Data read | Effect |
| --- | --- | --- |
| skip | none | keeps the previous frame's pixels |
| fill | none | fills the block with the descriptor's high byte |
| mono | one word from **mclr**, one from **mmap** | two colors and a 4×4 bitmap selecting between them |
| full | words from **full** | literal pixels, two per word: low byte left, high byte right |

Full blocks read the right-hand pair of a row before the left-hand pair.
`observed`

SMK4 prefixes each full block with a mode selector: one bit set means mode 1
(one word per row pair, low byte filling the left half and high byte the
right); otherwise a second bit set means mode 2 (two words per row pair,
repeated across both rows); otherwise mode 0, which is what SMK2 always uses.
`inferred` — no MM6 video is SMK4, so only mode 0 is exercised by real data.

Frames are deltas: a decoder asked for frame N out of order must replay from
the nearest preceding keyframe.

## Invalid-input behavior

The decoder rejects, deterministically and without reading out of bounds:

- a buffer shorter than the header;
- a magic other than `SMK2`/`SMK4`;
- zero or implausible dimensions or frame count;
- frame tables or frame payloads extending past the buffer;
- a malformed tree block.

A bit stream that runs out mid-picture leaves the remaining blocks as they
were, which is what a run of skip blocks would have done.

## Historical question status

> Audited in the [open-question register](../open-questions.md); the register
> supersedes unresolved hypotheses below.

- The Bink-audio variant. The Bink bit (27) is clear on **all 80 audio tracks**
  across the 127 videos, so no MM6 video uses it; it remains undecoded because
  nothing exercises it. `unknown` (the path, not the absence)
- Which of bits 31 and 30 is "compressed" and which is "present". Both are set
  on **all 80 tracks**, so this data cannot distinguish them; nothing in the
  decoder depends on telling them apart, since each chunk announces its own
  compression. `unknown`
- Whether `mmap_size`/`mclr_size`/`full_size`/`type_size` must be honored as
  allocation limits. They are **not** per-tree byte sizes of the tree block:
  their sum exceeds `trees_size` on all 127 videos, and the decoder bounds tree
  growth via `trees_size` and the block's own structure without consulting them
  — all 127 videos decode correctly as a result. They read as worst-case
  allocation hints the original allocator may have used, but whether the
  *original* engine enforced them is not provable from the data. `unknown`
