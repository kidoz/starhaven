---
title: "Terrain texturing"
summary: "Implemented data path from Might and Magic VI outdoor tile identifiers to textured StarHaven terrain."
doc_type: explanation
status: verified
last_updated: 2026-08-01
source_files:
  - src/core/world/tile_table.cpp
  - src/core/world/map_session.cpp
  - src/core/render/terrain_mesh.cpp
  - src/core/render/tile_set.cpp
tags:
  - rendering
  - terrain
  - textures
  - dtile
---
# Terrain texturing

StarHaven resolves every used outdoor tilemap byte through `DTILE.BIN` to a
named bitmap in `BITMAPS.LOD`, then installs the decoded game texture in a
dense `TileSet`. The terrain mesh retains the byte per triangle and uses
repeating, perspective-correct texture coordinates. Generated checker textures
are used only when no real ground texture can be resolved.

## Data path

The canonical binary mapping is documented in
[the ground tile table reference](../formats/dtile.md). The implementation
keeps each stage separate:

| Stage | Input | Output | Implementation |
| --- | --- | --- | --- |
| Terrain parser | 128×128 ODM tilemap | one `u8` tile id per cell | `src/core/world/odm_map.cpp` |
| Tile-table parser | `icons.lod/DTILE.BIN` | tile id to bitmap name and attributes | `src/core/world/tile_table.cpp` |
| Archive and image decoder | named `BITMAPS.LOD` entry | RGBA bitmap | `src/core/world/map_session.cpp` |
| Texture table | tile id and RGBA bitmap | dense 256-slot `render::TileSet` | `src/core/render/tile_set.cpp` |
| Scene submission | triangle tile id | sampled terrain texture | `src/main.cpp` |

`load_ground_tiles` resolves only ids used by the loaded map. Empty
`DTILE.BIN` names are reserved rows rather than parse errors; missing archive
entries or invalid bitmaps leave those individual texture slots empty.

## Mesh and sampling

`TerrainMesh::tile_ids` stores the tilemap byte for both triangles of each
terrain cell. `TerrainMesh::uvs` stores coordinates in cell units, so the
difference across one cell is exactly `1.0` even though adjacent cells share
vertices. Sampling with `WrapMode::Repeat` therefore places one full ground
bitmap over each cell without duplicating mesh vertices.

The textured triangle path interpolates `u/w`, `v/w`, and `1/w` and
divides per fragment. Ground art therefore remains stable under perspective
rather than swimming as an affine texture would.

## Placeholder fallback

`TileSet::make_placeholder` creates a two-tone checker for every possible
byte value. Its base color comes from `tile_type_color`, and its darker
quadrant makes broken or distorted UVs visible in hermetic tests.

The outdoor loader selects the placeholder set only when
`load_ground_tiles` resolves zero real textures. Placeholder pixels are
engine-generated test and failure-mode assets; they are not copied game art
and do not describe the original format.

## Water animation

For resolved tiles classified as water, the loader retains the raw bitmap and
finds its longest blue palette run. The frame loop rotates that run, decodes
the bitmap again, and replaces the corresponding `TileSet` slot every 180
milliseconds. The palette bytes are `observed`; choosing the blue run and the
180-millisecond cadence are StarHaven implementation decisions, not claims
about the original executable.

## Evidence boundary

The direct tilemap-byte to `DTILE.BIN` row lookup, bitmap names, group fields,
and section ordinals are `observed` and canonical in
[`dtile.md`](../formats/dtile.md). Perspective-correct sampling, the
placeholder checker, ground-kind classification, and water animation cadence
are engine behavior. Tests for the boundary live in
`tests/test_tile_table.cpp`, `tests/test_tile_set.cpp`,
`tests/test_terrain_mesh.cpp`, and `tests/test_texture.cpp`.
