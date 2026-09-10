---
title: Event-script coverage audit
summary: Reproducible coverage of every shipped MM6 event script against the current walker, with missing-handler locations and a prioritized backlog.
doc_type: explanation
status: partial
last_updated: 2026-09-10
source_files:
  - tools/evt_info.cpp
  - src/game/script_coverage.cpp
  - src/game/script_walk.hpp
  - tests/test_script_coverage.cpp
tags:
  - events
  - compatibility
  - coverage
  - evidence
---
# Event-script coverage audit

The current walker has **28 executable opcode cases and one metadata case**.
The shipped scripts contain **8 additional opcodes with original executable
handlers**, used in **233 records across 137 events in 44 scripts**. These are
the substantive dispatch backlog, not the previously estimated “45 of 90.”
Counts are `observed`; their gameplay consequences require further validation.

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

Audited 2026-09-10 after implementing opcode 23 on engine base revision `f192476`.
The original audit at `b6a5cc7` had 609 missing-handler records; opcode 13
removed 110, opcode 33 removed 88, opcode 41 removed 86 and opcode 42 removed
47; opcode 23 removed 45, leaving 233. Source: user-owned
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
| Records whose opcode has a walker handler | 12,664 |
| Metadata records (opcode 4) | 2,192 |
| Unsupported records | 648 |
| Unsupported records with an original handler | 233 |
| Unsupported records on the original default path | 415 |
| Dispatched records below the current argument-length guard | 301 |
| Distinct opcode values | 90 |
| Executable / metadata / unsupported opcode values | 28 / 1 / 61 |

The [original dispatch table](../formats/map-events.md#the-complete-opcode-table)
bounds execution to 1–43, with six default-path cases inside that range.
Thus 53 of the 61 unsupported values follow the original default path, leaving
8 missing real handlers. This distinction reuses the recorded executable
research; the audit does not newly establish original opcode semantics.
An explicit runtime policy for those default-path records remains follow-up
work; the audit preserves them in the raw unsupported count.

Among events containing a missing real handler, 38 also contain a quest-bit
operation, 13 a door opcode, and 2 travel; none contains an NPC mutation.
These groups overlap. They are syntactic co-occurrences, not proof that the missing
instruction blocks that feature or executes on the same branch. Feature tags
come from known opcode families, not from dialogue or NPC-table joins.

## Missing handlers, ordered by frequency

Names below retain the evidence level in the existing format note. Inferred
names are research leads, not implementation specifications. Counts include
template-like records; the TSV histogram shows their argument-size split.
Example coordinates are `script / event / sequence` and are `observed`.

| Opcode | Existing reading | Records | Scripts | Events | Example |
| ---: | --- | ---: | ---: | ---: | --- |
| 8 | Play effect/sound by category (`observed`) | 60 | 20 | 36 | `D17.EVT / 29 / 7` |
| 34 | Move to coordinates (`inferred`) | 55 | 11 | 22 | `D18.EVT / 56 / 2` |
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

1. **Doors and traversal: 34 and 43, with 10.** Opcode 23 now changes face
   attributes, including the CD2 passage collision. Opcode 34 has
   30 full-size records in `D18`, beginning at event 56. Inspect whether its
   tentative coordinate reading is correct before treating it as party travel.
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

The next implementation slice should investigate **opcode 34**, starting with
`D18 / 56 / 2`, before assuming its coordinate-operation label is correct.
The audit portion of FC-2 is complete; opcode completeness is still open.

## Short records and limits of the census

All 301 below-guard records are concentrated in these files (`observed`).
Since the original 263, thirteen short opcode-13, six short opcode-41, six short
opcode-42 and thirteen short opcode-23 records have moved from unsupported to below-guard:

| Scripts | Short records |
| --- | ---: |
| `DBM1.EVT` through `DBM5.EVT` | 44 each (220 total) |
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

The hermetic suite now contains 85 test executables. Coverage tests exercise
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
