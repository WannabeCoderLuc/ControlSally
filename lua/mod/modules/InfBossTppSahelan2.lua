-- InfBossTppSahelan2.lua
-- Sahelanthropus Unbound, test build 1 (local build, not for distribution).
-- Adds Sahelanthropus (TppSahelan2) as an Infinite Heaven r267 boss-event type.
--
-- How it plugs into IH:
--   InfBossEvent keeps its boss types in a local name table, so this module registers itself instead:
--   it wraps InfBossEvent.GetBossModules (re-run on every module reload) and InfBossEvent.AddMissionPacks
--   (decides per mission whether the type takes part, so its script block only exists where it can run).
--
-- Test build 1 scope:
--   Afghanistan free roam (30010) only. Sahelanthropus starts at Episode 31's battlefield (its locator) because
--   TppSahelan2 has no Warp command, and only Episode 31's Sahelanthropus navmesh is loaded.
--   Map-wide roaming needs the nav-world load limit raised and new navmesh (later builds).
--
-- Data used from the game (see Tools/build_sahelan_unbound.py): s10151 locator, Sahelanthropus navmesh,
-- routes (rts_shln_*), REX tower/snipe points, search-missile point, se_e_mgs sound package.
-- Life tables and combat grade values are Episode 31's (s10151_enemy.lua) and Episode 50's (s11151).

local InfCore=InfCore
local GetGameObjectId=GameObject.GetGameObjectId
local SendCommand=GameObject.SendCommand
local NULL_ID=GameObject.NULL_ID

local this={}
this.name="InfBossTppSahelan2"
this.debugModule=false

this.gameObjectType="TppSahelan2"
this.gameObjectTypeIndex=TppGameObject.GAME_OBJECT_TYPE_SAHELAN2
this.bossStatesName="bossEvent_"..this.gameObjectType.."State"
this.blockName=this.gameObjectType.."_block"
this.blockNameS32=InfCore.StrCode32(this.blockName)
this.hardcodedCount=true--one GameObject, one locator: IH always asks for exactly 1

local TYPE=this.gameObjectType
local bossStatesName=this.bossStatesName

--missions this type can run in. Each sub type addon may restrict itself with missions={[code]=true}.
--t2a: Africa free roam (30020) through the sahelan_mgs0_dominion_mafr sub type (locator at Nova Braga Airport)
this.supportedMissions={
  [30010]=true,
  [30020]=true,
}

--Episode 31 battlefield (s10151_npc_sp.fox2 locator "Sahelanthropus")
this.arenaPos={-612.161,504.688,-1145.311}
--player warp target: Episode 31 supply-drop point AREA03 (s10151_sequence.lua), ~170 m from the locator
this.playerArenaPos={-597.820,464.508,-975.634}
this.farFromArenaDistance=400

this.baseRoute="rts_shln_b_1000"--s10151_enemy.SetUpSahelan

--s10151_enemy.lua
this.lifeTables={
  NORMAL={
    maxLife=35000,
    parts={
      Body=12000,Bp=900,Head=3000,ArmR=900,ArmL=900,ThighR=900,ThighL=900,LegR=900,LegL=900,
      RGun=1200,Ldr=900,Tnk=900,Shield=0,PTLF=96,PTRF=96,PTLB=96,PTRB=96,
    },
    combatGrade={defenseValue=35000,defenseValueForWeakPoint=12000,offenseGrade=5,defenseGrade=1},
  },
  EXTREME={
    maxLife=55000,
    parts={
      Body=15000,Bp=1200,Head=4000,ArmR=1200,ArmL=1200,ThighR=1200,ThighL=1200,LegR=1200,LegL=1200,
      RGun=1600,Ldr=1200,Tnk=1200,Shield=0,PTLF=120,PTRF=120,PTLB=120,PTRB=120,
    },
    combatGrade={defenseValue=60000,defenseValueForWeakPoint=20000,offenseGrade=6,defenseGrade=2},
  },
}
--SetStageType index: 1 = Dominion (Episode 31), 2 = Dominion extreme (Episode 50)
this.stageTypes={NORMAL=1,EXTREME=2}

this.stateTypes={
  READY=0,
  DOWNED=1,
}

--InfBossEvent interface state
this.currentSubType=nil
this.currentInfo=nil
this.currentParams=nil
this.numBosses=0
this.gameIdToNameIndex={}
this.names={}
this.infos={}
this.enableSubTypeIvarNames={}
this.sahelanId=NULL_ID
this.packMission=nil--set by the AddMissionPacks wrapper

this.packages={
  scriptBlockData="/Assets/tpp/pack/mission2/boss/ih/"..TYPE.."_scriptblockdata.fpk",
}

local function Log(message,announce)
  InfCore.Log(this.name..": "..message,announce)
end

--registration>
function this.SupportsMission(missionCode)
  return missionCode~=nil and this.supportedMissions[missionCode]==true
end

function this.UpdateRegistration()
  local BossEvent=_G.InfBossEvent
  if not BossEvent or not BossEvent.bossModules then
    return
  end
  local missionCode=this.packMission or (vars and vars.missionCode)
  if this.SupportsMission(missionCode) then
    BossEvent.bossModules[TYPE]=_G[this.name] or this
  else
    BossEvent.bossModules[TYPE]=nil
  end
end

function this.InstallHooks()
  local BossEvent=_G.InfBossEvent
  if not BossEvent then
    Log("WARNING: InfBossEvent not loaded, boss type not registered",true)
    return
  end
  if BossEvent.sahelanUnboundHooked then
    this.UpdateRegistration()
    return
  end
  BossEvent.sahelanUnboundHooked=true

  local GetBossModules=BossEvent.GetBossModules
  BossEvent.GetBossModules=function(...)
    GetBossModules(...)
    local Module=_G.InfBossTppSahelan2
    if Module then
      Module.UpdateRegistration()
    end
  end

  local AddMissionPacks=BossEvent.AddMissionPacks
  BossEvent.AddMissionPacks=function(missionCode,packPaths)
    local Module=_G.InfBossTppSahelan2
    if Module then
      Module.packMission=missionCode
      Module.UpdateRegistration()
    end
    return AddMissionPacks(missionCode,packPaths)
  end

  this.UpdateRegistration()
  Log("registered with InfBossEvent")
end

function this.PostAllModulesLoad(isReload)
  this.InstallHooks()
end
--registration<

function this.DeclareSVars()
  if not InfBossEvent or not InfBossEvent.BossEventEnabled() then
    return{}
  end
  local saveVarsList={
    --index from 1 like the other IH boss modules (size+1)
    [bossStatesName]={name=bossStatesName,type=TppScriptVars.TYPE_INT8,arraySize=InfBossEvent.MAX_BOSSES_PER_TYPE+1,value=this.stateTypes.READY,save=true,sync=false,wait=false,category=TppScriptVars.CATEGORY_MISSION},
  }
  return TppSequence.MakeSVarsTable(saveVarsList)
end

function this.Messages()
  return Tpp.StrCode32Table{
    Block={
      {msg="OnScriptBlockStateTransition",func=this.OnScriptBlockStateTransition},
    },
    GameObject={
      {msg="Dead",func=this.OnDead},
      {msg="SahelanAllDead",func=this.OnAllDead},
      {msg="Damage",func=this.OnDamage},
      {msg="SahelanChangePhase",func=this.OnChangePhase},
      {msg="SahelanHeadBroken",func=function()
        Log("head broken",this.ShowMessages())
      end},
      {msg="SahelanPartsBroken",func=function()
        Log("part broken",this.ShowMessages())
      end},
    },
    Player={
      {msg="PlayerDamaged",func=this.OnPlayerDamaged},
    },
  }
end

local function MakeMessages()
  this.messageExecTable=nil
  if not InfBossEvent or not InfBossEvent.BossEventEnabled() then
    return
  end
  local packModeOn=this.PackMode() and this.missionPackSub~=nil
  if (not packModeOn and not this.IsEnabled()) or not this.SupportsMission(vars.missionCode) then
    return
  end
  this.messageExecTable=Tpp.MakeMessageExecTable(this.Messages())
end

function this.Init(missionTable)
  MakeMessages()
end

function this.OnReload(missionTable)
  MakeMessages()
end

function this.OnMessage(sender,messageId,arg0,arg1,arg2,arg3,strLogText)
  Tpp.DoMessage(this.messageExecTable,TppMission.CheckMessageOption,sender,messageId,arg0,arg1,arg2,arg3,strLogText)
end

function this.OnModuleLoad(prevModule)
  this.LoadInfos()
  this.AddSubTypeIvars()
end

--called by InfBossEvent.AddMissionPacks (only while registered, i.e. supported missions)
--load mode (ivar loadMode): MISSION_PACK (default) adds the sub type's packs to the free-roam mission packs, so the
--mech loads and unloads with the mission like in Episode 31. SCRIPT_BLOCK is the old IH boss-event block, which
--never reaches EMPTIED once loaded (every death/reload/despawn then hangs the next load; OptionB findings).
function this.PackMode()
  local ivar=this.ivarNames.loadMode and Ivars[this.ivarNames.loadMode]
  return ivar==nil or ivar:Is(0)
end

function this.MissionPackSubType(missionCode)
  for i,name in ipairs(this.names)do
    local info=this.infos[name]
    local ivarName=this.enableSubTypeIvarNames[name]
    local on=ivarName==nil or Ivars[ivarName]==nil or Ivars[ivarName]:Is(1)
    if on and info and info.missions and info.missions[missionCode] then
      return name
    end
  end
  return nil
end

function this.AddPacks(missionCode,packPaths)
  this.missionPackSub=nil
  if not this.SupportsMission(missionCode) then
    return
  end
  if this.PackMode() then
    if not Ivars[this.ivarNames.enable]:Is(1) then
      return
    end
    local sub=this.MissionPackSubType(missionCode)
    if not sub then
      Log("mission-pack mode: no enabled sub type for mission "..tostring(missionCode))
      return
    end
    for i,packPath in ipairs(this.infos[sub].packages)do
      packPaths[#packPaths+1]=packPath
    end
    this.missionPackSub=sub
    Log("mission-pack mode: "..sub.." loads with the mission")
    return
  end
  packPaths[#packPaths+1]=this.packages.scriptBlockData
end

--addons>
function this.LoadInfos()
  local folder="bosses/"..TYPE.."/"
  local files=InfCore.GetFileListInModFolder(folder) or {}
  for i,fileName in ipairs(files)do
    if fileName:find("%.lua$") then
      local info=InfCore.LoadSimpleModule(fileName)
      if info and info.name then
        if not this.infos[info.name] then
          table.insert(this.names,info.name)
        end
        this.infos[info.name]=info
        Log("loaded sub type "..info.name)
      else
        Log("WARNING: could not load "..tostring(fileName))
      end
    end
  end
end
--addons<

--manualOnly (default on): only appear when requested through StartEventNow (menu or pilot driver), never from IH's
--automatic countdown (a save made during an event keeps the countdown at 0, which spawned it on every Continue)
this.manualRequest=false
function this.IsEnabled()
  if not Ivars[this.ivarNames.enable]:Is(1) then
    return false
  end
  if Ivars[this.ivarNames.manualOnly]:Is(1) and not this.manualRequest then
    return false
  end
  return true
end

--OUT: {[subType]=1 or 0}, InfBossEvent keeps the 1s
function this.GetEnabledSubTypes(missionCode)
  if not this.SupportsMission(missionCode) or this.PackMode() then
    return {}--mission-pack mode: IH's boss event never picks or streams this type, StartEventNow spawns it
  end
  local enabled=IvarProc.GetIvarKeyNameValues(this.enableSubTypeIvarNames)
  for subType,value in pairs(enabled)do
    local info=this.infos[subType]
    if info and info.missions and not info.missions[missionCode] then
      enabled[subType]=0--this sub type's locator is on another map
    end
  end
  return enabled
end

--per sub type battlefield (addon fields arenaPos/playerArenaPos), falling back to Episode 31's
function this.ArenaPos()
  return (this.currentInfo and this.currentInfo.arenaPos) or this.arenaPos
end
function this.PlayerArenaPos(missionCode)
  for name,info in pairs(this.infos)do
    if info.missions and info.missions[missionCode] and info.playerArenaPos then
      return info.playerArenaPos
    end
  end
  return this.playerArenaPos
end

function this.SetBossSubType(bossSubType,numBosses)
  local info=this.infos[bossSubType]
  if not info then
    Log("ERROR: SetBossSubType: no sub type "..tostring(bossSubType))
    return
  end
  Log("SetBossSubType "..bossSubType)
  this.currentSubType=bossSubType
  this.currentInfo=info
  this.currentParams=info.eventParams
  this.numBosses=math.min(numBosses or 1,#info.objectNames)
end

function this.ClearBossSubType()
  this.currentSubType=nil
end

function this.ClearStates()
  for index=1,InfBossEvent.MAX_BOSSES_PER_TYPE do
    svars[bossStatesName][index]=this.stateTypes.READY
  end
end

function this.OnScriptBlockStateTransition(blockNameS32,blockState)
  if blockNameS32~=this.blockNameS32 then
    return
  end
  if blockState==ScriptBlock.TRANSITION_ACTIVATED then
    this.InitBoss()
  elseif blockState==ScriptBlock.TRANSITION_DEACTIVATED then
    this.sahelanId=NULL_ID
  end
end

--once the script block is active the GameObject exists (idle: no stage type yet)
function this.InitBoss()
  if this.currentSubType==nil then
    return
  end
  InfUtil.ClearTable(this.gameIdToNameIndex)
  InfBossEvent.BuildGameIdToNameIndex(this.currentInfo.objectNames,this.gameIdToNameIndex)
  this.sahelanId=GetGameObjectId(TYPE,this.currentInfo.objectNames[1])
  Log("InitBoss gameId:"..tostring(this.sahelanId))
end

function this.Difficulty()
  return Ivars[this.ivarNames.difficulty]:Is("EXTREME") and "EXTREME" or "NORMAL"
end

function this.ShowMessages()
  return Ivars[this.ivarNames.messages]:Is(1)
end

--InfBossEvent: Timer_BossAppear (and Timer_BossEventMonitor if updateToClosestCommandPost, which is off)
function this.Appear(appearPos,closestCp,closestCpPos,spawnRadius)
  this.manualRequest=false
  if this.IsAllCleared() then
    Log("Appear: already cleared")
    return
  end
  this.ClearStates()

  local gameId=GetGameObjectId(TYPE,this.currentInfo.objectNames[1])
  if gameId==NULL_ID then
    Log("ERROR: Appear: GameObject "..this.currentInfo.objectNames[1].." not found (pack not loaded?)",true)
    return
  end
  this.sahelanId=gameId

  local difficulty=this.Difficulty()
  local lifeTable=this.lifeTables[difficulty]
  local lifeScale=Ivars[this.ivarNames.lifeScale]:Get()/100

  SendCommand(gameId,{id="SetStageType",index=this.stageTypes[difficulty]})
  SendCommand(gameId,{id="SetMaxLife",life=lifeTable.maxLife*lifeScale})
  for partsName,partsLife in pairs(lifeTable.parts)do
    SendCommand(gameId,{id="SetMaxPartsLife",parts=partsName,life=partsLife*lifeScale})
  end
  if lifeTable.combatGrade then
    local command={id="SetCombatGrade"}
    for k,v in pairs(lifeTable.combatGrade)do
      command[k]=v
    end
    SendCommand(gameId,command)
  end
  --Episode 31 routes only exist in the Afghanistan pack; blank-slate sub types (noRoutes) get none
  if not this.currentInfo.noRoutes then
    SendCommand(gameId,{id="SetBaseRoute",route=this.baseRoute})
  end
  SendCommand(gameId,{id="SetEnableAttackToHoveringHeli"})
  if not this.currentInfo.noRoutes then
    SendCommand(gameId,{id="SetSearchMissileRouteAll",index=0,route=this.baseRoute})
  end

  if Ivars[this.ivarNames.marker]:Is(1) then
    TppMarker.Enable(this.currentInfo.objectNames[1],0,"attack","map_and_world_only_icon",0,false,false)
  end

  local playerPos={vars.playerPosX,vars.playerPosY,vars.playerPosZ}
  local distance=math.sqrt(TppMath.FindDistance(playerPos,this.ArenaPos()))
  Log(string.format("Sahelanthropus (%s, %s) is active at its battlefield, %d m away",difficulty,tostring(this.currentSubType),distance),this.ShowMessages())
  if distance>this.farFromArenaDistance then
    Log("You are far from the battlefield: use 'Warp to battlefield' in the "..TYPE.." menu, or raise Boss event escape distance",this.ShowMessages())
  end
end

--Clean up on mission restart, checkpoint reload, game over, abort, or end.
--Crucial: unloads the script block immediately so Fox Engine's streaming manager never deadlocks on reload.
function this.ForceUnloadBlock()
  if this.sahelanId~=nil and this.sahelanId~=NULL_ID then
    pcall(SendCommand,this.sahelanId,{id="SetStopSahelan"})
    pcall(SendCommand,this.sahelanId,{id="ResetParasiteEffect"})
  end
  this.DisableAll()
  if this.PackMode() then
    --mission-pack mode: nothing to unload, the mission load/unload owns the packs
    if WeatherManager and WeatherManager.RequestTag then
      pcall(WeatherManager.RequestTag,"Clear",7)
    end
    return
  end
  this.sahelanId=NULL_ID
  local blockId=ScriptBlock.GetScriptBlockId(this.blockName)
  if blockId and blockId~=ScriptBlock.SCRIPT_BLOCK_ID_INVALID then
    ScriptBlock.Load(blockId,"")
  end
  if WeatherManager and WeatherManager.RequestTag then
    pcall(WeatherManager.RequestTag,"Clear",7)
  end
end

--IH lifecycle callbacks
function this.EstablishedGameOverTop()
  this.ForceUnloadBlock()
end

function this.OnReload(missionTable)
  this.ForceUnloadBlock()
end

function this.OnMissionReset()
  this.ForceUnloadBlock()
end

function this.EstablishedMissionAbortTop()
  this.ForceUnloadBlock()
end

function this.EstablishedMissionClearTop()
  this.ForceUnloadBlock()
end

function this.OnExecuteMissionFinalize()
  this.ForceUnloadBlock()
end

function this.EndEvent()
  if this.currentSubType==nil then
    return
  end
  this.ClearStates()
  this.ForceUnloadBlock()
end

function this.DisableAll()
  if this.currentSubType==nil then
    return
  end
  for i,name in ipairs(this.currentInfo.objectNames)do
    if GetGameObjectId(TYPE,name)~=NULL_ID then
      TppMarker.Disable(name)
    end
  end
end

--Compact live status reporter for Sahelan Live console (under 250 bytes over pipe)
function this.ReportStatus()
  local P=string.char(124)
  local blockId=ScriptBlock.GetScriptBlockId(this.blockName)
  local bState=(blockId~=ScriptBlock.SCRIPT_BLOCK_ID_INVALID and ScriptBlock.GetScriptBlockState(blockId))or"none"
  local gameId=(this.currentInfo and this.currentInfo.objectNames and GetGameObjectId(TYPE,this.currentInfo.objectNames[1]))or NULL_ID
  local posStr=string.format("%.0f,%.0f,%.0f",vars.playerPosX,vars.playerPosY,vars.playerPosZ)
  local o={
    "t="..string.format("%.1f",Time.GetRawElapsedTimeSinceStartUp()),
    "m="..tostring(vars.missionCode),
    "p="..posStr,
    "buddy="..tostring(vars.buddyType),
    "sub="..tostring(this.currentSubType or"none"),
    "b="..tostring(bState),
  }
  if gameId~=NULL_ID then
    o[#o+1]="id="..tostring(gameId)
    local pos=SendCommand(gameId,{id="GetPosition"})
    if pos then
      o[#o+1]="pos="..string.format("%.0f,%.0f,%.0f",pos:GetX(),pos:GetY(),pos:GetZ())
    end
    local phase=SendCommand(gameId,{id="GetPhase"})
    if phase then o[#o+1]="ph="..tostring(phase) end
    local dist=SendCommand(gameId,{id="GetDistanceToPlayer"})
    if dist then o[#o+1]="d="..string.format("%.0f",dist) end
    local sees=SendCommand(gameId,{id="IsPlayerInSight"})
    if sees~=nil then o[#o+1]="sees="..tostring(sees) end
    if svars and svars[bossStatesName] then
      o[#o+1]="st="..tostring(svars[bossStatesName][1])
    end
  else
    o[#o+1]="id=none"
  end
  IHH.QueuePipeOutMessage("0"..P.."SahelanLive"..P.."status"..P..table.concat(o," "))
end

--messages>
local function IsSahelan(gameId)
  return gameId~=nil and gameId~=NULL_ID and this.gameIdToNameIndex[gameId]~=nil
end

local function SetDowned(gameId)
  local nameIndex=this.gameIdToNameIndex[gameId] or 1
  if svars[bossStatesName][nameIndex]==this.stateTypes.DOWNED then
    return
  end
  svars[bossStatesName][nameIndex]=this.stateTypes.DOWNED
  Log("Sahelanthropus destroyed",this.ShowMessages())
end

function this.OnDead(gameId)
  if IsSahelan(gameId) then
    SetDowned(gameId)
    if WeatherManager and WeatherManager.RequestTag then
      pcall(WeatherManager.RequestTag,"Clear",7)
    end
  end
end

function this.OnAllDead()
  if this.currentSubType~=nil and this.sahelanId~=NULL_ID then
    SetDowned(this.sahelanId)
    if WeatherManager and WeatherManager.RequestTag then
      pcall(WeatherManager.RequestTag,"Clear",7)
    end
  end
end

--keeps IH's event focus on the player while the fight is going on
function this.OnDamage(gameId,attackId,attackerId)
  if IsSahelan(gameId) or IsSahelan(attackerId) then
    InfBossEvent.SetFocusOnPlayerPos()
  end
end

function this.OnPlayerDamaged(playerIndex,attackId,attackerId)
  if IsSahelan(attackerId) then
    InfBossEvent.SetFocusOnPlayerPos()
  end
end

function this.OnChangePhase(gameId,phaseName)
  Log("phase "..tostring(phaseName))
  if not IsSahelan(gameId) then return end
  local pNum=tonumber(phaseName) or 0
  --trigger support attack pressure on phase shifts
  pcall(SendCommand,gameId,{id="SetSupportAttack"})
  --in later phases (phase 8+), summon red fog storm
  if pNum>=8 then
    if WeatherManager and WeatherManager.RequestTag then
      pcall(WeatherManager.RequestTag,"Sahelan_RedFog",7)
    end
    if TppSoundDaemon and TppSoundDaemon.PostEvent then
      pcall(TppSoundDaemon.PostEvent,"env_para_storm")
    end
    pcall(SendCommand,gameId,{id="SetParasiteEffect"})
  elseif pNum>0 and pNum<8 then
    if WeatherManager and WeatherManager.RequestTag then
      pcall(WeatherManager.RequestTag,"Clear",7)
    end
    pcall(SendCommand,gameId,{id="ResetParasiteEffect"})
  end
end
--messages<

function this.IsAllCleared()
  if this.currentSubType==nil then
    return true
  end
  for index=1,this.numBosses do
    if svars[bossStatesName][index]==this.stateTypes.READY then
      return false
    end
  end
  return true
end

function this.IsReady(nameIndex)
  return svars[bossStatesName][nameIndex]==this.stateTypes.READY
end

--menu commands>
function this.WarpToBattlefield()
  if not this.SupportsMission(vars.missionCode) then
    Log("Sahelanthropus battlefields: Afghanistan or Africa free roam",true)
    return
  end
  local pos=this.PlayerArenaPos(vars.missionCode)
  TppPlayer.Warp{pos={pos[1],pos[2]+1,pos[3]},rotY=vars.playerCameraRotation[1]}
  InfMenu.MenuOff()
end

function this.StartEventNow()
  if this.PackMode() then
    local sub=this.missionPackSub
    if not sub then
      Log("Sahelanthropus is not loaded in this mission: enable it (and free-roam boss events), then reload the mission",true)
      return
    end
    this.SetBossSubType(sub,1)
    this.InitBoss()
    if this.sahelanId==nil or this.sahelanId==NULL_ID then
      Log("Sahelanthropus GameObject not found (pack missing?)",true)
      return
    end
    this.ClearStates()
    local playerPos={vars.playerPosX,vars.playerPosY,vars.playerPosZ}
    this.Appear(playerPos,nil,playerPos,0)
    InfMenu.MenuOff()
    return
  end
  if not InfBossEvent.BossEventEnabled() then
    Log("Enable boss events for free roam first (Boss event menu)",true)
    return
  end
  this.manualRequest=true
  InfBossEvent.StartCountdown(true)
  Log("boss event starting (picks randomly between enabled boss types)",true)
  InfMenu.MenuOff()
end
--menu commands<

--Ivars, menu>
this.registerIvars={}
this.registerMenus={}
this.ivarNames={}

local ivarPrefix="boss_"..TYPE
local bossMenuName=TYPE.."_Menu"
table.insert(this.registerMenus,bossMenuName)

this[bossMenuName]={
  parentRefs={"InfBossEvent.bossEventMenu"},
  options={
    "InfBossTppSahelan2.WarpToBattlefield",
    "InfBossTppSahelan2.StartEventNow",
  },
}

local function OnChangeEnable(self,setting)
  if not TppMission.IsMissionStart()then
    MakeMessages()
  end
end

local function AddIvar(key,ivar)
  local ivarName=table.concat({ivarPrefix,key},"_")
  IvarProc.AddIvarToModule(ivarName,this,ivar,bossMenuName)
  this.ivarNames[key]=ivarName
  return ivarName
end

AddIvar("enable",{
  save=IvarProc.CATEGORY_EXTERNAL,
  default=1,
  range=Ivars.switchRange,
  settingNames="set_switch",
  OnChange=OnChangeEnable,
})
AddIvar("manualOnly",{
  save=IvarProc.CATEGORY_EXTERNAL,
  default=1,
  range=Ivars.switchRange,
  settingNames="set_switch",
})

AddIvar("loadMode",{
  save=IvarProc.CATEGORY_EXTERNAL,
  default=0,
  settings={"MISSION_PACK","SCRIPT_BLOCK"},
})

AddIvar("difficulty",{
  save=IvarProc.CATEGORY_EXTERNAL,
  default=1,
  settings={"NORMAL","EXTREME"},
})
AddIvar("lifeScale",{
  save=IvarProc.CATEGORY_EXTERNAL,
  default=100,
  range={min=10,max=500,increment=10},
})
AddIvar("marker",{
  save=IvarProc.CATEGORY_EXTERNAL,
  default=1,
  range=Ivars.switchRange,
  settingNames="set_switch",
})
AddIvar("messages",{
  save=IvarProc.CATEGORY_EXTERNAL,
  default=1,
  range=Ivars.switchRange,
  settingNames="set_switch",
})

function this.AddSubTypeIvars()
  for i,subType in ipairs(this.names)do
    if not this.enableSubTypeIvarNames[subType] then
      local ivarName=table.concat({ivarPrefix,subType,"enable"},"_")
      IvarProc.AddIvarToModule(ivarName,this,{
        description=(this.infos[subType].description or subType).." Enable",
        save=IvarProc.CATEGORY_EXTERNAL,
        default=1,
        range=Ivars.switchRange,
        settingNames="set_switch",
        OnChange=OnChangeEnable,
      },bossMenuName)
      this.enableSubTypeIvarNames[subType]=ivarName
    end
  end
end

this.langStrings={
  eng={
    [bossMenuName]="Sahelanthropus menu",
    [this.ivarNames.enable]="Sahelanthropus enable",
    [this.ivarNames.manualOnly]="Sahelanthropus manual spawn only",
    [this.ivarNames.difficulty]="Sahelanthropus difficulty",
    [this.ivarNames.loadMode]="Sahelanthropus load mode",
    [this.ivarNames.lifeScale]="Sahelanthropus health %",
    [this.ivarNames.marker]="Sahelanthropus map marker",
    [this.ivarNames.messages]="Sahelanthropus on-screen messages",
    warpToBattlefield="Warp to Episode 31 battlefield",
    startEventNow="Start boss event now",
  },
  help={
    eng={
      [this.ivarNames.enable]="Adds Sahelanthropus to IH boss events. Test build 1: Afghanistan free roam only, appears at Episode 31's battlefield.",
      [this.ivarNames.manualOnly]="On: Sahelanthropus only appears through 'Start boss event now' (or the pilot driver), never from the automatic boss-event countdown.",
      [this.ivarNames.loadMode]="MISSION_PACK (recommended): loads with free roam, reloads safely; changes need a mission reload. SCRIPT_BLOCK: old IH boss-event block, hangs the next loading screen after death, reload or despawn.",
      [this.ivarNames.difficulty]="NORMAL = Episode 31 values, EXTREME = Episode 50 values (stage type 2 + combat grade).",
      [this.ivarNames.lifeScale]="Scales Sahelanthropus' total and per-part health.",
    },
  },
}
--Ivars, menu<

return this
