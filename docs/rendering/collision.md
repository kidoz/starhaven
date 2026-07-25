# Collision and movement

How the walkers turn decoded map geometry into something you move through
rather than fly past. This is engine design, not a format specification: no
reverse engineering is involved, and none of the constants come from the game.

## What it is built from

`CollisionWorld` (`src/core/world/collision.{hpp,cpp}`) holds convex polygons in
renderer axes, each with the plane its format already provides:

- **Indoor**: every `.blv` face except those flagged invisible — the portals,
  which are boundaries between rooms rather than walls. Drawing or colliding
  with them would seal every doorway.
- **Outdoor**: every model facet. Terrain is *not* added as polygons; the
  heightfield is sampled directly, which is both cheaper and exact.

Both formats' planes were verified by the `normal · v + distance ≈ 0` check
during decoding, so the collision world inherits geometry already known to be
consistent.

## Queries

`floor_below` casts a vertical line and returns the **highest** surface at or
below the player. Highest rather than first is what keeps a walkway over a pit
from dropping the player through it.

`slide` moves a vertical cylinder and pushes it out of any wall it would end up
inside, along the wall's normal. Pushing along the normal is what produces
sliding: motion parallel to a wall survives, only the component into it is
removed. Two probe heights (knee and shoulder) mean a low sill and a high beam
both register, and up to four passes let a corner push out of both walls.

Faces are one-sided, so a wall only blocks a player who was **in front of it at
the start of the step**. That test doubles as tunnelling protection: the
signed distance at the destination alone cannot distinguish "just short of a
wall" from "already through it", but comparing against the start position can.

Queries are brute force over every polygon — a few thousand per level. That is
far cheaper than the rasterization already happening each frame, and it avoids
depending on the indoor BSP and room data, which is still undecoded.

## Movement

Per frame: horizontal intent, then collision, then gravity. Keeping them
separate is what lets a blocked step still slide.

Player proportions, in MM6 world units (a terrain cell is 512 across):

| Constant | Value | Why |
| --- | --- | --- |
| body radius | 64 | under a third of a cell, so doorways are passable |
| body height | 320 | |
| eye height | 280 | |
| step height | 96 | stairs and kerbs this tall are walked up, not blocked |
| gravity | −2400 /s² | tuned for the scale, not taken from the game |

These are engine choices, chosen to feel right at MM6's scale. The original's
values are unknown.

## Controls

Mouse-look uses SDL's relative mouse mode, enabled only when there is a window
to capture — the screenshot path leaves it off. `--fly` restores the old
free camera with no gravity or collision, which is still the easier way to
inspect a level from outside.

## Screenshots settle first

Both walkers simulate 90 frames before writing a capture. A frame-one
screenshot shows the camera wherever `--pos` put it, which is usually mid-air
and misrepresents where a player actually stands.

## Known limitations

- No step-up *smoothing*: the player snaps to the new floor height.
- The cylinder is tested at two heights, not swept as a volume, so a very fast
  step past a thin wall's edge can still slip by.
- Outdoor terrain has no wall behaviour: steep slopes are climbable at any
  angle, because the heightfield is sampled rather than treated as polygons.
