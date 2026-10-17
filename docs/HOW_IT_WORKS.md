# How ControlSally works

This document explains the whole mod: what each part does, why piloting Sahelanthropus was hard enough that it had not
been done before, and how each problem was solved. Addresses are for MGSV:TPP 1.0.15.4 (Steam, English exe).
Status tags: **done** = works in game, **in progress** = researched but not finished.

## 1. The pieces

```
 IH menu (ControlSally)            pilot driver (Python)
   InfControlSally.lua               sally_pilot.py  ── keyboard ──┐
        │  Lua calls                     │  IHHook named pipe       │
        ▼                                ▼                          │
 SahelanControl Lua library  ◄──── registered by SahelanControl.dll (IHHook plugin, MinHook)
        │                                │  runtime hooks in mgsvtpp.exe
        ▼                                ▼
 InfBossTppSahelan2.lua ──► Infinite Heaven boss event ──► loads ControlSally.mgsv pack ──► Sahelanthropus
```

| Part | Job |
|---|---|
| `ControlSally.mgsv` (spawn package) | A script block with the Sahelanthropus GameObject, its parameters, motions, sound and a locator, built from Episode 31's data. |
| `InfBossTppSahelan2.lua` | Adds Sahelanthropus to free roam: loads its packs with the mission and makes it appear (built on IH's boss-event module). |
| `InfControlSally.lua` | The in-game ControlSally menu: spawn, teleport, summon, settings. |
| `SahelanControl.dll` | Loaded by IHHook. Hooks the mech's AI, movement, weapons and targeting in memory, and registers the `SahelanControl` Lua library. |
| `sally_pilot.py` | Reads the keyboard and sends goals, jumps and fire commands to the DLL through IHHook's named pipe. |

Nothing on disk is patched: the game executable is protected by Denuvo, so every change is a hook installed at runtime
by the DLL.

## 2. Why this was hard

1. **The mech only exists inside three story missions.** Sahelanthropus is a one-off boss (Episode 12 as the
   "Hellbound" version, Episodes 31 and 50 as "Dominion"). Its GameObject, locator, motions, sounds and a private
   navmesh live in those missions' packs. Free roam has none of it, and Infinite Heaven's boss events (Skulls, Quiet,
   Man on Fire) don't include it.
2. **It can't be teleported.** `TppSahelan2` has no `Warp` command. It always appears at its locator.
3. **It lives in its own navigation world.** Episode 31 gives it a separate `sahelan` nav world (the arena). Outside
   that arena it has no ground to path on, which is the invisible Episode 31 boundary.
4. **Its AI is a scripted boss, not a soldier.** It runs a decision state machine (opening, idle, combat, tower
   break...) with an intro that waits for pylons. There is no "follow" or "go there" command.
5. **It is hard-wired to fight Snake.** Every target evaluation re-seeds the target from the local player. There is no
   faction or hostility logic to change: "hostile" means "the player" by construction.
6. **Almost nothing is named.** The exe has no symbols. Fox Engine identifies classes, properties, events and
   commands by 64- and 32-bit string hashes (StrCode64/32), so most of what matters starts as an unnamed number.
7. **Denuvo.** The executable can't be modified, so every change has to be a detour at runtime. That needs exact
   function boundaries and calling conventions, plus coexistence with other hook mods (V_Framework hooks some of the
   same functions).
8. **Engine traps.** Fox Lua numbers are 32-bit floats, so pointers can't round-trip through Lua. IHHook's pipe takes
   512-byte messages. Loaded the way IH loads bosses (a streamed script block), Sahelanthropus never unloads and
   freezes the next loading screen (section 9).

## 3. How it was researched
- **Static analysis** of a read-only copy of the exe: headless Ghidra with a project-wide symbol map, plus a fast
  cross-reference index (calls, data refs, vtables, function bounds) built straight from the PE file.
- **Name recovery:** a StrCode64/32 brute-forcer and token harvesting turned thousands of hashes back into names
  (classes, AI nodes, commands, messages).
- **Game data:** every `.fox2` entity and property of the game in one database, and a byte-exact `.nav2` navmesh
  reader/writer.
- **Probes:** each experiment is a DLL generated from the previous one by a patch script, with logging first and
  behaviour changes second. Every address was confirmed in game logs before the next step was built on it.
- **Unattended test runs:** a controller boots the game into free roam, sends keys, takes screenshots and runs Lua
  over the pipe, so most runs needed no human.

## 4. Getting it into free roam (done)
- The spawn package copies Episode 31's Sahelanthropus GameObject, `TppSahelan2Parameter`, locator, motion graph and
  `se_e_mgs` sound into one script block (`TppSahelan2_block`), packaged as an IH boss pack.
- `InfBossTppSahelan2.lua` registers the type with IH's boss-event system (it wraps `InfBossEvent.GetBossModules` and
  `AddMissionPacks`, because IH keeps its boss list in a local table), and runs IH's "spawn now" path on request only.
- Africa uses a "blank slate" pack: the same GameObject with its locator moved to Nova Braga Airport and none of
  Episode 31's arena data.
- Because there is no Warp, where it appears is decided by the locator in the package. The menu's "near me" / "map
  marker" spawn points let it appear at the locator and then make one native jump to the target (experimental).

## 5. Removing the boundary (done)
The mech's navigation system takes its nav world from a pointer that names the `sahelan` world
(`0x1418BAE48`). The DLL points it at the default map world before the mech is created (`navworld=1`). The mech then
paths on the normal free-roam navmesh everywhere.

## 6. Driving it

**Opening skip (done).** The decision state machine keeps its state in a blackboard byte (`bb[5]`, states 0–5).
State 0 is the passive intro. The decision-0 handler (`0x1418FE270`) is hooked to move straight to state 2 (combat
movement). The "break a pylon" override (decision 2 → 5) is disabled, and the scripted tower intro (type 18) never
starts. (A side note from testing: holding state 0 to "calm" the AI stops the walk controller completely.)

**Steering (done).** The AI moves the mech by building a 0xB0-byte move request (type byte at `+0xAA`) from a goal
stored in its AI data (`+0x260`) and submitting it through the action controller. The DLL:
- holds the AI's own decisions with a "gate" (bit mask over decision kinds), so the AI stops issuing its own goals;
- writes the pilot's goal and forces the path type to a direct path, so the mech walks straight at the goal instead
  of following navmesh routes;
- issues the move request right away for every new goal, which removes the delay.
The driver turns numpad keys into a goal about 24 m ahead along the camera direction, refreshed several times a
second. Direct paths ignore the navmesh on purpose, so the mech can walk into cliffs.

**Jumps (done).** The AI's own type-3 jump builder (`0x1418F2230`) lands within about 1.5 m of its target point. Combat
jumps are blocked by the gate; the pilot pushes a type-3 jump command with its own landing point.

**Weapons (done).** Weapons are actions pushed onto the mech's action controller (`PushCommand`). Action types decoded:
12 Vulcan (gatling), 13 stomp, 14 sword, 15 missiles, 16 flamethrower, 19 grenade launcher (built by
`0x1418E4C80`). The DLL pushes them with an aim point from the camera and keeps "hold" weapons alive while the key is
down.

**Phase lock (done).** The mech's phase AI (`PreUpdate 0x1418FF8E0`) is held in combat phase, so it never switches to
sneak, search or retreat behaviour.

**Eyes (done).** The eye-lamp colour update (`0x1418C51B0`) is hooked to apply presets or a rainbow cycle. V_Framework
hooks the same function, so ControlSally chains after V_Framework's jump instead of fighting it.

## 7. Not killing Snake
- **Target diversion (done).** `0x1418FEDB0` re-seeds the mech's target from the local player before every
  evaluation (59 times a second). The DLL hooks it and writes a decoy point instead (the pilot's aim point, or
  underground), so the combat AI never locks onto Snake.
- **In progress:** at close range the flamethrower still turns toward Snake through another aim path.

## 8. Damaging soldiers (in progress)
- Attacks are TppTarget **offense** shapes (sword, stomp, flame, jump impact) or bullets (gatling). The engine applies
  damage on contact; the Sahelanthropus code never processes hits itself.
- Shapes carry target-group bits from a registry of six groups: FRIEND, ENEMY, ENEMY2, ENEMY3, OBJECT, HEAVYPHYSICS.
  Snake defends as FRIEND, soldiers as ENEMY. The mech's offense setups mark it as ENEMY side, like the soldiers
  (D-Dog's attacks are marked FRIEND).
- Test results: the gatling's bullets do hit soldiers and the hit is accepted, vehicles and props break (the mech is
  credited as the attacker), but soldiers take no damage. A soldier-only "same army" check in the bullet code
  (`0x140D2DCAD`) drops hits between members of the same army. The fix being tested bypasses it for the mech's bullets
  only, and changes its offense side to the player's.

## 9. The reload hang and the mission-pack fix (done)
**Problem.** IH loads boss types in a script block that it streams in and out during play. Once the Sahelanthropus
block (`TppSahelan2_block`) had been loaded, unloading it never reached the EMPTIED state. Every death, checkpoint
reload, mission change or despawn then left the next loading screen waiting forever.

**Finding the cause.** A small script loads the block over the pipe with a chosen pack list, unloads it and polls
its state. The vanilla Sahelanthropus parts pack empties normally, and so does IH's boss-gauge pack. The game-object
pack (GameObject, parameters, motions, sound) is the part that never empties. Vanilla never does this either:
Episode 12 swaps whole mission area packs with a full load, and Episodes 31 and 50 load the mech with the mission.
Infinite Heaven itself keeps zombie soldiers' packs in the mission packs for a similar reason.

**Fix.** ControlSally's default load mode (`MISSION_PACK`) adds the Sahelanthropus packs to free roam's mission pack
list, so the mech loads and unloads with the mission, like in Episode 31. It waits parked at its spawn point until
you spawn it. "Spawn" wakes it (stage, health, settings) and "Despawn" stops it. Nothing is streamed mid-mission any
more. Verified: death, then Continue from checkpoint, reloads normally, and Respawn works. The old block mode is still
selectable (`SCRIPT_BLOCK`) for testing.

## 10. Talking to the DLL
- The DLL hooks the game's Lua library setup (`SetLuaFunctions`) to register a `SahelanControl` library, so both IH
  menu code and the driver can call it (`SahelanControl.SetGate`, `PilotJumpNow`, `SetEyePreset`, ...).
- The driver uses IHHook's named pipes (`mgsv_in` / `mgsv_out`). `ih_pipe_client.py` splits long Lua into chunks
  (512-byte limit, no `|` in a message) and matches replies by tag.
- Fox Lua numbers are 32-bit floats: anything that must stay exact (addresses, 64-bit hashes) goes through strings.

## 11. Credits
Infinite Heaven and IHHook (TinManTex, IH fork by caplag) make all of this loadable. V_Framework (Yazed0071) showed
several engine layouts (the bullet same-army check, buddy command tables, address sets). MinHook (Tsuda Kageyu) does
the hooking. The MGSV modding wiki documents the file formats.
