---
title: "Collision and movement"
summary: "Engine-side collision queries, swept movement, controls, and known limitations for StarHaven map traversal."
doc_type: explanation
status: partial
last_updated: 2026-08-01
tags:
  - rendering
  - collision
  - movement
  - engine
---
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
coupling collision behavior to the indoor BSP or sector-based render culling.

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

## Where this lives

`CollisionWorld` is engine code in `src/core/world/`. The player's proportions
and the movement step are viewer policy, so they sit in `src/game/player.hpp`
alongside the argument helpers — shared by both walkers without pretending they
are engine behaviour.

Rendering is shared too: `render::SceneRenderer` owns the transform, near-clip
and projection sequence, and `assets::AssetCache` resolves texture and sprite
names through the archives. Both walkers were carrying their own copy of each
before that.

## Controls

Mouse-look uses SDL's relative mouse mode, enabled only when there is a window
to capture — the screenshot path leaves it off. `--fly` restores the old
free camera with no gravity or collision, which is still the easier way to
inspect a level from outside.

## Screenshots settle first

Both walkers simulate 90 frames before writing a capture. A frame-one
screenshot shows the camera wherever `--pos` put it, which is usually mid-air
and misrepresents where a player actually stands.

## Terrain has a rise limit

Terrain is sampled rather than collided, so nothing stops the player being
placed at whatever height the destination has. Until this was fixed, walking
into a vertical cliff **teleported the player to the top of it** — not "steep
slopes are climbable", as an earlier revision of this page put it, but no rise
limit at all, at any speed.

A destination whose ground stands more than `kStepHeight` above the current
one is refused. The move is then retried along each axis separately, so a cliff
can be walked *along* rather than only away from — refusing the whole move
would pin the player against it.

## The step up is smoothed

Arriving on a ledge raises the feet at `kStepRate` — 600 units per second, so a
96-unit stair takes about a sixth of a second. Falling is unaffected: gravity
has already moved the feet down by the time this runs, and it only ever moves
them up.

## The body is swept

A move is divided into pieces no longer than half a body radius and collided
piece by piece. Testing only the endpoints lets a fast step straddle a thin
wall completely: at 20,000 units per second a single frame covers 333 units,
which is wider than most walls are thick.

## Known limitations

- The rise limit uses the sampled height at the destination, so a thin spike
  between two walkable points is not seen.
- Vertical movement is not swept; only the horizontal component is.

## Bounds, and who gets tested

Each polygon keeps its own bounding box, and a sweep dismisses a polygon by
comparing boxes before touching its plane. The box has to cover the **whole
step**, start and end: the one-sided plane test is what stops a fast step from
tunnelling, and dismissing a wall the destination has already passed through
brings the tunnelling back. A test caught exactly that.

Monsters use the same sweep, with one restriction: only those within 6,000
units of the party are swept against the level at all. On Mire of the Damned —
408 monsters against 3,062 polygons — sweeping all of them costs **5.2 ms per
update**, a third of a frame at sixty; sweeping the ones near the party costs
**0.21 ms**. A monster that drifts into a wall while the party is elsewhere is
pushed out on the step after they come near. `inferred`, and a trade rather
than a finding.
