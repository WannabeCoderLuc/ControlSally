# ControlSally

Pilot the real Sahelanthropus (ST-84) in *Metal Gear Solid V: The Phantom Pain* free roam.

ControlSally spawns Sahelanthropus in Afghanistan or Africa free roam and lets you drive it: camera-relative
movement, jumps on command, and its own weapons (sword, gatling, flamethrower, missiles, grenades, stomp). It uses the
game's own animation, sound and weapon code; nothing is faked with a different model.

> **Status: early, experimental.** Steering, jumping, weapons and the opening skip work. Weapons do not damage soldiers
> yet (vehicles and objects break). See [Known issues](#known-issues).

## Features
- Spawn Sahelanthropus in Afghanistan (30010) or Africa (30020) free roam, without the Episode 31 arena boundary.
- Direct, camera-relative steering; hold position; hand control back to the AI and take it again.
- Pilot-triggered 40 m jumps (the AI's own combat jumps are suppressed).
- 7 weapon slots mapped to keys, aimed where the camera looks.
- The mech does not pick Snake as its target (target diversion).
- Opening sequence skipped; combat phase locked.
- Eye lamp colours: presets and a rainbow mode.
- In-game **ControlSally menu** (Infinite Heaven / IHHook overlay): spawn, despawn, teleport to the mech, summon it,
  send it to a map marker, pick its spawn point, change settings live. See [docs/MENU.md](docs/MENU.md).

## Requirements
See [DEPENDENCIES.md](DEPENDENCIES.md) for exact versions and links.
- MGSV:TPP 1.0.15.4, Steam, English exe
- SnakeBite Mod Manager
- Infinite Heaven r267 and IHHook r25
- Python 3.10+ (for the pilot driver, standard library only)

## Install
See [docs/INSTALL.md](docs/INSTALL.md). In short:
1. Install SnakeBite, Infinite Heaven and IHHook.
2. Install `ControlSally.mgsv` with SnakeBite.
3. Copy `plugins\SahelanControl.dll` and `plugins\SahelanControl_gate.txt` into `MGS_TPP\plugins\`.
4. In free roam open the IH menu > **ControlSally**: run *Set up boss events* once, reload, then *Spawn*.
5. Run `python driver\sally_pilot.py` to steer.

## Controls
See [docs/CONTROLS.md](docs/CONTROLS.md). Movement is on the numpad (8/2/4/6), weapons on numpad keys and PageUp,
Ctrl+Shift+End releases control.

## How it works
Full write-up (architecture, why it was hard, how each part was solved): [docs/HOW_IT_WORKS.md](docs/HOW_IT_WORKS.md).

- `SahelanControl.dll` is an IHHook plugin. It installs runtime hooks (MinHook) on the running game: the mech's
  decision table, goal/path requests, targeting and weapon commands. **No game file or executable on disk is changed.**
- The Infinite Heaven module `InfBossTppSahelan2.lua` adds Sahelanthropus as a free-roam boss event (spawn only on
  request), and `InfControlSally.lua` adds the ControlSally menu.
- `driver\sally_pilot.py` reads the keyboard and talks to the DLL through IHHook's named pipe.

## Known issues
- **Reload hang:** dying, reloading a checkpoint, changing mission or despawning while Sahelanthropus is loaded can
  freeze the next loading screen. Save first; quit the game instead of reloading.
- Weapons don't damage soldiers yet: the game treats the mech as the soldiers' ally (its hits are dropped as
  friendly fire). Vehicles and props break. Being worked on.
- At close range the flamethrower can still turn toward Snake.
- Direct steering ignores the navmesh: the mech can walk into cliffs and buildings.
- Single player, offline only. Don't use it in FOB or online modes.

## Building from source
See [docs/BUILDING.md](docs/BUILDING.md).

## Credits
See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Thanks to TinManTex (Infinite Heaven, IHHook),
Yazed0071 (V_Framework), Tsuda Kageyu (MinHook) and the MGSV modding wiki contributors.

## License
MIT, see [LICENSE](LICENSE). Metal Gear Solid V is © Konami Digital Entertainment; this project contains no game
files.
