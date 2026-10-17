# The ControlSally menu

ControlSally adds its own page to the Infinite Heaven menu. Open it the usual IH way: in game or in the IHHook overlay
(the IHHook window mirrors the same menu), then go to **ControlSally**. It is in the in-mission menu (free roam) and
the safe-space menu (ACC; there only the settings matter).

## First time only
1. **Set up boss events (once)**: turns on IH free-roam boss events, the Sahelanthropus boss type and manual-only
   spawning (it never appears on its own).
2. Reload the mission once (restart free roam). IH only creates the boss-event save data when a mission starts.

## Commands
| Item | What it does |
|---|---|
| Spawn Sahelanthropus | Starts the boss event: the pack loads and the mech appears (Afghanistan: Episode 31 battlefield, Africa: Nova Braga Airport). Settings and the spawn point are applied a moment later. |
| Despawn Sahelanthropus | Ends the boss event and removes the mech. See the warning below. |
| Teleport me to Sahelanthropus | Puts Snake 8 m from the mech, facing it. |
| Summon Sahelanthropus to me (jump) | One native jump to about 35 m in front of you. Experimental. |
| Send Sahelanthropus to map marker (jump) | One native jump to your latest map marker. Experimental. |
| Release Sahelanthropus to its AI | Gives control back to its own AI. The pilot driver takes it again when you steer. |
| Teleport me to its spawn point | Takes Snake to the landing zone next to its locator. |
| Apply settings now | Pushes the settings below to the DLL (they also apply on spawn and when changed). |
| Show status | Shows where the mech is and which DLL is loaded. |

## Settings (saved by IH)
| Setting | Values | Default |
|---|---|---|
| Spawn point | DEFAULT (its locator), NEAR_ME, MAP_MARKER | DEFAULT |
| Never target Snake | on / off | on |
| Lock combat phase | on / off | on |
| Eye colour | vanilla, blue, red, white, purple, amber, green, disco | blue |
| Soldier damage side (experimental) | VANILLA, FRIEND, BOTH | VANILLA |

The Sahelanthropus boss menu (IH > Boss event menu > Sahelanthropus menu) keeps the boss settings: difficulty, health
%, map marker and on-screen messages.

## Steering
The menu doesn't steer. After **Spawn**, start the pilot driver (`python sally_pilot.py`) and use the keys in
[CONTROLS.md](CONTROLS.md). You can stop and restart the driver at any time; the mech stays.

## Warning: reload hang
Until the block-unload bug is fixed (see [HOW_IT_WORKS.md](HOW_IT_WORKS.md) section 9), **dying, reloading a
checkpoint, changing mission or using Despawn while Sahelanthropus is loaded can freeze the next loading screen.**
Save before you play. When you are done, quit the game instead of reloading.

## How the menu works
`lua/mod/modules/InfControlSally.lua` is an ordinary IH external module: IH loads every file in `mod\modules`. It
registers a menu (`controlSallyMenu`), five saved settings (ivars named `sally_*`) and its commands. Commands call the
boss module (`InfBossTppSahelan2`) for spawning and the DLL's `SahelanControl` Lua library for everything inside the
mech. Each DLL call is guarded, so the menu loads even without the DLL. It hooks the boss module's `Appear` to apply
settings and the spawn point once the mech exists, using game timers that retry until the DLL has taken hold of the
mech's controller.
