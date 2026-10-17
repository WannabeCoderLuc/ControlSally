# Dependencies

## To play
| Dependency | Version tested | Required | Where | Why |
|---|---|---|---|---|
| Metal Gear Solid V: The Phantom Pain | 1.0.15.4, Steam, English exe | yes | Steam | Hook addresses are for this exe only. Other versions or languages will not work and may crash. |
| SnakeBite Mod Manager | 0.9.2.5 | yes | https://www.nexusmods.com/metalgearsolidvtpp/mods/106 | Installs `ControlSally.mgsv` and Infinite Heaven. |
| Infinite Heaven | r267 | yes | https://www.nexusmods.com/metalgearsolidvtpp/mods/45 | Boss-event framework the spawn module plugs into; Lua side of the pipe. |
| IHHook | r25 | yes | https://www.nexusmods.com/metalgearsolidvtpp/mods/1226 | Loads `plugins\*.dll` (as `dinput8.dll`) and provides the named pipe the driver uses. |
| Python | 3.10 or newer, Windows | yes | https://www.python.org | Runs the pilot driver. Standard library only, nothing to `pip install`. |
| V_Framework | 1.6.0.1 | no | https://www.nexusmods.com/metalgearsolidvtpp/mods/2486 | Tested alongside. ControlSally chains its eye-lamp hook after V_Framework's when both are installed. |

## To build the DLL
| Dependency | Version | Notes |
|---|---|---|
| Visual Studio 2022 (MSVC v143, "Desktop development with C++") | 17.x | `src\SahelanControl\SahelanControl.vcxproj`, Release x64 |
| Windows 10/11 SDK | 10.0.26100.0 (any recent 10.0 SDK should do; pass `-Sdk`) | |
| MinHook | 1.3.x, bundled in `src\SahelanControl\minhook` | BSD-2-Clause, see THIRD_PARTY_NOTICES.md |

## To build the .mgsv package (maintainer)
`tools\build_sahelan_unbound.py` assembles the spawn package from **your own** extracted game files (Episode 31
locator, Sahelanthropus game-object pack). It currently needs the maintainer's ATOMIZER library (Fox `.fox2`/`.fpk`/
`.mgsv` read/write), which is not public yet. Players don't need it: use the released `.mgsv`.

## Runtime notes
- Offline single player only.
- The DLL writes logs next to itself in `MGS_TPP\plugins\` (`SahelanControl*.log`).
- Uninstall: delete `plugins\SahelanControl.dll` and `SahelanControl_gate.txt`, uninstall the `.mgsv` in SnakeBite.
