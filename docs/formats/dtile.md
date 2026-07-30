# Ground tile table (`DTILE.BIN`, Might and Magic VI)

Resolves the open question in `docs/rendering/terrain-coloring.md`: how an
`.odm` tilemap byte selects a ground texture. Each claim is tagged `observed`,
`inferred`, or `unknown`.

## Scope

Covers the global tile table and the tilemap-byte lookup, and settles that the
per-record `section` field is an ordinal within the group, not a pixel
selector: see "`section` is an ordinal, not an atlas index".

## Source provenance (non-expressive)

Observed against one user-supplied, legally obtained installation. Only
structural facts, names, sizes and counts are recorded here.

| Artifact | Value |
| --- | --- |
| `data/icons.lod` | 32,772,165 bytes, 2703 entries `observed` |
| `data/BITMAPS.LOD` | 46,673,937 bytes, 1958 entries `observed` |
| Entry `DTILE.BIN` | in `icons.lod`, stored 2112 bytes, container-uncompressed `observed` |
| Map used | `Outa1.odm`, `ground_name` = `"grastyl"` `observed` |

Reproduce with:

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/tile_probe --grep tile
./buildDir/tile_probe Outa1.odm
./buildDir/lod_browser extract <install>/data/icons.lod DTILE.BIN
```

## Where the table lives

`DTILE.BIN` is an entry of `icons.lod`, not of `BITMAPS.LOD`, and not of
`EVENTS.LOD` (which is absent from this installation). `observed`

## Outer layout of the entry

The stored entry is itself a container with the same 48-byte-header + zlib
shape the image entries use (see `bitmap.md`), even though the LOD directory
marks it container-uncompressed.

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| 0x00 | 16 | char[16] | name | observed | `"dtile.bin"`, NUL-padded |
| 0x14 | 4 | u32 | packedSize | observed | 2064 = 2112 − 48 |
| 0x28 | 4 | u32 | unpackedSize | observed | 22936 |
| 0x2C | 4 | u32 | unknown | unknown | 256 on this artifact |
| 0x30 | … | bytes | zlibData | observed | standard zlib stream (`78 9c`) |

## Decompressed table

| Offset | Size | Type | Field | Status |
| --- | --- | --- | --- | --- |
| 0x00 | 4 | u32 | recordCount | observed | 882 |
| 0x04 | 26 × count | TileRecord[] | records | observed | |

`882 × 26 + 4 = 22936`, matching `unpackedSize` exactly. `observed`

### TileRecord (26 bytes)

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| +0x00 | 16 | char[16] | name | observed | bitmap name, NUL-padded, lowercase |
| +0x10 | 2 | u16 | unknownA | observed | 0 on all 882 records; unused |
| +0x12 | 2 | u16 | unknownB | observed | 0 on all 882 records; unused |
| +0x14 | 2 | u16 | tilesetGroup | observed | matches the map header's tileset group ids |
| +0x16 | 2 | u16 | section | observed | 0-based ordinal within the group; not a pixel selector |
| +0x18 | 2 | u16 | attributes | observed | four bits; see below |

Record 0 is a sentinel: name `"pending"`, group 255, section 255. `observed`

### Group layout

Groups are contiguous runs, almost all exactly 36 records. `observed`

| Group | First index | First name | Count |
| --- | --- | --- | --- |
| 4 | 1 | `dirttyl` | 12 |
| 0 | 13 | `pending` | 113 |
| 5 | 126 | `wtrtyl` | 36 |
| 6 | 162 | `crktyl` | 36 |
| 3 | 198 | `voltyl` | 36 |
| 2 | 234 | `sandtyl` | 36 |
| 7 | 270 | `Swmtyl` | 36 |
| 8 | 306 | `Troptyl` | 36 |
| 1 | 342 | `snotyl` | 36 |
| 9 | 378 | `cstyl` | 36 |
| 22 | 774 | `drsrCROS` | 36 |

(Remaining groups 10–13, 16–17, 23–28 follow the same 36-record pattern.)

## The lookup: tilemap byte → record

**A tilemap byte is a direct index into this table.** `observed`

The competing hypothesis — that indices ≥ 90 are remapped through the map
header's four `(group, offset)` tileset definitions in blocks of 36 — is
**refuted** by the data:

| Evidence | Direct | Remapped |
| --- | --- | --- |
| `ground_name` is `"grastyl"`; index 90 covers 8989 of 16384 cells | record 90 = `grastyl` ✅ | record 162 = `crktyl` ❌ |
| Map also uses indices 1–4 | `dirttyl` (dirt) ✅ | `dirttyl` ✅ |
| Map uses 102–113 (306, 220, 216, 204, 118, 115, 111, 92, 88, 86 cells) | `grdrtE/N/S/W/NE/SE/XNE/XSE/XNW/XSW` — grass↔dirt transitions, coherent with the dirt at 1–4 ✅ | `crkdrt*` — cracked↔dirt transitions, incoherent with a grass map ❌ |

The `grdrt*` directional naming is a standard autotile transition set, and it
co-occurs in the same map with both of the surfaces it transitions between.
That, plus the exact `ground_name` match at index 90, settles it.

Consistency check: the map header's tileset definitions
`(6,162) (5,126) (6,162) (22,774)` each point at a record whose `tilesetGroup`
equals the declared group — 162→group 6, 126→group 5, 774→group 22. `observed`

## The lookup: record → pixels

`record.name` names an entry in `BITMAPS.LOD`, matched case-insensitively.
`observed` — `"grastyl"` resolves to entry `Grastyl`, the only `gras*` entry in
the archive.

`Grastyl` is 128×128 (`size` = 16384, `width` = 128, `widthLn2` = 7).
`observed`

## `section` is an ordinal, not an atlas index

The worry that opened this slice — twelve records named `grastyl` carrying
`section` 0–11 against a single 128×128 `Grastyl` bitmap — resolves to: **the
records intentionally share one image.** `section` does not select pixels.

The evidence is direct:

- `BITMAPS.LOD` holds exactly **one** `Grastyl`, 128×128. There is no atlas of
  twelve sub-tiles to index. `observed`
- The twelve `grastyl` records in `DTILE.BIN` are **byte-identical except for
  the `section` field itself**: masking `+0x16` collapses all twelve to one
  record. They name the same bitmap, the same group, the same attributes.
  `observed`
- The transition tiles that *do* differ are **distinct bitmaps with distinct
  names** — `GrdrtN`, `GrdrtNE`, `GrdrtXSE` … each a separate `BITMAPS.LOD`
  entry — not sub-rectangles of one. `observed`

`section` is therefore a 0-based ordinal within the group (the records' own
order), and a tile resolves to its art **by name**, which is what the engine
does. A name-based lookup is correct, not merely plausible. `observed`

The two readings the open question offered as alternatives — "the tiles are
smaller than 64×64" and "section is not an atlas index" — are both settled: the
tiles are 128×128, and section is not an atlas index.

## Attribute bits

The `attributes` word uses four bits across all 882 records:

| Bit | Value | Meaning | Records | Status |
| ---: | ---: | --- | ---: | --- |
| 1 | 2 | water base tile (`wtrtyl`) | 12 | observed |
| 6 | 64 | sentinel (`pending`) | 65 | observed |
| 8 | 256 | water-edge — set alone on the 12 empty-name water records, with bit 9 on the 12 `wtrdr*` water-to-dirt transitions | 24 | observed |
| 9 | 512 | transition / road tile (the `*dr*` directionals) | 303 | observed |

The remaining 490 records carry 0. The earlier inference (512 = transition,
2 = water, 64 = sentinel) holds and gains bit 8 as the water-edge marker.
`observed`

## The four map tileset definitions are indices into this table

The four `TilesetDef` entries in each map header (see
[`odm.md`](odm.md)) are not redundant: each `(group, offset)` is an index
**into `DTILE.BIN` itself**. `group` is the tileset group id, and `offset` is
the record index where that group begins.

The proof is across all 15 outdoor maps. For every 36-entry group the tileset
`offset` equals the group's first record index exactly: group 1 → 342,
group 2 → 234, group 3 → 198, group 5 → 126, group 6 → 162, group 7 → 270,
group 8 → 306, group 22 → 774. `observed` (Group 0, the 113-entry base
terrain, starts at record 13 and is addressed by offset 90, a point within its
own run.)

The four slots hold the same shape on every map: `tileset[1]` is always
`(5, 126)` (water) and `tileset[3]` is always `(22, 774)`, while `tileset[0]`
and `tileset[2]` are the map's terrain pair (grass/dirt/snow/etc.). A tile
resolves either way — by the name this table carries, or by the group+offset
the map header carries — and both reach the same record. The engine's primary
path is the index; the name is the readable convenience. `observed`

## The lookup holds on every outdoor map

All 15 `.odm` maps share the same `DTILE.BIN` and the same four-slot tileset
shape; only the terrain pair in slots 0 and 2 varies. The name-based lookup
that holds on `Outa1.odm` therefore holds on all fifteen — verified by
decompressing each map's header and reading its tileset block. `observed`

## Open questions (next slice)

- Whether indoor (`.blv`) maps carry an equivalent tile lookup; the `.blv`
  tail remains undescribed (see [`blv.md`](blv.md)). `unknown`
