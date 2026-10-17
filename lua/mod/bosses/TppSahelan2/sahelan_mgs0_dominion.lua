-- Sahelanthropus Unbound: boss sub-type addon for InfBossTppSahelan2 (read by its LoadInfos).
-- Dominion AI (Episode 31 / Episode 50 behaviour) at the Episode 31 battlefield, Afghanistan.
local this={
  type="TppSahelan2",
  name="sahelan_mgs0_dominion",
  description="Sahelanthropus (Dominion)",
  missions={[30010]=true},--Afghanistan free roam (Episode 31 locator, navmesh, routes)
  packages={
    -- vanilla parts, model, hitboxes, effects and textures (shared by Episodes 12, 31 and 50)
    "/Assets/tpp/pack/mission2/common/mis_com_sahelan.fpk",
    -- built by Tools/build_sahelan_unbound.py from Episode 31 data:
    -- GameObject + TppSahelan2Parameter, locator, motion graph/mtar, Sahelanthropus navmesh,
    -- Episode 31 routes, REX tower/snipe points, search-missile point, se_e_mgs sound package
    "/Assets/tpp/pack/boss/ih/TppSahelan2/TppSahelan2_gameobject.fpk",
  },
  -- locator name in TppSahelan2_locator.fox2 (taken from s10151_npc_sp.fox2)
  objectNames={
    "Sahelanthropus",
  },
  eventParams={
    spawnRadius=0,-- unused: TppSahelan2 has no Warp command, it starts at its locator
    faction="XOF",
    weather={"CLOUDY","FOG"},
  },
}
return this
