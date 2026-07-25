# VID video container (Might and Magic VI)

Status: **verified.** The container holding MM6's cutscene and shop videos.
Each claim is tagged `observed`, `inferred`, or `unknown`.

## Scope

Covers the `.vid` archive directory. The videos inside it are Smacker streams,
documented in [`smacker.md`](smacker.md).

## Source provenance (non-expressive)

| Artifact | Value |
| --- | --- |
| `Anims/Anims1.vid` | 46,157,620 bytes, 40 entries `observed` |
| `Anims/Anims2.vid` | 208,887,084 bytes, 87 entries `observed` |
| Payload format | every one of the 127 entries begins `"SMK2"` `observed` |

Reproduce with:

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/play_smk --list
```

## Layout

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| 0x00 | 4 | u32 | count | observed | number of directory entries |
| 0x04 | count × 44 | Entry[] | directory | observed | |

### Entry (44 bytes)

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| +0x00 | 40 | char[40] | name | observed | NUL-padded; no extension |
| +0x28 | 4 | u32 | offset | observed | absolute file offset of the payload |

The directory stores no sizes: an entry runs from its offset to the next
entry's, and the last runs to the end of the file. `observed`

The arithmetic checks exactly on both archives — `4 + count × 44` equals the
first entry's offset (1,764 for 40 entries; 3,832 for 87), so the payloads
begin immediately after the directory with no padding. `observed`

Entry names carry no extension (`Bank`, `3dologo`, `Apthcmid`). Lookups are
case-insensitive, matching how the rest of the archives behave. `inferred`

## Invalid-input behavior

The reader rejects, deterministically and without reading out of bounds:

- a buffer too short to hold the count;
- a count of zero, or one whose directory would not fit in the file;
- an offset that lands inside the directory or past the end of the file;
- offsets that do not ascend, which would otherwise underflow a derived size.

Unlike the `.LOD` readers, this one keeps the file open and reads a single
entry on demand rather than holding the archive in memory: `Anims2.vid` is
208 MB and callers want one video at a time.

## Open questions

- Whether any MM6 build ships a `.vid` containing Bink rather than Smacker
  payloads. None of the 127 entries here does. `unknown`
