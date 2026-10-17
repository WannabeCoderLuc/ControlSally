"""pilot_drive10.py: Option B Pilot Driver with Visual & Weapon Tuning (probe10b / probe10c)

Complete direct control of Sahelanthropus with:
  1. Direct Steering (Numpad 8/2/4/6, hold 5, release 0) - FIXED: Camera-relative Left/Right corrected!
  2. Direct Jump (Numpad +)
  3. Complete 7-Weapon Arsenal (Sword, Gatling, Flame, Missiles, Grenade, Stomp, Sword2)
  4. Visual Tuning: Real-time Eye Lamp Color Tuning with dual V_Framework + SahelanControl dispatch
     (Presets: blue, red, white, purple, amber, green, disco)
  5. Target Acquisition Protection: Player diverted away from Sahelanthropus targeting (0x1418FEDB0 hook)
  6. Phase Locking (IdleHold is off by default: it pins decision 0 and the mech never walks)
  7. probe10c: offense side (soldier damage, Arch 10.5): --side N at start, F10 cycles 1 -> 3 -> -1 -> 0
     (1 FRIEND like D-Dog, 3 FRIEND+ENEMY, -1 vanilla, 0 none). No-op on probe10b.
  8. Aim follows the camera: yaw + pitch from vars.playerCameraRotation, ray from
     vars.playerCameraPosition, cut at the player's ground height (--pitch-sign flips pitch).

Keys (active while MGSV window is in front):
  [Movement]
    Numpad 8 / 2 / 4 / 6 : Move relative to camera (8=Fwd, 2=Back, 4=Left, 6=Right)
    Numpad 5             : Hold position
    Numpad 0             : Release to vanilla AI (gate off)
    Numpad +             : Direct jump 40m ahead

  [7-Weapon Arsenal]
    Numpad 7             : Sword (slot 1)
    Numpad 9             : Gatling fire / hold (slot 2)
    Numpad 1             : Flamethrower fire / hold (slot 3)
    Numpad . (decimal)   : Missiles (slot 4)
    Numpad 3             : Grenade Launcher (slot 5, native builder 0x1418E4C80)
    Numpad - (minus)     : Stomp / ground slam (slot 6)
    PageUp               : Sword 2 (slot 7)

  [Visual Tuning: Eye Lamp Color]
    Numpad /             : Cycle Eye Color Presets (blue -> red -> white -> purple -> amber -> green -> disco)
    F6                   : Red eye (classic Dominion)
    F7                   : Blue eye (Option B pilot)
    F8                   : Disco Rainbow cycle mode
    F9                   : White searchlight eye

  [Phase & AI Locking]
    Insert               : Toggle Combat Phase Lock (Phase 3 Combat vs Vanilla AI)
    Delete               : Toggle Idle Hold (experimental: pins decision 0, stops walking)
    F10                  : Cycle offense side (probe10c): 1 -> 3 -> -1 (vanilla) -> 0
    F11                  : Cycle offense target groups (probe10e): 0x13 -> 0x1F -> 0x5F -> 0x12 -> original
    Numpad *             : Mark timestamp + screenshot
    Ctrl + Shift + End   : Panic abort (releases gate & mech immediately)

Usage:
  python Tools/pilot_drive10.py [--secs 1800] [--eye blue] [--phase 3]
"""
import argparse
import ctypes
import math
import os
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.append(ROOT)
from ih_pipe_client import IHPipe

sys.stdout.reconfigure(encoding="utf-8")
user32 = ctypes.windll.user32

VK = {
    "mark": 0x6A,     # Numpad *
    "fwd": 0x68,      # Numpad 8
    "back": 0x62,     # Numpad 2
    "left": 0x64,     # Numpad 4
    "right": 0x66,    # Numpad 6
    "hold": 0x65,     # Numpad 5
    "release": 0x60,  # Numpad 0
    "jump": 0x6B,     # Numpad +
    "stamp": 0x6D,    # Numpad -
    "eye_cycle": 0x6F,# Numpad /
    "f6_red": 0x75,   # F6
    "f7_blue": 0x76,  # F7
    "f8_disco": 0x77, # F8
    "f9_white": 0x78, # F9
    "phase_lock": 0x2D, # Insert
    "idle_hold": 0x2E,  # Delete
    "sword2": 0x21,   # PageUp
    "side": 0x79,     # F10
    "groups": 0x7A,   # F11
}

SIDES = [1, 3, -1, 0]
GROUPS = [0x13, 0x1F, 0x5F, 0x12, -1]   # probe10e: groups Sahelan damage offenses hit (vanilla 0x11 FRIEND|OBJECT)
SIDE_NAME = {1: "FRIEND (D-Dog)", 3: "FRIEND+ENEMY", -1: "vanilla", 0: "none", 2: "ENEMY"}

FIRE = {
    1: [0x67],        # Numpad 7: Sword
    2: [0x69],        # Numpad 9: Gatling
    3: [0x61],        # Numpad 1: Flame
    4: [0x6E],        # Numpad .: Missiles
    5: [0x63],        # Numpad 3: Grenade Launcher
    6: [0x6D],        # Numpad -: Stamp / Stomp
    7: [0x21],        # PageUp: Sword 2
}

SLOT_NAME = {
    1: "Sword",
    2: "Gatling",
    3: "Flamethrower",
    4: "Missiles",
    5: "Grenade Launcher",
    6: "Stomp",
    7: "Sword 2",
}

EYE_PRESETS = ["blue", "red", "white", "purple", "amber", "green", "disco", "vanilla"]

RGB_MAP = {
    "red": (1.0, 0.05, 0.05),
    "blue": (0.05, 0.6, 1.0),
    "white": (1.0, 1.0, 1.0),
    "purple": (0.85, 0.1, 1.0),
    "amber": (1.0, 0.65, 0.0),
    "green": (0.1, 1.0, 0.2),
    "vanilla": (1.0, 0.0, 0.0),
}

EXISTS = "local G=GameObject return tostring(G.GetGameObjectId('TppSahelan2','Sahelanthropus')~=G.NULL_ID)"
STATE = ("if vars.playerLife>0 then vars.playerLife=vars.playerLifeMax end "
         "local ok,y=pcall(function() return vars.playerCameraRotation[1] end) "
         "local okp,pt=pcall(function() return vars.playerCameraRotation[0] end) "
         "local okc,cp=pcall(function() return string.format('%.2f,%.2f,%.2f',vars.playerCameraPosition[0],"
         "vars.playerCameraPosition[1],vars.playerCameraPosition[2]) end) "
         "return SahelanControl.GetMechState()..'|'..tostring(ok and y or 'nil')..'|'"
         "..string.format('%.1f,%.1f,%.1f',vars.playerPosX,vars.playerPosY,vars.playerPosZ)"
         "..'|'..tostring(okp and pt or 'nil')..'|'..tostring(okc and cp or 'nil')")

def announce(ih, text):
    """Show a line in the in-game announce log (top left), best effort."""
    try:
        ih.eval(f"pcall(TppUiCommand.AnnounceLogView,'{text}') return 1", retries=0, timeout=2)
    except Exception:
        pass

def set_side(ih, n):
    """probe10c SetOffenseSide; returns the DLL status string (or n/a on older probes). Also announced in game."""
    r = ih.eval(f"if SahelanControl.SetOffenseSide then return SahelanControl.SetOffenseSide({n}) end return 'n/a'", retries=1)
    announce(ih, f"Sahelan side {n}: {SIDE_NAME.get(n, n)}")
    return r

def set_groups(ih, n):
    """probe10e SetOffenseGroups (-1 = original 0x11); announced in game."""
    r = ih.eval(f"if SahelanControl.SetOffenseGroups then return SahelanControl.SetOffenseGroups({n}) end return 'n/a'", retries=1)
    announce(ih, f"Sahelan target groups {'original' if n < 0 else hex(n)}")
    return r

def aim_point(parts, player, yr, dist, pitch_sign):
    """Point the camera looks at: ray from the camera position along yaw+pitch, cut at the player's ground."""
    pitch = 0.0
    if len(parts) > 3 and parts[3] not in ("nil", ""):
        pitch = math.radians(float(parts[3])) * pitch_sign
    origin = (player[0], player[1] + 1.6, player[2])
    if len(parts) > 4 and parts[4] not in ("nil", ""):
        try:
            origin = tuple(map(float, parts[4].split(",")))
        except ValueError:
            pass
    # positive pitch = looking down (right-handed, Y up, yaw 0 = +Z)
    d = (math.cos(pitch) * math.sin(yr), -math.sin(pitch), math.cos(pitch) * math.cos(yr))
    t = dist
    if d[1] < -1e-3:
        t = min(t, (origin[1] - player[1]) / -d[1])   # hits the ground plane before dist
    t = max(t, 5.0)
    return (origin[0] + d[0] * t, origin[1] + d[1] * t, origin[2] + d[2] * t), math.degrees(pitch)

BEH = {0x00: "none", 0x04: "move", 0x05: "transition", 0x08: "jump", 0x09: "give-up",
       0x0E: "engage0", 0x0F: "engage1", 0x10: "engage2", 0x11: "engage3", 0x12: "engage4",
       0x1C: "post-jump", 0x24: "fire-loop"}

def game_in_front():
    buf = ctypes.create_unicode_buffer(256)
    user32.GetWindowTextW(user32.GetForegroundWindow(), buf, 256)
    return "METAL GEAR SOLID V" in buf.value.upper()

def down(vk):
    return bool(user32.GetAsyncKeyState(vk) & 0x8000)

def set_eye(ih, p_name):
    """Dual dispatch eye color to both SahelanControl and V_Framework."""
    ih.eval(f"SahelanControl.SetEyePreset('{p_name}') return 1", retries=1)
    if p_name == "disco":
        ih.eval("GameObject.SendCommand({type='TppSahelan2'}, {id='SetEyeLampDisco', enabled=true, speed=2.0, a=1.0}) return 1", retries=1)
    else:
        ih.eval("GameObject.SendCommand({type='TppSahelan2'}, {id='SetEyeLampDisco', enabled=false}) return 1", retries=1)
        if p_name in RGB_MAP:
            r, g, b = RGB_MAP[p_name]
            ih.eval(f"GameObject.SendCommand({{type='TppSahelan2'}}, {{id='SetEyeLampColor', color={{r={r},g={g},b={b},a=1.0}}, phase=-1}}) return 1", retries=1)

def main():
    ap = argparse.ArgumentParser(description="Option B Pilot Driver with Visual & Weapon Tuning (probe10b)")
    ap.add_argument("--secs", type=float, default=1800, help="Session duration in seconds (default 30 min)")
    ap.add_argument("--gate", type=int, default=23, help="Decision gate mask")
    ap.add_argument("--dist", type=float, default=14, help="Movement step distance (m)")
    ap.add_argument("--overshoot", type=float, default=10, help="Movement overshoot buffer (m)")
    ap.add_argument("--refresh", type=float, default=0.35, help="Goal refresh interval (s)")
    ap.add_argument("--aim", type=float, default=60, help="Aim point distance along camera (m)")
    ap.add_argument("--eye", default="blue", choices=EYE_PRESETS, help="Initial eye color preset")
    ap.add_argument("--phase", type=int, default=3, help="Forced phase (default 3=Combat)")
    # idle hold pins decision 0 (beats intro skip in h_H0): the walk controller never starts. Experimental only.
    ap.add_argument("--idle", action="store_true", default=False, help="Start with idle hold active (blocks decision 2, mech stands still)")
    ap.add_argument("--run", default="probe10c_manual", help="Subdirectory for screenshots and logs")
    ap.add_argument("--side", type=int, default=1, help="probe10c offense side at start (1 FRIEND, 3 both, -1 vanilla, 0 none)")
    ap.add_argument("--groups", type=lambda v: int(v, 0), default=None,
                    help="probe10e target groups at start (e.g. 0x13; -1 original); default: keep the deploy config (0x13)")
    ap.add_argument("--pitch-sign", type=float, default=1.0, help="flip to -1 if aim goes up when the camera looks down")
    a = ap.parse_args()

    shots = os.path.join(ROOT, "runs", a.run, "shots")
    os.makedirs(shots, exist_ok=True)

    def shot(tag):
        if not os.path.exists(os.path.join(ROOT, "mgsv_ctl.py")):
            return
        out = os.path.join(shots, f"{int(time.time())}_{tag}.png")
        subprocess.Popen([sys.executable, os.path.join(ROOT, "mgsv_ctl.py"), "shot", out, "--scale", "0.4"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print(f"  [screenshot: {os.path.basename(out)}]", flush=True)

    print("================================================================================")
    print(" Sahelanthropus Option B Pilot Console (probe10b)")
    print(" Direct Steering + Phase Locking + Target Protection + 7 Weapons + Visual Tuning")
    print("================================================================================")
    print("Connecting to IHHook named pipe...")

    with IHPipe() as ih:
        info = ih.eval("return SahelanControl.GetInfo()", retries=2)
        print("SahelanControl info:", info, flush=True)
        if "probe10" not in info:
            print("WARNING: probe10 signature not confirmed in banner. (Loaded: " + str(info) + ")")

        # 1. Protect Player from Targeting (Divert Target to Aim Point / Decoy)
        ih.eval("SahelanControl.SetDivertTarget(true) return 1", retries=1)
        print("Player Targeting Protection: ACTIVE (SahelanAi_AcquirePlayerTarget diverted)")

        # 2. Set Eye Color Preset (Dual Dispatch)
        set_eye(ih, a.eye)
        print(f"Eye Lamp initialized: preset '{a.eye}' (dual dispatched)")

        # 3. Set Phase Lock
        phase_res = ih.eval(f"return SahelanControl.ForcePhase({a.phase})", retries=1)
        print(f"Phase Lock initialized: phase {a.phase} ({phase_res})")

        # 4. Set IdleHold for smooth steering (eliminates AI combat thrashing)
        idle = bool(a.idle)
        ih.eval(f"SahelanControl.SetIdleHold({'true' if idle else 'false'}) return 1", retries=1)
        print(f"IdleHold: {'ACTIVE (WARNING: blocks decision 2, mech will not walk)' if idle else 'INACTIVE (intro skip 0 -> 2)'}")

        # 4b. probe10c: offense side (soldier damage)
        side = a.side
        print(f"Offense side: {SIDE_NAME.get(side, side)} -> {set_side(ih, side)}", flush=True)
        groups = 0x13 if a.groups is None else a.groups
        if a.groups is not None:
            print(f"Offense groups: {set_groups(ih, groups)}", flush=True)

        # 5. Verify NavWorld patch
        if "navPatch=1" not in info:
            print("WARNING: navworld=1 patch not active! Mech may stay bound to arena.")

        # Ensure player godlife during testing
        ih.eval("if vars.playerLife>0 then Player.ChangeLifeMaxValue(50000) vars.playerLife=vars.playerLifeMax end return 1", retries=1)

        # Hook auto-start
        for c in ("StartSinkLog", "StartGoalHook"):
            ih.eval(f"local ok,w=SahelanControl.{c}() return tostring(ok)", retries=1)

        # Check if Sahelanthropus exists; if not, request spawn
        if ih.eval(EXISTS, retries=1) != "true":
            print("Sahelanthropus not in world. Requesting battlefield spawn...")
            ih.eval("SahelanControl.PilotMark('spawn_request') return 1", retries=1)
            ih.eval("InfBossTppSahelan2.WarpToBattlefield() return 1", retries=1)
            time.sleep(3)
            if ih.eval(EXISTS, retries=1) != "true":
                ih.eval("InfBossTppSahelan2.StartEventNow() return 1", retries=1)

        print("Waiting for Sahelanthropus GameObject to initialize...")
        wait_t0 = time.monotonic()
        while ih.eval(EXISTS, retries=1) != "true":
            try:
                ih.eval("if vars.playerLife>0 then vars.playerLife=vars.playerLifeMax end return 1", retries=1)
            except Exception:
                pass
            time.sleep(1.5)
            if time.monotonic() - wait_t0 > 45:
                print("Timed out waiting for Sahelanthropus. Ensure you are in free roam mission 30010 (Afghan) or 30020 (Africa).")
                break

        ih.eval("SahelanControl.PilotMark('mech_exists') return 1", retries=1)
        gate_on = a.gate
        print("Arming Decision Gate:", ih.eval(f"return SahelanControl.SetGate({gate_on})", retries=1), flush=True)

        print("\n" + "="*80)
        print(" PILOT CONTROLS READY (Ensure MGSV window is in focus):")
        print("   STEERING   : Numpad 8 (fwd), 2 (back), 4 (left), 6 (right) [FIXED]")
        print("   STANCE     : Numpad 5 (hold position), Numpad 0 (release to AI)")
        print("   JUMP       : Numpad + (direct 40m leap)")
        print("   WEAPONS    : Numpad 7=Sword | 9=Gatling | 1=Flamethrower | .=Missiles")
        print("                Numpad 3=Grenades | Numpad -=Stomp | PageUp=Sword 2")
        print("   EYE LAMPS  : Numpad / = Cycle Presets | F6=Red | F7=Blue | F8=Disco | F9=White")
        print("   PHASE LOCK : Insert = Toggle Combat Phase 3 Lock")
        print("   IDLE HOLD  : Delete = Toggle Idle Hold (body freeze for clean driving)")
        print("   SIDE       : F10 = cycle offense side 1 FRIEND -> 3 both -> -1 vanilla -> 0 none (probe10c)")
        print("   PANIC      : Ctrl + Shift + End (abort control immediately)")
        print("="*80 + "\n")

        eye_idx = EYE_PRESETS.index(a.eye) if a.eye in EYE_PRESETS else 0
        phase_locked = True
        held_at_spawn = False
        fire_last = {}
        seen = set()
        marks = 0
        prev = {}
        sent = None
        piloting = False
        last_send = last_aim = last_print = last_wait = last_off = 0.0
        fails = 0
        last_progress, last_pos = time.monotonic(), None
        end = time.monotonic() + a.secs

        try:
            while time.monotonic() < end:
                t0 = time.monotonic()
                try:

                    # Check panic combo (Ctrl + Shift + End)
                    if down(0x11) and down(0x10) and down(0x23): # Ctrl + Shift + End
                        print("\n[PANIC TRIGGERED] Aborting pilot control...")
                        break

                    st_eval = ih.eval(STATE, retries=1, timeout=3)
                    fails = 0
                    parts = (st_eval or "").split("|")
                    v = parts[0].split(",") if len(parts) >= 3 else []
                    if len(v) < 3:
                        # GetMechState() == "none": AI root not attached yet (still in decision 0 / no SetMoveGoal)
                        if time.monotonic() - last_wait > 3.0:
                            last_wait = time.monotonic()
                            print(f"  [waiting for mech AI root: {parts[0] if parts else st_eval!r}]", flush=True)
                        time.sleep(0.3)
                        continue

                    ms, yaw, ppos = parts[0], parts[1], parts[2]

                    mech = tuple(map(float, v[:3]))
                    if len(v) > 8 and int(v[8]) > 0:
                        print(f"DLL reported {v[8]} fault(s). Halting.", flush=True)
                        break

                    beh = int(v[10]) if len(v) > 10 else -1
                    if beh not in seen and beh >= 0:
                        seen.add(beh)
                        shot(f"beh{beh:02X}_{BEH.get(beh, 'unknown')}")

                    yr = math.radians(float(yaw)) if yaw not in ("nil", "") else 0.0
                    fwd = (math.sin(yr), math.cos(yr))
                    player = tuple(map(float, ppos.split(",")))
                    cam, pitch_deg = aim_point(parts, player, yr, a.aim, a.pitch_sign)

                    # Hold at spawn initially
                    if not held_at_spawn and gate_on:
                        ih.eval(f"SahelanControl.SetAimPoint({cam[0]:.1f},{cam[1]:.1f},{cam[2]:.1f}) "
                                f"SahelanControl.SetPilotGoal({mech[0]},{mech[1]},{mech[2]},105) return 1", retries=1)
                        held_at_spawn, sent, piloting = True, mech, True
                        print(f"Spawn anchored at ({mech[0]:.1f}, {mech[1]:.1f}, {mech[2]:.1f})", flush=True)

                    if gate_on & 4 and time.monotonic() - last_aim > 0.25:
                        last_aim = time.monotonic()
                        ih.eval(f"SahelanControl.SetAimPoint({cam[0]:.1f},{cam[1]:.1f},{cam[2]:.1f}) return 1", retries=1)

                    if last_pos is None or math.dist(mech, last_pos) > 1:
                        last_pos, last_progress = mech, time.monotonic()

                    fx = fz = 0
                    if game_in_front():
                        edge = {k: down(VK[k]) for k in VK}

                        # Mark / Shot
                        if edge["mark"] and not prev.get("mark"):
                            marks += 1
                            ih.eval(f"SahelanControl.PilotMark('mark{marks}') return 1", retries=1)
                            shot(f"mark{marks}")

                        # Jump
                        if edge["jump"] and not prev.get("jump"):
                            land = (mech[0] + fwd[0] * 40, mech[1], mech[2] + fwd[1] * 40)
                            res = ih.eval(f"return SahelanControl.PilotJumpNow({land[0]:.1f},{land[1]:.1f},{land[2]:.1f})", retries=1)
                            print(f"-> Jump command: {res}", flush=True)

                        # Weapon Firing (Slots 1-7)
                        for slot, keys in FIRE.items():
                            if any(down(k) for k in keys):
                                first = not prev.get(f"f{slot}")
                                edge[f"f{slot}"] = True
                                if first or time.monotonic() - fire_last.get(slot, 0) > 0.25:
                                    fire_last[slot] = time.monotonic()
                                    r = ih.eval(f"return SahelanControl.PilotFire({slot},450,{cam[0]:.1f},{cam[1]:.1f},{cam[2]:.1f})", retries=1)
                                    if first:
                                        print(f"-> Fire {SLOT_NAME[slot]}: {r}", flush=True)

                        # Eye Color Preset cycle (Numpad /)
                        if edge["eye_cycle"] and not prev.get("eye_cycle"):
                            eye_idx = (eye_idx + 1) % len(EYE_PRESETS)
                            p_name = EYE_PRESETS[eye_idx]
                            set_eye(ih, p_name)
                            print(f"-> Eye Preset cycled: [{p_name.upper()}]", flush=True)

                        # Dedicated Eye Preset keys (F6, F7, F8, F9)
                        if edge["f6_red"] and not prev.get("f6_red"):
                            set_eye(ih, 'red')
                            print("-> Eye Preset: RED", flush=True)
                        if edge["f7_blue"] and not prev.get("f7_blue"):
                            set_eye(ih, 'blue')
                            print("-> Eye Preset: BLUE", flush=True)
                        if edge["f8_disco"] and not prev.get("f8_disco"):
                            set_eye(ih, 'disco')
                            print("-> Eye Preset: DISCO RAINBOW", flush=True)
                        if edge["f9_white"] and not prev.get("f9_white"):
                            set_eye(ih, 'white')
                            print("-> Eye Preset: WHITE SEARCHLIGHT", flush=True)

                        # Phase Lock toggle (Insert)
                        if edge["phase_lock"] and not prev.get("phase_lock"):
                            phase_locked = not phase_locked
                            ph = 3 if phase_locked else -1
                            res = ih.eval(f"return SahelanControl.ForcePhase({ph})", retries=1)
                            print(f"-> Combat Phase Lock: {'LOCKED (Phase 3 Combat)' if phase_locked else 'RELEASED (Vanilla AI)'} [{res}]", flush=True)

                        # Idle Hold toggle (Delete)
                        if edge["idle_hold"] and not prev.get("idle_hold"):
                            idle = not idle
                            ih.eval(f"SahelanControl.SetIdleHold({'true' if idle else 'false'}) return 1", retries=1)
                            print("-> Idle Hold (smooth steering):", "ON" if idle else "OFF", flush=True)

                        # Offense side cycle (F10, probe10c)
                        if edge["side"] and not prev.get("side"):
                            side = SIDES[(SIDES.index(side) + 1) % len(SIDES)] if side in SIDES else SIDES[0]
                            print(f"-> Offense side: {SIDE_NAME.get(side, side)} [{set_side(ih, side)}]", flush=True)

                        # Offense target groups cycle (F11, probe10e)
                        if edge["groups"] and not prev.get("groups"):
                            groups = GROUPS[(GROUPS.index(groups) + 1) % len(GROUPS)] if groups in GROUPS else GROUPS[0]
                            print(f"-> Offense groups: {groups if groups < 0 else hex(groups)} [{set_groups(ih, groups)}]", flush=True)

                        # Release to vanilla AI (Numpad 0)
                        if down(VK["release"]) and piloting:
                            ih.eval("SahelanControl.ClearPilotGoal() SahelanControl.SetGate(0) return 1", retries=1)
                            piloting, sent, gate_on = False, None, 0
                            print("-> Control released to AI (gate off)", flush=True)

                        # Resume steering from released state if arrow keys pressed
                        if not gate_on and any(down(VK[k]) for k in ("fwd", "back", "left", "right", "hold")):
                            gate_on = a.gate
                            ih.eval(f"SahelanControl.SetGate({gate_on}) return 1", retries=1)
                            print(f"-> Gate re-armed ({gate_on})", flush=True)

                        # Stance hold (Numpad 5)
                        if down(VK["hold"]) and (sent is None or math.dist(mech, sent) > 2):
                            ih.eval(f"SahelanControl.SetPilotGoal({mech[0]},{mech[1]},{mech[2]},105) return 1", retries=1)
                            sent, piloting = mech, True
                            print(f"-> Position hold at ({mech[0]:.1f}, {mech[2]:.1f})", flush=True)

                        prev = edge
                        # FIXED: Inverted Left/Right camera steering mapping
                        fx = (1 if down(VK["left"]) else 0) - (1 if down(VK["right"]) else 0)
                        fz = (1 if down(VK["fwd"]) else 0) - (1 if down(VK["back"]) else 0)

                    # Steering movement vector calculation
                    if fx or fz:
                        n = math.hypot(fx, fz)
                        dx = (fz * math.sin(yr) + fx * math.cos(yr)) / n
                        dz = (fz * math.cos(yr) - fx * math.sin(yr)) / n
                        reach = a.dist + a.overshoot
                        goal = (mech[0] + dx * reach, mech[1], mech[2] + dz * reach)
                        stuck = time.monotonic() - last_progress > 1.0
                        if sent is None or math.dist(goal, sent) > 2 or stuck or time.monotonic() - last_send > a.refresh:
                            last_send = last_progress = time.monotonic()
                            ih.eval(f"SahelanControl.SetPilotGoal({goal[0]:.2f},{goal[1]:.2f},{goal[2]:.2f},105) return 1", retries=1)
                            sent, piloting = goal, True

                    # Periodic status printout
                    if time.monotonic() - last_print > 1.2:
                        last_print = time.monotonic()
                        p_info = ih.eval("return SahelanControl.GetPhase()", retries=1)
                        eye_stat = ih.eval("return SahelanControl.GetEyeStatus()", retries=1)
                        print(f"Mech=({mech[0]:.1f},{mech[2]:.1f}) Beh={BEH.get(beh, beh)} Phase=[{p_info}] Eye=[{eye_stat}] "
                              f"Aim=({cam[0]:.0f},{cam[1]:.0f},{cam[2]:.0f}) pitch={pitch_deg:.0f}", flush=True)
                        if time.monotonic() - last_off > 6.0:
                            last_off = time.monotonic()
                            print("  offense:", ih.eval("if SahelanControl.GetOffenseInfo then return SahelanControl.GetOffenseInfo() end return 'n/a'", retries=1), flush=True)
                            print("  bullets:", ih.eval("if SahelanControl.GetBulletHits then return SahelanControl.GetBulletHits() end return 'n/a'", retries=1), flush=True)
                            print("  soldier dmg:", ih.eval("if SahelanControl.GetDamageInfo then return SahelanControl.GetDamageInfo() end return 'n/a'", retries=1), flush=True)

                    # 5 Hz cap: ~12 Hz polling ended in "no reply for SCR778" + the DINPUT8 crash (probe10b run)
                    time.sleep(max(0.0, 0.2 - (time.monotonic() - t0)))
                except (TimeoutError, RuntimeError) as ex:
                    # one lost reply must not kill the session; give up only if the pipe is gone
                    fails += 1
                    print(f"  [pipe timeout {fails}: {ex}]", flush=True)
                    if fails >= 10:
                        print("Pipe not answering (game closed or IHHook hung). Stopping.", flush=True)
                        break
                    time.sleep(min(2.0, 0.3 * fails))

        finally:
            print("\nCleaning up pilot session...")
            for cmd in ("SahelanControl.ClearPilotGoal() SahelanControl.SetGate(0)",
                        "SahelanControl.SetIdleHold(false)",
                        "SahelanControl.SetDivertTarget(false)",
                        "SahelanControl.ForcePhase(-1)",
                        "if SahelanControl.DumpOffense then SahelanControl.DumpOffense() SahelanControl.SetOffenseSide(-1) end",
                        "SahelanControl.SetEyePreset('vanilla')"):
                try:
                    ih.eval(f"{cmd} return 1", retries=2, timeout=5)
                except Exception as e:
                    print(f"  cleanup failed: {cmd} ({e})")
            print("Session concluded.")

if __name__ == "__main__":
    main()
