# StarHaven

An open-source engine for **Might and Magic VI: The Mandate of Heaven**,
targeting macOS, Linux, and Windows. The goal is a portable reimplementation
that lets you play the original game using your **own legally obtained** copy.

This project is in its early stages. It currently reads the game's archives,
decodes outdoor maps, and renders them in 3D: textured terrain with the game's
own ground art, and every placed model drawn as a filled, textured mesh. Audio,
UI, and gameplay come in later slices.

## Legal posture

**StarHaven does not include any game data.** It is an original engine that reads
a copy of the game you already own. It does not bypass DRM, copy protection, or
authentication, and it does not redistribute executables, archives, media,
manuals, or any other content from the original game.

Might and Magic is a trademark of its rights holders; this project is
independent and not endorsed by them. Reverse engineering here is for
interoperability and compatibility with a legally purchased copy.

## What works today

- A bounds-checked little-endian byte reader (the single chokepoint for parsing
  untrusted binary data).
- A reader for the standard `MMVI` `.LOD` archive format (`BITMAPS.LOD`,
  `SPRITES.LOD`, `icons.lod`): enumerate entries, locate payloads, read
  uncompressed entries, and report compressed entries deterministically.
- A reader for the `Games.lod` container (the live-game-data archive holding
  maps `.blv`/`.odm` and event data `.dlv`/`.ddm`): enumerate entries and
  locate their raw bytes. This is a distinct layout from the standard
  `.LOD` archives.
- A parser for `.odm` outdoor map files: decompresses the zlib wrapper, reads
  the verified 176-byte map header (name, file name, version `"MM6 Outdoor
  v1.11"`, ground tileset name, and 4 tileset definitions), and extracts the
  128×128 terrain **heightmap** and **tile-type map**. Verified on all 15
  outdoor maps.
- A **software rasterizer** (`math3d`, `rasterizer`, `terrain_mesh`) and a
  **first-person 3D walker** (`starhaven`) that renders an `.odm` heightmap as a
  shaded, walkable 3D heightfield — the first 3D view of a real game world.
  No OpenGL; the rasterizer is pure C++20 writing into an SDL3 texture.
- A decoder for the `.odm` **model array** (placed props/buildings), confirmed
  by tracing `MM6.exe`'s map loader in radare2: reads each model's name, world
  position, bounding box, and vertex/facet counts.
- A decoder for the **whole model geometry stream** — every model's vertices
  and facets, plus the facet-ordering, BSP and texture-name arrays that sit
  between them. Facet records are a fixed 308 bytes carrying a 16.16
  fixed-point plane, a vertex-id list, per-vertex texture coordinates and a
  `BITMAPS.LOD` texture name. Verified across all 15 outdoor maps: 921 models,
  37,187 facets. `starhaven` renders the props **filled and textured**, so real
  MM6 houses, bridges and obelisks stand on the terrain.
- A decoder for `.odm` **decorations** — the placed sprites (trees, cacti,
  rocks, pedestals, the party start marker). Found at a computed offset right
  after the model geometry, with a parallel name array that cross-checks it:
  all 15 maps, 6,210 placements, 85 type ids each mapping to exactly one name.
  `starhaven` draws them as camera-facing billboards.
- **Real ground textures**: the `DTILE.BIN` global tile table (in `icons.lod`)
  resolves an `.odm` tilemap byte to a `BITMAPS.LOD` entry, so terrain is drawn
  with the game's own art rather than placeholder colors.
- A decoder for map **actors** — the named monsters and NPCs placed on outdoor
  maps, with their world positions, monster ids and variant numbers. 266 across 15 maps, of
  which 252 stand either on the terrain or inside a building footprint.
- A decoder for `DMONLIST.BIN`, the **monster table** (173 records): each
  monster's name and its eight animation sprite base names. `starhaven` draws
  actors as billboards, so MM6's townsfolk stand in the streets.
- A decoder for `.ddm` (outdoor) and `.dlv` (indoor) **event-data files** — the
  map interaction scripts (triggers, spawns, actions). Same zlib wrapper as
  `.odm`. Also enumerates the first event table's populated records (type +
  name); the full 548-byte record body is the next slice.
- A decoder for `.LOD` image entries to RGBA: parses the 48-byte NWC image
  header, inflates zlib-compressed pixel data, applies the embedded RGB palette,
  and honors index-0 transparency. Verified on real entries from `BITMAPS.LOD`
  and `icons.lod` (power-of-two and non-power-of-two sizes alike).
- A decoder for `.LOD` sprite entries to RGBA: parses the 32-byte sprite header
  and per-scanline line records, inflates zlib-compressed pixel data, resolves
  the sprite's shared `palXXX` palette from `BITMAPS.LOD`, and honors
  per-line transparent spans plus index-0 transparency. Verified on real entries
  from `SPRITES.LOD`.
- A parser for `.blv` **indoor map** files — the 52 dungeons, temples and
  building interiors, against 15 outdoor maps. Decodes the header, the vertex
  array, the 80-byte face records (16.16 plane, attributes, polygon size), the
  variable-length per-face index arrays (vertex ids and texture coordinates)
  the per-face texture names, and the face-extra array with its parallel names. Verified on all 52 maps: 114,833 vertices and
  89,091 faces, rendered in 3D by `starhaven`. Placed **decorations** (torches,
  barrels, trees, and the party's start point) are located too — by scanning,
  since the sections that would give their offset are still undecoded.
- A reader for the `.vid` video container (`Anims1.vid`, `Anims2.vid`) and a
  **Smacker video decoder**: header, Huffman trees, palette records and the
  block-coded frame stream, decoded to RGBA, plus the **DPCM audio tracks**.
  Verified on all 127 videos MM6 ships — every frame of every one — and the
  audio checked by signal statistics against all 77 tracks. `play_smk` plays
  video with sound through SDL3.
- A **collision and movement layer** shared by both walkers: the decoded face
  planes become a collision world with floor queries and wall sliding, so you
  walk on surfaces and into walls under gravity instead of flying through them.
  Mouse-look included; `--fly` restores the free camera.
- A reader for `Sounds/Audio.snd`, the **sound-effect archive** (1,526 entries),
  and a RIFF/WAVE decoder handling PCM and **IMA ADPCM** — the encoding every
  MM6 effect uses. All 1,526 decode, trimmed to the exact length the `fact`
  chunk declares.
- **Music playback**: the installation's fifteen MP3 tracks, decoded with the
  public-domain minimp3 (the project's only non-standard decoding dependency)
  and played through the same SDL3 sink as the sound effects.
- A reader for the **design data tables** — thirty tab-separated spreadsheets
  the developers left inside `icons.lod`, holding maps, monsters, items,
  spells, quests and NPC dialogue. All thirty parse. `MapStats.txt` and
  `MONSTERS.TXT` have typed views, and both join exactly to data already
  decoded: the table's 67 map names are precisely the 67 maps in `Games.lod`,
  and its 173 monsters line up one-to-one with `DMONLIST.BIN`. The walkers now
  show a map's real name — "Sweet Water", not `OutA1.Odm` — and play the music
  track the designers assigned it.
- A decoder for the **sprite frame table** (`DSFT.BIN`) — the 6,455 frames in
  1,656 animations that turn a name into the pictures to draw, in order, at the
  right size and in the right colours. Monsters and torches now animate. It
  also corrected a wrong conclusion: the B and C monster variants were never
  missing art, they are one picture drawn through three palettes, and going
  through the table raises monster sprite coverage from 31 of 173 to 173 of 173.
- A decoder for the **indoor event sections** (`.dlv`), so dungeons are
  populated: the same actor, loot and chest arrays the outdoor files use, after
  a shorter prefix. All 52 indoor maps decode — 76 monsters, 219 loot objects
  and 1,040 chests — with every actor standing inside the level's own geometry
  and every item id resolving in `ITEMS.TXT`. `starhaven` draws them. The state
  that follows is partly mapped too: a fixed 200-slot record array, then a
  region the writer trims to whatever it happened to fill.
- A decoder for the **sound table** (`DSOUNDS.BIN`) and the **decoration table**
  (`DDECLIST.BIN`), which together say what a place sounds like: 1,338 of the
  table's 1,345 names are entries of the already-decoded `Audio.snd`, and the
  seven decoration types that name an ambient sound all resolve — a campfire to
  `campfire`, a fountain to `fountain`, a cauldron to `bubbling cauldron01`.
  Both walkers mix those sounds by distance, so New Sorpigal's fountains and a
  dungeon's braziers are audible as you approach them.
- A decoder for the game's **bitmap fonts** and text drawing in the engine.
  Thirteen of the fourteen `.FNT` entries decode — 225 glyphs each, heights 14
  to 30 — and the engine draws the map's name in the game's own typeface.
  `font_info <font> <text>` renders a string as ASCII art, which is how the
  format was verified.
- **The decoded content, on screen.** `--labels` names every monster and every
  loot object in the world from `MONSTERS.TXT` and `ITEMS.TXT`. In New Sorpigal
  that is 38 of 38 monsters and 42 of 42 objects named — the joins those tables
  were decoded for, drawn where they belong rather than counted in a test.
  Both respect the depth buffer, so nothing behind a wall is named or described
  through it. Looking directly at one raises an **inspect panel** with its
  statistics: a
  monster's level, hit points, armour, experience, attacks and resistances, or
  an item's type, damage and value. Spells are named rather than printed as
  table codes — `Fireball`, not `Fireball,N,5`.
- A typed view over the **town establishments** (`2DEvents.txt`): 556 shops,
  temples, taverns and guilds with proprietors, titles, stock and opening
  hours, joined to the map each stands on — 95 in Free Haven, 46 in New
  Sorpigal. **Tab** lists them while you walk.
- Typed views over the **character tables**: `Spells.txt` (99 spells across
  nine schools, with per-mastery costs and effects), `Class.txt`, `stats.txt`
  and `SkillDes.txt`. `data_info --spells Fireball` prints one.
- A portable install/data-path layer (no drive letters, registry, or hardcoded
  paths).
- **`starhaven`, the engine itself**: `--maps` lists all 67 maps, and naming
  one loads it and renders it as a walkable world — outdoor terrain with its
  models or an indoor level's faces, with the map's music, ambient sound,
  monsters, loot and animated decorations. Indoor and outdoor go through one
  code path; the two separate walker programs this replaced are gone.
- `lod_browser`, a CLI tool to list, inspect, and extract entries from your own
  archives — handles both standard `.LOD` archives and `Games.lod`.
- `odm_info`, a CLI that decompresses one `.odm` outdoor map from `Games.lod`
  and prints its header metadata plus terrain (heightmap/tilemap) statistics.
- `view_heightmap`, a CLI that renders an `.odm` heightmap as a grayscale image
  in an SDL3 window (a visual sanity check of terrain extraction).
- `ddm_info`, a CLI that decompresses one `.ddm`/`.dlv` event-data file and
  reports non-expressive statistics (sizes, populated percentage).
- `view_bitmap`, a CLI that decodes one `.LOD` image or sprite entry and shows
  it in an SDL3 window (sprites auto-resolve their shared palette).
- `play_smk`, a CLI that lists and plays the game's Smacker videos with sound,
  and can dump a video's audio track to a WAV.
- `blv_info`, a CLI that decompresses one `.blv` indoor map and prints its
  geometry statistics.
- `data_info`, a CLI that lists the design tables, dumps any of them, and
  cross-checks `MONSTERS.TXT` against `DMONLIST.BIN`.
- `sft_info`, a CLI that lists the sprite animations, dumps one, and verifies
  the frame table against itself, `SPRITES.LOD` and `DMONLIST.BIN`.
- `font_info`, a CLI that lists the bitmap fonts and draws text as ASCII art.
- A hermetic Catch2 unit-test suite with synthetic fixtures (no game content).

The format specs are documented from observed behavior in
[`docs/formats/lod.md`](docs/formats/lod.md),
[`docs/formats/games-lod.md`](docs/formats/games-lod.md),
[`docs/formats/odm.md`](docs/formats/odm.md),
[`docs/formats/odm-terrain.md`](docs/formats/odm-terrain.md),
[`docs/formats/odm-models.md`](docs/formats/odm-models.md),
[`docs/formats/odm-model-mesh.md`](docs/formats/odm-model-mesh.md),
[`docs/formats/odm-model-facets.md`](docs/formats/odm-model-facets.md),
[`docs/formats/odm-decorations.md`](docs/formats/odm-decorations.md),
[`docs/formats/dtile.md`](docs/formats/dtile.md),
[`docs/formats/event-data.md`](docs/formats/event-data.md),
[`docs/formats/bitmap.md`](docs/formats/bitmap.md),
[`docs/formats/sprite.md`](docs/formats/sprite.md),
[`docs/formats/blv.md`](docs/formats/blv.md),
[`docs/formats/vid.md`](docs/formats/vid.md),
[`docs/formats/snd.md`](docs/formats/snd.md),
[`docs/formats/event-actors.md`](docs/formats/event-actors.md),
[`docs/formats/dmonlist.md`](docs/formats/dmonlist.md), and
[`docs/formats/smacker.md`](docs/formats/smacker.md).

## Build

Requirements: a C++20 compiler, [Meson](https://mesonbuild.com/) ≥ 1.0,
[Ninja](https://ninja-build.org/), **zlib** (system), and **SDL3** (system, or
built via the committed `subprojects/sdl3.wrap`). Catch2 v3 and minimp3 are
pulled in automatically via committed Meson wraps.

```bash
meson setup buildDir
meson compile -C buildDir
meson test -C buildDir
```

`buildDir` is a **debug** build (`-O0 -g`) — the right default for development,
but seven times slower than an optimised one, so do not time anything there.
For an optimised build and the renderer benchmark:

```bash
just release                 # buildRelease/, --buildtype=release
just bench CD1.Blv           # frame timings for one map
just bench-all               # every map, slowest ten
```

The worst map in the game runs at 82 fps and the mean is 335, with every face
drawn every frame and no visibility culling; see
[`docs/rendering/performance.md`](docs/rendering/performance.md).

## Documentation

Documentation tooling uses [uv](https://docs.astral.sh/uv/) with Python 3.14
and the dependency versions locked in `uv.lock`.

```bash
uv sync --locked
uv run --locked mkdocs serve
uv run --locked mkdocs build --strict
```

The equivalent task shortcuts are `just docs-serve` and `just docs-check`.
Generated HTML is written to the ignored `site/` directory.

## Use

Point the engine at your legal game installation with an environment variable:

```bash
export STARHAVEN_GAME_DIR=/path/to/your/MM6/install   # contains MM6.exe and data/
./buildDir/starhaven
```

Inspect an archive:

```bash
# List every entry (name, stored size, compressed?)
./buildDir/lod_browser list data/BITMAPS.LOD

# Show one entry's details
./buildDir/lod_browser info data/icons.lod 2HAxe1

# Extract one entry's raw stored bytes to <name>.out
./buildDir/lod_browser extract data/icons.lod 2DEvents.txt
```

Inspect `Games.lod` (maps and event data — auto-detected as a distinct format):

```bash
# List all maps and event-data entries
./buildDir/lod_browser list data/Games.lod

# Extract one outdoor map's raw bytes
./buildDir/lod_browser extract data/Games.lod Outa1.odm
```

Inspect an outdoor map's header (decompresses it on the fly):

```bash
./buildDir/odm_info Outa1.odm   # header, terrain stats, model/facet counts
```

Inspect an indoor map's geometry:

```bash
./buildDir/blv_info CD1.blv
```

Render an outdoor map's heightmap as a grayscale image (visual terrain check):

```bash
./buildDir/view_heightmap Outa1.odm --scale 4
```

Walk any map, indoor or outdoor, in a 3D software-rasterized view:

```bash
./buildDir/starhaven --maps      # the 67 maps, by file name and title
./buildDir/starhaven Outa1.odm   # WASD move, Q/E fly, arrows look, ESC quits
./buildDir/starhaven CD1.blv     # the same program reads indoor levels

# Reproducible one-frame capture from a chosen viewpoint
./buildDir/starhaven Outa1.odm --pos 6600,900,10600 --look 200,-4 \
    --screenshot village.ppm

# Overlay model bounding boxes (placement debugging)
./buildDir/starhaven Outa1.odm --boxes

# Name the monsters and loot standing in the world
./buildDir/starhaven Oute3.odm --labels
```

Play a music track (`--list` shows all fifteen):

```bash
./buildDir/play_music --list
./buildDir/play_music 10 --seconds 30
```

Play a sound effect (`--list` shows all 1,526):

```bash
./buildDir/play_sound --list
./buildDir/play_sound 01archerA_attack
./buildDir/play_sound door_open --dump door.wav
```

Play one of the game's videos (`--list` shows every name):

```bash
./buildDir/play_smk --list
./buildDir/play_smk Bank --scale 2      # SPACE pauses, ESC quits
./buildDir/play_smk 3dologo --frame 40 --screenshot logo.ppm
```

Decode and display an image entry in a window:

```bash
# Show a 64x64 texture upscaled 4x (ESC or close the window to quit)
./buildDir/view_bitmap data/BITMAPS.LOD apothmid --scale 4

# An icon from icons.lod (non-power-of-two sizes work too)
./buildDir/view_bitmap data/icons.lod 2HAxe1

# A sprite from SPRITES.LOD (its shared palette is resolved automatically)
./buildDir/view_bitmap data/SPRITES.LOD 3AMULET --scale 4
```

## Project layout

```
meson.build                  build definition (targets, tests, deps, wraps)
src/
  main.cpp                   thin application launcher
  config.h.in                version header template
  core/
    io/byte_reader.{hpp,cpp} bounds-checked little-endian reader
    lod/lod_archive.{hpp,cpp}     standard .LOD archive reader
    lod/game_lod_archive.{hpp,cpp} Games.lod container reader
    image/bitmap.{hpp,cpp}   .LOD image decoder (header + zlib + palette → RGBA)
    image/sprite.{hpp,cpp}   .LOD sprite decoder (header + lines + zlib → RGBA)
    image/palette.{hpp,cpp}  shared palette extraction + palXXX naming
    image/zlib_util.{hpp,cpp} shared zlib inflate helper
    world/odm_map.{hpp,cpp}   .odm parser (zlib, header, terrain, model meshes)
    world/tile_table.{hpp,cpp} DTILE.BIN ground tile table (index -> bitmap)
    world/monster_list.{hpp,cpp} DMONLIST.BIN monster table (id -> sprites)
    world/blv_map.{hpp,cpp}   .blv indoor map parser (vertices + faces)
    world/collision.{hpp,cpp} static collision world: floor queries, wall slide
    audio/snd_archive.{hpp,cpp} Audio.snd sound-effect archive reader
    audio/wav.{hpp,cpp}       RIFF/WAVE decoder (PCM and IMA ADPCM)
    audio/mp3.{hpp,cpp}       MP3 music decoding (wraps minimp3) + discovery
    video/vid_archive.{hpp,cpp} .vid video container directory reader
    video/smacker.{hpp,cpp}   Smacker video decoder (video only, no audio)
    world/map_event.{hpp,cpp} .ddm/.dlv event-data parser (zlib wrapper)
    render/math3d.hpp         Vec3/Vec4/Mat4, perspective, look-at, rotations
    render/rasterizer.{hpp,cpp} software z-buffered triangle rasterizer
    render/terrain_mesh.{hpp,cpp} build indexed mesh + normals from OdmTerrain
    render/scene.{hpp,cpp}    camera + scene renderer shared by both walkers
    assets/asset_cache.{hpp,cpp} texture and sprite lookup, cached
    render/texture.{hpp,cpp}  sampled texture with repeat/clamp wrapping
    render/tile_set.{hpp,cpp} per-map set of resolved ground tile textures
    platform/paths.{hpp,cpp} portable install/data paths
  game/player.hpp            player proportions and the movement step
  game/music_player.hpp      per-map music playback
  game/ambient_mixer.hpp     distance-mixed ambient sound
  game/sprites.hpp           frame-table lookup for a drawable sprite
  main.cpp                   the engine: load a map, render it, walk it
tools/
  lod_browser.cpp            archive list/info/extract CLI (standard + Games.lod)
  view_bitmap.cpp            decode an image or sprite entry, show in SDL3
  odm_info.cpp               print one .odm map's header, terrain and mesh stats
  view_heightmap.cpp         render an .odm heightmap as grayscale in SDL3
  tile_probe.cpp             ground tileset research probe
  play_smk.cpp               list and play the game's Smacker videos
  play_sound.cpp             list, play and dump the game's sound effects
  play_music.cpp             list and play the installation's music tracks
  blv_info.cpp               print one .blv indoor map's geometry stats
  ddm_info.cpp               decompress one .ddm/.dlv event file, print stats
tests/                       hermetic Catch2 unit tests (synthetic fixtures)
docs/
  formats/…                  evidence-backed format specifications
  rendering/software-rasterizer.md  rasterizer design and frame pipeline
  rendering/collision.md     collision world, movement and player proportions
```

## Roadmap (toward playable)

1. ~~Foundation: byte reader, `.LOD` archive reader, install paths.~~ ✓
2. ~~Decode a `.LOD` image entry and display it in SDL2.~~ ✓
3. ~~Decode sprite entries from `SPRITES.LOD` (per-line layout + shared palette).~~ ✓
4. ~~Decode the `Games.lod` container (maps + event data).~~ ✓
5. ~~Decode the `.odm` outer format (zlib wrapper + map header).~~ ✓
6. ~~Decode the `.odm` terrain grids (heightmap + tilemap).~~ ✓
7. ~~Refine the `.odm` header struct (tilesets) + document geometry anchors.~~ ✓
8. ~~Software rasterizer + first-person 3D terrain walker.~~ ✓
9. ~~Decode the `.odm` model array (props/buildings) via `MM6.exe` loader trace; overlay bboxes.~~ ✓
10. ~~Decode the first model's mesh vertices (12-byte world coords); render as a point cloud.~~ ✓
11. ~~Decode `.ddm`/`.dlv` event-data files (zlib wrapper).~~ ✓
12. ~~Enumerate the first event table's records (type + name, 548-byte stride).~~ ✓
13. ~~Resolve ground tile indices to real textures via `DTILE.BIN`.~~ ✓
14. ~~Decode the model geometry stream (facets); render filled, textured props.~~ ✓ (this slice)
15. ~~Read the `.vid` container and decode Smacker video to RGBA.~~ ✓ (this slice)
16. ~~Decode `.blv` indoor map geometry (vertices, faces, textures).~~ ✓ (this slice)
17. ~~Render indoor levels (`starhaven`), with per-face texture coordinates.~~ ✓ (this slice)
18. Decode the rest of the `.blv` payload (rooms, BSP, lights, doors) so decorations can be located by offset rather than by scanning.
19. ~~Decode `.odm` decorations and render them as sprite billboards.~~ ✓ (this slice)
20. ~~Collision, gravity and mouse-look in both walkers.~~ ✓ (this slice)
21. Decode event-record bodies.
22. ~~Smacker DPCM audio and an SDL3 audio sink.~~ ✓ (this slice)
23. ~~Decode the sound-effect archive and its IMA ADPCM waves.~~ ✓ (this slice)
24. ~~Decode actor placements from the event files.~~ ✓ (this slice)
25. ~~Resolve actors to sprites via `DMONLIST.BIN` and draw them.~~ ✓ (this slice)
26. ~~Verify the actor position field and map the record's fields.~~ ✓ (this slice)
27. ~~Share the scene, camera and asset code between the walkers; draw indoor decorations.~~ ✓ (this slice)
28. ~~Play the installation's MP3 music.~~ ✓ (this slice)
29. ~~Decode the BLV face-extra name array.~~ ✓ (this slice)
30. UI and gameplay systems.

## Contributing

C++20, Meson + Ninja, `warning_level=3`. Keep `src/main.cpp` thin; put logic in the
`starhaven_core` library so the app, tools, and tests share one code path. Never
commit game data, extracted assets, or any content from the original game —
fixtures must be synthetic.
