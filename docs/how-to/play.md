---
title: Play StarHaven
summary: Launch the engine, recover a missing installation, reach the title screen, shape a party, and save through the player journey.
doc_type: how-to
status: verified
last_updated: 2026-08-29
tags:
  - starhaven
  - player
  - launch
  - saves
  - mm6
---
# Play StarHaven

This guide walks the journey from launching the engine to standing in New
Sorpigal with a saved party, and explains what each recovery message means.
Everything here describes the engine's own behavior; the game's art, words and
world still belong to their rights holders.

## Prerequisites

- A built engine: `meson setup buildDir && meson compile -C buildDir` (the
  README's Build section has the details).
- Your own legally obtained MM6 installation, with `MM6.exe` and its `Data/`
  directory.

## Launch and first-run installation

```bash
./buildDir/starhaven
```

With no arguments the engine opens on its own. The first launch asks for your
game folder in a 640x480 selection window; the folder is validated read-only
and the choice is remembered in the platform user-data directory, so later
launches skip the question.

If the folder does not validate, the screen says which requirement is missing
and lets you choose again or quit. The engine never writes into the game
install.

Command-line and scripted runs can point at an installation explicitly:

```bash
./buildDir/starhaven --game-dir /path/to/your/MM6/install
export STARHAVEN_GAME_DIR=/path/to/your/MM6/install   # contains MM6.exe and Data/
```

## Opening movies

A normal launch plays the 3DO logo and then the intro, each once. Any key or
mouse click skips the reel on screen; one press per reel, and the next screen
is the same whether a reel played, was skipped, or was missing. A reel that is
unavailable or cannot be decoded shows a short warning ("is unavailable;
continuing.") and the journey continues.

`--no-movies` goes straight from launch to the title screen. It is a
development and accessibility option and changes nothing about saves or game
state.

## The title screen

Four stone plates hang on the sky at the painting's right edge, top to
bottom: **New**, **Load**, **Credits** and **Exit**.

- `←` `→` `↑` `↓` move the highlighted plate; `Enter` activates it. A gold
  border marks the focus.
- `N`, `L`, `C` choose New, Load and Credits directly; `Esc` exits the game.
- The mouse clicks a plate directly.

**Credits** plays the credits reel and returns to the title. **Exit** quits.

## Party creation

**New** opens the creation hall with four seats and a default dealt party.

- `1`–`4` choose a seat.
- `C` cycles the class, `F` the face, `N` deals a new name.
- `R` rerolls the seven attributes (the rolls are the engine's own).
- `Enter` begins the journey when every seat is complete. Until then a message
  under the controls explains what is missing, and the world does not open.
- `Esc` returns to the title and discards the draft.

Confirming hands the party to the loading boundary exactly as a saved game is
handed over.

## Save selection and loading

**Load** opens the nine save slots. Each row shows the saved map and day for a
valid save, or its status: `empty`, `corrupt`, `unsupported`, or
`missing map` with the file the save asks for.

- `↑` `↓` move the selection, `1`–`9` jump to a slot.
- `Enter` or the **Load** button opens the chosen slot; `Esc` or **Back**
  returns to the title.
- The mouse clicks rows and buttons directly.

The slot list refreshes every time the screen opens.

## The loading screen

Opening a world — new or loaded — shows the game's own `loading.pcx` with a
progress rectangle and the name of the step under way (the maps' memory, the
world, the party). The world appears only after every step succeeds; until
then nothing of it is rendered, simulated, or saved. Map music starts when the
world is live.

## Saving in the world

- `F5` writes the current slot.
- `F6` turns to the next slot and reports what it holds.
- `F9` loads the current slot: the same map, the party, the position and the
  world's memory as saved.

## Recovery messages

Every failure before play is recoverable; nothing silently starts a new party
or exposes a half-built world.

| Message | Meaning | Way out |
| --- | --- | --- |
| `Empty slot` | The slot holds no save. | Choose another slot or go Back. |
| `Not a StarHaven save` | The file is not this engine's save format. | Choose another slot or go Back. |
| `Unsupported save version N` | The save was written by another engine version. | Choose another slot or go Back. |
| `Save data is corrupt` | The file parsed but failed its checks. | Choose another slot or go Back. |
| `Map is not present in this installation` | The save names a map the install lacks; the row shows which. | Choose another slot or go Back. |
| `The map could not be opened. Choose another slot or Back.` | The save validated but its world refused to open. | The current session is untouched; try another slot. |
| `The world could not be opened. Press Enter to try again.` | A new game's world refused to open. | The draft is intact; `Enter` retries, `Esc` keeps it and returns to the title. |

## Verify the journey

The optional acceptance run drives a real installation through this whole
journey and prints non-expressive PASS/FAIL lines per step:

```bash
just smoke                                  # uses STARHAVEN_GAME_DIR
just smoke --game-dir /path/to/your/MM6/install
```

## See also

- [Startup journey](../explanation/startup-journey.md) — the compatibility
  evidence behind the journey, tagged `observed`, `inferred` or `unknown`.
- The repository README's Use section — developer flags and inspection tools.
