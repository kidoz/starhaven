# StarHaven Documentation

StarHaven is a portable, open-source compatibility engine for
**Might and Magic VI: The Mandate of Heaven**. It reads resources from a user's
legally obtained installation and does not distribute original game content.

The project is currently focused on decoding the original data formats and
rendering outdoor and indoor maps with a portable C++20 software renderer.
Audio, UI, and gameplay systems remain future work.

## Explore the documentation

- [LOD archives](formats/lod.md) and
  [Games.lod](formats/games-lod.md) describe the game's resource containers.
- [Outdoor maps](formats/odm.md), [terrain](formats/odm-terrain.md), and
  [model facets](formats/odm-model-facets.md) cover the outdoor world format.
- [Indoor maps](formats/blv.md) describe dungeon and building geometry.
- [Ground tiles](formats/dtile.md), [bitmaps](formats/bitmap.md), and
  [sprites](formats/sprite.md) cover visual resources.
- [VID containers](formats/vid.md) and
  [Smacker video](formats/smacker.md) document cinematics; the
  [texture frame table](formats/dtft.md) holds the walls that move.
- [Map event scripts](formats/map-events.md),
  [event tables](formats/event-tables.md) and the
  [design data tables](formats/text-tables.md) carry the quests, shops
  and monsters the engine runs on.
- [The monster table](formats/dmonlist.md), the
  [sprite frame table](formats/dsft.md) and the
  [sound table](formats/dsounds.md) give every monster its art, motion
  and voice.
- [Interface panels](formats/interface-panels.md),
  [portraits](formats/portraits.md) and the
  [paperdoll](formats/paperdoll.md) describe the screen furniture the
  interface wears.
- [Software rasterizer](rendering/software-rasterizer.md) explains the
  rendering pipeline.

## Evidence status

The format documents distinguish **observed**, **inferred**, and **unknown**
claims. Specifications are written from compatibility research, and automated
tests use synthetic fixtures rather than copied game content.

## Project

Source code, build instructions, and contribution information are available in
the [StarHaven repository](https://github.com/kidoz/starhaven).
