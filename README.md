# StarHaven

An open-source engine for **Might and Magic VI: The Mandate of Heaven**,
targeting macOS, Linux, and Windows. The goal is a portable reimplementation
that lets you play the original game using your **own legally obtained** copy.

This is a playable engine. It renders every outdoor and indoor map with the
game's own art, lights, music and sound, and runs the game's systems — event
scripts, real-time and turn-based combat, skills, spells, shops, hirelings,
promotions, reputation, travel, rest, saves — grounded in the game's own data
tables rather than invented: New Sorpigal's opening arc, from the letter to
Goblinwatch's reward to the coach out, runs end to end under a scripted
regression. Every decoded format is documented with its evidence tagged
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
- **The monster record, read to the last field**: the 34 silent bytes of
  `DMONLIST.BIN` gave up their meaning — a height and radius in world
  units at the front (bats at 56, Dragons at 487, spiders wider than
  tall), four sound ids stated outright (the Guards' fidget skips an id,
  refuting the old base-plus-offset reading the engine now no longer
  uses), a constant 140 whose meaning stays honestly `unknown`, and
  twenty zeroed bytes of runtime scratch. Reproduce with
  `sft_info --body`.
- **The game's own screen furniture**: the main view now sits in the
  original frame — the border strips, the right column with its windows,
  book buttons and medallions, the portrait bar whose measured oval seats
  hold the party's faces with the green hits and blue mana gauges standing
  in the bar's own grooves, and the footer strip carrying the message
  line. The two big panels ship as PCX inside the standard container, so
  the engine grew a PCX reader (see `docs/formats/interface-panels.md`).
- **The party fires back**: the best damage spell somebody knows flies at
  what the party aims at, out to the same missile band the monsters shoot
  across — a bolt in the school's own art (`fire04` is Fire Bolt, and the
  Water school's prefix is `cold`), the blow landing on arrival at the
  prose's numbers scaled by the caster's rank, with the `X`-variant burst
  flashing where it hit.
- **Nine save slots**: F5 and F9 speak to the current slot, F6 turns to
  the next and says what it holds — the map's name and the day, read off
  the save itself — with slot 1 keeping the old single file's name so
  existing saves survive.
- **The pack obeys its cursor**: wearing, drinking and mixing all start
  from the chosen cell when the pack is open — the sell key already did —
  falling back to the old first-found walk otherwise. Reading scrolls stays
  a world verb: it casts at what the party aims at.
- **The opening arc, proven**: `evt_info --arc` walks New Sorpigal's first
  hours through the same code the game runs — the letter refused until
  held, the delivery's 1000 gold and topic rotation, the Goblinwatch
  combination's award and experience, a lever throwing its doors once and
  holding, the coach priced through to Ironfist, the Fire guild's roll —
  and now the road to Ironfist: the Seer naming each stage in the bank's
  own words, Regent Humphrey paying 5000 and taking the letter (this
  delivery does take it), his "little detail" opening the first council
  task, and Lord Kilburn's shield closing it for award 2, the first
  council seal — ten beats, exit-nonzero on any break. Walking the global
  bank exposed that message indices were being read one byte wide, fine
  for every map's own strings and wrong past 255; the walk now reads the
  whole index. The audit found the one snag no
  unit test saw: bit 81's own note reads "Set when the party starts", so a
  fresh party now begins holding Sulman's letter, as the journal always
  claimed. It also recorded that neither the letter nor the combination
  scroll is ever taken — their events check for them and leave keepsakes.
- **The Quest column, closed**: the monster table's last unread column is
  zero on all 173 rows — vestigial. The quest items travel through the
  event scripts' gives, as the walker already delivers them.
- **The streets talk back**: T stops any of the monster table's own
  civilians — the hostility-zero Peasant rows — and a passerby persona
  assembles from the tables: a name from `npcnames.txt` by the sprite's own
  gender letter, the Peasant personality's reputation-gated greeting, and
  the same beg, bribe and threaten levers as anyone indoors, rumors and
  refusals in the table's wording. The assembly is the engine's; every word
  is a table's.
- **The monster table, read to the last column**: the second attack swings
  at its own Att% (whose 10..30 values betray it as the second's chance
  despite the header's grouping — a Cobra's poison fangs at 20), the Fly
  column lifts its bearers off the ground, and Pref aims monsters at the
  classes its initials name — the Terrible Eye goes for "D,S", the casters.
  The dozen digit-valued Pref rows are filed unknown.
- **The casts ring true**: the sound archive names spells by their own
  `Spells.txt` rows — `04firebolt01`, `31townportal03`, `21fly03` — so every
  scroll, wand wave, H-cast, portal and beacon now plays its spell's own
  sound through that join (91 of 99 ids have one). Bows release with
  `ArchShoot` and blades land with the archive's sword hits, those two picks
  the engine's own and marked.
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
- **The faces react**: the portrait families' 53 frames gave up their
  meanings to a contact sheet — the green poisoned face, the stone-grey
  petrified one, the slumped sleeper, the corpse, the wince — and the party
  strip now wears them: four portraits above the text, each in the frame
  its condition picks, flinching for half a second when a blow lands. The
  frame naming is an observation of the shipped art and says so.
- **The party can lose**: four down means somebody drags them back to the
  last town — a week gone, the name dented ten points, everyone waking at a
  single hit point with their conditions shaken off. Every number is the
  engine's own and says so; the tables never speak of defeat.
- **The dungeons found their lights**: the section after the BLV decoration
  block is the level's static lights — 12-byte records of position,
  brightness (31 on every shipped record) and radius, every point inside
  its map's own bounds on 48 of 52 maps — and the walker bakes them per
  face, so Goblinwatch's sconces light their own corners over a dim floor.
  The falloff curve is the engine's; the lights are the file's. The
  tree-shaped section after them is logged as the likely BSP, the next lead.
- **Magic items finally bite**: the generator's rolls ride the items now —
  through packs, purchases, saves and onto the body — and wearing them
  grants what the tables say: a standard bonus's own stat column at its
  rolled strength (the seven attributes, resistances, AC, hit and spell
  points), and the special bonuses' parsed prose — "+10 to all
  Resistances", "Adds 6-8 points of Cold damage" rolling apart on every
  swing and answered by its own element. Names follow their affixes' own
  shapes: "Longsword of Might", "Vampiric Dagger". What the prose doesn't
  phrase in numbers stays prose, honestly.
- **The drawer, emptied**: Lloyd's Beacon now honors its expert and master
  cells — "3 Beacons", "5 Beacons", decaying in their written days and weeks
  per point — through a place-or-recall list; the shops' opens/closes
  columns finally bar the door, a shut counter naming its hours instead of
  trading; and the lock column joins the trap column on the chests — a
  locked chest stays shut until the party's best Disarm reaches the map's
  own number.
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
- **A name the world reacts to**: the party carries reputation and fame,
  and `npcbtb.txt`'s thirteen gated greetings finally gate — the notorious
  get "Oh No! Please don't hurt me", the saintly an honor, the unknown
  "I only talk to real people" — each in the personality's own words, with
  second meetings remembered. Charity betters the name, threats sour it,
  and the Pirate's, Gypsy's, Duper's and Burglar's written "one full
  category" of reputation finally costs one while they're kept. The bands
  and prices are the engine's own and say so.
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
- **Guild doors check their dues**: `Awards.txt` names a membership per
  school — "Joined the Fire Guild" — and no shipped event sets any of them,
  so the counters sell what the original's executable sold: the shelves
  refuse non-members, J signs the roll at an engine-own hundred gold times
  the guild's own Val, and the membership is worn on the sheet like any
  honor, saved with the rest.
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
- **Promotions, a three-table join**: quest events set "Received Promotion
  to Crusader" awards, `Class.txt` holds the six ladders in row-order triples,
  and each promoted class's own prose prices itself — "an extra two hit
  points and spell points per level". Earning the award steps the matching
  characters up, pays the difference for the levels held, and keeps paying at
  the training halls; Honorary awards stay titles, worn on the sheet.
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
- **The fight's conditions cut both ways**: Mass Fear, Slow, Paralyze and
  Charm now do to monsters exactly what their prose says — fear holds their
  blows and breaks on damage, slow doubles their recovery, paralysis pins
  them where they stand and cannot retaliate, charm calms until hurt — for
  the "3 minutes per point of skill" their own descriptions state, read by
  the same duration parser the buffs use.
- **The honors have a home**: variable type 12 is an `Awards.txt` row on 193
  of 193 uses — Goblinwatch's reward sets 53, "Solved the Goblinwatch
  Combination", naming its own quest — so the character sheet now lists the
  honors the quests bestow, in the table's own words. Type 22 was tried
  against fame and reputation and stays honestly unknown.
- **A party of your own**: starting the walker now opens on the creation
  screen — the twelve portrait families, the six base classes of `Class.txt`
  (its every third heading; the other twelve read as promotions), names from
  `npcnames.txt`, and each class described in its own prose while you choose.
  The attribute rolls are this engine's and the screen says so. Every tooling
  flag skips the door; `--create` forces it open.
- **The fight finds its voice**: each monster's DMONLIST record names its
  four-sound `DSOUNDS.BIN` set at +0x08 — equal to the named set's base on 31
  of 31 monsters whose sound names carry their own table id, a block id on
  all 173 — so a swing plays that monster's attack sound and a kill its dying
  one, through the same mixer the campfires and doors already use.
  Reproduce with `sft_info --sounds`.
- **Hired help from the streets**: talking to anyone with a trade offers H to
  hire them at their `npcprof.txt` row's weekly cost, two seats as the
  original's follower panel gives. Their benefit prose is read literally —
  teachers' experience percents, guides shaving travel days, healers'
  daily rounds, cooks making food, smiths mending for free, the Enchanter's
  elemental wards, dawn casts of Bless and Heroism — and wages fall due
  every seventh day; an unpaid party walks alone again. The "%17 percent of
  gold" the prose threatens is in no column of the table, so nobody takes it.
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
- **Equipment slots**: ten of them, and which one an item goes in is its own
  `ITEMS.TXT` equip type — weapons, missiles and two-handers to the hand, the
  rest where the table says. **E** in a pack wears the first thing that can be
  worn and puts back whatever it replaces. A character now swings what is in
  their weapon slot rather than whatever happened to be first in the pack, and
  armour's flat modifier counts toward armour class while a weapon's dice do
  not.
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
- **Temples that mend for money**: the ten `Temple` rows write their own
  terms — margin notes naming Heal and Donate, `Val` as the price, and a
  service ceiling in the stock cell, from Temple Stone's "All OK" down to
  Temple Baa's "No Dead,Stone,Errad". The counter heals a character whole —
  hit points, spell points, poison — for the row's own price, and shows what
  each house cannot mend, ready for the conditions those words await. What a
  donation earns is this engine's hour of Bless, marked.
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
- **Fountains and potions that really work**: the temporary bonuses land
  now — a fountain's "+10 Might temporary" lies on the party, an Energy
  potion's "Set Temp 7 Stats to 10" on its drinker, Protection's AC and
  Resistance's elements in the sheet's own amounts, and the timed conditions
  ("Set Haste to 6 Hrs") run on the clock for exactly their written hours.
  The sheet shows all of it, a rest ends what lasts until rest, and it all
  rides in the save. The amounts and hours are the tables'; the until-rest
  convention and the party-wide reach of a fountain are this engine's.
- **Spells with the table's own numbers**: the damage and healing in
  `Spells.txt`'s prose follow few enough phrasings to parse exactly — 25 of
  99 spells yield their dice, every direct-damage spell and both heals.
  **X** reads the first spell scroll anyone carries: Fire Bolt rolls its
  written 1-4 per point of skill at what you aim at, answered by the
  monster's own elemental resistance, and the paper is spent. **H** lets a
  caster finally spend spell points — First Aid at the table's cost and
  amount. Who reads, and level standing in for skill, are this engine's and
  say so.
- **A bank that keeps your gold**: the six `Bank` rows' margin notes name
  the counter's two verbs — Deposit and Withdraw — and no column pays
  interest, so neither does the vault. The balance rides in the save. The
  town halls were scouted and left alone: no bounty table ships, so bounty
  hunts would be invention.
- **Ambushes that spring**: opcode 19 named itself against the encounter
  table — its slot stays within the map's own filled encounter slots on 272
  of 272 resolvable uses, its variant is the monster triple's own A/B/C, and
  its count runs to six. Step on the wrong plate and the engine fills the
  room from the map's own table, the new arrivals joining the fight at full
  health while everyone else keeps their wounds.
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
- **Training halls that teach**: the ten `Training` rows carry their own
  numbers — `Val` scales the fee, and the first stock cell writes each
  hall's ceiling, `"Max level = 15"` up to `"No Max"` — so the counter
  offers what the sheet says it should: train a character who has earned it,
  for that hall's price, up to that hall's limit. The experience curve and
  what a level grants are this engine's own and say so.
- **Saving**: **F5** writes the game, **F9** brings it back — quest bits and
  event variables, the purse, all four packs cell by cell, worn equipment,
  the party's numbers, the clock, the map and where the party stands on it,
  and the current map's opened chests and thrown doors, which are re-thrown
  on load so the portcullis you raised is still up. The format is this
  engine's own versioned text and says so; it neither reads nor writes the
  original's save files.
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
31. The game's own screen furniture: the interface layout and panel art
    around the 3D view, in place of the engine's overlay.
32. The rest of the spell book, beyond the buffs, cures and travel spells
    that are in.
33. Quest arcs beyond New Sorpigal, proven the same scripted way.
34. The unread corners: the BLV quad section, `DMONLIST.BIN`'s 34 silent
    bytes per record, the opcode tail.

## Performance

Measured with `--bench 120` at 640x480 on the heaviest maps, release build
(`meson setup buildRel --buildtype=release`): Castle Alamos (`CD1.Blv`,
4,000-odd faces) renders at ~80 fps median, Free Haven (`OutC2.Odm`) at
~125. A debug build is roughly eight times slower and is what `buildDir`
holds by default — bench release before believing a number. Faces the
camera provably cannot see are skipped by a conservative sphere test baked
per face at load, and the per-frame equipped sync reads parsed skill and
enchantment powers from a memo instead of re-parsing table prose.

## Contributing

C++20, Meson + Ninja, `warning_level=3`. Keep `src/main.cpp` thin; put logic in the
`starhaven_core` library so the app, tools, and tests share one code path. Never
commit game data, extracted assets, or any content from the original game —
fixtures must be synthetic.
