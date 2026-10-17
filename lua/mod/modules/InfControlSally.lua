-- InfControlSally.lua
-- ControlSally: in-game Infinite Heaven menu for piloting Sahelanthropus (ControlSally DLL + InfBossTppSahelan2).
--
-- Menu: IH menu (IHHook overlay or in game) > ControlSally. Lets you:
--   set up IH boss events for ControlSally once, spawn / despawn / respawn Sahelanthropus, teleport to it,
--   summon it to you or send it to your latest map marker, pick where it shows up, and change its settings live
--   (player protection, combat phase lock, eye colour, soldier-damage side).
--
-- How it talks to the rest:
--   InfBossTppSahelan2 (boss-event type) loads the Sahelanthropus pack and makes it appear at its locator.
--   SahelanControl (Lua library registered by SahelanControl.dll) does everything inside the mech: gate, goals,
--   jumps, targeting, eyes. Every DLL call is guarded, so the menu also loads without the DLL (DLL items then say so).
--   Steering with the keyboard still needs the pilot driver (driver\sally_pilot.py).
--
-- Moving the mech: TppSahelan2 has no Warp command and always appears at its locator (Afghanistan: Episode 31
-- battlefield, Africa: Nova Braga Airport). "Near me" / "map marker" spawn points and Summon / Send use one native
-- pilot jump (SahelanControl.PilotJumpNow) towards the target. Experimental: long jumps are untested.

local InfCore=InfCore
local SendCommand=GameObject.SendCommand
local GetGameObjectId=GameObject.GetGameObjectId
local NULL_ID=GameObject.NULL_ID

local this={}
this.name="InfControlSally"
this.debugModule=false

local TYPE="TppSahelan2"
local GATE_PILOT=23     --gate bits the pilot driver uses (decisions held, goals and jumps from the pilot)
local PLACE_TIMER="Timer_ControlSallyPlace"
local PLACE_RETRY_S=2
local PLACE_MAX_TRIES=15
local SUMMON_DIST=35    --metres in front of the player
local TELEPORT_DIST=8   --metres from the mech, towards the player. Kept short: the warp uses the mech's
                        --foot height and IH has no ground query, so a long offset can drop Snake off a slope (ATK_Fall)

this.eyePresets={"vanilla","blue","red","white","purple","amber","green","disco"}
this.spawnPoints={"DEFAULT","NEAR_ME","MAP_MARKER"}
this.sides={"VANILLA","FRIEND","BOTH"}
local sideValues={VANILLA=-1,FRIEND=1,BOTH=3}

this.placeTarget=nil
this.placeTries=0

local function Log(msg,show)
  InfCore.Log("ControlSally: "..tostring(msg),show)
end

local function Say(msg)
  Log(msg,false)
  pcall(TppUiCommand.AnnounceLogView,"ControlSally: "..tostring(msg))
end

local function DLL()
  return _G.SahelanControl
end

local function DllCall(fnName,...)
  local SC=DLL()
  if not SC or not SC[fnName] then
    return nil,"ControlSally DLL not loaded (plugins\\SahelanControl.dll)"
  end
  local ok,r=pcall(SC[fnName],...)
  if not ok then
    return nil,tostring(r)
  end
  return r==nil and true or r
end

local function Boss()
  return _G.InfBossTppSahelan2
end

local function MechId()
  local B=Boss()
  local name=(B and B.currentInfo and B.currentInfo.objectNames and B.currentInfo.objectNames[1]) or "Sahelanthropus"
  local ok,gameId=pcall(GetGameObjectId,TYPE,name)
  if ok and gameId and gameId~=NULL_ID then
    return gameId
  end
  return nil
end

local function MechPos()
  local gameId=MechId()
  if not gameId then return nil end
  local ok,pos=pcall(SendCommand,gameId,{id="GetPosition"})
  if ok and pos then
    return {pos:GetX(),pos:GetY(),pos:GetZ()}
  end
  return nil
end

local function PlayerPos()
  return {vars.playerPosX,vars.playerPosY,vars.playerPosZ}
end

local function InFront(dist)
  local yaw=math.rad(vars.playerCameraRotation[1] or 0)
  local p=PlayerPos()
  return {p[1]+math.sin(yaw)*dist,p[2],p[3]+math.cos(yaw)*dist}
end

local function MarkerPos()
  if not InfUserMarker or not InfUserMarker.GetLastAddedUserMarkerIndex then return nil end
  local index=InfUserMarker.GetLastAddedUserMarkerIndex()
  if not index then return nil end
  return InfUserMarker.GetMarkerPosition(index)
end

local function SettingName(ivarName,list)
  local ivar=Ivars[ivarName]
  if not ivar then return nil end
  return list[ivar:Get()+1]
end

--settings>
function this.ApplySettings(quiet)
  local SC=DLL()
  if not SC then
    if not quiet then Say("DLL not loaded, settings apply once it is installed") end
    return
  end
  DllCall("SetDivertTarget",Ivars.sally_protectPlayer:Is(1))
  DllCall("ForcePhase",Ivars.sally_phaseLock:Is(1) and 3 or -1)
  DllCall("SetEyePreset",SettingName("sally_eyes",this.eyePresets) or "vanilla")
  if SC.SetOffenseSide then
    DllCall("SetOffenseSide",sideValues[SettingName("sally_side",this.sides) or "VANILLA"])
  end
end

local function OnChangeSetting(self,setting)
  if MechId() then
    this.ApplySettings(true)
  end
end
--settings<

--placement (spawn point, summon, send)>
local function JumpTo(target)
  DllCall("StartGoalHook")
  DllCall("SetGate",GATE_PILOT)
  return DllCall("PilotJumpNow",target[1],target[2],target[3])
end

function this.StartPlacement(target)
  this.placeTarget=target
  this.placeTries=0
  GkEventTimerManager.Stop(PLACE_TIMER)
  GkEventTimerManager.Start(PLACE_TIMER,PLACE_RETRY_S)
end

function this.OnPlaceTimer()
  if not this.placeTarget then return end
  this.placeTries=this.placeTries+1
  local r,err=JumpTo(this.placeTarget)
  if r and type(r)=="string" and r:find("^jump") then
    Say(string.format("jumping to %.0f, %.0f",this.placeTarget[1],this.placeTarget[3]))
    this.placeTarget=nil
    return
  end
  if this.placeTries>=PLACE_MAX_TRIES then
    Say("could not move Sahelanthropus ("..tostring(err or r)..")")
    this.placeTarget=nil
    return
  end
  GkEventTimerManager.Start(PLACE_TIMER,PLACE_RETRY_S)--controller not ready yet, retry
end

local function SpawnPointTarget()
  local point=SettingName("sally_spawnPoint",this.spawnPoints)
  if point=="NEAR_ME" then
    return InFront(SUMMON_DIST)
  elseif point=="MAP_MARKER" then
    local m=MarkerPos()
    if not m then Say("no map marker set, using the default spawn point") end
    return m
  end
  return nil
end
--placement<

--menu commands>
function this.SetupBossEvents()
  local changed=false
  local function Set(name,value)
    if Ivars[name] and not Ivars[name]:Is(value) then
      Ivars[name]:Set(value)
      changed=true
    end
  end
  Set("bossEvent_enableFREE",1)
  Set("boss_TppSahelan2_enable",1)
  Set("boss_TppSahelan2_manualOnly",1)
  if changed then
    Say("boss events set up. Reload the mission (or restart free roam) once, then use Spawn")
  else
    Say("already set up")
  end
  InfMenu.MenuOff()
end

function this.Spawn()
  local B=Boss()
  if not B then Say("InfBossTppSahelan2 module missing (install ControlSally.mgsv)") return end
  if not B.SupportsMission(vars.missionCode) then
    Say("only in Afghanistan or Africa free roam")
    return
  end
  if MechId() and B.currentSubType then
    Say("Sahelanthropus is already here (use Respawn to start over)")
    return
  end
  if not InfBossEvent.BossEventEnabled() then
    Say("run 'Set up boss events' first, then reload the mission")
    return
  end
  this.pendingPlace=SpawnPointTarget()
  B.StartEventNow()
  Say("Sahelanthropus requested")
end

function this.Despawn()
  this.placeTarget=nil
  this.pendingPlace=nil
  DllCall("ClearPilotGoal")
  DllCall("SetGate",0)
  if InfBossEvent and InfBossEvent.EndEvent then
    InfBossEvent.EndEvent()
  end
  Say("Sahelanthropus removed")
  InfMenu.MenuOff()
end

function this.Respawn()
  this.Despawn()
  this.respawnPending=true
  GkEventTimerManager.Start("Timer_ControlSallyRespawn",6)
  Say("respawning in a few seconds")
end

function this.OnRespawnTimer()
  if not this.respawnPending then return end
  this.respawnPending=false
  this.Spawn()
end

function this.TeleportToMech()
  local m=MechPos()
  if not m then Say("Sahelanthropus is not here") return end
  local p=PlayerPos()
  local dx,dz=p[1]-m[1],p[3]-m[3]
  local len=math.sqrt(dx*dx+dz*dz)
  if len<1 then dx,dz,len=0,1,1 end
  local pos={m[1]+dx/len*TELEPORT_DIST,m[2]+1,m[3]+dz/len*TELEPORT_DIST}
  local rotY=math.deg(math.atan2(m[1]-pos[1],m[3]-pos[3]))
  TppPlayer.Warp{pos=pos,rotY=rotY}
  InfMenu.MenuOff()
end

function this.SummonToMe()
  if not MechId() then Say("Sahelanthropus is not here") return end
  this.StartPlacement(InFront(SUMMON_DIST))
  InfMenu.MenuOff()
end

function this.SendToMarker()
  if not MechId() then Say("Sahelanthropus is not here") return end
  local m=MarkerPos()
  if not m then Say("place a map marker first") return end
  this.StartPlacement(m)
  InfMenu.MenuOff()
end

function this.ReleaseToAi()
  DllCall("ClearPilotGoal")
  local r,err=DllCall("SetGate",0)
  Say(r and "released to the AI" or err)
end

function this.WarpToSpawnPoint()
  local B=Boss()
  if B and B.WarpToBattlefield then B.WarpToBattlefield() end
end

function this.ShowStatus()
  local mech=MechPos()
  local info=DllCall("GetInfo") or "DLL not loaded"
  Say(mech and string.format("mech at %.0f, %.0f, %.0f",mech[1],mech[2],mech[3]) or "no Sahelanthropus")
  Say(tostring(info):sub(1,90))
end

function this.ApplySettingsNow()
  this.ApplySettings(false)
  Say("settings applied")
end
--menu commands<

--messages>
function this.Messages()
  return Tpp.StrCode32Table{
    Timer={
      {msg="Finish",sender=PLACE_TIMER,func=this.OnPlaceTimer},
      {msg="Finish",sender="Timer_ControlSallyRespawn",func=this.OnRespawnTimer},
      {msg="Finish",sender="Timer_ControlSallyApply",func=function()
        if MechId() then
          this.ApplySettings(true)
          if this.pendingPlace then
            this.StartPlacement(this.pendingPlace)
            this.pendingPlace=nil
          end
        else
          this.applyTries=(this.applyTries or 0)+1
          if this.applyTries<20 then GkEventTimerManager.Start("Timer_ControlSallyApply",2) end
        end
      end},
    },
  }
end

function this.Init(missionTable)
  this.messageExecTable=Tpp.MakeMessageExecTable(this.Messages())
end

function this.OnReload(missionTable)
  this.messageExecTable=Tpp.MakeMessageExecTable(this.Messages())
end

function this.OnMessage(sender,messageId,arg0,arg1,arg2,arg3,strLogText)
  Tpp.DoMessage(this.messageExecTable,TppMission.CheckMessageOption,sender,messageId,arg0,arg1,arg2,arg3,strLogText)
end

--apply settings and the spawn point once the boss module has made it appear
function this.PostAllModulesLoad(isReload)
  local B=Boss()
  if not B or B.controlSallyHooked then return end
  B.controlSallyHooked=true
  local Appear=B.Appear
  B.Appear=function(...)
    Appear(...)
    local CS=_G.InfControlSally
    if CS then
      CS.applyTries=0
      GkEventTimerManager.Start("Timer_ControlSallyApply",2)
    end
  end
end
--messages<

--Ivars, menu>
this.registerIvars={}
this.registerMenus={"controlSallyMenu"}

this.controlSallyMenu={
  parentRefs={"InfMenuDefs.safeSpaceMenu","InfMenuDefs.inMissionMenu"},
  options={
    "InfControlSally.Spawn",
    "InfControlSally.Despawn",
    --"InfControlSally.Respawn",--off until the TppSahelan2_block reload hang is fixed (unload never empties)
    "InfControlSally.TeleportToMech",
    "InfControlSally.SummonToMe",
    "InfControlSally.SendToMarker",
    "InfControlSally.ReleaseToAi",
    "InfControlSally.WarpToSpawnPoint",
    "Ivars.sally_spawnPoint",
    "Ivars.sally_protectPlayer",
    "Ivars.sally_phaseLock",
    "Ivars.sally_eyes",
    "Ivars.sally_side",
    "InfControlSally.ApplySettingsNow",
    "InfControlSally.ShowStatus",
    "InfControlSally.SetupBossEvents",
  },
}

local function AddIvar(name,ivar)
  IvarProc.AddIvarToModule(name,this,ivar,"controlSallyMenu")
end

AddIvar("sally_spawnPoint",{
  save=IvarProc.CATEGORY_EXTERNAL,
  default=0,
  settings=this.spawnPoints,
})
AddIvar("sally_protectPlayer",{
  save=IvarProc.CATEGORY_EXTERNAL,
  default=1,
  range=Ivars.switchRange,
  settingNames="set_switch",
  OnChange=OnChangeSetting,
})
AddIvar("sally_phaseLock",{
  save=IvarProc.CATEGORY_EXTERNAL,
  default=1,
  range=Ivars.switchRange,
  settingNames="set_switch",
  OnChange=OnChangeSetting,
})
AddIvar("sally_eyes",{
  save=IvarProc.CATEGORY_EXTERNAL,
  default=1,
  settings=this.eyePresets,
  OnChange=OnChangeSetting,
})
AddIvar("sally_side",{
  save=IvarProc.CATEGORY_EXTERNAL,
  default=0,
  settings=this.sides,
  OnChange=OnChangeSetting,
})

this.langStrings={
  eng={
    controlSallyMenu="ControlSally",
    spawn="Spawn Sahelanthropus",
    despawn="Despawn Sahelanthropus",
    respawn="Respawn Sahelanthropus",
    teleportToMech="Teleport me to Sahelanthropus",
    summonToMe="Summon Sahelanthropus to me (jump)",
    sendToMarker="Send Sahelanthropus to map marker (jump)",
    releaseToAi="Release Sahelanthropus to its AI",
    warpToSpawnPoint="Teleport me to its spawn point",
    applySettingsNow="Apply settings now",
    showStatus="Show status",
    setupBossEvents="Set up boss events (once)",
    sally_spawnPoint="Spawn point",
    sally_protectPlayer="Never target Snake",
    sally_phaseLock="Lock combat phase",
    sally_eyes="Eye colour",
    sally_side="Soldier damage side (experimental)",
  },
  help={
    eng={
      spawn="Brings Sahelanthropus in through the boss event (Afghanistan or Africa free roam). Then start the pilot driver to steer it. KNOWN BUG: dying or reloading while it is loaded can hang the loading screen.",
      despawn="Ends the boss event and removes Sahelanthropus. KNOWN BUG: after this (or after dying, or any mission reload while it is loaded) the next loading screen can hang; save first and restart the game afterwards.",
      respawn="Despawn, then spawn again a few seconds later.",
      summonToMe="One native jump to a point in front of you. Experimental: very long jumps may fail.",
      sendToMarker="One native jump to your latest map marker. Experimental.",
      releaseToAi="Hands control back to its own AI (the pilot driver takes it again when you steer).",
      setupBossEvents="Turns on free-roam boss events, the Sahelanthropus boss type and manual-only spawning. Reload the mission once afterwards.",
      sally_spawnPoint="DEFAULT = its locator (Afghanistan: Episode 31 battlefield, Africa: Nova Braga Airport). NEAR_ME / MAP_MARKER = appear there, then jump in front of you or to your latest map marker.",
      sally_protectPlayer="Diverts its target search away from Snake.",
      sally_phaseLock="Keeps it in combat phase so it never retreats or searches.",
      sally_side="Which side its attacks count as. VANILLA = enemy side (soldiers immune). Needs a DLL with SetOffenseSide.",
    },
  },
}
--Ivars, menu<

return this
