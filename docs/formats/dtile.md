# Ground tile table (`DTILE.BIN`, Might and Magic VI)

Resolves the open question in `docs/rendering/terrain-coloring.md`: how an
`.odm` tilemap byte selects a ground texture. Each claim is tagged `observed`,
`inferred`, or `unknown`.

## Scope

Covers the global tile table and the tilemap-byte lookup. Does **not** settle
how the per-record `section` field selects pixels within a shared bitmap; see
"Open questions".

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
| +0x10 | 2 | u16 | unknownA | unknown | 0 in every observed record |
| +0x12 | 2 | u16 | unknownB | unknown | 0 in every observed record |
| +0x14 | 2 | u16 | tilesetGroup | observed | matches the map header's tileset group ids |
| +0x16 | 2 | u16 | section | inferred | 0-based ordinal within the group |
| +0x18 | 2 | u16 | attributes | inferred | bit flags; 0, 2, 64, 512 observed |

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

## Open questions (next slice)

- **How `section` selects pixels.** Records 90–101 all carry the name
  `grastyl` with `section` 0–11, but `BITMAPS.LOD` holds a single 128×128
  `Grastyl`. Twelve sections cannot be 64×64 cells of a 128×128 atlas (that
  would allow four). Either the tiles are smaller than 64×64, `section` is not
  an atlas index, or several records intentionally share one image.
  `unknown` — this must be settled before textures can be considered correct
  rather than merely plausible.
- **`unknownA` / `unknownB`** are zero in every observed record. `unknown`
- **`attributes` bit meanings.** 512 co-occurs with every transition tile
  observed; 2 with water; 64 with the sentinel. `inferred`, not confirmed.
- **What the map header's four tileset definitions are for**, given the lookup
  does not need them. They may declare which groups the map loads. `unknown`
- Whether the direct lookup holds for maps whose tilesets differ from
  `Outa1.odm`. Only one map has been checked. `unknown`
