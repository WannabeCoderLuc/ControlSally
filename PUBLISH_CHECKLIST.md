# Publish checklist

Maintainer notes. Nothing here is automatic: each push or upload is a manual step.

## Before the first public push (GitHub)
- [ ] Pick the release DLL source: `python Tools\sync_controlsally.py --probe <probeN>` from the workspace
      (default probe10b). The sync ends with a leak scan; it must report 0 findings.
- [ ] `build.ps1` builds cleanly on this machine.
- [ ] Review `src/SahelanControl/dllmain.cpp` for research-only code you don't want public (trace/census loggers,
      big binary dumps). The release should be a cut-down build without the probe history.
- [ ] `lua/mod/modules/InfBossTppSahelan2.lua` header still says "test build 1 (local build, not for distribution)":
      update it in the workspace source (`Tools\sahelan_unbound_lua`), then resync.
- [ ] Driver banner still says "probe10b"; give it the release name/version.
- [ ] `.mgsv` must include `mod/modules/InfControlSally.lua` (menu) next to `InfBossTppSahelan2.lua`; drop
      `InfProbe9aDamageLog.lua` from the package build.
- [ ] Check `lua/mod/modules/InfBossTppSahelan2.lua`: confirm none of it is copied from Infinite Heaven's own boss
      modules. If any is, credit TinManTex/caplag and check IH's terms.
- [ ] Decide which research docs to publish (`docs/` only has user docs now). No raw decompiles or game dumps.
- [ ] Update the project rule "everything stays local and unpublished" in the workspace `CLAUDE.md`.

## Nexus Mods release
- [ ] Spawn package: `ControlSally.mgsv` contains data copied from the game (Episode 31 locator, Sahelanthropus pack,
      navmesh). It is common for MGSV mods to ship game-derived packs, but read the Nexus terms for this game and
      decide. It must never go into the git repo (`.gitignore` blocks `*.mgsv`).
- [ ] `build.ps1 -Package`, add `ControlSally.mgsv`, zip `release\` as `ControlSally-<version>.zip`.
- [ ] Nexus page: `nexus/description.bbcode`. Set the requirements tab: SnakeBite, Infinite Heaven, IHHook
      (V_Framework optional).
- [ ] Version scheme: `0.1.0` for the first public build. Show it in `SahelanControl.GetInfo()`.
- [ ] Screenshots / video: the mech walking, firing, rainbow eyes.

## Known blockers for 1.0
- **Reload hang:** `TppSahelan2_block` never empties (HOW_IT_WORKS section 9). Blocks Despawn/Respawn and any reload.
- Soldier damage (same-army check, probe10e).
- Flamethrower close-range aim toward Snake.
- Optional: buddy-menu control (research in progress).
