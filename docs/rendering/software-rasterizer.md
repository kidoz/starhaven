# Software rasterizer

The engine renders 3D terrain with a self-contained **software rasterizer**
that writes directly into the RGBA framebuffer shown via SDL2. There is no
OpenGL dependency: the single SDL2 renderer backend receives a CPU-filled
texture each frame. This keeps the build portable and matches the
software-rendered look of the original game.

## The depth buffer is readable

`Framebuffer::depth()` and `depth_at(x, y)` expose the z-buffer, in the same
[0,1] NDC depth `project_point` reports. Overlays use it: a name or an inspect
panel is suppressed when the world has already drawn something nearer than the
thing being annotated, so nothing is described through a wall.

The comparison carries a small tolerance. A billboard writes its own depth, and
a sprite's centre sits marginally nearer than the world point it is anchored
at, so an exact test makes a monster occlude its own label.

## Layers

- **`src/core/render/math3d.hpp`** — `Vec3`/`Vec4`/`Mat4` (column-major), with
  `mat4_perspective` (NDC depth mapped to `[0,1]`, so the z-buffer is a plain
  comparison), `mat4_look_at`, yaw/pitch rotations, and camera forward/right
  helpers. Pure header, fixed-size, no dependencies.
- **`src/core/render/rasterizer.{hpp,cpp}`** — `Framebuffer` (RGBA + `[0,1]`
  z-buffer) with `clear`/`clear_depth` and z-buffered `draw_triangle`. Triangle
  rasterization uses edge functions with a top-left fill rule, barycentric
  depth interpolation, optional backface culling (CCW front faces), and
  per-fragment brightness (flat/smooth shading channel). `clip_near` clips a
  view-space triangle against the near plane into 0, 1, or 2 triangles.
- **`src/core/render/terrain_mesh.{hpp,cpp}`** — builds an indexed mesh from an
  `OdmTerrain` heightmap: 128×128 vertices positioned at
  `(x·cell_size, height·height_scale, y·cell_size)` centered on the origin,
  per-vertex normals via finite differences, and 2 triangles per cell.

## Depth convention

`mat4_perspective` maps view-space `z ∈ [-far, -near]` to NDC `z ∈ [0,1]`
(0 at the near plane, 1 at the far plane). The z-buffer is cleared to 1.0
(farthest) and a fragment passes when its interpolated NDC z is **less than**
the stored value. This avoids the depth-precision quirks of flipping the range.

## Frame pipeline (in `src/main.cpp`)

For each frame:
1. Read keyboard input (WASD move, Q/E fly, arrows look, shift = fast).
2. Build `view = look_at(cam_pos, cam_pos + forward, up)` and
   `proj = perspective(60°, aspect, near=1, far=20000)`.
3. For each terrain triangle: transform to view space, `clip_near`, project the
   survivors to screen space, and `draw_triangle` with backface culling and flat
  Lambertian shading (per-face normal · sun direction + ambient).
4. `SDL_UpdateTexture` the framebuffer and present.

## How future geometry plugs in

When the `.odm` vertex/facet/model geometry is decoded, it feeds the same
`draw_triangle` path: geometry vertices are transformed identically, and facets
become triangles. The rasterizer needs no changes for that — it already handles
arbitrary indexed triangle lists with z-buffering and shading.

## Non-goals (this slice)

- No texturing (flat/Lambertian shading only).
- No mouse-look (arrow-key look; relative-mouse is a follow-up).
- No collision/gravity — the camera flies freely.
- No `.odm` model/prop geometry (terrain only).
