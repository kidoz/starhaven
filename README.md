# StarHaven

An open-source engine for **Might and Magic VI: The Mandate of Heaven**,
targeting macOS, Linux, and Windows. The goal is a portable reimplementation
that lets you play the original game using your **own legally obtained** copy.

This is a playable engine. It renders every outdoor and indoor map with the
game's own art, lights, music and sound, and runs the game's systems — event
scripts, real-time and turn-based combat, skills, spells, shops, hirelings,
promotions, reputation, travel, rest, saves — grounded in the game's own data
tables rather than invented — and its whole interface wears the shipped
art, from the title painting to the campfire. The entire main quest, from
Sulman's letter to the Hive's last flush, runs as a 24-beat scripted
regression, and all 58 award-granting events walk and grant on the
record. Every decoded format is documented with its evidence tagged
`observed`, `inferred` or `unknown`, and where the engine had to choose a
number the tables don't give, the code and docs say so.

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

### The world, drawn and walked

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

- **The decoded content, on screen.** `--labels` names every monster and every
  loot object in the world from `MONSTERS.TXT` and `ITEMS.TXT`. In New Sorpigal
  that is 38 of 38 monsters and 42 of 42 objects named — the joins those tables
  were decoded for, drawn where they belong rather than counted in a test.
  Both respect the depth buffer, so nothing behind a wall is named or described
  through it. Looking directly at one raises an **inspect panel** with its
  statistics: a
  monster's level, hit points, armour, experience, attacks and resistances, or
  an item's type, damage and value. Spells are named rather than printed as
  table codes — `Fireball`, not `Fireball,N,5`. Interactables name themselves
  under the cursor from their event's own header — "Exit Door", "Lever",
  "Drink from Fountain" — and an establishment's door names the establishment
  from its `2DEvents.txt` row.
- A typed view over the **town establishments** (`2DEvents.txt`): 556 shops,
  temples, taverns and guilds with proprietors, titles, stock and opening
  hours, joined to the map each stands on — 95 in Free Haven, 47 in New
  Sorpigal. **Tab** lists them while you walk, with the people inside: 396
  named NPCs from `NPCdata.txt`, every one of the 367 who stands somewhere
  resolving to an establishment that exists, and every one of the 332 with a
  profession resolving to `npcprof.txt`. Each person's conversation topics are
  listed beside them, from `npctopic.txt` and `npctext.txt` — all 298 of the
  NPC table's references resolve, and all 298 have words.
- A decoder for the **outdoor tile index** — the last undescribed part of the
  `.odm` format, and the largest: a list, per terrain tile, of what stands near
  it, plus the map's spawn points. All fifteen maps are now consumed byte for
  byte. The index is the map's own answer to "what is near here", and the
  engine uses it rather than searching: looking at a tree names it. Its rule
  reproduces exactly — a decoration is listed against the tiles whose centre is
  within 1024 units, on 6,210 of 6,210 decorations.
- The **map event scripts** decoded, structurally: they are not in `Games.lod`
  at all but in `icons.lod` beside the design tables — 83 `.EVT` scripts and 76
  `.STR` string tables, one pair per map. A script is a run of size-prefixed
  records carrying an event id, a sequence number, an opcode and arguments, and
  **all 83 are consumed exactly**, 15,504 records with nothing left over. The
  field this project had carried as unknown at +0x1A of an indoor face extra is
  the event id: **1,408 of 1,441** name an event that map's own script defines.
  Three of the 90 opcodes are named: 29 shows a message (`"The door is
  locked."`, `"You pick an apple."`), 30 a longer one, and 35 names what you
  are looking at (`"Door"`, `"Sign"`, `"Chest"`). Each was identified by its
  argument never leaving its map's own string count *and* using that range,
  then confirmed by what the strings say. Opcode 5 makes four: it points at the
  map's own name on 53 of its 54 resolving uses. The other 73 are unknown, and
  Opcode 2 is the one that enters a building: its argument is a `2DEvents.txt`
  row id, 474 of 504 resolving, and per map the count of distinct values tracks
  the count of establishments. So the party walks to a shop door, uses it, and
  the counter opens. Opcode 7 is the chests': its argument indexes the event
  file's twenty-slot array, and since the shipped chests are all empty, opening
  one rolls its contents from the map's own treasure level. Opcode 4, the
  commonest in the game, turns out to be an event's opening step — 2,182 of
  2,182 events that contain it begin with it — and its byte is the thing's own
  label, a string index naming "Door" 419 times, "Chest" 244, "Lever",
  "Drink from Fountain" (1,523 of 1,542 non-establishment events; an
  establishment's header is its `2DEvents.txt` row instead, 620 of 633).
  Opcode 21 launches a sprite: its u16 names an animation group of `DSFT.BIN`
  in file order on 154 of 154 uses — `fire04` bolts down Castle Darkmoor's
  halls, thrown pillows and stalactites in the haunted spiral — and the
  engine flies them from the recorded point toward the recorded target, or at
  the party when the record states none, at an engine-own speed. They fly and
  land; whether they hurt is not in the record. Reproduce a trap with
  `starhaven CD2.Blv --walk 25`. Opcode 26 asks for a typed answer — its
  three u32s are the prompt and two spellings of the accepted word ("JBARD" /
  "jbard", the Pyramid's "kcopS") and its byte the step a match jumps to, 19
  of 19 on all three counts — and the engine plays it: the question stops at
  the message line, the party types, and Enter resumes the event at whichever
  step the answer earned, the shipped miss branch being every event's own
  "Wrong!". Opcode 25 rolls one of up to six steps — 452 of 452 nonzero
  entries are steps of their own event — which is D05's mine paying 400, 600
  or 800 gold against two chances of "Cave-in!", weights written as repeats.

- **Opcode 6 is the door out, and the world is connected**: its argument is a
  spawn point — X, Y, Z and a facing — and a NUL-terminated destination, either
  `"0"` for a teleport within the map or a map file name. Of the 99 named
  destinations across all scripts, 97 are maps the design table lists, and the
  pairs are symmetric: GoblinWatch's exit names New Sorpigal's map and New
  Sorpigal's cellar door names GoblinWatch's. The engine walks them — using an
  exit door loads the destination map through the same one-path loader and
  stands the party at the recorded spot, so the 67 maps are now one world
  rather than 67 command lines.

- **The skies half-seated themselves**: the exe's outdoor loader
  carries `sky%02d` with `sky01` beside it — the fourteen panoramas are
  the outdoor sky set with `sky01` its stated floor, now the engine's
  default in place of its earlier guess; what number feeds the `%02d`
  (month and weather are the untested candidates) is filed `unknown`.

- **A sky over Enroth**: the ODM header's slot at 0x60 turned out to
  name the sky — `plansky2` on New Sorpigal, empty elsewhere — and the
  engine now drapes the named panorama (defaulting to `sky01`, the
  loader's own stated floor) behind the terrain as a yaw-wrapped
  cylinder, dimmed
  by the same daylight the sun follows. What the fourteen `sky01`..
  `sky14` panoramas are for remains an open lead.

- **The walls that move, move**: `DTFT.BIN` decoded — DSFT's small
  sibling, nineteen records in four loops verified against the same
  shape — and the engine steps them, so the moss-and-wood wall breathes
  and the two haunted paintings cycle. The water gamble half of this
  item closed as a negative: no shipped table animates `WtrTyl`, and the
  original's shimmer is most plausibly exe-held palette rotation, filed
  `unknown` in `docs/formats/dtft.md`.

- **The doors slide**: a thrown lever no longer teleports geometry — the
  door's vertices travel between their shut and open stations at the
  file's own open and close speeds, read as world units a second (three
  seconds for Goblinwatch's stone slabs), with collision following the
  faces while they move.

- **The bodies take up space**: the monster record's height and radius
  now govern the world — the party keeps a body's radius away instead of
  walking through titans, aiming casts a ray against each monster's own
  cylinder so a dragon is hard to miss and a bat hard to hit, and the Fly
  column's riders hover one body height up, the record's own number in
  place of the engine's old constant.

- **The dungeons found their lights**: the section after the BLV decoration
  block is the level's static lights — 12-byte records of position,
  brightness (31 on every shipped record) and radius, every point inside
  its map's own bounds on 48 of 52 maps — and the walker bakes them per
  face, so Goblinwatch's sconces light their own corners over a dim floor.
  The falloff curve is the engine's; the lights are the file's. The
  tree-shaped section after them is logged as the likely BSP, the next lead.

- **Day and night**: outdoors the sky and the sun follow the clock — dark blue
  before dawn, warm at the edges of the day, blue overhead at noon — and the
  world is lit by where the sun actually is. **Tab** now marks each
  establishment open or shut against the hours `2DEvents.txt` gives it, and
  tells you what the trade inside talks about *today*: `PROFTEXT.txt` is
  decoded, the largest table in the archive, and all 77 hireable professions
  resolve with all 539 of their profession-days filled.

- **Time, and somewhere to sleep**: a clock in the corner counting the hours,
  the days and the seven weekdays `PROFTEXT.txt` names; **R** rests eight hours
  and everyone still standing wakes up whole, unless something alive is close
  enough to object. Each map refills with monsters on the interval its own
  `MapStats.txt` row gives — 168, 224 or 672 days — and a map that ships
  spawn points refills the way it filled: new groups rolled at its own
  points from its own encounter slots, with the placed townspeople left
  standing. A map of placed monsters stands its fallen back up instead.

- **Fountains and potions that really work**: the temporary bonuses land
  now — a fountain's "+10 Might temporary" lies on the party, an Energy
  potion's "Set Temp 7 Stats to 10" on its drinker, Protection's AC and
  Resistance's elements in the sheet's own amounts, and the timed conditions
  ("Set Haste to 6 Hrs") run on the clock for exactly their written hours.
  The sheet shows all of it, a rest ends what lasts until rest, and it all
  rides in the save. The amounts and hours are the tables'; the until-rest
  convention and the party-wide reach of a fountain are this engine's.

- **Ambushes that spring**: opcode 19 named itself against the encounter
  table — its slot stays within the map's own filled encounter slots on 272
  of 272 resolvable uses, its variant is the monster triple's own A/B/C, and
  its count runs to six. Step on the wrong plate and the engine fills the
  room from the map's own table, the new arrivals joining the fight at full
  health while everyone else keeps their wounds.

- **Doors that open**: the indoor event files' fixed 200-slot block turned
  out to be the door array — per door an id, a direction, a distance, two
  speeds, and id arrays whose total is byte-exact against the size the level
  declares. The bases equal the shipped vertex on 4,067 of 4,067 across 795
  doors on the 52 maps, so a door ships shut and opens by sliding its own
  vertices its own distance. Throw a lever and the portcullis rises, the
  wall texture flips, and the collision world lets you through.

- **Coaches and boats you can ride**: the stables' and docks' rows write
  their routes in the stock columns, the designers' way — destination and
  area code, departure weekdays, days of travel — and the counter reads them
  back as a timetable. Ride on a departure day and the fare is paid, the
  clock advances the route's own days, and the party stands on the
  destination map. The fare scale and the arrival point are this engine's
  and say so.

- **Monsters that collide**: they no longer walk through walls, buildings,
  trees or each other, or stand inside the party to attack it — and the party
  cannot walk through them either. Trees block them
  by the radius `DDECLIST.BIN` gives each kind — a field that had been unknown,
  and that no two decorations on a map are ever placed closer together than.

- **Monsters that move, and that are drawn from the side you see them from**:
  each wanders near where it started, as far and as fast as its `MONSTERS.TXT`
  row says, and turns toward you — or away, if it is a Wimp — when you come
  within range its AI type decides. The frame table's five views turned out to
  be angles relative to the viewer rather than compass headings, measured by
  the left-right symmetry of all 1,153 directional frames, so a monster now
  shows you its front, its profile or its back.

- **Monsters, spawned the way the map asks for them**: an outdoor map ships no
  wandering monsters, only places where they appear. Each spawn point names one
  of the map's three `MapStats.txt` encounter slots, the slot names a monster
  and how many of it appear, and all 138 filled slots across the 79 maps
  resolve to a `MONSTERS.TXT` row. All fifteen outdoor maps now populate — 62
  monsters in Sweet Water, 260 in New Sorpigal — placed on the ground around
  their points, from a seed fixed by the map so a place always populates the
  same way.
- Typed views over **how NPCs react**: `npcbtb.txt` says which of begging,
  bribing and threatening works on each personality, and how each phrases the
  twenty-four things it can say. All twelve personalities the professions name
  are described there, and the file states the matrix three times over — in the
  column headings, in the three rows, and in which messages a personality has
  at all — agreeing on all 39 pairs. Plus `GLOBAL.TXT`, the 596 interface
  strings the original drew on its panels.
- Typed views over the **journal**: `Quests.txt`, `Awards.txt` and
  `Autonote.txt` — 512 quest bits, 100 awards and 128 categorised automatic
  notes. Only 52 quest bits carry player-facing text; the rest are recorded as
  the blanks they are.
- Typed views over the **character tables**: `Spells.txt` (99 spells across
  nine schools, with per-mastery costs and effects), `Class.txt`, `stats.txt`
  and `SkillDes.txt`. `data_info --spells Fireball` prints one.
- A portable install/data-path layer (no drive letters, registry, or hardcoded
  paths).


### The interface, in the game's own art

- **The aimed monster shows its blood**: the original's `MHP` bar stands
  across the viewport's top when something is in the sights — the backing
  and its end caps the art's own, the fill strip green, yellow or red as
  the target falls (the two thresholds are the engine's, marked).

- **The dungeon doors invite you in**: the entrance establishments join
  their maps by name — 34 of the 39 rows match a MapStats display name
  exactly, the five lords' castle doors matching none because the
  original gave them no maps — and walking up to one now shows the
  mouth's own video with a single choice: Enter descends.

- **The sheet grew its four pages**: the character sheet now turns
  between the game's own framed pages — `fr_stats` with attributes,
  conditions and the class's words, `fr_skill` with every skill and its
  raise price, `fr_inven` standing the paperdoll itself, `fr_award` with
  the honors in full — left and right arrows turning the leaf.

- **Rest at the campfire**: R raises `restmain`'s own camp panel — the
  three plates offering the eight-hour rest, sleep timed to end at dawn,
  or an hour by the fire, the food counted beside the apple and the hour
  on the marble slab — clicked or keyed, with the exit plate folding the
  blanket.

- **The game opens like the game**: `MM6TITLE.PCX` and its four plates
  stand in front now — New Game walks into the creation hall, Load pulls
  the saved slot, Credits says whose art this is, Exit leaves — clicked
  or keyed, with the world holding its breath underneath until a choice
  is made.

- **The shops show their wares**: the stock list became a rack — each
  item's own art hanging at full length in the room, prices staggered
  beneath, the arrows walking a gold border along the shelf and Enter
  buying what it holds, with the picked piece named above. The digits
  still work for the impatient.

- **The sheet and the chest take their art**: the character sheet's
  numbers now sit inside `fr_stats`' own gilded boxes, and an opened
  chest shows its own face — the record's first word turned out to be
  the `DCHEST.BIN` row, whose art column numbers the `CHEST01`..`08`
  screens, closing half of that record's old open question — with the
  finds written on the planks and any key closing the lid.

- **The rooms come alive**: the service and talk screens play their
  interiors live — one Smacker decoder stepped at the video's own frame
  rate, each frame written over the cached still so every screen sees
  the forge fire move through its ordinary lookup, and each frame's
  DPCM audio chunk fed to a streaming room voice in the mixer: the
  tavern murmurs while you trade. Closing the screen stops both.

- **The interiors play**: the seating chart is in use — each
  establishment's service screen and its people's talk screens stand on
  the first frame of its own `Anims*.vid` interior, dimmed to half under
  the words: Caine at his forge behind the weapon list, the apothecary
  among their bottles, every guild in its own hall. An install without
  the Anims archives falls back to the marble panel, honestly.

- **The hourglass turns**: turn-based mode retired its text tag for the
  game's own `HGLAS` hourglass, standing in the corner readout and
  stepping through its 80 frames as rounds resolve.

- **The services take the marble too**: temple, bank, training hall,
  travel desk and shop all stand `BACKEVT`'s panel on the left with the
  establishment's name, trade and proprietor written down it, their
  words beside it and their key legends on the `FOOTER` strip — the last
  five debug-styled screens retired.

- **Talk wears the game's face**: the conversation screen stands
  `BACKEVT`'s marble panel on the left with the speaker's own `NPC###`
  plate on it — the portraits join `NPCdata.txt` by row id, 396 of 398
  exactly — and the words move beside the panel. The negative is filed
  too: 2DEvents' Picture column (1..118) indexes neither the 39 shipped
  `EVPAN` panels nor the 129 named shop videos by any order measured, so
  everyone talks on the marble until that join closes.

- **Party creation in the game's own hall**: `Makeme.pcx` is the whole
  screen — four marble columns whose oval seats (at the art's own x 17,
  176, 334, 493) hold the creation portraits, names on their plates, each
  character's class and rolled numbers down their green slab, the chosen
  class described along the bottom panel, under the `MAKESKY`/`MAKETOP`
  band. Same keys as before; the rolls still say they are this engine's.

- **The maps page, for real**: the globe book no longer apologizes.
  Outdoors it draws the 128x128 tilemap with every cell in its own ground
  art's average colour — Sweet Water's roads, lake and mountains read at
  a glance — and indoors it traces the floor plan from the BLV's own
  upward faces, with the party's red arrow on both. The cells and floors
  are the maps' own; the flat-colour reading and the projection are the
  engine's.

- **The frame answers the mouse**: M frees the cursor, and the painted
  furniture works — the shelf's sword book opens the quest journal, the
  quill the establishment notes, the key the calendar on `TIME_BG`'s own
  page (the globe's maps page honestly says it is not written yet); the
  four medallions cast, rest, open the spell book and save; a click on a
  portrait's oval seat opens that member's sheet. The zones are read off
  the panels' own art.

- **The spell book, in its own art**: B opens the `Book` page over the
  viewport with the game's school tabs down its edge and every known
  spell wearing its `FIRE004`-style icon — the same school-and-number
  naming the projectiles fly by. Arrows browse, Enter readies, and the
  cast key then throws the player's own choice instead of the "best
  damage wins" heuristic. This install ships no Water icons and only
  Light's emblem, so those spells stand as their names, honestly.

- **The game's own screen furniture**: the main view now sits in the
  original frame — the border strips, the right column with its windows,
  book buttons and medallions, the portrait bar whose measured oval seats
  hold the party's faces with the green hits and blue mana gauges standing
  in the bar's own grooves, and the footer strip carrying the message
  line. The two big panels ship as PCX inside the standard container, so
  the engine grew a PCX reader (see `docs/formats/interface-panels.md`).

- **Nine save slots**: F5 and F9 speak to the current slot, F6 turns to
  the next and says what it holds — the map's name and the day, read off
  the save itself — with slot 1 keeping the old single file's name so
  existing saves survive.

- **The pack obeys its cursor**: wearing, drinking and mixing all start
  from the chosen cell when the pack is open — the sell key already did —
  falling back to the old first-found walk otherwise. Reading scrolls stays
  a world verb: it casts at what the party aims at.

- **The faces react**: the portrait families' 53 frames gave up their
  meanings to a contact sheet — the green poisoned face, the stone-grey
  petrified one, the slumped sleeper, the corpse, the wince — and the party
  strip now wears them: four portraits above the text, each in the frame
  its condition picks, flinching for half a second when a blow lands. The
  frame naming is an observation of the shipped art and says so.

- **Wizard Eye opens the corner automap**, exactly where its prose puts it
  — "the upper right corner ... while outdoors", an hour per point of
  skill — showing what each rank cell says: monsters, treasure at expert,
  points of interest at master. The window's reach and the dot colours are
  the engine's; the hired Cartographer keeps it lit at expert as their row
  promises. `--eye` lights it for reproducing.

- **A journal at last**: J opens the page the walker has been keeping all
  along — every held quest bit's note in `Quests.txt`'s own words, the
  honors beneath — and `--journal` opens it from the command line for
  reproducing.

- **Equipment slots**: ten of them, and which one an item goes in is its own
  `ITEMS.TXT` equip type — weapons, missiles and two-handers to the hand, the
  rest where the table says. **E** in a pack wears the first thing that can be
  worn and puts back whatever it replaces. A character now swings what is in
  their weapon slot rather than whatever happened to be first in the pack, and
  armour's flat modifier counts toward armour class while a weapon's dice do
  not.

- **Loot you can pick up, and somewhere to put it**: walk over a thing lying
  on a map and the first character with room takes it. **I** opens a pack,
  drawn with the game's own item icons — all 229 of them resolve out of
  `icons.lod` — on a grid, with what each thing is and what it is worth.

- **A paperdoll, dressed**: the pack screen stands the character's own doll
  beside the grid — twelve uniform 112x298 bodies in `icons.lod`, lettered
  the way the twelve portraits are — and draws what is worn at the
  `Equip X`/`Equip Y` point `ITEMS.TXT` gives each item. The items place the
  body itself: every boot's art bottoms out at row 350 and the helms centre
  on one column, which pins the doll at (504, 52) with the panel flush in
  the corner. Body armor swaps the torso for its own overlay — chain drapes
  the shoulders, plate reaches the boots — and a cloak's larger half hangs
  behind the body. See `docs/formats/paperdoll.md`, and reproduce the
  measurements with `doll_info`.

- **A party, and the character sheet**: four characters with the game's own
  portraits — twelve faces of 53 frames each, found in `icons.lod` — named from
  `npcnames.txt`, classed from `Class.txt`, and laid out with the field names
  `stats.txt` itself lists, in its order. **C** opens the sheet and **1**-**4**
  choose whose. What a character *starts* with is not in any shipped table, so
  those numbers are this engine's and say so where they are defined.


### Combat, both ways

- **The monsters' spells fly and speak**: a caster's cast — already
  rolled from the table's own "Spl,Mas,Skil" cell at its written mastery
  and skill — now flies its school's bolt at the party and plays the
  spell's own sound, the same two joins the party's casting uses; and a
  caster with no Miss-column missile can finally cast across the missile
  band instead of waiting to be cornered. The liches stop swinging fists.

- **The party fires back**: the best damage spell somebody knows flies at
  what the party aims at, out to the same missile band the monsters shoot
  across — a bolt in the school's own art (`fire04` is Fire Bolt, and the
  Water school's prefix is `cold`), the blow landing on arrival at the
  prose's numbers scaled by the caster's rank, with the `X`-variant burst
  flashing where it hit.

- **Wands cast their charges**: a wand equips as a weapon — "you must equip
  it as though you were equipping a weapon", the rows' own instruction — and
  X waves it first: its Mod1 S-number is the spell, its Mod2 the fresh
  charge count, the generator's rolled charges riding the item through
  packs and saves. A charge burns per cast; a spent wand says so.

- **Turn-based combat**: Enter holds the world's breath, the original's own
  toggle. Time then flows only in rounds a party action spends — a strike,
  a cast, a scroll — each advancing the fight, the wanderers, the launches
  and the clock by one engine-own second, everything inside it still running
  on the tables' numbers. Enter again and time flows free.

- **The party can lose**: four down means somebody drags them back to the
  last town — a week gone, the name dented ten points, everyone waking at a
  single hit point with their conditions shaken off. Every number is the
  engine's own and says so; the tables never speak of defeat.

- **Magic items finally bite**: the generator's rolls ride the items now —
  through packs, purchases, saves and onto the body — and wearing them
  grants what the tables say: a standard bonus's own stat column at its
  rolled strength (the seven attributes, resistances, AC, hit and spell
  points), and the special bonuses' parsed prose — "+10 to all
  Resistances", "Adds 6-8 points of Cold damage" rolling apart on every
  swing and answered by its own element. Names follow their affixes' own
  shapes: "Longsword of Might", "Vampiric Dagger". What the prose doesn't
  phrase in numbers stays prose, honestly.

- **The fight found its range**: a monster whose Miss column names a
  missile — Arrow, Fire, Elec, Cold, Pois, Ener, Magic — now attacks from
  afar, its shot flying as a frame-table sprite (the arrow's own "ARRA", the
  elements' bolt families; the sprite picks and the shared range are the
  engine's). A drawn bow — the Missile equip type, the item table's word —
  answers over the same ground while everyone else needs arm's length.

- **Torch Light and Lloyd's Beacon**: the torch brightens the indoor lamp
  for its cell's own hour per point of skill (this renderer has no light
  radius, so the whole lamp glows — marked as our reading); Lloyd's places
  one marker at normal rank exactly as its cell writes — cast once to set
  it, cast again to return and burn it, decaying in an hour per point —
  both kept in the save.

- **The cures close the loop**: Cure Poison, Cure Disease, Remove Fear,
  Awaken, Remove Curse, Cure Weakness, Cure Paralysis and Cure Insanity lift
  exactly the conditions their first sentences name — in the monster
  column's own vocabulary, misspellings included — from the first sufferer,
  or the whole party where Awaken says "all". The H-cast prefers a cure when
  someone is afflicted, above heals, above smiting.

- **The travel spells cash in**: Fly grants the engine's existing flight for
  its rank cell's own minutes per point of skill, outdoors only as its prose
  insists, and Town Portal behaves exactly as written — a 10% chance per
  point of Water skill, to "the last town visited" at a normal-rank scroll,
  with the Gate Master's daily master-rank cast giving the promised choice of
  destination (P opens the list of towns seen, kept in the save). The Wind
  Master's daily two hours of Fly lift at dawn. What counts as a town — an
  outdoor map with counters — is the engine's own reading, and says so.

- **The chests fight back**: `MapStats.txt`'s "Trap 0-10" column finally
  gates something — a chest on a trapped map may blast the party for the
  difficulty's worth of dice, defused outright when the best Disarm Traps
  reaches the map's own number and dodged at five percent a point of
  Perception ("increases chance to avoid traps", the table's line). Which
  chests are trapped, the dice and both scales are the engine's own and say
  so; the Tinkers, Locksmiths, Scouts and Psychics finally earn their wages.

- **Loot arrives unknown**: `ITEMS.TXT`'s two unread columns now work —
  found things above their `ID/Rep/St` difficulty show only their own
  "Not identified name" and keep their worth hidden, until the party's
  Identify skill reaches the item's number, a hired Scholar's unlimited eye
  sees it, or a counter names everything for a tenth of its value (Y at any
  shop, answered by the merchant table's own Identify line).

- **The fight's conditions cut both ways**: Mass Fear, Slow, Paralyze and
  Charm now do to monsters exactly what their prose says — fear holds their
  blows and breaks on damage, slow doubles their recovery, paralysis pins
  them where they stand and cannot retaliate, charm calms until hurt — for
  the "3 minutes per point of skill" their own descriptions state, read by
  the same duration parser the buffs use.

- **Loot from kills**: `MONSTERS.TXT`'s treasure column is a format, not a
  note — a chance, a roll of gold and an item level with a kind — and all 145
  coded rows parse. A kill pays what its own row says: the gold goes to the
  purse, and the item is rolled by the same generator that fills chests and
  shelves, since every kind the codes name is a selector it already takes.

- **Fighting, with consequences**: a monster flinches into its Wince animation
  when hit and keeps its Death picture where it fell; the party splits the
  experience the monster's own row is worth; a character at zero hit points
  goes down, stops swinging and stops being a target. Every monster carries
  eight animation names and 1,382 of the 1,384 resolve, so the pictures are the
  game's own. **Space** strikes whatever you are aiming at within reach, with
  the weapon that character is carrying — a longsword rolls the `3d3` its own
  `ITEMS.TXT` row gives — and the monsters strike back on the recovery their
  rows give, for the damage their rows give, answered by the resistances their
  rows give. The damage notation parses everywhere it appears: 212 of 212
  monster attacks and 78 of 78 weapons. What a *character* hits for is not in
  any table, and is marked as this engine's where it is defined.

- **The monsters' whole dirty vocabulary**: the on-hit column's 38 values
  mostly land now — `DrainSP` drains, `Stealx2` cuts the purse, `Agex3` puts
  years on, `Disease1..3` runs poison's scaffold at half pace, and the three
  `Brk` words break the slot they name: a broken sword swings as a fist, a
  broken hauberk counts for nothing, and **F** at any counter mends it all
  for half value in the merchant table's own Repair words. And the named
  conditions now bite: the asleep and the paralyzed act for nobody until
  struck awake, cured or rested; the afraid keep their feet but lose their
  swing; a character at zero hit points is down, a blow that lands on the
  downed kills them, and a night's rest wakes the knocked-out at a single
  hit point while the dead wait for a temple whose row does not say "No
  Dead". Triggers, levels and the temples' ceilings are the tables';
  magnitudes, the one-in-five, and the conditions' rules are this engine's,
  marked `inferred`.

- **Poison, and being knocked out**: the first conditions. The monster
  table's on-hit column names them — `Poison1x5`, `Uncon` — and the herbs
  and potions write both ends: Poppysnaps "Set Poison1 condition", Cure
  Poison and Restoration cure it. A poisoned character loses their poison's
  level in hit points each game hour, down to the last point but not through
  it, and the party strip says so. The levels and cures are the tables'; the
  hourly rate and the one-in-five on-hit chance are this engine's.

- **Buff scrolls with the sheet's own hours**: the four conditions the
  potions set exist as spells too, and their rank cells write the duration —
  "Duration 1 hour + 5 minutes per point of skill" — in phrasing the parser
  now reads apart. A scroll of Haste, Bless, Heroism or Stone Skin sets the
  same character condition the potion would, for exactly the written time at
  the reader's level.

- **Books that teach, casters that know**: all 99 spell books carry their
  spell as the same S-number the scrolls use — one book per spell, 99 of 99
  — and **U** on one follows the USEITEMS header's own rule: the character
  learns it and the book is spent, or nothing happens if it is known.
  **H** now casts from what a character actually knows, at the table's cost
  and the prose's numbers: the best heal they can afford when someone is
  wounded, else the best damage at what the party aims at. Knowledge rides
  in the save; that only casters read, and what "best" means, are this
  engine's.

- **Monsters that cast**: `MONSTERS.TXT`'s spell column carries everything —
  `"Fireball,N,5"` is the spell's name, its mastery, and a real skill value,
  cast as often as the row's own percent says. The name resolves in
  `Spells.txt` (tolerating the sheet's two typos), the prose's per-skill
  dice roll at the written skill, and the character's own elemental
  resistance answers. Fire Archers finally throw Fireballs, with no invented
  numbers anywhere in the chain.

- **Spells with the table's own numbers**: the damage and healing in
  `Spells.txt`'s prose follow few enough phrasings to parse exactly — 25 of
  99 spells yield their dice, every direct-damage spell and both heals.
  **X** reads the first spell scroll anyone carries: Fire Bolt rolls its
  written 1-4 per point of skill at what you aim at, answered by the
  monster's own elemental resistance, and the paper is spent. **H** lets a
  caster finally spend spell points — First Aid at the table's cost and
  amount. Who reads, and level standing in for skill, are this engine's and
  say so.

- **Potions you can drink, from the alchemy's own table**: `USEITEMS.TXT`
  turned out to hold the herbs and potions with their effects in the
  designers' prose, their fates — a drunk potion becomes the empty bottle,
  item 163, exactly as written — and a full mixing matrix where 50 pairs
  combine and 390 explode in four written-out grades. **U** in a pack drinks
  the first thing the table knows, applies its cures and says its effect in
  the table's words; **M** pours the first potion into the second, yielding
  the matrix's own answer — a new potion, or the graded explosion's fire
  damage on the mixer. Spell scrolls carry their spell as an S-number in
  `ITEMS.TXT`, and `Scroll.txt` is the message scrolls' full prose — the
  Sulman letter is readable data.


### The story, proven and running

- **The conditional machinery, decoded and running**: seven more opcodes fall
  together as one machine — check-and-jump (its jump target is a step of its
  own event on 1,951 of 1,951 uses), give, take, set, goto, end, and a door
  toggle — and an eighth, 11, repaints a face: all 215 of its named uses are BITMAPS.LOD textures, so a thrown lever is drawn thrown. Three variable types are pinned by their closed sets: item ids that
  never leave 1..578, quest bits at 1..376 of `Quests.txt`'s 512, and round
  gold amounts. Whole shipped events confirm the flow: New Sorpigal's fountain
  is a complete if-else around `"+10 Might temporary."`, and Castle Alamos's
  exit checks quest bit 54 before its travel step runs. The engine walks
  events through this machine now, so gated doors gate, switches throw once,
  and a quest turn-in takes the item and pays the reward.

- **Every grantor walks, on the record**: `evt_info --ledger walk` runs
  all 58 award-granting events through the walker — each with its checks
  satisfied, bare, and with each fact left out in turn — and asserts
  every one actually grants its award: 58 of 58 hold. Getting there
  fixed two real walker bugs the sweep exposed: steps after a chest
  never ran (the obelisk event grants award 61 behind its chest), and
  class checks needed equality for the honorary branches to exist.

- **The chronicle writes itself**: variable type 205 turned out to be
  the autonotes — its values are `Autonotes.txt` rows, the Seer writing
  the very stage its line speaks — so the walker now keeps the
  collected notes, the save carries them, and the journal grew a second
  page where the story's chronicle assembles itself as the events run.

- **Three strangers came home**: the gamble on `GLOBAL.TXT` paid for
  %09, %10 and %16 — his/her, Lord/Lady and son/daughter sit at rows
  383-393, right where sir and morning already lived — and they now
  substitute by the listener's gender. %03 waits on modelling the
  speaker's gender; %07, %08, %13 and %14 stay honestly open.

- **The award ledger, audited**: `evt_info --ledger` maps all 86 worded
  awards to their granting event or to "no script grants this" — 58
  script-granted, 28 orphans, and the orphans sort themselves: the
  seventeen guild memberships (counter services, which this engine
  already grants), the seven arena-and-bounty counters, Archibald's
  library, and three story honors whose mechanism stays unknown.

- **The placeholder gamble mostly paid**: the census of every `%NN`
  across the shipped prose first separated the false hits (MONSTERS'
  treasure codes) and then pinned five new codes by their own sentences
  — the reputation word, the hireling's gold percent, and the identify
  price now substitute alongside the six already known — with the seven
  strangers left open, each filed with its context.

- **The people move with the story**: the quest chain's opcode-40 moves
  now show — whoever it sends somewhere arrives off the full roster even
  from another map or from no establishment at all, and whoever it
  removes stays gone; the King's Library wears its three faces in turn —
  Archibald's statue until award 35, the freed king until he hands over
  the Ritual, the empty room after.

- **Award 35 found its house**: "Freed Archibald" is granted by no
  script — but everything about it converges on the King's Library on
  map D3, whose three 2DEvents rows are the statue, freed and gone
  screens and whose two extra row ids are exactly the two stray NPC
  plates. The executable does the swap and the grant; the engine now
  reads walking in as the deed, with the true precondition filed as
  unknown.

- **The opening arc, proven**: `evt_info --arc` walks New Sorpigal's first
  hours through the same code the game runs — the letter refused until
  held, the delivery's 1000 gold and topic rotation, the Goblinwatch
  combination's award and experience, a lever throwing its doors once and
  holding, the coach priced through to Ironfist, the Fire guild's roll —
  and now the road to Ironfist: the Seer naming each stage in the bank's
  own words, Regent Humphrey paying 5000 and taking the letter (this
  delivery does take it), his "little detail" opening the first council
  task, and Lord Kilburn's shield closing it for award 2, the first
  council seal — and onward through the whole council: Albert Newton's
  Hourglass, Osric Temper's Devil's Post, the Prince of Thieves handed to
  Anthony Stone (the check is variable type 214 against 17, and NPCdata
  row 17 is the Prince himself — "this person follows" is the reading),
  Loretta Fleise's stable prices, Erik Von Stromgard's winter, and the
  final ladder that sets bit 167 once every seal and the exposed traitor
  are in — then into act two: the letter that unseats Slicker
  Silvertongue (award 32, bit 168), the Oracle waking on award 33 and
  spending the four memory-crystal bits, and the Control Cube's 500000
  experience opening the Control Center with award 34 — and to the end:
  Archibald Ironfist handing over the Ritual of the Void, and the Hive's
  own flush event refusing the unprepared, then spending the Ritual for
  award 36, "Destroyed the Hive and Saved Enroth" — twenty-one beats,
  the whole main quest from Sulman's letter to the last explosion — and
  the first side chain beside it: Avinril Smythers setting the hunt for
  Snergle and the dwarf king's axe paying 20000 experience for award 37,
  the template for putting every side quest under the same regression,
  and four more walked behind it — the Candelabra, Andrew's Harp, the
  Pearl of Putrescence and the Wicked Crystal, each asserting its own
  event's gold, experience and honor —
  and two promotions walked the same way: the Crusader and Wizard events
  proved that variable type 2 is the character's class id, checked
  against the qualifying rung and rewritten to the next one beside the
  promotion's own award — and now all six lords' ladders, each with its
  token, its qualifying class and both branches: the promotion award for
  those who qualify, the honorary one for those who cannot. Reading them
  settled the branch order too — passing the check promotes and gives
  the lower award, falling through gives the honorary. Thirty-four
  beats, exit-nonzero on any break. One honest gap: no script in any event file
  grants award 35, "Freed Archibald" — wherever the game bestows it, it
  is not in the scripts. Walking the global
  bank exposed that message indices were being read one byte wide, fine
  for every map's own strings and wrong past 255; the walk now reads the
  whole index. The audit found the one snag no
  unit test saw: bit 81's own note reads "Set when the party starts", so a
  fresh party now begins holding Sulman's letter, as the journal always
  claimed. It also recorded that neither the letter nor the combination
  scroll is ever taken — their events check for them and leave keepsakes.

- **A name the world reacts to**: the party carries reputation and fame,
  and `npcbtb.txt`'s thirteen gated greetings finally gate — the notorious
  get "Oh No! Please don't hurt me", the saintly an honor, the unknown
  "I only talk to real people" — each in the personality's own words, with
  second meetings remembered. Charity betters the name, threats sour it,
  and the Pirate's, Gypsy's, Duper's and Burglar's written "one full
  category" of reputation finally costs one while they're kept. The bands
  and prices are the engine's own and say so.

- **The hurt opcode, decoded**: opcode 9 is `[target][element][amount]` —
  the element in the resistance columns' own order, vouched for by the
  Pyramid's six trap rooms sweeping every type, poison in the sewer and at
  Sweet Water's wells, electricity in the Control Center. Cave-ins, trap
  floors and poisoned wells now hurt, answered by each victim's own
  resistance like any blow.

- **The last currency named**: variable type 22 is found gold — the mine's
  "Gold vein" digs, the sewer's stashes, D13's rising piles — `inferred`
  from the finds' own labels, and it is exactly the "gold you find" the
  Factor's and Banker's rows take their percent of, which the engine now
  pays on it. The same sweep retired four other unknowns to the record:
  opcode 21's byte is neither distance nor ticks, opcode 6's tail reads as
  an 8.8 fixed-point scalar rather than any id, opcode 32's last dozen
  dangle for good, and a dice-shaped opcode 9 on the "Cave-in!" branches is
  logged as the next lead.

- **Promotions, a three-table join**: quest events set "Received Promotion
  to Crusader" awards, `Class.txt` holds the six ladders in row-order triples,
  and each promoted class's own prose prices itself — "an extra two hit
  points and spell points per level". Earning the award steps the matching
  characters up, pays the difference for the levels held, and keeps paying at
  the training halls; Honorary awards stay titles, worn on the sheet.

- **The honors have a home**: variable type 12 is an `Awards.txt` row on 193
  of 193 uses — Goblinwatch's reward sets 53, "Solved the Goblinwatch
  Combination", naming its own quest — so the character sheet now lists the
  honors the quests bestow, in the table's own words. Type 22 was tried
  against fame and reputation and stays honestly unknown.

- **The reward types, named by their own prose**: a give-step and the number
  its event speaks sit side by side, and where they match, the word after the
  number names the type — experience (2,000 of it under Goblinwatch's
  "Here's your gold!"), food, a cure's hit and spell points, the seven
  attributes given permanently ("+2 Luck permanent" is the barrel's string),
  and the five resistances. Quests pay experience now — the training halls
  already knew how to spend it — and barrels raise stats for keeps.
  Reproduce with `evt_info --currencies`.
  The engine reads them: across the 52 indoor maps 5,560 faces carry an event,
  206 name themselves under the crosshair and 1,567 say something when the
  party uses them. Outdoors the same trigger sits at +0x124 of a model facet —
  1,718 of 1,719 resolve — so New Sorpigal's fountains and doors answer too.
- The indoor **sector table** identified: the region in front of the
  decorations opens with a room count and continues as count-and-pointer pairs
  over arrays of face indices. The pointers are stale and cannot be followed,
  but 9,247 of 13,728 consecutive ones across the 52 maps are exactly
  `2 x count` bytes apart, which no coincidence produces. On `D01` every one of
  its 2,291 faces is referenced and none only once — 2,051 exactly twice —
  which is what a wall listed by the room on either side looks like. Which
  faces belong to which room is still not readable — but the record's size is:
  116 bytes, holding six lists apiece, `116 x count` landing exactly where the
  face-index array begins.
- The **indoor decoration block** decoded: an indoor map's decorations are a
  count followed by 28-byte placements and 32-byte names — the same shape an
  outdoor map uses — so all 52 maps now decode exactly rather than by scanning,
  5,776 decorations in full 32-bit coordinates. One map gains six the scan had
  been rejecting. What lies before the block is still undescribed.

- **The quest bank, reachable from the world**: of the faces whose event id
  no map's own script defines, 66 of 88 name events of `GLOBAL.EVT`, the
  shared script dense with quest checks and rewards — and using such a face
  walks the global event. The "unheaded events" mystery also came apart: part
  was another record framing misread (`OUT.EVT` and kin carry no sequence
  byte), and the rest are ordinary event bodies that begin with work.

- **Quests that speak and pay**: `GLOBAL.EVT`'s voice was found by content —
  its letter event's two branches say `npctext.txt` rows 1 and 3, word for
  word the *"Oh!  The Seal"* payoff and the *"you get no money!"* refusal —
  which pinned the larger fact that topic id, prose row and global event
  share one id space, 170 of 298 topics carrying logic. Ask Andover Potbello
  about The Letter with item 505 in a pack and his event pays 1,000 gold and
  advances the journal from bit 81 to 82; ask empty-handed and he refuses,
  in his own words.

- **Quest chains that move on**: opcodes 39 and 40 verify whole against the
  NPC table — set one of an NPC's three topic slots (132 of 132 uses in
  range) and move an NPC to an establishment or away (29 of 29). The engine
  applies the rewrites when the party talks and keeps them in the save, so
  after the letter is paid for, Andover offers his next topic, not the same
  letter forever — and a person an event moves is really moved: gone from
  their counter, standing at the new one when it is on the same map.


### The people, and the counters between you

- **The streets talk back**: T stops any of the monster table's own
  civilians — the hostility-zero Peasant rows — and a passerby persona
  assembles from the tables: a name from `npcnames.txt` by the sprite's own
  gender letter, the Peasant personality's reputation-gated greeting, and
  the same beg, bribe and threaten levers as anyone indoors, rumors and
  refusals in the table's wording. The assembly is the engine's; every word
  is a table's.

- **The counter buys back properly**: the pack screen grew a cursor — arrows
  choose a cell, the line above names the thing and what an open counter
  would pay, and S sells exactly that, at the offer price bent toward the
  item's value by the same Merchant reading the buy side uses, never past
  the value itself.

- **Beg, bribe and threaten, in their own words**: the three approaches the
  talk screen has always advertised now work — keys 5, 6 and 7, answered in
  each personality's own `npcbtb.txt` phrasing, acceptance, refusal and the
  "I already said no" for pressing twice. A success coaxes a rumor off the
  news table; the rumor's pick and the fifty-gold bribe are the engine's.

- **Guild doors check their dues**: `Awards.txt` names a membership per
  school — "Joined the Fire Guild" — and no shipped event sets any of them,
  so the counters sell what the original's executable sold: the shelves
  refuse non-members, J signs the roll at an engine-own hundred gold times
  the guild's own Val, and the membership is worn on the sheet like any
  honor, saved with the rest.

- **A party of your own**: starting the walker now opens on the creation
  screen — the twelve portrait families, the six base classes of `Class.txt`
  (its every third heading; the other twelve read as promotions), names from
  `npcnames.txt`, and each class described in its own prose while you choose.
  The attribute rolls are this engine's and the screen says so. Every tooling
  flag skips the door; `--create` forces it open.

- **Hired help from the streets**: talking to anyone with a trade offers H to
  hire them at their `npcprof.txt` row's weekly cost, two seats as the
  original's follower panel gives. Their benefit prose is read literally —
  teachers' experience percents, guides shaving travel days, healers'
  daily rounds, cooks making food, smiths mending for free, the Enchanter's
  elemental wards, dawn casts of Bless and Heroism — and wages fall due
  every seventh day; an unpaid party walks alone again. The "%17 percent of
  gold" the prose threatens is in no column of the table, so nobody takes it.

- **People you can talk to**: **T** at a counter talks to whoever the NPC
  table places there. Five decoded tables meet and none of it is invented — the
  greeting is their personality's own line from `npcbtb.txt`, what their trade
  is talking about comes from `PROFTEXT.txt`'s column for today's weekday, the
  topics are theirs from `npctopic.txt` with the answers from `npctext.txt`,
  and whether begging, bribing or threatening would work is the matrix. Across
  eight towns, 239 people: all 239 greet, 218 have something for today, 111 can
  be asked about something. Four of the `%01`-style placeholders are decoded —
  the speaker, the person addressed, the time of day and the honorific — so a
  villager says "Good morning! I'm Dini Mahgreb." rather than "Good %05! I'm
  %01." A shopkeeper's lines fill in too — the item, the price asked and the
  price named, and their own trade — so the counter reads "Ordinarily I sell
  things like this Longsword for 50 gold." The codes nobody has read yet stay
  visible instead of being blanked.

- **Shops you can trade with**: **Tab** then a number opens an establishment's
  counter. Its shelves are *generated*, not invented — the row's own
  `"L1 Weap"`-style stock line feeds the random-item generator that reproduces
  the original's item path — and priced by the multiplier the row gives, 1.5 or
  2. The shopkeeper answers out of `Merchant.txt`, 21 of whose 24 lines are
  filled. Selling is this engine's arithmetic and says so.

- **Guilds that sell their school**: the magic guilds' rows write their
  shelves outright — `"Type = Fire, Spells 1-7"` at an Initiate guild,
  `1-11` at an Adept — and the counter now stocks exactly those spells'
  books at the books' own values times the row's `Val`. With books teaching
  and **H** casting what is known, the whole arc is table-fed: buy the
  Fireball book at the Adept Guild of Fire, learn it, and burn something.

- **Temples that mend for money**: the ten `Temple` rows write their own
  terms — margin notes naming Heal and Donate, `Val` as the price, and a
  service ceiling in the stock cell, from Temple Stone's "All OK" down to
  Temple Baa's "No Dead,Stone,Errad". The counter heals a character whole —
  hit points, spell points, poison — for the row's own price, and shows what
  each house cannot mend, ready for the conditions those words await. What a
  donation earns is this engine's hour of Bless, marked.

- **A bank that keeps your gold**: the six `Bank` rows' margin notes name
  the counter's two verbs — Deposit and Withdraw — and no column pays
  interest, so neither does the vault. The balance rides in the save. The
  town halls were scouted and left alone: no bounty table ships, so bounty
  hunts would be invention.

- **Training halls that teach**: the ten `Training` rows carry their own
  numbers — `Val` scales the fee, and the first stock cell writes each
  hall's ceiling, `"Max level = 15"` up to `"No Max"` — so the counter
  offers what the sheet says it should: train a character who has earned it,
  for that hall's price, up to that hall's limit. The experience curve and
  what a level grants are this engine's own and say so.


### The sound of it

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

- **The party found its voice**: the sound archive names its voice lines
  exactly like the portrait frames — the sheet letter and a two-digit
  line, a and b takes — so `portrait_entry` is the whole join. Each face
  now speaks in its own voice when a wound crosses below half, on a
  kill, waking from rest and reaching a level — and now also drinking a
  potion, springing a chest trap, finding gold and bidding the camp
  goodnight, with the b takes played where a face recorded one. Which
  line serves which moment is the executable's knowledge; the eight
  numbers used are marked as the engine's picks.

- **The world sounds underfoot**: the party's footfalls play the
  archive's own Walk/Run set keyed by the ground beneath — grass, snow,
  desert, swamp, water, the stone hall indoors — off the tile art's own
  names; a landed blow speaks in the weapon's voice (sword, axe, blunt,
  arrow, light to heavy, by the weapon's own skill group); and the
  title, frame and camp buttons click with the archive's Click set. Each
  join is the engine's choice among the archive's names and says so.

- **The casts ring true**: the sound archive names spells by their own
  `Spells.txt` rows — `04firebolt01`, `31townportal03`, `21fly03` — so every
  scroll, wand wave, H-cast, portal and beacon now plays its spell's own
  sound through that join (91 of 99 ids have one). Bows release with
  `ArchShoot` and blades land with the archive's sword hits, those two picks
  the engine's own and marked.

- **The fight finds its voice**: each monster's DMONLIST record names its
  four-sound `DSOUNDS.BIN` set at +0x08 — equal to the named set's base on 31
  of 31 monsters whose sound names carry their own table id, a block id on
  all 173 — so a swing plays that monster's attack sound and a kill its dying
  one, through the same mixer the campfires and doors already use.
  Reproduce with `sft_info --sounds`.


### The record: measurements, closures and honest negatives

- **Recovery, found on the fifth attempt**: approaching from the party's
  side instead of the monster table's turned up the counter four
  searches had missed — the `u16` at `+0x137c`, whose every writer in
  the executable is now catalogued: two tick-downs burning it at a
  hundredth of the elapsed unit, the party-reset clear (which also
  showed the skill block runs `+0x1380`..`+0x1410` in eight-byte steps),
  and one setter in `AI.CPP` that pre-scales its input by 32/15. Reading
  further settled two more things: that setter serves **party members
  only** — a queued message whose kind is 4 and index 0..3 — so the
  monster table's `Rec` column never reaches this field; and the tick is
  Haste-aware, burning `elapsed × 1.5` while effect 17 is on the
  character, which the engine now applies to a hasted swing. The elapsed
  unit itself is still unread.
- **The shots use the shot's own bar**: a drawn bow and a monster's
  Miss-column attack now roll against `2 × armour + 30` instead of the
  plain bar, so archery against armour asks for skill exactly the way
  the traced arithmetic says it should — and the spell paths stay on the
  plain bar, which is what the call sites show.
- **The kind byte read as the room's category**: the gamble half-paid —
  17 of the 32 kinds in the interior table speak for exactly one
  establishment type (the nine guilds, stables, temples, boats, jail),
  but the rest carry strays and three broad buckets hold the houses and
  entrances, so the byte classifies the *room*, not the establishment.
  The census ships in `data_info --backdrops`.
- **A whole-install smoke check**: `data_info --smoke` loads every map
  the design table lists — geometry, script, actors, chests, doors,
  lights, establishments — and reports what each holds. All **55 shipped
  maps load clean** (the other twelve rows are the table's own
  placeholders): 3,541 placed actors, 1,100 chests, 767 doors, 4,701
  lights, 537 establishments. It exits nonzero on the first map that
  fails, and the workflow runs it wherever a real install is present.
- **The monsters' recovery stayed hidden**: every write into a monster's
  runtime record across the AI cluster was enumerated and none is a
  countdown — the large-offset runs are position and velocity (the
  `+0x84` triple is a velocity scaled by 50), so the monsters' cooldown
  is either a parallel array keyed by actor index or re-derived each
  frame from the `Rec` column. The engine keeps its own reading, and the
  ground covered is on the record.
- **The last two shipped screens hang**: `QUIKREF`'s five-column frame
  now carries the party at a glance — class, level, the seven
  attributes, hits, spells, armour and skills read across four columns
  the art's own borders measure — and `Options` states what the engine
  actually offers: window scale, fullscreen, turn-based, and the two
  switches read from the install's own `MM6.ini`. The frame's third and
  fourth medallions open them.
- **A school of spells read out of the executable, not its prose**: the spell
  queue dispatches on a 102-entry jump table at `0x429c74`, one case body per
  `SPELLS.TXT` id, each entered with the caster's skill points and mastery.
  All eleven Body cases are now read: First Aid's flat 5/7/10, Cure Wounds'
  2 a point plus five, Power Cure's 2 a point plus ten across the whole
  party, Speed's and Power's 10 plus 2 (3 at expert) with master widening
  the reach. Six of those the table's words confirm exactly; two it never
  states — the buffs last an hour a point, and the timed cures forgive three
  minutes, three hours and three days a point, where the prose claims one
  hour and one day.
- **Stats 7, 8 and 9 named**: not from a display routine, so it is marked
  inferred — but four things agree. `stats.txt`'s rows after Luck are Hit
  Points, Armor Class and Spell Points; the hit-point routine folds in 7 and
  the spell-point routine 8; "of Life" and "of Earth" give 7; "of The Golem"
  gives 9 beside fifteen Endurance. Three special rows that pointed at
  nothing now point at something.
- **Levels, and a retraction**: the party now spends what it earns — a
  sitting reaches level two and a Knight gains exactly the four hit points
  its class table promises, a Cleric two. Finding where the level lives also
  overturned a claim from last batch: **stat 14 is not the character's
  level**, it is a gear bonus worth five for an item "of Power". The level
  is the word at `+0x32`, with a modifier at `+0x34`. What a level *costs*
  was hunted and not found, so the experience staircase stays marked as this
  engine's own.
- **Spirit, Mind and Light, finished**: twenty-one more cases, and every
  figure their rows state the executable matches — Golden Touch's 40/60/80
  percent, Divine Intervention's one, two and three casts a day,
  Telekinesis' and Shared Life's and Create Food's one, two and three a
  point, Meditation's and Precision's ten plus two. Three things the rows
  never say: the hour a point the buffs run, the ten Day of the Gods adds on
  top of its multiple, and Bless and Heroism's real sixty-four-minute base.
  With every timed cure now read the ladder question closes too: seven of
  the nine forgive three minutes, three hours and three days a point where
  their rows claim one hour and one day; only Remove Curse and Raise Dead
  say what they mean.
- **Shoring up the single-source claim, and a bug fell out**: the
  weapon-recovery table was checked a second way — every equippable row
  `ITEMS.TXT` ships, tallied by skill group. The table names twelve groups;
  the file uses **thirteen**, giving three weapons a "Club" group that is no
  skill at all. The executable copes because it loads the bare-hand default
  first and only overwrites it when the skill byte names a row; StarHaven
  was reading the empty lookup as a recovery of **zero**, which made a club
  strike instantly. Fixed. The buff slot map came back consistent: no two
  spells share a slot and none is orphaned.
- **The sitting casts**: `--teach` gives the casters the four cheapest aimed
  spells and they throw them ahead of a fist, which puts the traced dice, the
  aimed-spell list and the spell-point purse under load for the first time.
  The result is consistent: a Cleric with sixteen spell points gets eight
  casts for thirty damage, which is the ~4 a cast that Flame Arrow, Cold
  Beam, Magic Arrow and Spirit Arrow average between them, and the Knight
  with no points casts nothing.
- **The last four buffs do something**: Bless reaches the to-hit roll,
  Heroism the damage and Stone Skin the armour class, each at the "5 + 1 per
  point of skill" their rows all three state; Shield halves incoming missile
  damage, which is the whole of what its row promises. Twelve slots that
  filled are now twelve buffs that matter.
- **The attribute question closed, and an off-by-one with it**: the stored
  attributes run from **`+0x14`**, four bytes apart, each an even word
  holding the value and the odd word after it a modifier — script variables
  **31 to 37** and **38 to 44**. Both formulas confirm the mapping from the
  other side, each reading the pair for the stat it asks the getter about:
  hit points ask for id 3 and read `+0x20`/`+0x22`, spell points ask for id 2
  and read `+0x1c`/`+0x1e`. So an attribute is **stored value + stored
  modifier + spell bonus + gear bonus**, and the ladder applies to that sum.
  The engine had the variable run starting one too high, so a script raising
  Might raised nothing; both runs now land.
- **The gamble on item rolling: the premise was half wrong, and the rest
  stays unfound**: the percentile split — how often a rolled item takes a
  standard bonus, a special one or neither, per treasure level — turns out to
  be read from `RNDITEMS.TXT` already, so it was never invented. What *is*
  invented is the ladder saying which class letter each level may draw from,
  and that was hunted and not found: no bitmask, no low/high pairs, and the
  generator routine not located. It is now marked `inferred` where it lives.
  Three consecutive "find the one routine" searches have now come back empty,
  and the pattern is recorded: the searches that succeed start from a field or
  a table and work outward; the ones that fail start from a behaviour.
- **Conditions: how they begin, and an honest miss**: the time-advance rolls
  `rand() % 100` against a byte at `0x908d6c` — times five, then times ten —
  and on success stamps a condition's timestamp. That byte is **zeroed by
  resting**, so it is a fatigue counter that makes sickness likelier the
  longer the party stays awake, which the engine has no equivalent of. The
  same pass showed the attribute *modifiers* are cleared every tick and
  rebuilt from gear and spells, confirming the composition from another
  angle. What poison and disease actually *do* was not found: conditions are
  timestamps, and every routine that reads one asks only whether it is set,
  never how long. The invented drain stays marked as the engine's own.
- **What a rest does, traced**: `0x42c840` sets a **480-minute** target —
  eight hours, which confirms from the code what the interface string and
  this engine had both only asserted — snapshots the world clock in three
  places, and then **clears every buff**. The routine it calls walks the
  party's sixteen slots and each character's sixteen and expires them all,
  which is a mechanic the engine did not have and now does. It is also a
  third, independent confirmation of both buff arrays' base, stride and
  count, from a routine that had nothing to do with finding them.
- **The economy, attempted from the field — and blocked for a nameable
  reason**: the strings `2DEvents`, `events.lod` and their variants **do not
  appear anywhere in the executable**, so the file's name is built at
  runtime and the usual route — filename to reader to table — is closed. Nor
  does the table turn up by its indexing: the runtime row tables that can be
  found that way are enumerable (items stride 40, monsters stride 72, two
  others) and none has `2DEvents`' shape. With the arithmetic route already
  ruled out, the negative is now specific: **not findable by name, by shape,
  or by arithmetic**. Recorded as blocked rather than unattempted.
- **The character record does not tally, and the reason is the point**: the
  party record yielded to a count of absolute references because it lives at
  a fixed address and nothing else does. The character record is reached by
  *pointer*, so absolute references touch only character zero (48 offsets),
  and a tally of instruction displacements is contaminated — a displacement
  belongs to no particular structure, and the offsets that score highest fall
  inside ranges other structures use too. Recorded as a caution rather than a
  ranking. Two fields did come out of the absolute references: **`+0x1618`**
  is frame scratch the time-advance clears every tick, and **`+0x60`** is the
  base of a byte array with two hundred bytes to live in before the item
  records begin.
- **The gamble on the economy: empty, and the pattern is now four for
  four**: starting from the price factor did not converge — no routine in the
  executable multiplies an item's value by a runtime float in anything like a
  counter; every register-relative float multiply in the image is geometry.
  What the search did leave is a partial map of the **runtime item table at
  `0x560c14`, 40 bytes a row**: a name pointer at `+0x00`, a second string at
  `+0x08`, the equip type at `+0x14`, the skill group at `+0x15`. And the
  method pattern is now unambiguous — four searches begun from a *behaviour*
  have all failed, four begun from a *field or table* have all landed. The
  economy is worth one more attempt, from a field.
- **Age put under the sitting, and it found the wrong curve**: `--age` and
  `--poisoned` set a party up and derive its maxima, and the run showed
  spell points falling with age where the executable has them **rising**.
  Spell points have a **fourth row of their own** at `0x4c284c` — 100, then
  **150** through the fifties to nineties, back to 100 through the hundreds,
  and a tenth past a hundred and fifty. The old grow wiser before they fail.
  My wiring had used the hit-point row; corrected. Second time the instrument
  has caught a wiring error within minutes of being pointed at new work.
- **Four of the party record's dense fields named**: `+0x1c0` is the
  **turn-based flag** — every site compares it against 1, and the branch
  decides whether recovery is set directly or scaled by the same −32/15 the
  running clock uses. `+0x0fd` is the **quest-bit array**, loaded as `this`
  into a routine that shifts right three for a byte and masks three bits for
  the bit. `+0x0e0` is **script variable 20**, a party dword the interface
  prints with `%lu` — which quantity stays unknown, since the engine names 16,
  17, 21, 22 and others but not 20. `+0x074` travels in one write with the
  z, the facing and the second angle, so it is part of a position snapshot.
- **The gamble: there is a party record, and it closes at both ends**:
  tallying every reference into the neighbourhood gives **123 distinct
  offsets** in one dense run — not scattered globals. The arithmetic settles
  it: a **708-byte header** at `0x908c70`, then the four 5660-byte character
  records beginning at `0x908f34`, ending at `0x90e7a4` — which is exactly
  the bound Power Cure's case loops to. Two independently-found numbers
  meeting at both ends. Twelve fields are named, including the world clock at
  `+0x098` (the most-referenced field in the whole data segment, at 239
  sites), the position triple, the fatigue byte and the buff array; ninety
  more are recorded with their reference counts so the next sitting can start
  with the ones that matter.
- **The thirty-one skill slots, named** — and who may hold them.
  `SKILLDES.TXT` ships exactly thirty-one rows for exactly thirty-one bytes,
  so slot `n` is row `n`; and `0x4c2694` is a **six by thirty-one table** of
  what each class family may do with each. Zero means never, which the
  trainer's list tests directly. The two slots marked `1` in each row are the
  pair that class begins play with, and they come out right for all six —
  Knight sword and leather, Cleric mace and body, Sorcerer dagger and fire,
  Paladin sword and spirit, Archer bow and air, Druid staff and earth. That
  fit also **corrected an earlier list** that had Light and Dark ahead of the
  three self schools; read that way, no Sorcerer could ever learn Dark.
- **The skills now reach play.** A made character is given the **two** skills
  its class row grants, at one point each, replacing an invented single one
  that had a Paladin starting with Mace and a Sorcerer with Staff; the
  wielder's own points in the held weapon's group go onto the attack bonus,
  which is the first line of all nine weapon rows and was never read; the
  trainer refuses past sixty points and refuses outright what a class may
  never hold; and the sitting now throws every spell at the caster's real
  points in its school rather than a flat five. Skills and the pool already
  survived a save.
- **A longer sitting**: `sitting` now takes several maps comma-separated and
  carries the party, the clock, the fatigue counter and the tally across all
  of them; `--level N` makes a party that survives long enough to be
  measured, `--train N` hands out a skill pool and spends it on the training
  routine's own terms, and `--rest` camps for eight world hours when everyone
  is at or below half. Eight world hours over four outdoor maps, 1236 actors,
  a level-twelve party: **38 killed, 793 gold, hit rates of 33 to 48 percent**
  — and the party is overwhelmed inside half an hour, camps once, and three of
  its four are put straight back down. The camp restores in full, which is
  this engine's reading; no rest routine has been traced.
- **The `+0x60` array is the skills**: thirty-one bytes, one a slot, points in
  the low six bits and the mastery in the top two. Training costs `n + 1` to
  buy the `n + 1`th point, stops at sixty, and spends a pool that lives in a
  dword at `+0x1410`. The length is not a guess — the check that lets a made
  party start walks slots `0` through `0x1e` in all four characters and
  demands four non-zero in each.
- **The actor's timer grid, withdrawn.** Last batch named the run at `+0xf4`,
  `+0x114` and `+0x124` a buff array of the actor's own, on a sixteen-byte
  spacing three readings agreed about, and marked it `inferred` for want of a
  writer. The writer was then looked for properly and **there is none**: all
  twenty-five stores at those offsets in the whole image belong to other
  records — a character's item array, a forty-byte-record reset that writes
  `+0xf4` as a *word*, an unrelated cluster that never touches `+0x114` — and
  of the ten absolute references into actor zero's `+0xf4`..`+0x128`, not one
  is a store. A buff array nothing casts into is not a buff array, so the name
  goes; the reads, the pairs and the spacing stand. Either the values arrive
  wholesale from the map or the save, or the three AI actions gated on them
  are unreachable.
- **The gamble on the actor block: the three timers are dead.** The actor
  array moves as a **straight image** — `memcpy` of `548 × count` in, `fwrite`
  of `548 × count` out, no field touched, which also names `0x5b22f8` as the
  actor count the AI reads eighty-one times. So a file could carry values the
  code never writes. It does not: `evt_info --actor-timers` walks every actor
  block Games.lod ships and finds **342 actors across 67 maps with all four
  64-bit fields zero, without exception**. A save can only hold what memory
  held, and no instruction sets them. So the fields are always zero, every
  branch that asks whether they are greater than zero always answers no, and
  **the three AI actions behind them never run in the shipped game**. The
  gamble's risk was that the answer would be "they come from the file"; the
  answer is better than that.
- **The hundred counters at `0x5b2293` are not counters.** Three readers index
  the same array by an attribute's raw value and add the byte to 400 before
  handing it to a string routine — it is the table that turns a number into
  the word printed beside it on the sheet. Which means the property setter's
  ids 105..204 write over a **display table**: id 105 lands on its base
  exactly. Either that stretch of the numbering is dead in the shipped game or
  the original scribbles on its own sheet text; either way nothing here should
  implement it.
- **The gamble on `+0x157c`: the quick spell.** The most-referenced unnamed
  field left on the record says what it is in five instructions — take the
  word, subtract two, bound it at 97, and jump through a byte selector into a
  **98-way switch**. A value in 2..99 dispatching that is a spell id, and
  `stats.txt` has the row for it: **Quick Spell**. It is written with 33 and
  tested against 21, the Fly spell, in a movement path asking whether the
  bound spell is the one that flies. It is not the readied spell at `+0x152f`
  — that is what the cast key throws now, this is what stays bound — and it
  now survives a save. The stated risk was an interface index, real but dull;
  it is an interface binding the game's own stat table names.
- **The hirelings' stated numbers, finally applied.** `npcprof.txt` gives them
  in plain words and this engine had **parsed** them for a long while — an Arms
  Master's two points to every weapon skill, a Weapons Master's three, a
  Squire's to armour and weapons both, a Scout's to Perception — and applied
  none of them, because there was nowhere for a skill point to land until the
  skill array became real. Now a hired master's points go onto the attack
  bonus beside the wielder's own, a Squire's lift the armour drag the way a
  rung does, and a Scout's join the trap dodge. Two masters of one trade are
  not two bonuses: the largest stands, which is this engine's reading of rows
  that never mention stacking.
- **Seven stored terms, and a fit turned into a reading.** Each of the three
  named getters pushes its own stat id and then reads that stat's own table —
  the maximum-hit-point getter pushes **7** and indexes the class base at
  `0x4c2630`, the maximum-spell-point getter pushes **8** and indexes
  `0x4c2638`, the armour-class getter pushes **9** and adds the Speed row of
  the ladder. So `special_stats.hpp`'s Hit Points 7, Spell Points 8, Armour
  Class 9 stop being a fit and become observed. Four more getters of the
  identical shape push **15, 16, 19 and 20** and read the stored terms at
  `+0x1570`..`+0x1577`. Which four stats those are is inferred, but pointedly:
  id 15's getter reads **Speed's modifier word at `+0x2a`**, which is exactly
  the mixture the attack bonus was traced to use, and id 20's starts from the
  item at the **weapon's own equipment anchor**, which suits a damage figure.
  Two of the four now reach a blow.
- **What a kill actually pays: fifty reputation.** The hunt for the experience
  award came up empty again — the monster table is 72-byte rows from
  `0x56c188` and only five of its columns are referenced anywhere, none
  feeding an accumulator — but the death handler paid out something else.
  `0x403730` sets the death bit and the death animation and then, once per
  death and unconditionally, takes **50** off the global at `0x908d48`. That
  global is party **`+0xd8`**, carried until now as "a counter": it is the
  **reputation**. `0x43c598` tests it against **-1000**, and what waits there
  names the scale — the branch resets it, counts the occasion, and grants
  award **83**, which `Awards.txt` reads **"Served %u Prison Terms"**. So
  prison is the bad end, a death is a twentieth of the way, and the engine's
  greeting bands are now scaled to that span instead of to nothing.
- **The gamble on the `+0x1570` run: the vestige withdrawn.** It was filed as
  dead — six getters read it, nothing wrote it — and the property setter's
  full-restore clears reopened it. Every getter that reads the run has the
  identical shape, and it is the shape just named for `+0x30`: ask both stat
  getters for one stat id, **add the stored byte**, add the attribute ladder's
  byte, sum. Two are certain from the class tables they read — **`+0x1578` is
  the maximum hit points** (`0x482060` reads the class base at `0x4c2630` and
  floors at 1) and **`+0x157a` the maximum spell points** (`0x4821c8`, base at
  `0x4c2638`) — and the four below them belong to four more derived stats. It
  also explains the clears the last pass could not place: a temporary addition
  to a ceiling goes when the pool behind it is refilled. Both are wired, with
  the getter's floor.
- **The last two unnamed fields of the dense record.** **`+0x30` is an
  armour-class term**: the getter at `0x482860` builds the armour class from
  both stat getters at **stat id 9**, plus this stored word, plus the Speed
  row of the attribute ladder, and **floors the total at zero** with a
  branchless `setl`/`dec`/`and`. It is the only place the word is read, which
  makes property id 8 the one way a script can move a character's armour class
  directly — and the floor was missing from this engine. **`+0x11` is a
  boolean beside the class**: `0x42b073` turns it into **2011 when zero and
  2010 when not**, handed to the sound object, and `0x421a68` uses it as an
  alternative to the class in one lookup. That it is a boolean is observed;
  that the boolean is sex is inferred from two adjacent voice ids being what a
  male and a female line look like.
- **The teacher has a door now**, and the proposal's premise needed correcting
  on the way: "Teacher", "Arms Master", "Weapons Master" and "Spell Master"
  are not buildings at all — `npcprof.txt` shows them as **hirelings**, with
  their own stated numbers (a Teacher gives ten percent more experience for
  300 a week, an Instructor fifteen for 700, an Arms Master two points to
  every weapon skill for 300, a Spell Master four to every spell skill for
  2000). What the building tables *do* give is two thirds of the answer: a
  magic guild's row names its own school in the same `Type = Fire` cell that
  stocks its shelves, so the Fire Guild teaches Fire; and a weapon or armour
  shop stocks items whose `skill_group` is a `SKILLDES.TXT` heading, so what
  it can teach is what it sells. A rung is now bought only at a door that
  teaches that skill. The rest — Perception, Diplomacy, Learning — has no door
  in the tables, and is left without one rather than given an invented one.
- **What Learning is worth: nothing the executable says.** The experience at
  `+0x1420` turns out to be **64-bit** — the adder carries into `+0x1424` and
  clamps at four billion, and the sheet switches fonts past 9,999,999 — and a
  new character is created with **`rand() % 100 + 251`** of it, in the same
  breath that sets its level to 1. But `+0x1420` is touched by **eighteen
  instructions in the whole image**, five of them writes, all of them the
  script property routines or creation; and every read of the skill array is
  accounted for by training, the trainer's list, the creation chooser, the
  teacher and two display paths. **The two sets never meet**: nothing
  multiplies experience and nothing reads slot 30. So Learning has no
  implementation to copy, and what a point is worth here says it is this
  engine's own. One honest open end falls out of the same count — **the award
  for a kill is not among those eighteen references either**, and has not been
  found.
- **The gamble on `0x434e50`: the loading screen.** The routine whose
  forty-byte records disproved the actor buff grid is the constructor of a
  static object at `0x52d0a8` — `atexit` is handed its destructor two
  instructions later — and its own loader names every field: a progress-bar
  rectangle of 122, 151, 449, 56, then six forty-byte **image slots** holding
  `loading.pcx`, `womover.pcx`, `demover.pcx`, `womover2.pcx`, `demover2.pcx`
  and a `fireball` sprite. So the risk landed as stated: a rendering structure
  with no bearing on the game. What it buys is that the routine standing
  behind the buff-grid withdrawal is no longer an unknown, and that the
  forty-byte image slot is a shape worth recognising elsewhere.
- **A sitting that is not always the same four classes.** `--classes` fields
  any four of `Class.txt`'s eighteen headings, and with it the last of the
  three flourishes fired for the first time: **the dagger's triple strike, 12
  of them for a Sorcerer party**, which had never once fired because no class
  in the fixed four holds Dagger. Each family brings its own — the Archer's
  second arrow, the Cleric's mace stun, the Druid's staff stun, the
  Sorcerer's dagger. Four world hours on one map, armed and trained, novice
  against master: **Knight/Paladin/Archer/Cleric 16 → 34 kills, four Knights
  25 → 32, Sorcerer/Druid 14 → 17, four Druids 15 → 18**. The spread is the
  weapon skills doing what their rows say, and it is the first time the class
  table's six rows have been exercised in play rather than in a test.
- **The audit of what the effect-line fix moved**, run as a tool rather than
  by eye: `data_info --skill-audit` prints every one of the thirty-one rows
  beside what this engine now reads from it, rank by rank. All thirty-one have
  exactly four columns, so the `+1` holds everywhere, and each rank's line is
  now its own. Two things fell out of it. **Sixteen rows grant nothing the
  parser can read** — their normal line is prose and their rungs are the bare
  "Double effect of skill" and "Triple effect of skill", which *is* readable
  and is the whole of what a rank is worth to them; Identify and Repair now
  take it. And **three consumers were still taking the packed byte for a point
  count** — Identify, Repair, and the merchant's haggle, where an expert's two
  points pack to `0x42` and would have beaten a novice's forty. A pleasant
  cross-check fell out too: `Thievery`'s row is placeholder text — "Normal
  Text goes here" — which is the class table's finding that no class may hold
  it, arriving from the design side.
- **The teacher is in the game.** Four batches of tracing had left a rank that
  could be read and never earned, and a character that died with the two
  skills its class handed it. Now a guild can teach a skill the class's row in
  `0x4c2694` allows — refusing outright what it zeroes, whatever the price —
  and the rungs above novice are bought from the party's purse at the
  teacher's own **2000 and 5000 gold**, one rung at a time, setting the bits
  `0x4969e4` sets and leaving the points alone. The sheet shows a skill's
  points and rung and what the next one costs; shift over its number buys it.
  It also fixes a visible bug: the sheet had been printing the packed byte, so
  a master's five points read as 133.
- **The gamble on `0x90e19c`: six clocks that belong to the scripts alone.**
  Property ids 216..221 stamp eight-byte world-clock values into a fixed array
  just past the party record, and **four instructions in the whole executable
  touch it** — a setter, an adder, a clear, and a getter that multiplies by the
  same 30/128 calendar float the world clock uses and divides by sixty, so a
  script reads its stamp back in game time. Nothing else looks at them ever.
  So they are neither interface state nor engine state: the game keeps six
  timers purely so a map script can mark when something happened and later ask
  how long ago. The stated risk was screen cooldowns with no bearing on the
  game; they are the opposite.
- **The higher lines fire at last** — and finding out why they never had
  turned up a real bug. `DescriptionTable` hands over every description column
  from the second onward, and `SKILLDES.TXT`'s columns are *Skill /
  Description / Normal / Expert / Master* — so the prose is `lines[0]` and the
  rank-`r` line is `lines[r + 1]`. `skill_power` walked from `lines[0]`,
  reading the prose as the normal line, the normal line as the expert one and
  the expert line as the master one. **Nothing above novice ever did what it
  says**, in the shell as well as headless. Beside it, four flourishes in the
  strike were assigning over one another instead of appending, so only the
  last to fire was ever spoken. With both fixed, `sitting --arm` (which hands
  each character the cheapest weapon in a group it actually holds — a made
  party carries nothing, which is why none of this had ever been exercised)
  shows the ranks doing what the table says: over four world hours on one map,
  **16 kills at novice against 34 at master, with 4 stuns and 11 second arrows
  at master and none at either below it**. The triple strike still never fires
  because no class in the starting four holds Dagger.
- **Whose resistances are whose.** Two claims stood in the same record and
  both could not be right. `0x421dc0`, which turns an element id into a
  resistance, reads the six bytes at `+0x50`..`+0x55` — and **its argument is
  an actor**: one caller reaches the record as `esi - 0xa0` off the AI's state
  pointer, two more test the actor's `+0x114`/`+0x118` pair on the same
  register in the same breath, and the routine holds the *character* in a
  different register entirely. So the six bytes with 200-means-immune are the
  monster's, and the element order rotated by two is that jump table's alone.
  **A character's five are the words at `+0x1254`..`+0x1267`**, base and
  modifier in pairs under the buff array. As by-catch it names what the dead
  timers were for: while `+0x114` is positive the blow does no damage, so the
  pair was a **damage-shield window** — one that never opens.
- **Who sets the rank bits, and what a rank costs.** The teacher is at
  `0x4969e4`: it masks the skill byte with `0x3f`, then ORs exactly one bit —
  `0x40` for expert, `0x80` for master — built by `neg`/`sbb`/`and 0x40`/`add
  0x40` off a single flag. So **`0xc0` never occurs**, which closes an
  `unknown`: the pair is one of three states, not four. Three instructions
  later the same flag builds the price the same way, `and 0xbb8` then
  `add 0x7d0` — **2000 gold for expert, 5000 for master**. The convention also
  shows up in the game's own text: `MONSTERS.TXT`'s parser masks a cell with
  `0x3f` and ORs the same two bits for the suffixes `E` and `M`.
- **The rank is the two bits, not the point count.** This engine placed expert
  at four points and master at seven — invented, because no table says where
  the ranks begin. No table says it because a **teacher sets the bits**: three
  traced sites agree and none looks at the number, the training routine
  raising the points while leaving `0xc0` standing, the armour-recovery
  routine halving on `0x40` and dropping on `0x80`, and the property setter
  masking with `0xc0` before it writes. So thirty points and no bit is a
  novice and two points with `0x80` is a master. Every reader now splits the
  byte instead of guessing, which also fixed six places that were passing a
  packed byte where a point count was meant — a master's five points would
  have read as a hundred and thirty-three. `sitting --rank` is the teacher,
  and the armour drag it feeds is wired but does not bite there yet: a made
  party wears nothing.
- **All fifty-three property bodies, read.** Every case names its own field in
  its first two instructions, so one pass settles nine constants and corrects
  two. **The level is variable 9, not 8** — both routines share one selector
  and index it with `id - 1`, and following it through lands id 9 on the body
  that writes `+0x32`, which is the word the max-hit-point getter reads as the
  level; the old reading took the jump-table entry number for the id. **The
  `+0x1570` run is not written by nothing** — that negative is withdrawn:
  cases 4 and 6 clear `+0x1578`..`+0x157b` when hit points and spell points
  are set to their maxima, so the run is what a full restore resets. And it
  names four fields outright: **`+0x1420` is the experience** (clamped at four
  billion), **`+0x1254`..`+0x1267` the five resistances** as base-and-modifier
  words sitting just below the buff array, **`+0x12` the class**, and the
  condition array is **eighteen** slots rather than seventeen — case 104
  clears 144 bytes of it while only seventeen have a setter id.
- **The gamble on what a level grants: a negative, and a dispatcher.** The
  pool grant at `0x441314` turns out to be one case of a general **"add this
  much to that property of this character"** routine — 225 property ids, a
  byte selector at `0x441ff0`, 53 case bodies — and the amount is simply what
  the caller passes. It has four callers in the whole image, two of them
  reading the id out of an event instruction; none is a level-up. So what a
  level is worth is still `unknown`, the second approach from the writing side
  to end there. What it gave instead is the property map, and each row is
  fixed by the offset its body computes rather than by a fit: **56..86 are the
  thirty-one skills** (masked `0x3f`, `0xc0` kept, stopped at sixty — the
  trainer's arithmetic from a second direction), **87..103 the seventeen
  conditions** at `+0x1380`, stamped with the world clock out of the party
  record, and **225 the skill pool**. It also **corrected the attribute
  variable runs**: the modifiers begin at 25 and the bases at 32, not 31 and
  38. See `docs/formats/character-properties.md`.
- **The gamble on `0x55e5d0`: the sine table.** The most-referenced runtime
  table in the executable — 290 reads and not one write — is read through
  quadrant folding around a quarter and a half held at `0x55f614` and
  `0x55f618`, and what comes out goes into a 64-bit multiply shifted back
  sixteen bits. So the risk landed exactly as named: it is trigonometry. What
  it settles anyway is the arithmetic every moving thing runs on — the angle
  scale, the two circle constants by name, and **16.16 fixed point** as the
  engine's convention. See `docs/formats/fixed-point.md`.
- **A longer sitting**: `sitting` now takes several maps comma-separated and
  carries the party, the clock, the fatigue counter and the tally across all
  of them; `--level N` makes a party that survives long enough to be
  measured, `--train N` hands out a skill pool and spends it on the training
  routine's own terms, and `--rest` camps for eight world hours when everyone
  is at or below half. Eight world hours over four outdoor maps, 1236 actors,
  a level-twelve party: **38 killed, 793 gold, hit rates of 33 to 48 percent**
  — and the party is overwhelmed inside half an hour, camps once, and three of
  its four are put straight back down. The camp restores in full, which is
  this engine's reading; no rest routine has been traced.
- **The `+0x60` array is the skills**: thirty-one bytes, one a slot, points in
  the low six bits and the mastery in the top two. Training costs `n + 1` to
  buy the `n + 1`th point, stops at sixty, and spends a pool that lives in a
  dword at `+0x1410`. The length is not a guess — the check that lets a made
  party start walks slots `0` through `0x1e` in all four characters and
  demands four non-zero in each.
- **The actor's timers sit on a buff grid**: the AI tests bits `0x4000` and
  `0x8000` of the flags dword first, then reduces two 64-bit values to
  booleans by the same sign-then-zero idiom the party and character buff
  arrays use for an expiry. The two are at `+0x114` and `+0x124`, and state
  5b's is at `+0xf4` — **a sixteen-byte grid**, which is exactly the buff
  stride on both other arrays. Three shapes agree, so the actor almost
  certainly carries a buff array of its own; it stays `inferred` because,
  unlike the other two, no clearing routine or writer has been found. It does
  unblock the reading: 5b and 7 branch on whether particular actor buffs are
  still running.
- **The gamble on the `+0x1570` run: a vestige**: neither branch of the
  guess was right. The eight bytes are read by **six** stat getters, each
  adding a signed byte to its total — and **nothing writes them**. Across the
  whole disassembly the only other instruction touching those offsets is the
  per-tick clear that zeroes them. So the attack bonus's last term is
  structurally present and always zero: not a UI artefact, and not a live
  contribution. The four bytes above the run *do* have writers and are tied
  to hit and spell points instead. One caveat is stated rather than glossed:
  a write through a computed pointer would not show in a scan by
  displacement.
- **The AI's last three are blocked, not unfound**: state 5b turns out to
  walk the *whole* actor array, testing a 64-bit pair at `+0xf4`/`+0xf8` on
  every one. That is the pattern in all three — 5b, 7 and 9 branch on 64-bit
  timer pairs on the actor record, and nothing here knows what those timers
  hold. They are the same shape as the character's condition timestamps,
  which were only named once three independent readings agreed. So the next
  job is naming the actor's timers, not reading more action bodies.
- **State 10 named: a commanded move**: it calls the pathfinder with a
  target of **zero** — the caller's own point rather than one it works out —
  and every caller is outside the AI, including the aiming code. State 7 was
  read far enough to see it branch on two of the actor's own 64-bit timers,
  which is as far as it goes; 5b and 9 stay open. Across three sittings the
  tally is four actions named outright, two named as families, five still
  open.
- **State 16 named: reanimation**: it has exactly one caller anywhere —
  inside the case for **Reanimate** — and its body restores the hit points
  at `+0x28` from a stored word at `+0x5c`, which names that field as the
  actor's full total. Three of the eleven actions are now named (death,
  hurt, reanimation) plus two families (closing/backing off, approach). State
  9's body was read and is still not nameable; 5b, 7 and 10 were not reached.
- **The attack bonus composes — lopsidedly, and that is the finding**: read
  to the getter's return, it is the sheet's ladder applied to *one*
  attribute's spell and gear bonuses plus *another's* stored pair cut by age
  and condition, then stat 15's three contributions and a byte at `+0x1570`.
  The mixture is not a misreading: the two anchors that fix the stored run —
  hit points ask id 3 and read `+0x20`, spell points ask id 2 and read
  `+0x1c` — put the pair it reads one stat away from the one it asks about.
- **Age wired**: the three curves now reach the numbers they band. A
  character's Endurance term is cut to three quarters past fifty for hit
  points, where the recovery and attack curves hold until a hundred; past a
  hundred and fifty all three keep a tenth. Age and condition compound, each
  applied to the attribute term rather than the result, which is where the
  executable applies them. A level bought at sixty is now worth less than one
  bought at twenty.
- **The gamble: the middle term is age — and it retires two old unknowns**:
  reading the attack-bonus getter from the top shows it turn the world clock
  into years, add a stored word, subtract a party word, add **1165**, band
  the result at `{50, 100, 150}` and take a percentage. That banded term is
  the one this project twice filed as an unexplained "day-of-week"
  contribution in the recovery and hit-point routines. It is **age**, and the
  three curves differ sensibly: hit points fall to three quarters at fifty
  where the other two hold until a hundred, and past a hundred and fifty a
  character keeps a tenth of all three. What stays open is why the getter
  uses Accuracy's *bonuses* beside Speed's *stored* value.
- **Two more AI actions named, by the same trick**: the state-6 pair is
  near-identical and differs in one condition. Both test a flag at `+0x46`
  and, when set, aim **512 units above** the party rather than at it; the
  second additionally requires the world kind to be outdoors first. So the
  pair is one behaviour — approach — in two forms, one of which only rises
  outdoors. A flag that lifts an actor's aim half a thousand units, gated on
  being outdoors, is what flying looks like and `MONSTERS.TXT` has a `Fly`
  column — but by the rule the audit just wrote down, that name is `inferred`
  and only the arithmetic is `observed`.
- **An audit of claims named from a fit, and one was wrong**: the
  load-bearing combat claims were re-derived from their instructions rather
  than from how their numbers read. The **to-hit roll** came back exact, and
  so did **immunity at 200**. The **resistance byte order** did not: this
  project said the six bytes at `+0x50` hold the resistances "in the design
  table's own order", which was an assumption dressed as a measurement — the
  routine's jump table shows the mapping is **rotated by two**. Nothing
  depended on it, since the engine reads resistances from the table's columns
  by name. The rule it leaves is now written down: `observed` covers what an
  instruction does; a *name* that follows from how numbers read is
  `inferred`, however good the fit.
- **The condition multiplier wired everywhere it belongs**: it was traced in
  three routines and only the attack bonus used it. Now the recovery's Speed
  term and the hit-point and spell-point routines' Endurance and Personality
  terms are all cut by the worst condition before the ladder reads them —
  which is where the executable applies it, to the attribute term rather than
  to the result. A poisoned character recovers as though slower and is
  hardier by three quarters; a drunk one by a tenth.
- **The gamble paid, and cost a retraction**: what a condition costs is not
  charged on the clock — it is a **percentage multiplier** on the
  character's numbers, applied wherever they are computed. The recovery,
  hit-point and attack-bonus routines each walk a fourteen-entry list, take
  the **worst condition** the character carries, and scale by a per-condition
  table: **Poisoned 75, Diseased 60, Afraid 50, Drunk 10**.
  
  That list and that table were written up here as a *skill* priority order
  and per-skill percentages, and three counter services were wired on them.
  Withdrawn. The instruction settles it: the walk reads **eight bytes and ORs
  the halves** to test for non-zero, which is how you read a timestamp, not a
  count of skill points — and the run is confirmed as the condition run three
  other ways. Haggling and the counter services are back to the engine's own
  one percent a point; the attack bonus keeps only the raw attribute and the
  condition scaling. The coincidence that made the wrong reading persuasive
  is recorded: ids 9, 10 and 11 carry 30, 25 and 10, which read beautifully
  as Leather, Chain and Plate descending.
- **The fatigue put under the sitting, and it found a bug**: the mechanic
  went in from a trace and had never been played. A sitting showed Weak
  landing at world minute **180** where the routine's own
  `inc al; cmp al, 1; jbe` fires on the **second** increment — an off-by-one
  in my wiring, not the trace. Fixed; it now lands at minute 120, and the
  sitting reports the hours awake and when the party first went Weak.
- **A behaviour named at last: closing and backing off**: states 2, 12 and
  13 open on the same three words and differ in **one constant** — state 2
  offsets a point from the actor's position by **+0.75** of its radius,
  states 12 and 13 by **−0.75**, and all three hand it with the party's
  position to the reachability test. One closes, the others back away. That
  is the fleeing behaviour two earlier batches hunted and missed, sitting in
  the sign of a floating-point constant.
- **And there is no column map, because the columns are never copied**: the
  hunt for an actor-preparation routine turned up the encounter roll, a
  display scratch buffer and the array's read/write to file — but no
  preparation, because the actor carries only a **monster id at `+0x34`** and
  everything from `MONSTERS.TXT` is looked up through a runtime table at
  `0x56c1c0`, 72 bytes a row, with the name pointer at `+0x10`. The question
  retires rather than being answered: what a monster *is* stays in the table,
  what it is *doing* is the record.
- **The nine unnamed AI actions, grouped**: reading each action's opening
  record access does not name them but narrows the next attempt — states 2,
  12 and 13 all open on the word at `+0x7a` and are one family in three
  variants; state 1+3 opens on two coordinate *pairs*, the shape of comparing
  one position with another; and states 6b, 9 and 10 read nothing from the
  record at all, taking everything from arguments. So the eleven actions are
  not eleven independent behaviours.
- **A further negative on what a condition costs**: the per-character pass
  runs **once a real second**, gated by a due-time advanced by 128 units —
  the clock unit confirmed from a third place — and within it nothing reads a
  condition timestamp's age or subtracts hit points for one. Two batches have
  now looked. A condition's cost is not charged on the clock, and the search
  space is recorded as narrowed rather than the answer guessed.
- **The gamble on the monster record: seven fields, but not the column map**:
  reading the AI named the flags dword at `+0x24`, the **hit points at
  `+0x28`**, the row index at `+0x34`, the animation byte at `+0x3e`, the
  disposition flag at `+0x4e` and the two percentage bytes at `+0x47` and
  `+0x4d`. What did not come out is where `MONSTERS.TXT`'s columns land: the
  routine that prepares an actor assembles the record **field by field on the
  stack** rather than copying columns in a loop, so there is no per-column
  trace to follow and the table-outward method has nothing to grip. Recorded
  as a batch of its own rather than guessed at.
- **The fatigue counter, traced end to end and wired**: the time-advance
  routine increments a byte once an hour, and the moment it passes one — so
  after **two hours awake** — it walks all four characters and stamps
  condition slot 1, **Weak**, on any who is not already afflicted. The same
  byte then gates two further rolls each tick, `rand() % 100` against five
  times it and against ten times it, which is how every other condition
  begins. Resting sets it back to zero. The engine now does all of it, so a
  rest buys something besides hit points.
- **The conditions name themselves**: a run of eight-byte timestamps from
  `+0x1380`, and three independent readings agree on the numbering. The cure
  spells push the id they lift — Cure Weakness **1**, Cure Poison **6**, Cure
  Disease **7**. The heal refuses slots **14** and **16**. The AI treats a
  character as not there at all when any of **2, 12, 13, 14, 15, 16** is set.
  Those three sets fit one ordering and no other: Cursed, Weak, Asleep,
  Afraid, Drunk, Insane, Poisoned, Diseased … Paralyzed, Unconscious, Dead,
  Stoned, Eradicated, Zombie — with poison and disease adjacent because MM6
  carries a single level of each where later games split them into three.
  Four ids the ordering does not reach are left nameless rather than guessed.
  What being poisoned *costs* is still unfound: the slots are timestamps and
  nothing reads their age.
- **The AI's decision routine, read**: it opens with a **damage-over-time
  pass** — resistance-checked damage to the hit points at `+0x28`, then the
  **state-4** action if they go negative and **state-8** if they do not. So
  **state 4 is death and state 8 is being hurt**, which is why the spell and
  impact code call the two a few bytes apart everywhere. Then, per actor: if
  the distance reaches **5120** it idles — that is the awareness range — and
  otherwise a routine rolls **two percentage bytes** on the actor to pick one
  of three dispositions. That last is a different shape from this engine's:
  the original rolls a monster's condition **every tick**, so an afflicted
  monster wavers, where the engine holds a timer with a fixed effect.
- **The AI cluster, mapped wholesale**: read the way the two failed visits
  concluded it had to be. Thirty-nine functions, of which **seventeen** end
  by putting the actor into a state, and **`0x4017a0`** — the largest, and
  the only one entered from outside — calls eleven of them. It is not a
  switch and never was: a long chain of conditions ending in one call, which
  is exactly why looking for a dispatch failed twice. Fourteen states are
  named by the functions that set them, `0x4035e0` is the common ending
  every movement falls into, and the four actions reachable from outside the
  cluster are reachable from the spell and impact code in pairs — what
  happens to a monster that is hit, one for a blow it survives and one for a
  blow it does not. The map does not name the behaviours; it makes naming
  them bounded, which is the next batch's work.
- **The gamble: one behaviour cannot be lifted out either**: following "what
  makes a monster flee" from the state word named three states — **4 acting**
  (paired with 5 recovered by the countdown), **7 moving**, **9 standing** —
  and a distance of 1024 that picks between two behaviours. But fleeing was
  not found, and the reason is structural: the states are *movement* states
  shared by everything, and what would distinguish fleeing from approaching
  is the target handed to the mover, not the state. Taken with the previous
  visit, the answer is that the AI must be read **wholesale or not at all**,
  which is worth knowing before another batch is spent on it.
- **The gamble: the AI has no switch to find**: three visits have gone
  looking for the monster AI's decision point on the assumption that
  something that large would dispatch through a table, as the spell code and
  the special-bonus code do. It does not, and that is the finding — the
  cluster branches on a state word with two dozen scattered comparisons and
  no selector anywhere. What did come out: **the AI state is the word at
  `+0xa0`**, its states are 0, 4, 5, 6, 7, 8, 9 and 16, and the recovery
  countdown transitions **4 to 5** when the counter lapses, so "recovered" is
  a state change rather than just a number reaching zero. Recorded so a
  fourth visit does not start from the same wrong assumption.
- **The new work survives a save**: the party's sixteen slots and every
  character's sixteen now write as their own records — an empty slot costs
  nothing, so an unbuffed party's file is unchanged — and the level and
  banked experience were already carried. A sitting or a session no longer
  throws away three batches of work on reload.
- **All twelve character buff slots, and the ladder finally moves a stat**:
  every case loads the array's base as an immediate before calling the
  setter, which gives the whole map at once — Bless 0, Heroism 1, Haste 2,
  Shield 3, Stone Skin 4, Lucky Day 5, Meditation 6 and 7, Precision 8,
  Speed 9, Power 10 and 11. Slots 4 to 11 are the eight the stat getter
  reads, and **every attribute-buffing spell lands on the slot for the
  attribute its own row names** — Lucky Day on Luck, Precision on Accuracy,
  Speed on Speed, Meditation on Intellect and Personality, Power on Might
  and Endurance. Eight independent agreements, and the ten-plus ladder now
  raises a statistic on the sheet instead of setting a flag.
- **The open question closed, against the earlier reading**: the script
  variable table names the attribute fields outright — ids 32 to 44 write
  seven pairs of words in the run `+0x16` to `+0x30`, a value and a modifier
  each. So the attributes are **nowhere near** `+0x12b0`, which settles what
  the words there are: the **power fields of the character's buff records**.
  Meditation landing on Intellect and Personality and Power on Might and
  Endurance is a consequence, not a coincidence. `0x483800` is therefore not
  the base getter it was called — it returns a spell bonus — and how the
  stored attribute enters the hit-point formula is re-opened as unknown. The
  class tables are untouched by this; they were confirmed five ways from
  their own prose.
- **The character's own buff array**: the routine that clears the party's
  slots clears these too — sixteen 16-byte records at `+0x1268` on every
  character, beside a 28-byte item array at `+0x144`. Which spell takes
  which slot came off the `lea` before each call to the setter, and four
  check out against their own rows: Meditation takes slots 6 and 7, which
  line up with Intellect and Personality exactly as it says, and Power takes
  10 and 11, which line up with Might and Endurance. One thing is left
  explicitly open rather than built on: the attribute words the stat getter
  reads sit at `+8` of these same records, which may mean the array *is* the
  attribute-bonus store — four agreements is suggestive, not proof, and
  after two recent retractions it stays flagged.
- **The gamble, third attempt: the threshold stops being hunted**: what the
  attempt yielded is the door's name. Both routines that write the level
  dispatch on a **script variable id**, and ids 7 to 11 land on the adjacent
  words — so the level is **script variable 8**, beside experience at 13,
  and every level gained is a script adding to it. The threshold itself has
  now escaped three approaches: the strings are fetched by a computed index,
  the data holds no ladder, and the field's only writer is a generic verb
  carrying no condition. Recorded as absent from what can be read.
- **The gamble: what a level costs, still unfound — but the shape changed**:
  going at the field instead of the strings turned up exactly **two**
  instructions in the whole executable that write the level, and both are
  the generic "set a character field" and "add to a character field" that
  map scripts use, capped at 255. **Nothing raises a level automatically.**
  The only door is a script, which is to say the training hall — so the
  engine no longer levels the party the moment it earns enough. Experience
  is banked and spent at a hall, and what a level is worth there now comes
  from the traced class tables rather than parsed prose.
- **A verification pass, and it earned its keep**: after two retractions,
  every load-bearing claim was re-checked from an angle it was not derived
  from. **Three of the thirty-four spell dice were wrong** — caught against
  the bands `SPELLS.TXT` prints in words, and traced to two misread case
  shapes; Static Charge, Cold Beam and Magic Arrow are corrected and the
  roller now distinguishes all four forms. The attribute ladder came back
  confirmed overwhelmingly (73 reference sites, not the one it was found
  in), the class tables five ways from `Class.txt`'s own prose, and the
  weapon-recovery table is flagged as the one claim resting on a single
  routine — corroborated only by `SkillDes.txt` calling daggers quick and
  axes slow, which its numbers match.
- **The spell switch, finished**: the last four cases close. Healing Touch
  is 2d3 plus one, three or five — its row's 3–7, 5–9, 7–11 exactly. Dispel
  Magic and Prismatic Light really do take nothing from their rank. And Sun
  Ray and Moon Ray carry a **condition** rather than a number: both read the
  world kind and the hour, and Sun Ray refuses outside 05:00–21:00 while
  Moon Ray refuses inside it — a daylight spell and a night one, which no
  row mentions. Hour of Power's odd twelve turned out to be a duration
  multiplier, not the power its row describes.
- **The party's spell buffs, on the executable's own array**: sixteen slots
  of sixteen bytes, and which spell owns which slot read off the `mov ecx`
  before each call to the setter — Protection from Fire slot 0, Cold 1,
  Electricity 2, Magic 3, Poison 4, then Water Walk, Fly, Guardian Angel,
  Wizard Eye and Torch Light. **Day of Protection writes seven of them in a
  row**, which is what its row means. Three batches of traced powers and
  durations finally have somewhere to go: the protections now actually
  protect, at one, two or three a point for an hour a point, and the five
  resistance columns they answer are Fire, Electricity, Cold, Poison and
  Magic — `MONSTERS.TXT`'s own order, confirmed by "of Protection" reaching
  four of the five and not magic.
- **Whose resistances? Neither — a retraction and a better answer**: the
  five globals the stat getter reads were written up last time as the
  party's resistance store, which was wrong. The routine that clears them
  gives the shape away: sixteen records of sixteen bytes, each an expiry, a
  power and a skill — **the party's spell-buff array**, and the five ids
  read the power of the five "Protection from" spells. A character's own
  resistances are still the six bytes at `+0x50`, so this engine had the
  shape right all along.
- **The stat block read off**: the base getter's jump table gives one field
  per id, and the twenty-three split four ways — an eight-field attribute
  run confirming Might through Luck from the other side, the five buff
  powers above, a stored pair, and eight ids with no base at all that the
  bonused getter builds from gear.
- **Every thrown spell's dice, found at the fourth attempt**: `0x432ad0` is
  the damage routine — spell id, skill, and a 98-entry switch in which 34
  spells have a case and the rest return nothing. Flame Arrow 1d8, Fire Bolt
  skill d4, Fireball skill d6, Incinerate skill d15 + 15, Lightning Bolt
  skill d8, Harm skill d2 + 8, Sun Ray skill d20 + 20, Dragon Breath skill
  d25, Ring of Fire skill + 6, Armageddon skill + 50. `SPELLS.TXT` carries
  none of it. The earlier misses came of reading the collision handler's
  *monster* branch; the party's spells are the branch below it, and it calls
  the damage routine in its first four instructions.
- **A class is worth something again**: the sitting's one complaint is
  answered. Both the hit-point and spell-point routines read the class id at
  `+0x12` and index a table by class and another by family — Knight 4 hit
  points a level against a Champion's 8 and a Cleric's 2, bases of 30, 25
  and 20, spell points from nothing for the Knight line to five a level for
  an Archmage. `Class.txt`'s own "an extra two hit points per level" for a
  Cavalier is the check. A starting party now reads Knight 42, Paladin 28,
  Archer 28, Cleric 24 rather than four characters at twenty-something.
- **The gamble on the other forty-six specials, answered both ways**: there
  *is* one walk that applies them — in the bonused stat getter, dispatching
  on ids 1 to 57 — and it deliberately handles only eighteen. The run 3..41
  falls to a do-nothing case, which is precisely the run holding the twelve
  elemental riders, Vampiric, of Recovery and of Darkness, each answered
  where it matters instead. The eighteen that do reach the sheet are now
  read and wired: of The Dragon 25 to Might, of The Troll 15 to Endurance,
  of The Unicorn 15 to Luck, of The Gods 10 to every attribute, of Doom a
  point to everything with no test at all. The tables say none of it.
- **The projectile object, laid out — and a third miss, narrowed**: the
  thrown-spell objects live in a thousand 100-byte slots at `0x5c9ad8`, and
  two offsets found from outside the launcher pin the rest: a flags word the
  AI clears across the array, and the owner handle the collision handler
  unpacks into a kind and an index. That fixes the spell id at `+0x3e` and
  the caster's skill at `+0x42`. Searching for everything that reads those
  two back gives a definite negative — nothing does, absolutely, and the
  collision handler never touches them — so where a spell's damage is rolled
  is still unfound, but the remaining search is now four named routines
  rather than the whole executable.
- **Fire and Dark out of the switch, beside Body**: thirteen more cases read,
  and the table's words match the executable on all eleven figures it
  states — Fire Blast's 3/5/7 shots, Meteor Shower's 8/12/16 meteors,
  Shrapmetal's 3/5/7 fragments, Reanimate's 10/20/30 a point, Mass Curse's
  2/3/4 minutes, Day of Protection's 2/3/4×, Armageddon's one, two and three
  casts a day. Three numbers it never gives are now measured: Torch Light's
  "brighter" and "brightest" are 3 and 4, Ring of Fire's "small radius" is
  512 world units and its "larger" 1024, and Haste's base is sixty-four
  minutes rather than the hour the prose rounds it to. The blast radius the
  engine had guessed at turns out to have been right.
- **The engine can now be played without a window**: `sitting` makes a
  party, opens a real map, and steps the world loop against the actors that
  map places — reporting what a starting character is, how often it swings,
  what it deals and takes, and when it falls. It was built to settle whether
  the traced attribute ladder had broken anything, and it says the ladder
  holds: level one comes out at the class tables' own hit points, one blow
  every 1.7 seconds — which is the traced recovery arriving where the trace
  predicted — and 60–84% of blows landing. Walking into everything on the
  opening map, a bare-handed party kills 29 things and is dead in twenty
  world minutes.

  Its first version reported that the monsters never once hit back, and
  **that was the instrument's fault, not the engine's**: it let the party
  strike at any distance while the monsters could only reply within the 400
  units the game gives them. With the party held to the same reach the fight
  is symmetric, and the earlier reading is withdrawn.
- **What every weapon special actually does**: the game's own table names
  "of Ice" and prices it and says nothing more. The executable's post-hit
  walk carries the numbers, and all thirteen are now read and wired — cold
  3–4, 6–8, 9–12; sparks 2–5, lightning 4–10, thunderbolts 6–15; fire 1–5,
  flame 2–12, infernos 16–18; poison a flat 5, 8 and 12; of The Dragon
  10–20, each met by its own resistance column. Vampiric and of Darkness
  take a fifth of the blow back as health instead, and two artifacts are
  named by id rather than enchantment: Hades adds twenty, Ares thirty. Found
  while chasing something else — see below.
- **A second corrected expectation, recorded as such**: the dispatcher hunted
  as the projectile-impact handler is nothing of the kind. It switches on a
  worn item's enchantment after a melee blow, not on an object kind, and
  where a thrown spell's damage is rolled remains unfound after two attempts.
- **The clock unit proven from Windows itself**: the two elapsed globals
  that had to be assumed equal turn out to be the same field of two
  instances of one timer class, and the method that fills it samples
  `GetTickCount()` and keeps `ms × 128 ÷ 1000`. So a clock unit is exactly
  1/128 of a real second by construction, not by inference, and a monster's
  `Rec` really does spend at sixty points a second like the party's.
- **The new pace, walked**: `data_info --pace` now reports what the traced
  rates mean in real time over the game's own tables — a world day in 48
  real minutes, shop days between 8 and 42, a bare fist 1⅔ seconds a blow
  against a dagger's 1, monsters between ⅔ and 1⅔. The 34-beat arc, the
  58-grantor ledger walk and the 55-map smoke check all still pass, and
  day and night still look different. The walk did turn up one real defect
  and it is fixed: 404 rows carry no opening hours at all — every house,
  dungeon mouth, the City Council, the Library, the Oracle, the Seer — and
  reading equal hours as "shut" had locked the party out of all of them.
- **The sheet's attribute curve, found by accident**: finishing the
  attack-recovery routine turned up the descending ladder every attribute
  reads its bonus through — 13 the pivot at nothing, 15 one, 17 two, 19
  three, 21 four, 25 five, 100 eleven, topping out at 30. StarHaven's own
  guessed curve is gone, which moves hit points, spell points, armour class,
  melee damage and recovery onto the original's numbers together. The strike
  now also gives back the Speed bonus, the level of a Sword, Axe or Bow held
  at expert or better, and a flat 20 for anything worn "of Swiftness".
- **The spell switch's machinery, and a corrected expectation**: the three
  case bodies most spells share carry no damage numbers at all — they are one
  projectile launcher, copied three times, and the damage is decided at
  impact by a dispatcher keyed on the object's kind. What the sitting did
  yield: `+0x1418` is the spell-point purse, guarded by a routine that
  refuses with sound 209; the engine's universal object handle is an index
  over a three-bit kind; and a complete traced list of the **47 spells that
  are aimed at the world**, a partition no table states.
- **The monsters' recovery counter, found after five failed hunts**: it is
  the dword at `+0x6c` of the 548-byte actor record, filled by the same
  queued-message handler that fills a character's — kind 3 with an actor
  index where kind 4 takes a party slot — scaled by the same 32/15 and
  counted down to zero the same way. The earlier hunts missed it because
  they scanned the AI for writes through a record pointer, and the only
  write to the field comes from outside the AI and addresses it absolutely.
  What remains unproven is filed rather than smoothed over: the elapsed it
  subtracts is a second global, and its equality with the world clock's own
  is inferred.
- **The world turns at thirty times real time**: the calendar routine's own
  arithmetic — 128 clock units to a real second, 30/128 world seconds to a
  unit — fixes a rate the engine had only ever guessed at. Half a world
  minute a second: a day now takes forty-eight minutes of playing, and shop
  hours, the sky's daily re-roll, rest and the day/night turn all keep the
  original's pace.
- **A strike costs what the executable says it costs**: the attack-recovery
  routine at `0x481a80` reads a fourteen-word table — a bare fist 100, a
  dagger 60, a sword 90, a staff or axe or bow 100, plate armour another 30
  on top — with the slower hand setting the pace and the armour skills'
  higher lines halving then erasing what the armour adds. The party's flat
  invented second is gone.
- **Recovery's unit, closed — sixty points a second**: the world clock at
  `0x908d08` counts in units of 1/128 of a real second and turns into a
  calendar by ×30/128, so the world runs at thirty times real time; the
  recovery counter is drained by exactly those units and filled at 32/15 of
  a `Rec` point, which puts a point at 1/60 of a second. Two earlier
  readings are withdrawn with it: the drain's 1.5× belongs to a worn item
  "of Recovery" (the special table's row 17), not to a Haste effect, and the
  routine at `0x482bb0` is First Aid's copy of the drain, not the
  time-advance path's.
- **The counters answer to the executable's numbers**: the same traced
  table weighs the three services the engine used to price by guess —
  Merchant at 20% (so ten points take two off a hundred, not ten),
  Identify Item and Repair Item at 120% (a point carries further than
  itself). Haggling, whether loot arrives known, and the smith's bill
  all read those weights now; only the half-price floor stays the
  engine's own.
- **The doubt settled, and the hit roll closed**: reading the to-hit's
  three callers decides which side is which — the second argument is
  always the monster record the armour is read from, so the first is the
  striker and the getter really is the **attacker's bonus**. The engine
  now assembles it the original's way: an attribute read raw plus the
  first skill of the priority order the character holds, weighted by
  that skill's own percentage. That an attack bonus weighs Plate at 10%
  and searches armour skills first is odd, and is filed as odd rather
  than smoothed away.
- **The area spells strike the crowd**: a damaging spell's reach is now
  read from its own prose — "targets a single monster, but explodes to
  hurt anyone else caught in the blast" and "a large radius surrounding
  your chosen target" burst around what was aimed at, "all creatures in
  sight" takes the room, everything else stays a dart — and the blow
  rolls separately on each victim, answered by its own resistance. The
  blast's distance is the engine's, since the prose names a blast
  without measuring it.
- **The class and skill numbering closed — and cast a doubt**: both runs
  live in `GLOBAL.TXT` and the executable indexes them by id (class =
  row 253 + id, skill = row 271 + id), which independently confirms
  every promotion beat: 9→10 is Paladin→Crusader, 7→8 Wizard→Archmage.
  With names attached, the traced percentage table reads Shield 50,
  Leather 30, Chain 25, Plate 10 — which looked like armour class and
  put the getters' side in question.
- **The weapon path confirms the formula from the other side**: the
  getter the to-hit routine uses when a weapon is involved reads the
  equipped weapon slot, then computes exactly as its sibling does —
  stat 4 plus a skill scaled by the per-skill percentage — with a
  separate branch for the two blasters that bands a value against
  thresholds 50/100/150 first. The skill it scales is the first one the
  character *has* off a fixed priority list, not the weapon's own, which
  is why the id-to-name join keeps failing.
- **The skill-name join came up empty, with a finding beside it**: the
  percentages could not be matched to skill names — the executable's
  priority list interleaves armour skills and magic schools in an order
  no reading of `SkillDes.txt` explains, and the getter may prove to be
  the armour-class sibling rather than the melee bonus. What the attempt
  did settle: the character's skills live at `+0x1380` in eight-byte
  records, and both driving tables are transcribed in the record doc.
- **The attack bonus traced to stat 4**: the base getter's own jump
  table names a field per stat id — the seven attributes in a 16-byte
  run, and the attack bonus among them — so the original adds the
  attribute **raw**, not through the sheet's bonus curve, then adds the
  weapon skill at a percentage the weapon's kind picks (100 down to 10,
  a twelve-entry table). The engine now reads accuracy raw; joining the
  percentages to this engine's skill names is the one step left.
- **The evpan gamble closed**: the format string's single caller reads
  its number from a 16-byte table in the executable — and that table
  turned out to be the interior list itself, carrying each room's
  `EVPAN` side panel beside its video name. All 52 panel numbers it
  names ship, and the talk screens now wear each establishment's own
  authored panel instead of the generic marble. The record's fourth
  column was first read as a sound id; the claim is **withdrawn** —
  none of its values resolves in `DSOUNDS.BIN` or `GLOBAL.TXT`, so the
  engine plays no hum and the field is filed unknown.
- **The player record, mapped from the traces**: three separate digs kept
  re-deriving the same offsets, so they are now written down —
  `docs/formats/player-record.md` collects the resistance bytes, the
  armour class the to-hit roll reads, the packed skill byte, the
  equipped-slot arrays with their broken-flag bit, and the stat
  dispatcher at 0x482e80 whose 23-id jump table shows how any stat is
  fetched with its bonuses. The recovery field is named as the one thing
  still missing.
- **The hit roll traced, and the oldest guess retired**: the routine at
  0x421cb0 — one function above the resistance rule — rolls
  `rand() % (armour + 2*attack + 30)` against a bar the blow's kind
  picks (plain `armour+15`, a shot `2*armour+30`, the steep kind half
  again more). The engine's `50 − AC` is gone from both directions of
  the fight, and the arithmetic's own consequence is now true here: an
  unskilled archer cannot touch an armoured target, while a skilled one
  can.
- **Resistance traced, and it was never a percentage**: the hunt for the
  hit roll turned up the resistance routine at 0x421dc0 instead, and it
  refutes the engine's oldest combat guess — the element byte answers
  immune at 200, and otherwise the blow is halved up to four times, each
  halving bought by a `rand() % (resistance + 30)` landing 30 or above.
  The engine now rolls exactly that, and the old percentage test is
  replaced by one asserting the traced rule.
- **The sky selector traced to the last die**: the picker at 0x46df60
  re-rolls the outdoor sky once per game day — 80% from nine fair skies,
  20% from seven others, two authored tables in the executable, `sky01`
  the fallback — and the engine now rolls the same way (nineteen skies
  ship, three of them in neither table, noted). The header-named sky
  still wins where a map states one.
- **The chest flag finally leaned**:- **Fullscreen at F11**: the display story closes — windowed at any
  integer scale, fullscreen at a keystroke, the logical 640x480 and its
  square pixels kept either way.
- **The eye joined the globe**: with Wizard Eye lit (or a Cartographer
  hired), the maps page carries the spell's dots at their true places —
  the living in red at rank one, treasure in gold at rank two — on both
  the outdoor render and the indoor plan.
- **The world remembers, and the save remembers with it**: travel keeps
  a per-map memory the way the original's state files did — the fallen
  stay fallen, opened chests stay open and thrown doors stay thrown when
  the party returns, each map forgetting on its own `Refil Days` clock
  (and never, where the table says it never refills). Those memories now
  ride in the save file as their own record kind, the map underfoot
  written in beside the ones left behind and restored before the map
  opens, round-tripped in the save test.
- **OUT.EVT unmasked as a null sink**: the shared outdoor script's 87
  stubs are identical do-nothing husks, and exactly three shipped
  facets — two in Sweet Water, one in New Sorpigal — point into it,
  each with an event id its own map never defines. A defined nothing
  for stray ids to land on; why the other 84 husks exist stays open.
- **The window grows without lying**: `--scale N` (default 2) presents
  the honest 640x480 at an integer multiple through SDL's logical
  presentation, mouse zones converted back to the game's own
  coordinates — square pixels on modern glass.
- **The opening plays as it shipped**: the 3DO logo and the intro run
  before the title — full-screen Smacker with their own sound through
  the room player's machinery, any key sending the reel forward — and
  the Credits plate now rolls the credits film itself.
- **The last two orphans ruled cut content**: "Returned the Prince" and
  "Gave Bat Guano to Barad" have no mechanism anywhere — the guano
  pouch ships as item 554 that no event touches, and Barad exists in no
  table, no prose and no executable string. They join the Quest column
  as rows the shipped game cannot reach, and the orphan ledger is now
  fully explained: services, counters, the library, the obelisks, and
  two cuts.
- **MM6.ini honored**: the install's own three-line config follows the
  player in — `AlwaysRun=1` makes shift walk instead of run (footsteps
  agreeing), `LoudMusic` picks the music gain, and `FlipOnExit`, which
  has nothing here to flip, is left alone on the record.
- **The dark turn-ins walked**: the census of rival item branches found
  the one that matters — Slicker Silvertongue's own topic takes the
  Zenofex letter shown to the wrong man, swaps bit 200 for 201 (the
  traitor warned) and hands cultist cloaks to the empty-handed — now
  beat twenty-six, with the friendlier branches (the charm-seer's three
  tokens, the musician's flute-or-harp) filed beside it.
- **The chronicle spells the secret**: the fifteen fragments render as
  the fifteen-column grid they are — held stones side by side in a
  fixed hand, dark columns for the missing — so the obelisk sentence
  assembles visually on the chronicle page the way the puzzle intends.
- **The quad section survived round three**: with the sector table and
  doors now readable, the BLV's last big unknown took two more
  refutations — fields 2+3 are not an MM7-style coplanar offset-and-count
  (the intervals overlap by the hundreds though they leave no gaps), and
  field 2 tracks neither sectors nor lights across maps. The interval
  test ships in `blv_info --bsp` so round four starts where three ended.
- **The obelisk trail read end to end**: all fifteen outdoor obelisks
  turn out to write their fragments as autonotes 79..93, one per map —
  so the chronicle assembles the scrambled message on its own as the
  party travels, and the fifteenth fragment now earns award 62, the
  puzzle's own honor, placed by the engine the way the library's was.
  Sweet Water's fragment is beat twenty-five of the regression.
- **The doors that ship open, open**: the attribute bit's traced
  meaning — the loader tests `attribute & 1` and swings those doors wide
  before the first frame — is now honored at load, so D07's twenty
  standing-open doorways stand open here too.
- **The bounty board opens**: Town Halls post a monthly head — a
  hostile row picked deterministically by the month, the same in every
  hall — the battle now reports its kills, and a taken head claims a
  level-times-hundred purse at the counter, ticking award 81 the way
  the ledger said nothing could. The month's length, the pick and the
  purse are the engine's numbers, marked; the award is the table's.
- **The water finally breathes**: the shimmer negative got its second
  half — each water tile's own palette carries one long blue run
  (`WtrTyl`'s spans 182..254, four-fifths of its pixels), and the engine
  now rotates the detected ring a step at a time and re-bakes the tile,
  so the coasts of Enroth move. The run is the palette's own; the
  cadence is the engine's, marked.
- **The arena fights back**: the standing level got its tournament —
  1 through 4 at the gate take the Page, Squire, Knight or Lord
  challenge, a crowd drawn from the monster table's own level bands
  storms the sand, and clearing it pays the purse and ticks the rank's
  counter award 84..87 that no script could. The bands, counts and
  purses are the engine's numbers, marked; the original kept its own in
  the executable.
- **The arena found**: awards 84-87 count victories no script grants —
  because the tournament is executable code around a real level:
  `zarena.blv` ships in `Games.lod`, names itself "The Arena", stands
  191 polygons tall and renders under this engine today
  (`starhaven zarena.blv`). The wave machinery is a lead for the engine
  to build the way the guild counters were built.

- **The loose-ends drawer, emptied and inventoried**: the door block's
  second count word's high half is closed — it duplicates the vertex
  count on 795 of 795 doors — while two measurements stay honestly open
  with their refutations filed: the door attribute word is 1 on exactly
  41 of 795 doors (no pattern tested fits), and the chest's last u16
  splits 149/1191 along neither appearance nor contents.

- **The chest grid closed the same way the third grid did**: the 140
  i16s beside the chest's item slots are zero on all 187,600 cells
  across the 1,340 shipped chests — runtime loot layout shipped empty.
  `ddm_info` prints the nonzero count so the claim stays checkable; the
  chest record now has exactly one unread u16 left.

- **The third grid gamble closed at zero**: the 128x128 grid at 0x80B0
  — the outdoor format's oldest unknown — ships as all zeroes on every
  one of the fifteen maps, with the model array starting right behind
  it: runtime state shipped empty, like the monster records' tails.
  `odm_info` now prints its nonzero count so the claim stays checkable.


- **The backdrop gamble paid in full**: the 2DEvents Picture column's
  target was recovered from the game's own executable — `MM6.exe`'s
  EVENTS.CPP string block lists the interior-video names, and read in
  descending address order from `blcksrch` they number exactly 1..118.
  Every anchor checks: smithies under the weapon shops, each guild on
  its school's screen, the P/M/R houses on their own rooms, the Seer in
  the poor oracle's hut, all twenty dungeon entrances in order. The
  mapping ships in `data_info --backdrops`; playing the interiors on the
  service screens is now one join away.

- **The Misc Special gamble closed in one measurement**: the monster
  table's last prose column turned out to hold no prose at all — a
  literal 0 on every one of the 173 rows, vestigial like the Quest
  column beside it. The whole of `MONSTERS.TXT` is now either read and
  running or measured empty; `data_info --riders` reproduces the sweep.

- **The last monster columns, measured**: `Hst` is hostility — zero on
  exactly the nine Wimp peasants, four on the other 164; `Rec` is the
  blow-to-blow recovery, 40..100 read as hundredths of a second; and
  every one of `Bonus`'s 39 distinct on-hit words now lands on an engine
  rider — the census found three that used to fall through, so monsters
  whose word is `Stone`, `Dead` or `Errad` now petrify, kill and
  eradicate as written. Reproduce with `data_info --riders`.

- **The gamble split down the middle**: whether `Dif 1-5` picks a spawn's
  A/B/C letter could not be read from the shipped placements — only 17 of
  the 340 placed actors match an encounter slot at all, noise over five
  difficulties, filed as the honest negative — but the cross-tab cracked
  a different unknown on the way: the actor record's variant byte at
  +0x35 **is** the letter, 1=A 2=B 3=C on 319 of 340, with a shared value
  15 and three stragglers left honestly open. Reproduce with
  `ddm_info --variants`.

- **The monster record, read to the last field**: the 34 silent bytes of
  `DMONLIST.BIN` gave up their meaning — a height and radius in world
  units at the front (bats at 56, Dragons at 487, spiders wider than
  tall), four sound ids stated outright (the Guards' fidget skips an id,
  refuting the old base-plus-offset reading the engine now no longer
  uses), a constant 140 whose meaning stays honestly `unknown`, and
  twenty zeroed bytes of runtime scratch. Reproduce with
  `sft_info --body`.

- **The Quest column, closed**: the monster table's last unread column is
  zero on all 173 rows — vestigial. The quest items travel through the
  event scripts' gives, as the walker already delivers them.

- **The monster table, read to the last column**: the second attack swings
  at its own Att% (whose 10..30 values betray it as the second's chance
  despite the header's grouping — a Cobra's poison fangs at 20), the Fly
  column lifts its bearers off the ground, and Pref aims monsters at the
  classes its initials name — the Terrible Eye goes for "D,S", the casters.
  The dozen digit-valued Pref rows are filed unknown.

- **The drawer, emptied**: Lloyd's Beacon now honors its expert and master
  cells — "3 Beacons", "5 Beacons", decaying in their written days and weeks
  per point — through a place-or-recall list; the shops' opens/closes
  columns finally bar the door, a shut counter naming its hours instead of
  trading; and the lock column joins the trap column on the chests — a
  locked chest stays shut until the party's best Disarm reaches the map's
  own number.

- **Skills, read off their own table**: `SKILLDES.TXT` writes what each of
  its 31 skills does in effect lines — "Skill added to Attack Bonus",
  "...to Attack Damage", "...to Armor Class", "Skill adjusts shop prices in
  your favor" — and the engine applies exactly those: weapon points ride the
  to-hit roll and the damage where granted, armour and shield points join
  the AC, a school's points are what its spells scale by, and a merchant in
  the party haggles every counter. The expert and master lines wake at
  engine-own thresholds and do what they say: stuns and triple blows at
  odds "equal to skill", the Bow's second arrow, doubled and tripled
  shields and discounts, faster swords. Points come five a level and are
  spent on the sheet at an engine-own staircase; the hired Arms Masters,
  Squires and merchants add their rows' bonuses to the same numbers.


### The machine itself

- **The save file caught up**: the audit found three things the record
  had outrun — each member's readied spell, the turn-based toggle and
  the hourglass's count — now appended as new record kinds the old
  parser skips and the old saves simply lack, round-tripped in the save
  test. UI cursor state (the sheet's page, the book's tab) stays
  deliberately unsaved.

- **The portability claim has a tested witness**: a GitHub Actions
  workflow runs the README's own three commands — `meson setup`,
  `ninja`, `meson test` — on macOS, Linux and Windows runners, and the
  whole path has now been walked here rather than assumed: a clean
  clone with nothing but the four wrap files configures, downloads
  Catch2, builds 327 targets and passes 65 tests; forcing the SDL3 and
  zlib fallbacks the way a Windows runner will builds both from source
  and passes 66. Windows now gets that fallback explicitly, and a failed
  run uploads its meson logs. Only the synthetic-fixture tests run
  there; the game data never leaves the player's machine.

- **Saving**: **F5** writes the game, **F9** brings it back — quest bits and
  event variables, the purse, all four packs cell by cell, worn equipment,
  the party's numbers, the clock, the map and where the party stands on it,
  and the current map's opened chests and thrown doors, which are re-thrown
  on load so the portcullis you raised is still up. The format is this
  engine's own versioned text and says so; it neither reads nor writes the
  original's save files.

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
- `evt_info`, a CLI that prints one map's event script and the strings it
  refers to, resolving the opcodes that name a string.
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
[`docs/formats/odm-tile-index.md`](docs/formats/odm-tile-index.md),
[`docs/formats/portraits.md`](docs/formats/portraits.md),
[`docs/formats/map-events.md`](docs/formats/map-events.md),
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
18. ~~Decode the `.blv` lights and light the dungeons with them.~~ ✓ (the quad section after them is measured but unread; exact section offsets remain)
19. ~~Decode `.odm` decorations and render them as sprite billboards.~~ ✓ (this slice)
20. ~~Collision, gravity and mouse-look in both walkers.~~ ✓ (this slice)
21. ~~Decode event-record bodies.~~ ✓ (90 opcodes measured, two dozen named
    and running — the scripts walk, ask, pay, hurt and teleport)
22. ~~Smacker DPCM audio and an SDL3 audio sink.~~ ✓ (this slice)
23. ~~Decode the sound-effect archive and its IMA ADPCM waves.~~ ✓ (this slice)
24. ~~Decode actor placements from the event files.~~ ✓ (this slice)
25. ~~Resolve actors to sprites via `DMONLIST.BIN` and draw them.~~ ✓ (this slice)
26. ~~Verify the actor position field and map the record's fields.~~ ✓ (this slice)
27. ~~Share the scene, camera and asset code between the walkers; draw indoor decorations.~~ ✓ (this slice)
28. ~~Play the installation's MP3 music.~~ ✓ (this slice)
29. ~~Decode the BLV face-extra name array.~~ ✓ (this slice)
30. ~~Gameplay systems from the game's own tables: combat, skills, spells,
    shops, hirelings, promotions, reputation, travel, rest, saves.~~ ✓
31. ~~The game's own screen furniture: frame, book, maps, calendar,
    creation hall, sheets, chests, camp, services, talk — the whole
    interface now wears the shipped art.~~ ✓
32. ~~The spell book and combat casting, both directions, at the tables'
    own numbers.~~ ✓
33. ~~Quest arcs beyond New Sorpigal: the whole main quest and the first
    side and promotion chains run as a 24-beat regression, and all 58
    script grantors walk and grant.~~ ✓
34. ~~The unread corners: `DMONLIST.BIN` read to its last constant, the
    outdoor third grid and the chest grid measured empty, the chest word
    and the door count word closed.~~ ✓ (the BLV quad section and a
    handful of named constants stay honestly open)
35. The animated remainder: the water's palette rotation, the `sky%02d`
    selector, the shop videos played in full motion with their sounds.
36. The exe-side services still unmodelled: the arena counters, the
    bounty board, the obelisk assembly.
37. The long tail of side quests, walked into the regression the way
    Snergle's was.

## Performance

Measured with `--bench 120` at 640x480 on the heaviest maps, release build
(`meson setup buildRel --buildtype=release`): Castle Alamos (`CD1.Blv`,
4,000-odd faces) renders at ~81 fps median, Free Haven (`OutC2.Odm`) at
~118 — re-measured after the sky cylinder, the water's palette re-bake,
the health bar and the room players landed; the outdoor sky's full-screen
pass costs Free Haven about 6% of its former ~125 and is left as is. A debug build is roughly eight times slower and is what `buildDir`
holds by default — bench release before believing a number. Faces the
camera provably cannot see are skipped by a conservative sphere test baked
per face at load, and the per-frame equipped sync reads parsed skill and
enchantment powers from a memo instead of re-parsing table prose.

## Contributing

C++20, Meson + Ninja, `warning_level=3`. Keep `src/main.cpp` thin; put logic in the
`starhaven_core` library so the app, tools, and tests share one code path. Never
commit game data, extracted assets, or any content from the original game —
fixtures must be synthetic.
