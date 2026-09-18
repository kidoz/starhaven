---
title: Event-script coverage audit
summary: Reproducible coverage of every shipped MM6 event script against the current walker, with missing-handler locations and a prioritized backlog.
doc_type: explanation
status: partial
last_updated: 2026-09-18
source_files:
  - src/game/script_object_effects.cpp
  - tools/evt_info.cpp
  - src/game/script_coverage.cpp
  - src/game/script_walk.hpp
  - tests/test_script_coverage.cpp
  - src/game/script_loot.cpp
  - tests/test_script_loot.cpp
  - src/game/script_objects.cpp
  - tests/test_script_objects.cpp
  - src/game/temporary_objects.cpp
  - tests/test_temporary_objects.cpp
tags:
  - events
  - compatibility
  - coverage
  - evidence
---
# Event-script coverage audit

The current walker has **29 executable opcode cases and one metadata case**.
**Seven original handlers remain absent**, accounting for **178 records**.
Opcode 34 has a dispatch case but **partial runtime support**: persistent ID-1
loot and temporary IDs 1000/1050/2081/2100/4070/8080 have bounded live paths.
Actor responses and presentation gaps remain; other IDs report explicit errors.
These counts measure dispatch, not completed behavior; the reduced unsupported count must not be read as full opcode-34
compatibility.

This is a static census of every `.EVT` in `icons.lod`, including global,
unused and template-like scripts. It does not prove event reachability, full
argument semantics, outcome application by the UI, or campaign completion.
Opcode 13 is integrated; its
[decoration semantics](../formats/map-events.md#opcode-13-changes-a-placed-decoration)
supersede the audit's tentative variable-operation label. Opcode 33 now
[displays a modal and resumes the event](../formats/map-events.md#opcode-33-displays-a-message-and-suspends-the-event)
after dismissal; all 88 records are covered. Opcode 41 now
[generates item rewards](../formats/map-events.md#opcode-41-generates-an-item-reward);
all 80 complete records generate valid items, with six short records reported.
Opcode 42 now [changes the current decoration's event](../formats/map-events.md#opcode-42-changes-the-current-decorations-event);
41 complete records pass, with six short records reported.
Opcode 23 now [changes indoor face attributes](../formats/map-events.md#opcode-23-changes-indoor-face-attributes):
32 complete records pass, with 13 short records reported. Collision and texture
animation respond to the flags. The alternate draw-path effect of mask `0x10`
remains a rendering gap; storing that bit does not certify its appearance.
Opcode 34 is now [decoded as object spawning](../formats/map-events.md#opcode-34-spawns-sprite-objects),
correcting its tentative party-coordinate label. Its
[persistent loot](../formats/map-events.md#persistent-event-loot) now survives
pickup, map memory and save/load. Temporary IDs 1000/1050/2081/2100/4070/8080 now move, collide
with indoor/model geometry and outdoor terrain, animate and expire in the live
adapter; ID 8080 also has its resisted actor response and 8081 replacement. Other
actor contacts, terrain-material responses, trails, sound and other
original object behavior remain follow-up work.

## Reproduce

```bash
just build
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/evt_info --coverage > /tmp/event-coverage.tsv
./buildDir/evt_info --coverage --strict > /tmp/event-coverage-strict.tsv
```

The normal audit exits 0 when every script was read and parsed, even with
known coverage gaps. `--strict` returns 1 if any unsupported or short-argument
record remains; it currently fails by design. Missing resources, failed
payloads, malformed containers/records, duplicate case-insensitive script
names, or an archive with no `.EVT` entries also return 1. Invalid coverage
options return 2. Failed script names remain in the report; partial counts
must never be presented as a complete census.

The deterministic TSV contains schema comments and four row types:

| Row | Contents |
| --- | --- |
| `SUMMARY` | Completion flag, scripts seen/parsed, scoped event count and record totals |
| `OPCODE` | Numeric opcode, current dispatch classification, original handler/default path, distinct script/event counts, record totals and argument-size histogram |
| `SCRIPT` | Filename, parse status, unique event count and record totals |
| `LOCATION` | Every unsupported or short-argument record: filename, event, sequence, zero-based record index, opcode, argument size and event features |

Counts columns are `records`, `dispatched`, `metadata`, `unsupported`,
`short_arguments`, `original_handler_gaps`. Dispatched, metadata and unsupported
sum to `records`; the last two columns are subsets of dispatched and unsupported,
respectively. A repeated opcode in one event adds multiple records but only
one distinct event. Equal event IDs in different scripts remain distinct.
All branches and records after terminators are counted, without executing them.

Filenames escape backslash, tab and line endings. Output contains no argument
bytes, dialogue, quest text or string-table contents. An exhaustive synthetic
test probes the real walker with all 256 byte opcodes to detect classification
drift. Minimum argument sizes reflect current length guards, not a complete
format validator; checks on values, strings and variable types remain outside
this measure.

## Measured baseline

Audited 2026-09-17 with partial opcode-34 loot support on engine base `f28413c`.
The original audit at `b6a5cc7` had 609 missing-handler records; opcode 13
removed 110, opcode 33 removed 88, opcode 41 removed 86 and opcode 42 removed
47; opcode 23 removed 45. Adding opcode-34 dispatch moves another 55 records
out of that static category, leaving 178, despite incomplete object semantics.
Source: user-owned
MM6 GOG installation, `data/icons.lod`,
32,772,165 bytes, SHA-256
`2e8f2c0d0b88776eb2b09c5ad1a6937b5e4aff88c2190d3340327eb5b813bd18`.
Build: Apple Clang 21 on ARM64 macOS, Meson 1.12.0. Original files remained
read-only. No claim is made for other editions without a separate census.

All figures below are `observed` from `evt_info --coverage`:

| Measure | Count |
| --- | ---: |
| Scripts enumerated and parsed | 83 / 83 |
| Parse or payload failures | 0 |
| Distinct `(script, event)` pairs | 3,332 |
| Total records | 15,504 |
| Records whose opcode has a walker handler | 12,719 |
| Metadata records (opcode 4) | 2,192 |
| Unsupported records | 593 |
| Unsupported records with an original handler | 178 |
| Unsupported records on the original default path | 415 |
| Dispatched records below the current argument-length guard | 311 |
| Distinct opcode values | 90 |
| Executable / metadata / unsupported opcode values | 29 / 1 / 60 |

The [original dispatch table](../formats/map-events.md#the-complete-opcode-table)
bounds execution to 1–43, with six default-path cases inside that range.
Thus 53 of the 60 unsupported values follow the original default path, leaving
7 absent real handlers, alongside partial opcode 34. This distinction reuses the recorded executable
research; the audit does not newly establish original opcode semantics.
An explicit runtime policy for those default-path records remains follow-up
work; the audit preserves them in the raw unsupported count.

Feature co-occurrences in the TSV identify candidate quest, door and travel
risks. They do not prove the missing instruction executes on the same branch
or blocks the feature. Opcode-34 resource errors are outside this static
missing-handler classification and must also be considered during playtesting.

## Missing handlers, ordered by frequency

Names below retain the evidence level in the existing format note. Inferred
names are research leads, not implementation specifications. Counts include
template-like records; the TSV histogram shows their argument-size split.
Example coordinates are `script / event / sequence` and are `observed`.

| Opcode | Existing reading | Records | Scripts | Events | Example |
| ---: | --- | ---: | ---: | ---: | --- |
| 8 | Play effect/sound by category (`observed`) | 60 | 20 | 36 | `D17.EVT / 29 / 7` |
| 22 | Reset dialogue/choice buffer (`inferred`) | 28 | 21 | 22 | `D04.EVT / 52 / 2` |
| 12 | Set variable with name pointer (`inferred`) | 25 | 13 | 19 | `OUTE3.EVT / 231 / 5` |
| 3 | Spawn sprite object (`observed`) | 24 | 12 | 18 | `D17.EVT / 29 / 3` |
| 24 | Variable operation (`inferred`) | 19 | 9 | 10 | `OUTD3.EVT / 220 / 2` |
| 10 | Set boolean game-state flag (`observed`) | 15 | 9 | 9 | `OUTC1.EVT / 211 / 2` |
| 43 | Read variable by type (`inferred`) | 7 | 7 | 7 | `T7.EVT / 1 / 1` |

## Prioritized next work

Priority is `inferred` from frequency, event co-occurrence and the existing
handler research. Trace each selected operation's inputs and state effects
before implementing it, then add a synthetic walking test and an install-backed
behavior check. A handler that merely consumes a record is not completion.

1. **Object lifecycle and remaining traversal state: 34 and 43, with 10.**
   Opcode 34 creates sprite objects, including persistent loot and temporary
   effects. Its 30 complete D18 records require motion, collision, expiration
   and impacts. IDs 1000/1050 now run in live simulation and rendering, with an
   event-56 branch probe; CD2's ID-2081 effect and ID-1 loot pass a combined
   entry-path probe, including pickup and save/map return.
   Next resolve the remaining actor contacts, trails, sound and other types. Keep
   the absent descriptor ID 36 in ZNWC visible. Opcode 23 already changes passage collision.
   Opcode 43 has one seven-byte use, `T7 / 1 / 1`, in a door event. The two
   two-byte uses of opcode 10 are `OUTC1 / 211 / 2` (quest) and
   `OUTD3 / 203 / 0` (travel); low frequency does not make them harmless.
2. **Remaining world and interaction effects: 12, 22, 3, 8 and 24.** All 12
   fifteen-byte opcode-12 uses share quest-bit events. Thirteen opcode-22
   records share quest-bit events. Opcode 3 and 8 occur together in
   `D17 / 29`, a door event. Resolve state-changing effects before assuming
   these are presentation-only gaps.
3. **Default-path policy and short records.** Keep the 415 original-default
   records accounted for, without inventing handlers for authoring leftovers.
   Establish which short records are reachable and which are unused before
   setting a reachable-script acceptance gate.

The operand audit, live ID-1000/1050/2081/2100/4070/8080 effects and persistent ID-1 loot are
implemented. Temporary effects clear on successful map open/load; original
persistence parity and the remaining IDs are still open. FC-2's audit exists;
opcode completeness remains open.

## Short records and limits of the census

All 311 below-guard records are concentrated in these files (`observed`).
Since the original 263, thirteen short opcode-13, six short opcode-41, six short
opcode-42 and thirteen short opcode-23 and ten short opcode-34 records have moved from
unsupported to below-guard:

| Scripts | Short records |
| --- | ---: |
| `DBM1.EVT` through `DBM5.EVT` | 46 each (230 total) |
| `DDB1.EVT` | 35 |
| `OUT.EVT` | 27 |
| `LWSPIRAL.EVT` | 13 |
| `SPIRAL.EVT` | 4 |
| `DWJ1.EVT` | 2 |

They parse successfully as framed records. The repetitive DBM/OUT patterns
suggest authoring templates (`inferred`), but nothing is excluded from totals.
Not all short records are empty placeholders: `LWSPIRAL` events 14–26 and
`SPIRAL` events 1–4 use opcode 26 with four argument bytes, below the current
13-byte Ask guard. Determine whether these scripts are ever loaded and whether
they represent an older layout before extending the parser or walker.

Coverage also does not establish correct event entry points, timer firing,
branch targets, character selection, typed-variable behavior, persistence of
every outcome, or service/UI integration. These remain behavioral acceptance
work, alongside the [campaign completion contract](campaign-completion.md).

## Validation

Coverage tests exercise
all 256 opcode classifications against the actual walker, repeated records and
scoped events, mixed-case extensions, deterministic output, argument guards,
bad containers/records/payloads, duplicate names, no-script input, and TSV
escaping without raw argument output. The local census reads the original
installation without executing events or writing saves. The separate
`evt_info --messages` mode probes all 88 message records and verifies three
full-event pause/resume flows in disposable walker state. The 34-beat arc
regression now explicitly acknowledges the obelisk message before expecting
its journal fragment.

`evt_info --generated-items` loads the user-owned item-generation tables and
checks every opcode-41 request with a fixed seed: 86 records, six short,
80 generated, ten explicit ID overrides, zero failures. Its additional
GLOBAL 426 flow verifies reward generation, subsequent variable mutation and
opcode 42 changing the next event to 424. A second walk of 424 gives no further
item reward. Synthetic tests
cover generated metadata, subsequent item checks/takes, modal continuation,
full packs, save/reload random continuity, and malformed save rejection.

`evt_info --decoration-events` verifies all 47 setter records (six short,
17 hide operations, 24 event changes) and loads all 67 raw map files, including
unused maps. All 788 active implicit decoration interactions resolve to GLOBAL
under the engine's deterministic initialization. This checks joins, not every
branch of every global event. The normal map smoke uses the 55 non-placeholder
catalogue maps. Synthetic tests cover initialization thresholds, the 124-slot
limit, explicit local events, targeting and wall occlusion, per-placement state,
modal context, visibility ordering, byte wrapping and version-5 save validation.

## Indoor face verification

`evt_info --face-bits` validates all 32 complete opcode-23 records against
four loaded maps. It reports the 13 short records separately and preserves
all 45 in the denominator. It also walks CD2 event 33 from sequence 5 to
open the passage, D12 event 22 followed by 23 to start/stop a painting,
and D17 event 55 to start another painting. All three flows include a
version-6 save/reload check; no saves are written to the user's slots.
The 15 uses of mask `0x10` verify stored attributes only, with the rendering
difference explicitly unresolved. These are effect probes, not proof of every
branch's reachability in a player campaign.

## Object-spawn verification

`evt_info --object-spawns` decodes every opcode-34 request and checks its
DOBJLIST/DSFT/ITEMS joins without executing it. The 2026-09-11 audit on engine
base `f743726` finds 55 records in 11 scripts and 22 events: ten short,
44 fully resolved and one missing descriptor (`ZNWC / 65 / 1`, object ID 36).
Complete requests specify 165 objects, of which five belong to the unresolved
record. All 44 matched descriptors select sprite group heads, including frame
zero. The tool returns 1 for the missing descriptor and explicitly labels runtime
support as partial IDs 1/1000/1050/2081/2100/4070/8080. This expected audit failure is an installation finding,
not a failed hermetic test or evidence of a supported opcode.

Synthetic tests cover all 22 truncation boundaries, signed extremes, zero and
maximum counts, nonzero scatter flags, trailing bytes, distinct descriptor and
item ID spaces, first-match semantics, compiled-byte narrowing, missing tables,
and descriptor indices at and beyond the 16-bit limit. The new 22-byte dispatch
guard moves ten short opcode-34 records into the 311 below-guard records.

`evt_info --object-loot` passes CD2 events 35/36 from entry with the default
counter: ID-2081 motion, drawable animation and expiration without impact,
followed by persistent-loot full-pack handling, memory, save/reload and pickup
exactly once. Repeat activation and the alternate counter branch are checked.
No user saves are written. These are installed engine paths, not proof of
natural player reachability or original-runtime visual parity. Synthetic
regressions add wall occlusion, modal continuation, refill and malformed saves.

`evt_info --object-impact` passes D01 event 47's object requests: three
ID-2100 objects follow gravity and loaded geometry, become stationary ID-2101
animations, then expire. It checks the event's one-time counter and drawable
replacement frames; all three finish by tick 253 in the inspected installation.
Actor contacts and the event's companion chest/summon outcomes are outside this
probe. See the [temporary-object specification](../formats/map-events.md#temporary-object-lifecycle)
for the actor-contact exception and remaining presentation/persistence gaps.

`evt_info --object-expiry` seeds OUTE3 event 220 at sequence 4: 45 ID-4070
objects change into ID 4071 at tick 256 and disappear at tick 336, with 3,600
drawable replacement samples. Geometry contacts preserve ID 4070, as verified
by synthetic floor/ceiling tests. The probe skips opcode 3 and the preceding
ID-1050 requests. The terrain-enabled probe records 110 terrain contacts and
15,075 changed position samples against a model-only control. Character
collision and terrain-material responses remain gaps; this is object-lifecycle
evidence, not full event acceptance.

`evt_info --object-removal` exercises OUTD3 event 200 after its unsupported timer
record, with activation counter 105 seeded. Its three ID-8080 requests now
animate and remove on geometry contact or expiry without a detonation or
replacement. The capped/disabled branches are checked; an empty-geometry
control verifies the full 768-tick lifetime. The companion summons and actor
contacts are excluded from that probe. The separate `--object-actor` probe now
checks accepted, resisted and immune contacts using controlled overlap and
installed resources, including all 96 ticks of the stationary 8081 animation.
This does not prove natural activation or original collision selection; see the
[temporary-object specification](../formats/map-events.md#temporary-object-lifecycle).

### Temporary-object simulation probe

Run `STARHAVEN_GAME_DIR=/path/to/MM6 ./buildDir/evt_info --object-lifecycle`.
The 2026-09-18 probe seeds D18 event 56's six spawn branches through the
walker and the same `ScriptObjectEffects` application helper used by the live
adapter. It simulates against the loaded map's collision polygons and resolves
the selected animation frames with a fixed explicit seed. ID 1000 selects the
zero-scale null frame and has no billboard; its missing trail is still a visual
gap. The probe counts those samples separately and requires drawable 1050/1051
sprites.
It creates all 12 requested objects, records eight 1050→1051 transitions and
16 bounces, and removes all 12 by tick 768. It fails on missing resources,
rejected requests, pool drops, unfinished objects or absent movement/impact
coverage. Output is numeric metadata; it does not write saves.

Synthetic tests additionally cover signed/zero speed, frame zero, scatter draw
consumption under capacity exhaustion, resource failure atomicity, pause,
expiry and replacement lifetime, floor bounce and rest, deep initial overlap,
large-step collision, and the far-impact cutoff. The collision helper covers
faces, edges, vertices, floors, walls and ceilings.

This is a seeded branch/application/resource witness, not an original-runtime
comparison or natural player-reachability proof. Synthetic regressions also
cover the shared loot/effect capacity, fractional ticks, paused animation, impact
animation reset and map-boundary clearing. Original collision arithmetic, actor
contacts, visual parity, sound, trails and temporary persistence remain open. See the
[temporary-object specification](../formats/map-events.md#temporary-object-lifecycle).
