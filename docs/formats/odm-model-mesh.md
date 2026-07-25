# ODM model mesh vertices (Might and Magic VI)

Status: **draft, evidence-backed — superseded in part.** This document records
the verified layout of a model's vertex array. The "blocker" it described for
walking subsequent models was resolved by the next slice, and its
variable-length conclusion was **wrong**: see
[`odm-model-facets.md`](odm-model-facets.md) for the corrected geometry-stream
layout and for how all models are now walked. Each claim is tagged `observed`,
`inferred`, or `unknown`.

## Scope

This document covers the **vertex arrays of placed models** — the 3D points
that define each prop/building's mesh. The first model's vertices are decoded
here; the facet data that sits between models' vertex arrays is covered by
[`odm-model-facets.md`](odm-model-facets.md), which also documents the rest of
the per-model geometry block.

## What is verified

### Model record vertex/facet counts (file data)

The 188-byte `MapModel` record (see [`odm-models.md`](odm-models.md)) carries
two genuine file-data count fields (the `+0x48`/`+0x54` fields are runtime
memory pointers, not file data):

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| 0x44 | 4 | u32 | vertex_count | observed | e.g. bridge2ns=100, burnt1E=109 |
| 0x4C | 4 | u32 | facet_count | observed | e.g. bridge2ns=62, burnt1E=62 |

`observed` across six sampled models: small, plausible counts.

### ModelVertex layout (12 bytes)

Each vertex is three little-endian i32 world coordinates:

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| 0x00 | 4 | i32 | x | observed | world X |
| 0x04 | 4 | i32 | y | observed | world Y (height) |
| 0x08 | 4 | i32 | z | observed | world Z |

`observed`: the first 8 vertices of `Outa1.odm` model 0 (`bridge2ns`), read as
12-byte triples, all fall inside that model's bounding box
(X[-13704..-13368], Y[-8000..-5824], Z[640..896]). Reading the same bytes as
6-byte (i16×3) triples produces garbage, ruling out that interpretation.

### First model's vertex location

The geometry section begins immediately after the model array, at offset
`0xC0B4 + model_count × 188`. The first model's `vertex_count` vertices (12
bytes each) are stored there. `observed` on Outa1: 100 vertices at 0xD198.

## The blocker for subsequent models (resolved — and misdiagnosed)

After the first model's vertices come its **facets**, and the next model's
vertices sit behind them, so walking model N depends on decoding models
0..N-1's facets first. That much held. `observed`

This document originally concluded the facets were **variable-length**, from
the observation that no fixed record size among 80/52/44/28 bytes placed model
1's first vertex inside its bounding box. That conclusion was wrong — the set
tested simply did not contain the answer. The record is a fixed **308 bytes**,
followed by two more per-facet arrays. See
[`odm-model-facets.md`](odm-model-facets.md) for the layout and the evidence.

The lesson worth keeping: a negative result over a handful of candidate sizes
is evidence about those candidates, not about the shape of the format.

## Decoding (this slice)

1. Parse the model array (see `odm-models.md`).
2. The geometry section starts at `0xC0B4 + model_count × 188`.
3. Read the first model's `vertex_count` (record field +0x44).
4. Read `vertex_count × 12` bytes as i32×3 world coordinates.

## Invalid-input behavior

The decoder rejects, deterministically and without reading out of bounds:

- a payload too short to hold the model array + first model's vertex_count × 12;
- a first-model vertex_count that would read past the end of the payload.

## Open questions

Both questions this document opened are answered in
[`odm-model-facets.md`](odm-model-facets.md): the facet record is 308 bytes,
and facets do come immediately after each model's vertices. Facet normals are
stored per facet in 16.16 fixed point. What remains unknown there is the
meaning of several spans inside the facet record and of the facet ordering
array.
