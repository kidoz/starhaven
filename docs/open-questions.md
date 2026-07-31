# Open-question register

Status: **audited 2026-07-31.** This page gathers every section headed
"Open question" or "Open questions" under `docs/` and records the best
answer currently supported by evidence. It is the authoritative status page;
the older sections in individual format notes are research history and may
describe hypotheses that this audit supersedes.

## Evidence and scope

The answers combine the shipped MM6 GOG data censuses already recorded in
these documents with two public compatibility references:

- OpenEnroth at commit `539943f`, especially its
  [MM6 snapshot structures](https://github.com/OpenEnroth/OpenEnroth/blob/539943fb171138dbc54d0894cc93d397ad1587e4/src/Engine/Snapshots/EntitySnapshots.h)
  and
  [event decoder](https://github.com/OpenEnroth/OpenEnroth/blob/539943fb171138dbc54d0894cc93d397ad1587e4/src/Engine/Evt/EvtInstruction.cpp)
  and
  [event enums](https://github.com/OpenEnroth/OpenEnroth/blob/539943fb171138dbc54d0894cc93d397ad1587e4/src/Engine/Evt/EvtEnums.h);
- GrayFace MMExtension at commit `9d73c0f`, especially its
  [common MM6 structures](https://github.com/GrayFace/MMExtension/blob/9d73c0ff0482bd231ea522f752f4fe9b26640925/Scripts/Structs/01%20common%20structs.lua).

Those projects are corroborating references, not substituted game assets.
Claims below remain tagged by what the available evidence supports:

- **resolved**: enough information exists to implement the MM6 behavior;
- **scoped**: the question concerns an unobserved edition or an unused path and
  is not an MM6 compatibility requirement;
- **unresolved**: no stable answer is supported yet. The unknown bytes must be
  preserved or safely ignored rather than guessed.

## Register

| Document | Answer | Status |
| --- | --- | --- |
| [`bitmap.md`](formats/bitmap.md) | `0x0100` marks a compressed pseudo-image/non-image payload decoded through the entry's zlib path. `anotherPaletteId` is a runtime loaded-palette slot and is zero on disk in the examined MM6 files. `0x0010` and a separate nonzero colorkey remain unsupported by evidence. | partial |
| [`blv.md`](formats/blv.md) | The high-entropy middle is the sector array followed by shared `u16` list pools. A 116-byte sector has counted lists for floors, walls, ceilings, fluids, portals, draw facets, cogs, decorations, markers, and lights, then water/mist/light fields, the first BSP node, exit data, and a bounding box. BSP nodes are four `i16`: front, back, coplanar offset, coplanar count. | resolved |
| [`dchest.md`](formats/dchest.md) | There was no remaining question: `frame_index` is authoring residue and is not consumed at runtime. | resolved |
| [`dift.md`](formats/dift.md) | The premise was a shifted parse. A row is `group_name[12]`, `icon_name[12]`, time, total group time, bits, and runtime icon index. The names resolve in `icons.lod`; time is in 1/16-second units; bit 0 means another frame follows and bit 2 starts a group. | resolved |
| [`dmonlist.md`](formats/dmonlist.md) | `+0x04` is the descriptor's serialized default movement velocity. Actor preparation normally replaces it with `MONSTERS.TXT`'s per-monster speed, explaining why every shipped descriptor can contain 140. `+0x06` is unused in MM6. | resolved |
| [`dobjlist.md`](formats/dobjlist.md) | `+0x2E` is default object speed. Bits `0x08`, `0x10`, and `0x400` mean lifetime comes from SFT, cannot be picked up, and line trail. The full remaining set is invisible `0x01`, intangible `0x02`, temporary `0x04`, no gravity `0x20`, impact action `0x40`, bounce `0x80`, particle trail `0x100`, and fire trail `0x200`. | resolved |
| [`doverlay.md`](formats/doverlay.md) | The premise was a shifted parse: the four `u16` fields are overlay id, overlay type, SFT group index, and resolved/runtime SFT group. There is no 8.8 scale field. | resolved |
| [`dpft.md`](formats/dpft.md) | The fields are portrait-animation group id, portrait texture/frame index, frame time, total group time, and flags. Time is in 1/32-second units; bit 0 means another frame follows and bit 2 starts a group. The zero group rows are ordinary ungrouped/sentinel frames, and the alleged width/height fields are times. | resolved |
| [`dsft.md`](formats/dsft.md) | Time is in 1/32-second units. Flags are: another frame `0x0001`, luminous `0x0002`, group start `0x0004`, single-direction image `0x0010`, center `0x0020`, fidget `0x0040`, runtime-loaded `0x0080`, and mirror directions 0..7 at `0x0100..0x8000`. Higher MM7-only image/glow/transparency bits are not part of MM6's on-disk table. | resolved |
| [`dsounds.md`](formats/dsounds.md) | Type 0 is level-local, 1 is always-loaded/system, 2 is level-local with replacement/flush behavior, and 4 stays locked until shutdown; type 3 remains unnamed but is not needed to distinguish loop, attenuation, or routing. The 17 trailing words are runtime sound buffers/pointers. IDs are direct table IDs rather than a derivable allocation scheme. `DDECLIST +0x4A` is decoration flags. Ambient radius is engine policy, absent from DSOUNDS; StarHaven's 2,048 is its own tuning. | resolved for format; type 3 unknown |
| [`dtile.md`](formats/dtile.md) | Indoor maps have no equivalent terrain-tile index. BLV geometry names textures per face and uses sector/portal lists for spatial organization. | resolved |
| [`event-actors.md`](formats/event-actors.md) | `+0x35` is monster level, not A/B/C variant. The 548-byte actor includes stats, attacks, spells, AI/graphic states, current position and velocity, facing/look/sector, action timing, home and guard positions, item, frame/sound IDs, 14 buffs, group/ally, eight schedules, summoner, and last attacker. `Dif 1..5` selects A/B/C encounter odds: 90/8/2, 70/20/10, 50/30/20, 30/40/30, and 10/50/40 percent. | resolved |
| [`event-data.md`](formats/event-data.md) | Actor fields are covered above; the chest record is covered by `event-tables.md` and `items.md`. The 883-byte prefix and zero tails are fixed runtime scratch/state buffers shipped empty, not a hidden record array. | resolved |
| [`event-objects.md`](formats/event-objects.md) | The 100-byte record is fully named below in “Corrected binary layouts.” In particular, `+0x18` is look angle, not sound id; `+0x1E/+0x20` are age/max age; `+0x40..+0x55` hold spell, skill, mastery, owner, target, range bucket, and attack type. Attribute bits are listed below. | resolved |
| [`event-tables.md`](formats/event-tables.md) | Every bullet already contains its answer: the fixed zero blocks are runtime state, the chest words are appearance and flags, the 140 cells are runtime item placement, and door bit 0 means starts open. | resolved |
| [`font.md`](formats/font.md) | `FONTPAL` is the default palette loaded with fonts; the font structure can hold up to five palette pointers. Call sites may supply explicit text/outline colors, otherwise palette indices are used. ARRUS is the principal heading/dialog font, LUCIDA body/status/options, SMALLNUM HUD numbers, COMIC map coordinates, BOOK/BOOK2 book text/titles, AUTONOTE journal/quest text, SPELL Lloyd's Beacon, and CCHAR/QUICK creation or credits. | resolved |
| [`games-lod.md`](formats/games-lod.md) | Header size 100 and its 80-byte reserve are fixed writer/version fields unused by the reader. BLV/ODM/DLV/DDM are now covered by their dedicated documents. The examined MM6 Games.lod has no Games7 per-entry compression; support it only when a separately sourced archive exercises it. | resolved/scoped |
| [`items.md`](formats/items.md) | MM6 item condition uses only identified bit 0 and broken bit 1; later temporary/stolen/hardened/refundable bits are MM7+. `+0x18` is body location, followed by max charges, owner, and padding. `ITEMS.TXT` Mod1 is dice/reagent power, Mod2 is addend or enchantment strength, Material is normal/artifact/relic/special, and Equip X/Y are paperdoll offsets. Mirrored cloak/boot branches are shop/item-generation selectors feeding equipment rendering. The uninitialized first chest percentile has no stable serialized meaning; exact bug compatibility requires a runtime trace, not a guessed value. | resolved except bug-for-bug initial stack value |
| [`lod.md`](formats/lod.md) | `+0x18` is reserved zero; at `+0x1C`, low 16 bits are child count and high 16 bits priority. Leaf entries use zero. `size_field` is authoritative for sprites too. Games.lod uses the same 32-byte MM6 entry shape with a different archive type/version. Compressed generic entries use a `91969, "mvii", stored_size, unpacked_size` header followed by zlib, not LZ. | resolved except priority policy |
| [`map-events.md`](formats/map-events.md) | Opcode 6 carries X/Y/Z, yaw, pitch, vertical speed, house id, exit-picture id, and destination string. Opcode 11's outdoor id is a model-facet id, not a terrain tile. Opcode 21 carries spell id, mastery, skill, source point, and destination point; a zero destination aims at the party's current eye position. Opcode 1's byte is consumed and ignored. Opcode 9 targets 4/5/6 are active character/party/random character. Variable ids are the `EvtVariable` enum. The remaining provenance questions are listed below. | partial |
| [`odm-decorations.md`](formats/odm-decorations.md) | The 28-byte placed record is descriptor id, flags, position, direction, event variable, event id, trigger radius, and fallback direction in degrees. `DDECLIST +0x4A` is flags: no-block, no-draw, three flicker rates, marker, slow loop, emit fire, dawn sound, dusk sound, and smoke. Its preceding fields are type, height, collision radius, light radius, and SFT id; sound id and padding follow. Missing art is reached through the SFT group, whose scale is passed directly to billboard rendering. The ~110 KB map tail is the fixed terrain/spatial tables decoded by `odm-tile-index.md`. | resolved |
| [`odm-model-facets.md`](formats/odm-model-facets.md) | `+0x10` is three texture-gradient helpers; `+0x98` is three 20-entry X/Y/Z intercept-displacement arrays; `+0x12F` is polygon type. Array 3 is unused/garbage rather than an ordering permutation. BSP nodes are front, back, coplanar offset, coplanar count. Invisible is `0x00002000`; winding supplies sidedness and no distinct MM6 two-sided bit is required. Stored U/V values are authoritative. | resolved |
| [`odm-model-mesh.md`](formats/odm-model-mesh.md) | This section was already closed: 308-byte facets immediately follow each model's vertices and include a 16.16 plane. | resolved |
| [`odm-models.md`](formats/odm-models.md) | The geometry and pointer/count questions were already closed. The third 128×128 grid is zero in every examined MM6 map and has no demonstrated loader consumer; treat it as reserved/runtime state and preserve it. There is no evidence-backed semantic name. | unresolved name; compatibility behavior resolved |
| [`odm-terrain.md`](formats/odm-terrain.md) | Height bytes use a vertical scale of 32 world units. Tile bytes resolve through DTILE. Model vertices/facets are covered by the model documents. The only remaining grid question is the all-zero third 128×128 block, which has no demonstrated MM6 runtime meaning. | resolved except reserved-grid name |
| [`odm-tile-index.md`](formats/odm-tile-index.md) | Spawn indices 1..3 select M1..M3 random tiers, 4..6 the A variants, 7..9 B, and 10..12 C. The field is an `i16` at `+0x10`; `+0x12` is attributes, with bit 0 “on alert map.” Entry kinds 0..7 are nothing, door, object, monster, party, decoration, facet, and light. Indoor maps use sectors rather than this index. | resolved |
| [`odm.md`](formats/odm.md) | Model vertex/facet/BSP arrays, decorations, spawns, and tile mapping are now documented by the linked child pages. The remaining third terrain grid is all zero and should be treated as reserved/runtime state. | resolved for parsing |
| [`paperdoll.md`](formats/paperdoll.md) | The original draws back doll, bow, cloak back, body/hair, armor back, helm back, boots back, armor, arms, belt, cloak collar, helm, main hand, hands, off hand/shield, remaining hand layers, then magnifier/ring overlays. BODY assets are body/arm pose overlays selected by character body and equipped armor; LEFTHAND and backhand are hand/equipment overlays, and FACEMASK is a face equipment overlay. Exact per-body anchors still need image-level measurement. | resolved behavior; anchors unresolved |
| [`portraits.md`](formats/portraits.md) | DPFT groups select the 53 expression frames: normal and conditions (cursed through petrified), blinks/looks/talk mouth shapes, yes/no, damage reactions, smiles/sad/cast, and late special reactions. Several late frames have no reliable semantic label but are addressable by id. Portrait id is runtime character state serialized in the save; it is chosen during party creation, not by a separate global mapping table. Paperdoll order is summarized above. | resolved for implementation; some labels unknown |
| [`smacker.md`](formats/smacker.md) | No shipped MM6 track exercises Bink audio. Bits 30 and 31 always co-occur and the chunk syntax makes separating their names unnecessary. The four sizes are allocator worst-case hints, not decode bounds; `trees_size` and block structure are authoritative. | scoped/resolved |
| [`snd.md`](formats/snd.md) | The examined MM6 archive is PCM; other editions cannot be inferred from it and are a separate provenance sample. Sound names are labels, while DSOUNDS numeric IDs are what monster descriptors, decorations, events, and engine call sites store. Monster action names corroborate those joins; there is no universal name-to-id formula. | scoped/resolved |
| [`sprite.md`](formats/sprite.md) | The on-disk word described as flags has no demonstrated serialized behavior and is runtime/opaque in the compatibility structures. `emptyBottomLines` is the redundant count of clear bottom rows. No shipped sprite exercises uncompressed data or a distinct nonzero colorkey, so those safe decoder branches remain compatibility fallbacks rather than MM6 findings. | resolved/scoped |
| [`text-tables.md`](formats/text-tables.md) | Encounter A/B/C odds and `Dif` are given above. `Pref` letters form class/gender preference bits; digits 2, 3, and 4 are the number of party characters hit by the relevant attack. `USEITEMS.TXT` is already parsed as alchemy; `Trans.txt` holds transition descriptions; `Merchant.txt` supplies buy, sell, repair, and identify speech. No MM6 consumer for `passwords.txt` was identified; map riddles use strings embedded in each map's EVT tables instead. | resolved except unused `passwords.txt` provenance |
| [`vid.md`](formats/vid.md) | All examined MM6 entries are Smacker. Whether another edition contains Bink is not answerable without that edition and does not block the MM6 reader. | scoped |
| [`terrain-coloring.md`](rendering/terrain-coloring.md) | Closed by `dtile.md`: the map tile byte resolves through `DTILE.BIN` to a ground bitmap/atlas cell. The renderer no longer needs an executable trace to establish that join. | resolved |

## Corrected binary layouts

Several old questions came from plausible-looking but shifted field names.
These compact layouts are the corrections that matter for implementation.

### DIFT, DPFT, and DOVERLAY

```text
DIFT (32 bytes)
  00 char group[12]       0c char icon[12]
  18 u16 time             1a u16 total_time
  1c u16 flags            1e u16 runtime_icon_index

DPFT (10 bytes)
  00 u16 group_id         02 u16 frame_index
  04 u16 time             06 u16 total_time
  08 u16 flags

DOVERLAY (8 bytes)
  00 u16 overlay_id       02 u16 overlay_type
  04 u16 sft_group        06 u16 runtime_sft_group
```

The last DIFT/DOVERLAY indices are serialized as zero or loader-era values and
are resolved when the tables are prepared.

### Map sprite object (100 bytes)

| Offset | Field |
| ---: | --- |
| `+0x00` | object/look id |
| `+0x02` | DOBJLIST descriptor index |
| `+0x04` | current position, three `i32` |
| `+0x10` | velocity, three `i16` |
| `+0x16` | direction |
| `+0x18` | look angle |
| `+0x1A` | attributes |
| `+0x1C` | room/sector |
| `+0x1E` | age |
| `+0x20` | maximum age |
| `+0x22` | light multiplier |
| `+0x24` | 28-byte item instance |
| `+0x40` | spell id |
| `+0x44` | spell skill |
| `+0x48` | spell mastery/level |
| `+0x4C` | owner object reference |
| `+0x50` | target object reference |
| `+0x54` | range bucket |
| `+0x55` | attack type |
| `+0x58` | origin position, three `i32` |

Attribute bits are visible `0x001`, temporary `0x002`, halt turn-based
`0x004`, dropped by player `0x008`, ignore range `0x010`, no Z-buffer
`0x020`, skip a frame `0x040`, attach to head `0x080`, missile `0x100`, and
removed `0x200`.

### BLV sector and BSP node

The sector's ten list categories, in record order, are floors, walls,
ceilings, fluids, portals, draw facets, cogs, decorations, markers, and
lights. The unused cylinder slot between draw facets and cogs is always
reserved. Each BSP node is:

```text
i16 front_node
i16 back_node
i16 coplanar_offset
i16 coplanar_count
```

## Questions that remain genuinely open

The audit reduces the old sections to these bounded unknowns:

- `bitmap.md`: the exact role of ubiquitous on-disk flag `0x0010`, exact
  non-index-zero colorkey behavior, and whether an MM6 file ever serializes a
  nonzero secondary palette id. The secondary field is known to become a
  runtime loaded-palette index.
- `lod.md`: the root-directory entry's `priority` policy. Its packing is
  known: low 16 bits are child count and high 16 bits priority; leaf entries
  use zero. The adjacent word is reserved zero. Compression is a separate
  `91969, "mvii", stored_size, unpacked_size` envelope followed by zlib, not
  those directory words.
- `map-events.md`: why unused `OUT.EVT` husks exist, headerless framing for all
  shared scripts, and the authoring provenance of orphaned topic, face, and
  global events. These do not change the decoded opcode layouts.
- `odm-models.md`: a semantic name for the always-zero third 128×128 grid.
- `paperdoll.md`: exact pixel anchors for every body/armor combination.
- `portraits.md`: human-readable names for a handful of late expression ids.
- `text-tables.md`: whether `passwords.txt` is a discarded authoring file or
  has an untraced executable consumer. It is not the source of map-riddle
  answers, which are local EVT strings.
- Cross-edition questions in bitmap, sprite, SND, VID, and Smacker require a
  separately sourced edition. They are not unknown properties of the examined
  MM6 data.

These are evidence boundaries, not invitations to invent constants. Readers
should preserve unknown serialized fields, reject malformed bounds, and keep
edition-specific behavior behind explicit provenance.
