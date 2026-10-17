# Building

## DLL (`SahelanControl.dll`)
Needs Visual Studio 2022 with "Desktop development with C++" (MSVC v143) and a Windows 10/11 SDK.

```
powershell -ExecutionPolicy Bypass -File build.ps1
```
Output: `src\SahelanControl\bin\Release\SahelanControl.dll`. Options: `-Sdk 10.0.22621.0` to pick another SDK
version, `-Package` to also assemble `release\` (DLL, default config, driver, docs).

Or open `src\SahelanControl\SahelanControl.vcxproj` in Visual Studio and build **Release | x64**.

## Spawn package (`ControlSally.mgsv`)
`tools\build_sahelan_unbound.py` builds the package from your own extracted game files. It needs the maintainer's
ATOMIZER library (not public yet). Set `SALLY_GAME_DATA` to the folder holding your extracted 1.0.15.4 game files.

## Layout
| Path | What |
|---|---|
| `src/SahelanControl/` | DLL source (single `dllmain.cpp`) + bundled MinHook |
| `dist/plugins/SahelanControl_gate.txt` | default DLL config (feature switches) |
| `driver/` | keyboard pilot driver + IHHook pipe client |
| `lua/mod/` | Infinite Heaven modules: `InfBossTppSahelan2.lua` (boss-event type + sub types in `bosses/`) and `InfControlSally.lua` (ControlSally menu) |
| `tools/` | package builder |

## Notes for contributors
- Runtime hooks only. Never patch `mgsvtpp.exe` on disk.
- All addresses are for 1.0.15.4 EN. Each hook checks its target bytes before installing.
