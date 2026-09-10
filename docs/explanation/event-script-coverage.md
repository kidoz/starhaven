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

The current walker has **23 executable opcode cases and one metadata case**.
The shipped scripts contain **13 additional opcodes with original executable
handlers**, used in **609 records across 379 events in 66 scripts**. These are
the substantive dispatch backlog, not the previously estimated “45 of 90.”
Counts are `observed`; their gameplay consequences require further validation.

This is a static census of every `.EVT` in `icons.lod`, including global,
unused and template-like scripts. It does not prove event reachability, full
argument semantics, outcome application by the UI, or campaign completion.
No gameplay behavior changes as part of this audit.

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

Audited 2026-09-10 against engine revision `b6a5cc7`, with the audit tooling
added in this change. Source: user-owned MM6 GOG installation, `data/icons.lod`,
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
| Records whose opcode has a walker handler | 12,288 |
| Metadata records (opcode 4) | 2,192 |
| Unsupported records | 1,024 |
| Unsupported records with an original handler | 609 |
| Unsupported records on the original default path | 415 |
| Dispatched records below the current argument-length guard | 263 |
| Distinct opcode values | 90 |
| Executable / metadata / unsupported opcode values | 23 / 1 / 66 |

The [original dispatch table](../formats/map-events.md#the-complete-opcode-table)
bounds execution to 1–43, with six default-path cases inside that range.
Thus 53 of the 66 unsupported values follow the original default path, leaving
13 missing real handlers. This distinction reuses the recorded executable
research; the audit does not newly establish original opcode semantics.
An explicit runtime policy for those default-path records remains follow-up
work; the audit preserves them in the raw unsupported count.

Among events containing a missing real handler, 85 also contain a quest-bit
operation, 19 a door opcode, 6 travel, and 11 an NPC mutation. These groups
overlap. They are syntactic co-occurrences, not proof that the missing
instruction blocks that feature or executes on the same branch. Feature tags
come from known opcode families, not from dialogue or NPC-table joins.

## Missing handlers, ordered by frequency

Names below retain the evidence level in the existing format note. Inferred
names are research leads, not implementation specifications. Counts include
template-like records; the TSV histogram shows their argument-size split.
Example coordinates are `script / event / sequence` and are `observed`.

| Opcode | Existing reading | Records | Scripts | Events | Example |
| ---: | --- | ---: | ---: | ---: | --- |
| 13 | Set/compare variable by id (`inferred`) | 110 | 29 | 72 | `CD1.EVT / 59 / 2` |
| 33 | Mode-dependent sub-screen enter/exit (`inferred`) | 88 | 33 | 69 | `GLOBAL.EVT / 20 / 6` |
| 41 | Open panel/dialogue (`inferred`) | 86 | 24 | 61 | `GLOBAL.EVT / 129 / 4` |
| 8 | Play effect/sound by category (`observed`) | 60 | 20 | 36 | `D17.EVT / 29 / 7` |
| 34 | Move to coordinates (`inferred`) | 55 | 11 | 22 | `D18.EVT / 56 / 2` |
| 42 | Conditional check (`inferred`) | 47 | 7 | 47 | `GLOBAL.EVT / 411 / 3` |
| 23 | Variable operation (`inferred`) | 45 | 11 | 20 | `CD2.EVT / 33 / 5` |
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

1. **Quest and NPC state: 13 and 33, then 41/42.** Opcode 13 is the most
   frequent missing handler; 40 of its records share events with quest-bit
   operations, and 9 share NPC mutations. Start with `CD1 / 59 / 2` and
   `OUTE3 / 226 / 7`. Opcode 33 appears in `GLOBAL / 20 / 6`, alongside quest
   and NPC state, and in `OUTE3 / 240 / 1`. Inspect its effect on subsequent
   operations. GLOBAL alone contains 37 opcode-41 and 41 opcode-42 records;
   use `GLOBAL / 426 / 0,2` as a compact paired case. Exact semantics and
   player-visible consequences remain `unknown` at this audit boundary.
2. **Doors and traversal: 23, 34 and 43, with 10.** Opcode 23 shares 14 records
   with door events and 4 with travel events (`CD2 / 33 / 5,6`). Opcode 34 has
   30 full-size records in `D18`, beginning at event 56. Inspect whether its
   tentative coordinate reading is correct before treating it as party travel.
   Opcode 43 has one seven-byte use, `T7 / 1 / 1`, in a door event. The two
   two-byte uses of opcode 10 are `OUTC1 / 211 / 2` (quest) and
   `OUTD3 / 203 / 0` (travel); low frequency does not make them harmless.
3. **Remaining world and interaction effects: 12, 22, 3, 8 and 24.** All 12
   fifteen-byte opcode-12 uses share quest-bit events. Thirteen opcode-22
   records share quest-bit events. Opcode 3 and 8 occur together in
   `D17 / 29`, a door event. Resolve state-changing effects before assuming
   these are presentation-only gaps.
4. **Default-path policy and short records.** Keep the 415 original-default
   records accounted for, without inventing handlers for authoring leftovers.
   Establish which short records are reachable and which are unused before
   setting a reachable-script acceptance gate.

The immediate implementation slice should therefore investigate **opcode 13**,
using its quest/NPC examples to pin behavior and provide a first regression.
The audit portion of FC-2 is complete; opcode completeness is still open.

## Short records and limits of the census

All 263 below-guard records are concentrated in these files (`observed`):

| Scripts | Short records |
| --- | ---: |
| `DBM1.EVT` through `DBM5.EVT` | 38 each (190 total) |
| `DDB1.EVT` | 31 |
| `OUT.EVT` | 23 |
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

The hermetic suite now contains 80 test executables. New coverage tests exercise
all 256 opcode classifications against the actual walker, repeated records and
scoped events, mixed-case extensions, deterministic output, argument guards,
bad containers/records/payloads, duplicate names, no-script input, and TSV
escaping without raw argument output. The local census reads the original
installation without executing events or writing saves.
