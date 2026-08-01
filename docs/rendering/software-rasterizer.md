---
title: "Software rasterizer"
summary: "Architecture, depth convention, scene pipeline, and SDL3 presentation path of the StarHaven software renderer."
doc_type: explanation
status: verified
last_updated: 2026-08-01
source_files:
  - src/core/render/math3d.hpp
  - src/core/render/rasterizer.cpp
  - src/core/render/scene.cpp
  - src/main.cpp
tags:
  - rendering
  - software-rasterizer
  - sdl3
  - graphics
---
# Software rasterizer

StarHaven draws the 3D world with a CPU software rasterizer and has no OpenGL
or other GPU rendering dependency. The renderer writes RGBA color and
normalized depth buffers, while SDL3 presents the completed color buffer in a
streaming texture. Outdoor terrain, model geometry, indoor faces, sprites,
lighting, and depth-aware overlays share this path.

## Architecture

- `src/core/render/math3d.hpp` provides fixed-size vectors, matrices,
  perspective projection, camera orientation, and renderer-space helpers.
- `src/core/render/rasterizer.{hpp,cpp}` owns the RGBA framebuffer, the
  `[0,1]` depth buffer, triangle clipping, flat and textured triangle
  rasterization, and debug primitives.
- `src/core/render/scene.{hpp,cpp}` converts world-space triangles and
  billboards into the rasterizer's clipped, projected inputs for one camera.
- `src/core/render/terrain_mesh.{hpp,cpp}` turns the 128×128 outdoor height
  grid into indexed triangles with normals, texture coordinates, and tile ids.
- `src/main.cpp` resolves scene assets, submits world and interface layers,
  and presents the framebuffer through SDL3.

## Depth convention

`mat4_perspective` maps view-space depth from the near and far planes to NDC
`z ∈ [0,1]`, with zero at the near plane and one at the far plane. The depth
buffer is cleared to `1.0`; a fragment passes when its interpolated NDC depth
is less than the stored value. `clip_near` clips view-space triangles before
projection so the renderer never divides geometry behind the camera by a
non-positive clip-space `w`.

Texture coordinates use perspective-correct interpolation. The projection
stores `u/w`, `v/w`, and `1/w`; the rasterizer interpolates those values
in screen space and divides per fragment.

## Frame pipeline

For each visible game frame, `src/main.cpp`:

1. updates the camera and calls `SceneRenderer::begin` to clear color and
   depth;
2. draws the outdoor sky and terrain plus model facets, or the indoor face
   geometry;
3. draws camera-facing decorations, actors, objects, and launched sprites;
4. draws depth-tested labels and world overlays, followed by the game-screen
   frame, party strip, books, dialogs, and other interface layers;
5. uploads `Framebuffer::color()` with `SDL_UpdateTexture` and presents the
   SDL3 renderer.

World geometry is near-clipped, projected, depth-tested, and shaded through
`SceneRenderer::draw_triangle`. Alpha-zero textured fragments do not write
color or depth, which preserves cut-out foliage, fences, and sprite
silhouettes.

## Textures and lighting

Outdoor tile ids resolve to real ground textures through
[`DTILE.BIN`](../formats/dtile.md). Outdoor model facets and indoor faces use
their decoded texture names, and sprite-frame tables select directional
billboards. Texture samples are modulated by daylight, the outdoor sun, indoor
static lights, or per-object brightness before the fragment reaches the color
buffer.

If an outdoor installation cannot resolve any ground tiles, the loader
substitutes generated checker textures so geometry and UV behavior remain
testable without proprietary fixtures. The fallback boundary is described in
[terrain texturing](terrain-coloring.md).

## Depth-aware overlays

`Framebuffer::depth()` and `depth_at(x, y)` expose the same `[0,1]` NDC
depth reported by `SceneRenderer::project_point`. Labels and inspection
panels compare their projected anchor against the existing depth, with a small
tolerance for the billboard that owns the label. As a result, an annotation is
suppressed when nearer world geometry already occupies that pixel.

## Portability boundary

SDL3 creates the window, accepts input, streams audio, and presents the
CPU-filled texture; SDL3 does not rasterize the 3D scene. Rendering behavior is
therefore controlled by portable C++20 code and covered by synthetic unit tests
under `tests/test_math3d.cpp`, `tests/test_rasterizer.cpp`,
`tests/test_scene.cpp`, `tests/test_texture.cpp`, and
`tests/test_terrain_mesh.cpp`.
