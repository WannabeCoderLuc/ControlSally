-- Sahelanthropus Unbound: boss sub-type addon for InfBossTppSahelan2 (read by its LoadInfos).
-- Dominion AI (Episode 31 / Episode 50 behaviour) in Africa free roam, starting at Nova Braga Airport (t2a).
local this={
  type="TppSahelan2",
  name="sahelan_mgs0_dominion_mafr",
  description="Sahelanthropus (Dominion, Africa)",
  missions={[30020]=true},--Africa free roam
  noRoutes=true,--blank slate: no Episode 31 routes, navmesh, REX or search-missile points in this pack
  --locator: Nova Braga Airport north LZ ground point (InfLZ lz_drp_pfCamp_N0000), ~420 m north of the airfield
  arenaPos={1061.84,6.78,731.21},
  --player warp: the airport drop LZ (lz_drp_pfCamp_I0000)
  playerArenaPos={846.46,-4.97,1148.62},
  packages={
    -- vanilla parts, model, hitboxes, effects and textures (shared by Episodes 12, 31 and 50)
    "/Assets/tpp/pack/mission2/common/mis_com_sahelan.fpk",
    -- built by Tools/build_sahelan_unbound.py from Episode 31 data:
    -- GameObject + TppSahelan2Parameter, Africa locator, motion graph/mtar, se_e_mgs sound (blank slate:
    -- no Episode 31 navmesh, routes, REX/snipe or search-missile points)
    "/Assets/tpp/pack/boss/ih/TppSahelan2/TppSahelan2_gameobject_mafr.fpk",
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
