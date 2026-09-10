---
title: Campaign completion
summary: How StarHaven records quest resolutions and measures its frozen journal manifest across saves.
doc_type: explanation
status: partial
last_updated: 2026-09-10
source_files:
  - src/game/completion.cpp
  - src/game/script_walk.hpp
  - src/game/save.cpp
  - tests/test_completion.cpp
  - tools/completion_census.cpp
tags:
  - quests
  - completion
  - saves
  - evidence
---
# Campaign completion

StarHaven measures a manifest of 52 journal quests, 58 script-granted awards
and 93 chronicle notes. Its percentage is an engine-defined journal measure,
not proof of a complete MM6 playthrough or an original-game percentage.

## Active assignments and resolutions

Quest bits describe active journal entries. The opening assignment sets bit 81;
the first delivery clears 81 and sets 82. Counting set bits therefore counts
assignments and loses progress when those assignments finish. This transition
is `observed` by `evt_info --arc`.

Every one of the 52 journal rows has a quest-bit clear operation in the
shipped scripts (`observed` by `completion_census`). StarHaven records a
resolution when execution actually reaches that operation. It keeps the
resolved bit in a separate set, even when a later assignment changes the active
journal. Reaching the clear counts even if the party obtained the objective
before accepting the assignment. Repeating it counts only once.

Treating these executed clears as quest resolutions is `inferred` engine
policy. A static census alone does not establish reachability, reward delivery,
or full campaign compatibility. Unsupported opcodes still need implementation;
the runtime reports their script, event, sequence and opcode to standard error.

## What the percentage includes

The auditor counts resolved quest IDs, held award IDs and collected chronicle
IDs against the frozen manifest. Active quest bits are never supplied as
completed quests. The total is the sum of the three categories, with the
percentage rounded down. Non-manifest IDs do not contribute.

Promotions, arena services, bounty lifecycles, traversal and presentation are
not independent categories in this manifest. A 100% journal value does not
certify these systems or replace the planned campaign acceptance run.

## Persistence and older saves

Version-2 StarHaven saves record resolution history and disabled events,
namespaced by script. An event disabled in one map does not disable the same
number in another map or in `GLOBAL.EVT`.

Version 3 additionally preserves per-map decoration changes from
[opcode 13](../formats/map-events.md#opcode-13-changes-a-placed-decoration).
Version-1 and version-2 saves remain readable, with no decoration overrides.

Version 4 additionally preserves script reward instances waiting for pack
space, the opcode-41 generator seed, and its artifact-found flags.
The next reward therefore continues the saved random sequence. Versions 1–3
remain readable with an empty waiting list and the default script seed; they
cannot recover rewards skipped by older engines. The required `scriptitems`
record and each `reward` record are validated before a load changes state.

Version 5 additionally records the current event byte of a decoration changed
by [opcode 42](../formats/map-events.md#opcode-42-changes-the-current-decorations-event),
alongside its descriptor and visibility. Versions 1–4 remain readable.
The map namespace and placement index keep one used object from changing another.

Version 6 additionally preserves indoor face attributes and textures after
scripted changes. Collision and per-face texture animation are restored with the
map; versions 1–5 remain readable without inferring changes absent from the save.

[Opcode 33](../formats/map-events.md#opcode-33-displays-a-message-and-suspends-the-event)
pauses an event for modal text. Save/load shortcuts are held until dismissal;
the continuation is transient. Rewards and journal changes after the message
are applied only when the event resumes, then use the existing save fields.

Complete version-1 saves remain readable. They did not record these fields,
so their histories start empty. Earlier quest resolutions cannot safely be
reconstructed from active bits: an absent bit could mean either finished or
never accepted. Consequently, the quest count after loading such a save covers
only resolutions recorded from then onward. No historical completion is
invented.

The save reader rejects malformed numeric cells, non-finite coordinates,
missing required world or character records and invalid collection counts.
Version 2 also requires its final `end` record, detecting interrupted writes.
Failed reads leave the caller's state unchanged and are shown as corrupt slots.

## Reproduction

With the user's own installation configured:

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/completion_census "$STARHAVEN_GAME_DIR/data"
./buildDir/evt_info --arc
meson test -C buildDir completion script_walk save save_repository --print-errorlogs
```

The unit tests use synthetic scripts to cover assignment, resolution, repeated
rewards, namespace isolation and save/reload. The install-backed arc checks the
opening bit transition against the shipped script.
