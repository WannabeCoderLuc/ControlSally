# Install

Game: MGSV:TPP **1.0.15.4, Steam, English exe**. Offline single player only. Back up your save first
(`Steam\userdata\<id>\287700\`).

1. **Prerequisites** (see [DEPENDENCIES.md](../DEPENDENCIES.md)):
   - SnakeBite 0.9.2.5
   - Infinite Heaven r267 (install with SnakeBite)
   - IHHook r25 (SnakeBite build, plugins-folder version)
   - Python 3.10+ (tick "Add to PATH" in the installer)
2. **Spawn package:** in SnakeBite, install `ControlSally.mgsv` from the release.
3. **Plugin:** copy `plugins\SahelanControl.dll` and `plugins\SahelanControl_gate.txt` from the release into
   `...\steamapps\common\MGS_TPP\plugins\`.
4. **Driver:** unzip `driver\` anywhere (it holds `sally_pilot.py` and `ih_pipe_client.py`).

## Play
1. Start the game and load into **Afghanistan or Africa free roam**.
2. First time only: IH menu > **ControlSally** > *Set up boss events (once)*, then restart free roam.
3. IH menu > **ControlSally** > *Spawn Sahelanthropus* (see [MENU.md](MENU.md) for spawn point and settings).
   Skip this step if you like: the driver spawns it too.
4. Open a command prompt in the `driver` folder and run:
   ```
   python sally_pilot.py
   ```
   It prints `PILOT CONTROLS READY` when the mech is under control.
5. Switch back to the game. The keys only work while the game window is in front. See [CONTROLS.md](CONTROLS.md).
6. Stop with Ctrl+C in the driver window or Ctrl+Shift+End in game. Control goes back to the AI.

## Uninstall
- Delete `plugins\SahelanControl.dll`, `plugins\SahelanControl_gate.txt` and `plugins\SahelanControl*.log`.
- Uninstall `ControlSally` in SnakeBite.

## Troubleshooting
| Symptom | Fix |
|---|---|
| `no reply from game over the pipe` | Game not in free roam yet, paused, or IHHook not installed. |
| Mech stands still and the driver prints `waiting for mech AI root` | Wait a few seconds after it spawns. If it persists, check `plugins\SahelanControl.log` for `INTRO-SKIP`. |
| `SahelanControl info` doesn't show the expected version | An older or other `SahelanControl.dll` is installed. Replace it. |
| Game crashes at start | Wrong game version or language. Remove the DLL. |
| Loading screen never ends after dying or reloading | IH > Boss event menu > Sahelanthropus menu > *load mode* must be MISSION_PACK (default). SCRIPT_BLOCK has this bug. |
| No ControlSally page in the IH menu | `InfControlSally.lua` missing from `mod\modules` (reinstall the `.mgsv`). |
