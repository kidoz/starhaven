---
title: "Startup journey compatibility contract"
summary: "Evidence boundaries and reproducible traces for launch, opening media, title choices, party creation, loading, and entry into New Sorpigal."
doc_type: explanation
status: partial
last_updated: 2026-08-12
source_files:
  - src/game/startup_flow.cpp
  - src/game/startup_media.cpp
  - src/core/platform/game_install.cpp
  - src/main.cpp
  - tests/test_startup_flow.cpp
  - tests/test_startup_media.cpp
  - tests/test_game_install.cpp
tags:
  - mm6
  - startup
  - title-screen
  - new-game
  - compatibility
---
# Startup journey compatibility contract

This page freezes the evidence-backed journey from launching StarHaven to a
playable first visit to New Sorpigal. It deliberately separates MM6 behavior
from StarHaven implementation status. Claims about the original game are tagged
`observed`, `inferred`, or `unknown`; an unknown is not a compatibility
requirement.

No proprietary capture, decoded payload, executable byte sequence, or game file
is reproduced here. The artifact names and numeric state below are
non-expressive facts needed to reproduce the observations against a user's own
legal installation.

## Scope and evidence boundary

This contract covers:

- installation discovery before opening media;
- the two opening video entries and their order;
- skip behavior and the four title choices;
- the New and Load input journeys;
- the first map, position, facing, and loading-screen boundary;
- completion of Credits and recovery from an invalid load.

It does not specify MM6 save-file compatibility, exact transition timing,
loading progress arithmetic, or startup behavior for other Might and Magic
versions.

The opening archive was not configured in the verification environment on
2026-08-12, so this page consolidates the repository's earlier authorized
observations rather than claiming a new media run. The canonical supporting
references are the [VID container](../formats/vid.md),
[Smacker stream](../formats/smacker.md),
[interface panels](../formats/interface-panels.md), and
[party runtime record](../formats/party-record.md) pages.

## MM6 compatibility observations

| Journey fact | Evidence status | Compatibility statement |
| --- | --- | --- |
| Opening entries | observed | The user-owned `Anims1.vid` / `Anims2.vid` set contains entries named `3dologo` and `MM6Intro`. Entry names have no extension. |
| Opening order | observed | Normal startup presents `3dologo`, then `MM6Intro`, then the title screen. |
| Skip | observed | A keyboard input advances the active opening reel; the next input advances the next reel rather than skipping the whole sequence. Original mouse-button skip behavior is `unknown`. |
| Title choices | observed | The title art presents New, Load, Credits, and Exit. |
| New destination | observed | New creates the party for `oute3.odm`, New Sorpigal. |
| New position | observed | MM6 coordinates are x = -9728, y = -11319, z = 160, facing 512 of 2048, with level pitch. The render-space axis conversion is documented in the [party runtime record](../formats/party-record.md#where-a-new-game-begins). |
| Loading pictures | observed | The loading-screen object names `loading.pcx`, four travel/game-over variants, a `fireball` sprite, and `bardata`; it has two modes and a progress rectangle. |
| Loading mode selection | unknown | The evidence does not yet tie each New, Load, or map-transition path to one of the two modes, nor establish exact progress updates. |
| Load selection | unknown | The original game's slot-list layout, default slot, corrupt-slot response, and cancel path are not established by this trace. |
| Credits completion | unknown | Returning to Title after the credits reel is StarHaven's explicit recovery rule; this note does not claim the original return target has been independently observed. |

## Reproducible input traces

Build once, then point the commands at a legal installation. Archive inspection
is read-only, and no extracted output is needed:

```bash
meson setup buildDir
meson compile -C buildDir
STARHAVEN_GAME_DIR=/path/to/mm6 ./buildDir/play_smk --list
./buildDir/starhaven --game-dir /path/to/mm6
```

Confirm that the list contains `3dologo`, `MM6Intro`, and `credits`. Do not add
the listing or captured frames to the repository.

### New path

| Step | Input | Expected observation |
| --- | --- | --- |
| 1 | Launch with no map or debug option. | The 3DO logo is active; the world does not accept movement. |
| 2 | Press one ordinary key. | Only `3dologo` ends and `MM6Intro` becomes active. |
| 3 | Press one ordinary key. | Only `MM6Intro` ends and the title with four choices appears. |
| 4 | Press `N`, or click New. | Party creation appears. Escape returns to Title. |
| 5 | Choose the four party records and press Enter. | The flow commits the new party and enters the world. |
| 6 | Move and turn. | The active map is `OutE3.Odm`; the initial body coordinates and facing match the observed values above after axis conversion. |

Repeating steps 1–3 without pressing a key confirms natural reel completion in
the same order. Either run is sufficient for the state ordering; the second
separately checks decoder completion.

### Load path

Prepare a valid StarHaven slot by reaching the world and pressing F5, then
restart:

| Step | Input | Expected StarHaven observation |
| --- | --- | --- |
| 1 | Launch and finish or skip each opening reel. | Title appears. |
| 2 | Press `L`, or click Load. | The current slot is validated before the startup flow can become Playing. |
| 3 | Use a valid slot. | Map, party, position, and recorded world memory replace the prepared session atomically enough for the player-visible flow, then movement becomes active. |
| 4 | Repeat with an empty or corrupt current slot. | The flow returns to Title and does not expose a fresh party as a successful load. |

This is a trace of StarHaven's present single-slot title action, not a claim that
the target save-selection UI is complete.

## StarHaven behavior and remaining gaps

| Area | Behavior on 2026-08-12 | Compatibility consequence |
| --- | --- | --- |
| Resource discovery | `--game-dir`, a persisted per-user setting, then `STARHAVEN_GAME_DIR` are validated without writing to the install. A 640x480 recovery window can select another folder. | Player-facing boot recovery is implemented; opening videos remain optional later in the flow. |
| Opening and title | `StartupFlow` owns logo, intro, title, credits, and skip transitions. `StartupMediaController` owns one decoder at a time and reports finished, skipped, unavailable, or failed reels. `--no-movies` reaches Title without changing game state. | Opening playback completes before map construction; the order, one-reel-at-a-time skip contract, and failure cleanup are testable without SDL or proprietary data. |
| New | Title New enters the existing party-creation screen; Enter activates the preloaded New Sorpigal session. | Destination and spawn match the observed record, but construction is not yet transactional. |
| Load | Title Load immediately asks the current StarHaven slot to load. Invalid completion returns to Title. | A visible save-slot selector and per-slot validation are still missing. |
| Loading | The map is prepared before title interaction, and no dedicated loading presentation owns the replacement. | This does not yet satisfy the target lifecycle even though the first location is playable. |
| Credits | The `credits` reel returns to Title on finish, skip, unavailable media, or Back. | This is a deliberate recoverable engine rule while the original return behavior remains unknown. |

## Testable requirements

The evidence above establishes only these startup compatibility requirements:

1. Preserve `3dologo` -> `MM6Intro` -> Title order when both entries are
   available.
2. Advance at most one active reel for one skip input.
3. Expose New, Load, Credits, and Exit at Title.
4. Put a new party in `OutE3.Odm` at the observed coordinates and facing.
5. Treat missing media and invalid saves as recoverable startup results.
6. Do not infer loading mode, slot UI behavior, mouse skip, or original credits
   return behavior until direct evidence resolves them.

The SDL-free transition contract is covered by `tests/test_startup_flow.cpp`;
media lifecycle behavior by `tests/test_startup_media.cpp`; and installation
precedence and failure diagnostics by `tests/test_game_install.cpp`. All three
suites use synthetic state, media structures, and filesystem fixtures only.
