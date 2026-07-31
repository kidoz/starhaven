# Terrain coloring by tile type

Status: **evidence-backed interim approach.** This documents the current
terrain-coloring strategy in `starhaven`: coloring each terrain cell by its
**tile-type index** rather than by a decoded ground-tile texture. The final
tile-index→texture mapping is deferred (needs the `MM6.exe` tile-loader trace).

## Why not real textures yet

An `.odm` map references a ground tileset (e.g. `"grastyl"`) and carries a
128×128 **tilemap** of indices (1–~212) selecting the ground tile per cell.
Mapping those indices to actual tile bitmaps requires resolving the engine's
tileset tables (`dtile.bin` / the `Grastyl`-style atlases plus a
tile-index→atlas-cell table) — a lookup chain whose exact on-disk form is
unconfirmed and needs the loader trace.

## What this slice does instead

Each distinct tile-type index gets a **stable, deterministic color** derived
from a hash of the index. The result: large same-tile regions (e.g. the
dominant base tile 90) render as a uniform color, while roads, water,
transitions, and feature tiles each get their own color. Combined with the
heightfield shading, this makes terrain regions clearly distinguishable in the
3D view — a big readability gain over flat-shaded gray, using only data we
already decode reliably (the tilemap, slice 6).

## Coloring rule

`tile_type_color(index)` returns an RGB color:

- a fixed palette for the most common base tiles (so the dominant ground reads
  as a natural green and water reads as blue);
- a deterministic hash-to-RGB for all other indices, so each distinct tile is a
  distinct, stable color.

The brightness from the heightfield's Lambertian shading is still applied on
top, so elevation and tile type are both visible.

## Verified

On `Outa1.odm` the tilemap shows clear spatial regions (a large tile-90 base,
a textured eastern band of tiles 162–165, scattered paths/features, and
water/transition tiles in the 180s–200s). Coloring these distinctly produces a
readable region map. `observed`.

## Update: the renderer now takes textures, the mapping still does not exist

`starhaven` no longer flat-shades the terrain. It samples a texture per cell
through the perspective-correct textured rasterizer:

- `TerrainMesh::uvs` carries per-vertex texture coordinates in **cell units**
  (the vertex at grid `(x, y)` gets uv `(x, y)`). Terrain vertices are shared
  between adjacent cells, so no per-vertex assignment can give each cell its
  own 0..1 span; cell units sidestep that, because the *difference* across any
  one cell is exactly 1.0. Sampling with `WrapMode::Repeat` therefore lays one
  full tile across every cell while the vertices stay shared.
- `TerrainMesh::tile_ids` carries the tilemap index per triangle (both
  triangles of a cell agree), read at the cell's top-left corner — the same
  convention `build_terrain_colors` uses.
- `render::TileSet` maps a tile index to a `Texture`.

**The textures are generated stand-ins, not MM6 art.** `TileSet::make_placeholder`
synthesizes a two-tone checker per index, tinted by the same
`tile_type_color()` palette this document describes. The checker exists so that
a wrong UV shows up as a warped or missing pattern instead of hiding inside a
flat color. Nothing here reads or reproduces game content, and the placeholder
generator is hermetic — the unit tests need no install.

So the readability story is unchanged (tile regions are still distinguished by
color); what changed is that the pixels now travel the real texturing path.

## Historical question status

> Audited in the [open-question register](../open-questions.md); the register
> supersedes unresolved hypotheses below.

The tile-index→ground-tile-bitmap mapping, so the terrain can show the actual
MM6 ground textures instead of type colors. Needs the `MM6.exe` tile-loader
trace (same category of work as the model-facet and event-record blockers).

`TileSet` is the single seam where that lands. A real loader only has to
populate a `TileSet` from the map's ground tileset name (e.g. `"grastyl"`) plus
the tile-index→atlas-cell table; `starhaven`, the mesh, and the rasterizer need
no further change. `status: unresolved`.
