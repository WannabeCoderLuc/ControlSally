// SahelanControl.dll - Option B: B1 pad lease + B2 move-goal probe
// Loaded by IHHook from MGS_TPP\plugins\. Runtime hooks only; never touches the exe on disk.
//
// Target: mgsvtpp.exe 1.0.15.4 EN (Tpp_steam_mst_en_day3900). All addresses below are absolute
// VAs assuming image base 0x140000000; they are rebased at runtime (ASLR).
//
// Sources:
//   SetLuaFunctions, FoxLuaRegisterLibrary : V_FrameWork.dll AddressSet (idx 43, 12)
//   SetupWeaponInfo                        : V_FrameWork.dll WeaponKey getter, EN 1.0.15.4 case
//   lua_*                                  : IHHook mgsvtpp_adresses_1_0_15_4_en.h
//   pad lease vtable slots                 : V_FrameWork.dll 0x1800F3740 (remote missile lease)
//   Sahelan move-request builders, aiData  : static RE, see the maintainer's research notes (OptionB_B1_Findings) "B2"

#include <windows.h>
#include <xinput.h>
#include <cstdio>
#include <share.h>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <cstdarg>
#include <intrin.h>
#include "minhook/MinHook.h"

#pragma comment(lib, "xinput9_1_0.lib")

#define SC_VERSION "SahelanControl B2 r6c-probe10b (Option B Fixes: Target Diversion + Eye Hook Chaining + Weapons)"

// ---------------------------------------------------------------- addresses
namespace Addr {
	constexpr uintptr_t ImageBase             = 0x140000000;
	constexpr uintptr_t SetLuaFunctions       = 0x1408D81F0;
	constexpr uintptr_t FoxLuaRegisterLibrary = 0x14006B8C0;
	constexpr uintptr_t SetupWeaponInfo       = 0x14105FDB0;
	constexpr uintptr_t lua_settop            = 0x141A11F70;
	constexpr uintptr_t lua_tonumber          = 0x141A121F0;
	constexpr uintptr_t lua_toboolean         = 0x141A120C0;
	constexpr uintptr_t lua_type              = 0x141A12300;
	constexpr uintptr_t lua_pushnumber        = 0x141A11950;
	constexpr uintptr_t lua_pushboolean       = 0x141A11750;
	constexpr uintptr_t lua_pushstring        = 0x141A11970;
	constexpr uintptr_t lua_tolstring         = 0x141A12150;
	constexpr uintptr_t SahelanMoveReqType3   = 0x1418F2230; // (ctx, idx): req pos = aiData+0x260, f = +0x270
	constexpr uintptr_t SahelanMoveReqType2   = 0x1418D0C00; // (ctx, idx): req pos = aiData+0x260 (stage type 1)
}

// Sahelan per-instance AI data: aiData = (*(ctx+0x78)->+0x28)->vtbl[0x58](comp, idx, 0, AiDataKey)
namespace Sahel {
	constexpr size_t   CtxRoot    = 0x78;
	constexpr size_t   RootComp   = 0x28;
	constexpr size_t   CompGetVt  = 0x58;
	constexpr uint64_t AiDataKey  = 0x47EB44EF48E3;
	constexpr size_t   RootXform  = 0x10;  // *(*(root+0x10)+0x20) + idx*16 = mech position (x,y,z,w)
	constexpr size_t   XformArr   = 0x20;
	constexpr size_t   Target     = 0x240; // target (normally player), +0x27C bit 0x20 = valid
	constexpr size_t   NodePos    = 0x250; // current route node position
	constexpr size_t   MoveGoal   = 0x260; // move goal consumed by the request builders
	constexpr size_t   MoveParam  = 0x270; // float paired with the goal
	constexpr size_t   Flags      = 0x27C;
	constexpr size_t   NodeId     = 0x218;
}

// Fox pad lease (ApplicationSystem-side object, reached via player+0x138 -> +0xB0)
namespace Pad {
	constexpr uint32_t Tag          = 0x5341484C; // 'SAHL' (V_Framework uses 'VFMS' 0x56464D53)
	constexpr size_t   ComponentTbl = 0x138;
	constexpr size_t   PadSlot      = 0xB0;
	constexpr size_t   VtLease      = 0xB0;      // (obj, tag, mask)
	constexpr size_t   VtRelease    = 0xB8;      // (obj, tag)
	constexpr size_t   VtLease2     = 0xC0;      // (obj, tag, 1)  second lease V_Framework pairs with the first
	constexpr size_t   VtRelease2   = 0xC8;      // (obj, tag)
}

// ---------------------------------------------------------------- lua types
struct lua_State;
typedef int (*lua_CFunction)(lua_State*);
struct luaL_Reg { const char* name; lua_CFunction func; };
constexpr int LUA_TNONE = -1, LUA_TNIL = 0;

typedef void   (__fastcall *lua_settop_t)(lua_State*, int);
typedef double (__fastcall *lua_tonumber_t)(lua_State*, int);
typedef int    (__fastcall *lua_toboolean_t)(lua_State*, int);
typedef int    (__fastcall *lua_type_t)(lua_State*, int);
typedef void   (__fastcall *lua_pushnumber_t)(lua_State*, double);
typedef void   (__fastcall *lua_pushboolean_t)(lua_State*, int);
typedef void   (__fastcall *lua_pushstring_t)(lua_State*, const char*);
typedef void   (__fastcall *FoxLuaRegisterLibrary_t)(lua_State*, const char*, const luaL_Reg*);
typedef void   (__fastcall *SetLuaFunctions_t)(lua_State*);
typedef void   (__fastcall *SetupWeaponInfo_t)(void*, void*, uint32_t, void*);

typedef void  (__fastcall *SahelanMoveReq_t)(void*, uint32_t);
typedef void* (__fastcall *CompGet_t)(void*, uint32_t, uint32_t, uint64_t);

typedef void (__fastcall *PadLease_t)(void*, uint32_t, uint32_t);
typedef void (__fastcall *PadRelease_t)(void*, uint32_t);

static lua_settop_t            p_settop;
static lua_tonumber_t          p_tonumber;
static lua_toboolean_t         p_toboolean;
static lua_type_t              p_type;
static lua_pushnumber_t        p_pushnumber;
static lua_pushboolean_t       p_pushboolean;
static lua_pushstring_t        p_pushstring;
static FoxLuaRegisterLibrary_t p_FoxLuaRegisterLibrary;
static void PB(lua_State* L, int v) { if (L) p_pushboolean(L, v); }
static void PS(lua_State* L, const char* v) { if (L) p_pushstring(L, v); }

static SetLuaFunctions_t o_SetLuaFunctions;
static SetupWeaponInfo_t o_SetupWeaponInfo;
static SahelanMoveReq_t  o_MoveReq3;
static SahelanMoveReq_t  o_MoveReq2;
// probe6 decision gate
static volatile LONG g_gate;                      // 1 JUMP, 2 ATTACK, 4 TARGET
static volatile LONG g_gateTypes = (1 << 4) | (1 << 14) | (1 << 15) | (1 << 18) | (1 << 26);
static volatile LONG g_jumpAllow;                 // PilotJump armed: next AI jump passes, landing overridden
static volatile DWORD g_jumpAllowUntil;           // type-3 commands pass until this tick
static float g_jumpLand[3], g_aim[3];
static volatile LONG64 g_gateDropped, g_gateSteered, g_gateTargets, g_gateJumps;

// ---------------------------------------------------------------- state
static HMODULE   g_self;
// probe6b: nav world patch state (0 = not tried, 1 = patched to "", 2 = disabled by config, -1 = bytes mismatch)
static volatile LONG g_navPatch;
static int g_cfgNavWorld = 1;
static void* volatile g_ctl;            // probe7: Sahelan ActionController (SetCommand this from Sahelan code)
static volatile LONG g_aiSeen;          // probe7: SetMoveGoal ran since the last spawn (the AI tree is live)
static volatile DWORD g_spawnTick;
static volatile LONG g_aimSet;
static int g_cfgSkipOpening = 0;
static volatile LONG g_grenadeLauncher = -1, g_grenadeAlt;   // probe8b: -1 = alternate 0/3   // probe8: superseded by the native intro skip
static volatile LONG g_pushSerial = -1; // our active PushCommand serial, -1 = none
static char* AiRoot();
static bool ReadMechState();
static void SetNavWorld(uint64_t hash, const char* why);
namespace NavWorld { constexpr uint64_t Default = 0xB8A0BF169F98, Sahelan = 0x7D629F4833BF; }
static uintptr_t g_exeBase, g_exeEnd;
static FILE*     g_log;
static CRITICAL_SECTION g_logLock;

static void* volatile g_player;     // *(SetupWeaponInfo this + 8), refreshed on every call
static void*  g_leasedObj;           // pad object we leased, null when not leased
static bool   g_leased2;
static uint32_t g_leaseMask;

// B2 probe snapshot, written by the move-request hooks (game thread), read by Lua
struct SahelSnap {
	int      type;           // 2 or 3: which builder fired last
	uint32_t idx, nodeId, flags;
	void*    ctx; void* ai;
	float    mech[4], target[4], node[4], goal[4], param;
	uint32_t calls2, calls3, overrides;
	DWORD    tick;
};
static CRITICAL_SECTION g_snapLock;
static SahelSnap g_snap;
static volatile bool g_goalOn;

// r4: background aiData tracer. Read-only sampling from our own thread, no game calls, so it can't
// change game state; worst case on a race is a torn sample. ctx/ai come from the last move request.
namespace Trace {
	constexpr size_t   Bytes   = 0x400;              // aiData bytes per sample
	constexpr uint32_t Magic   = 0x52544853;         // 'SHTR'
	constexpr DWORD    MaxSecs = 600;
}
#pragma pack(push, 1)
struct TraceHeader { uint32_t magic, version, bytes, periodMs; };
struct TraceSample { uint32_t tickMs; uint32_t valid; float mech[4]; uint8_t ai[Trace::Bytes]; };
#pragma pack(pop)
static volatile LONG g_traceOn;
static volatile DWORD g_traceUntil, g_tracePeriod = 100;
static volatile uint32_t g_traceCount;
static float g_goal[3];

// ---------------------------------------------------------------- log
static void Log(const char* fmt, ...) {
	if (!g_log) return;
	EnterCriticalSection(&g_logLock);
	SYSTEMTIME t; GetLocalTime(&t);
	fprintf(g_log, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
	va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a);
	fputc('\n', g_log); fflush(g_log);
	LeaveCriticalSection(&g_logLock);
}

static void OpenLog() {
	wchar_t path[MAX_PATH]{};
	GetModuleFileNameW(g_self, path, MAX_PATH);
	wchar_t* dot = wcsrchr(path, L'.');
	if (dot) wcscpy_s(dot, MAX_PATH - (dot - path), L".log");
	g_log = _wfsopen(path, L"w", _SH_DENYWR); // readers allowed while the game runs (r1 locked it)
}

// ---------------------------------------------------------------- helpers
static uintptr_t Rebase(uintptr_t va) { return va - Addr::ImageBase + g_exeBase; }

static bool Readable(const void* p, size_t n) {
	MEMORY_BASIC_INFORMATION mbi;
	if (!p || !VirtualQuery(p, &mbi, sizeof mbi)) return false;
	if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
	return (uintptr_t)p + n <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

static bool InExe(const void* p) { return (uintptr_t)p >= g_exeBase && (uintptr_t)p < g_exeEnd; }

// Walks player -> +0x138 -> +0xB0 and checks the vtable and the four slots point into the exe.
static void* GetPadObject(const char** why) {
	void* player = g_player;
	if (!player) { *why = "player not captured yet"; return nullptr; }
	if (!Readable((char*)player + Pad::ComponentTbl, 8)) { *why = "player unreadable"; return nullptr; }
	void* tbl = *(void**)((char*)player + Pad::ComponentTbl);
	if (!Readable((char*)tbl + Pad::PadSlot, 8)) { *why = "component table unreadable"; return nullptr; }
	void* obj = *(void**)((char*)tbl + Pad::PadSlot);
	if (!Readable(obj, 8)) { *why = "pad object unreadable"; return nullptr; }
	void** vt = *(void***)obj;
	if (!InExe(vt) || !Readable(vt, Pad::VtRelease2 + 8)) { *why = "pad vtable outside exe"; return nullptr; }
	static const size_t slots[] = { Pad::VtLease, Pad::VtRelease, Pad::VtLease2, Pad::VtRelease2 };
	for (size_t s : slots)
		if (!InExe(vt[s / 8])) { *why = "pad vtable slot outside exe"; return nullptr; }
	*why = "ok";
	return obj;
}

static void* VSlot(void* obj, size_t off) { return (*(void***)obj)[off / 8]; }

// ---------------------------------------------------------------- B2 probe
static void* CallCompGet(void* comp, uint32_t idx) {
	__try { return ((CompGet_t)VSlot(comp, Sahel::CompGetVt))(comp, idx, 0, Sahel::AiDataKey); }
	__except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

// Resolves aiData for (ctx, idx) with pointer checks at each step. Null if anything looks wrong.
static char* GetAiData(void* ctx, uint32_t idx) {
	if (!Readable((char*)ctx + Sahel::CtxRoot, 8)) return nullptr;
	char* root = *(char**)((char*)ctx + Sahel::CtxRoot);
	if (!Readable(root + Sahel::RootComp, 8)) return nullptr;
	void* comp = *(void**)(root + Sahel::RootComp);
	if (!Readable(comp, 8) || !InExe(*(void**)comp) || !Readable(*(void**)comp, Sahel::CompGetVt + 8)) return nullptr;
	if (!InExe(VSlot(comp, Sahel::CompGetVt))) return nullptr;
	char* ai = (char*)CallCompGet(comp, idx);
	return Readable(ai + Sahel::Target, Sahel::Flags + 4 - Sahel::Target) ? ai : nullptr;
}

static bool GetMechPos(void* ctx, uint32_t idx, float out[4]) {
	char* root = *(char**)((char*)ctx + Sahel::CtxRoot);
	if (!Readable(root + Sahel::RootXform, 8)) return false;
	char* x = *(char**)(root + Sahel::RootXform);
	if (!Readable(x + Sahel::XformArr, 8)) return false;
	char* arr = *(char**)(x + Sahel::XformArr);
	if (!Readable(arr + idx * 16, 16)) return false;
	memcpy(out, arr + idx * 16, 16);
	return true;
}

static void ProbeMoveReq(int type, void* ctx, uint32_t idx) {
	char* ai = GetAiData(ctx, idx);
	bool overridden = false;
	if (ai && g_goalOn) {
		float* g = (float*)(ai + Sahel::MoveGoal);
		g[0] = g_goal[0]; g[1] = g_goal[1]; g[2] = g_goal[2]; // keep w as the game set it
		overridden = true;
	}
	EnterCriticalSection(&g_snapLock);
	SahelSnap& s = g_snap;
	s.type = type; s.idx = idx; s.ctx = ctx; s.ai = ai; s.tick = GetTickCount();
	(type == 2 ? s.calls2 : s.calls3)++;
	if (overridden) s.overrides++;
	if (ai) {
		memcpy(s.target, ai + Sahel::Target, 16);
		memcpy(s.node, ai + Sahel::NodePos, 16);
		memcpy(s.goal, ai + Sahel::MoveGoal, 16);
		s.param = *(float*)(ai + Sahel::MoveParam);
		s.flags = *(uint16_t*)(ai + Sahel::Flags);
		s.nodeId = *(uint16_t*)(ai + Sahel::NodeId);
	}
	if (!GetMechPos(ctx, idx, s.mech)) s.mech[0] = s.mech[1] = s.mech[2] = s.mech[3] = 0;
	SahelSnap c = s;
	LeaveCriticalSection(&g_snapLock);

	static DWORD lastLog;
	DWORD now = GetTickCount();
	if (now - lastLog >= 500) {
		lastLog = now;
		Log("MoveReq%d idx=%u ctx=%p ai=%p mech=(%.1f,%.1f,%.1f) goal=(%.1f,%.1f,%.1f) f=%.2f target=(%.1f,%.1f,%.1f) node=%u(%.1f,%.1f,%.1f) flags=%04X%s",
			type, idx, ctx, ai, c.mech[0], c.mech[1], c.mech[2], c.goal[0], c.goal[1], c.goal[2], c.param,
			c.target[0], c.target[1], c.target[2], c.nodeId, c.node[0], c.node[1], c.node[2], c.flags,
			overridden ? " OVERRIDE" : "");
	}
}

typedef uint64_t (__fastcall *Fn6_t)(void*, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
static void JumpEvent6(const char* what, uintptr_t ret);
static uint64_t __fastcall h_MoveReq3(void* ctx, uint64_t idx, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
	ProbeMoveReq(3, ctx, (uint32_t)idx);
	uintptr_t ret = (uintptr_t)_ReturnAddress();
	if (g_jumpAllow) {
		__try {
			char* ai = GetAiData(ctx, (uint32_t)idx);
			if (ai) memcpy(ai + Sahel::MoveGoal, g_jumpLand, sizeof g_jumpLand);
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		InterlockedExchange(&g_jumpAllow, 0);
		g_jumpAllowUntil = GetTickCount() + 2000;
		JumpEvent6("PILOT", ret);
	}
	else if (g_gate & 1) { InterlockedIncrement64(&g_gateJumps); JumpEvent6("BLOCKED", ret); return 0; }
	else JumpEvent6("AI", ret);
	return ((Fn6_t)o_MoveReq3)(ctx, idx, a3, a4, a5, a6);
}
static void __fastcall h_MoveReq2(void* ctx, uint32_t idx) { ProbeMoveReq(2, ctx, idx); o_MoveReq2(ctx, idx); }

// ---------------------------------------------------------------- sink1 experiment (read-only observer)
// One hook on the request sink every Sahelan move-request builder calls: vtbl[0] of the controller object
// *(*(ctx+0x78)+0x38). All candidates are instantiations of ActionController<T>::SetCommand (static RE):
// ABI (this, uint32 idx, const req*) -> rax; no r9/xmm/stack inputs. The hook is only installed when the live
// object's vtbl[0] equals one of these verified entries. The detour never writes the request or game memory,
// calls the original with the same three arguments and returns its rax unchanged. Calls on other controller
// objects that share the instantiation are only counted.
namespace Sink {
	constexpr uintptr_t Verified[] = { 0x140C5A140, 0x140E926E0, 0x141368980, 0x1417A9370, 0x141819C90, 0x1419E4630, 0x140CE6900 };
	constexpr size_t   CtlFromRoot = 0x38;   // *(root+0x38) = controller object
	constexpr size_t   ReqBytes    = 0xB0;
	constexpr size_t   ReqType     = 0xAA;   // u8 request type (3 = jump to point, verified)
	constexpr size_t   ReqFlags    = 0xA8;   // u16
	constexpr uint32_t RingSize    = 256;
	// sink2b: hook the verified Sahelan controller SetCommand directly and keep calls returning into Sahelan code
	constexpr uintptr_t Target     = 0x140CE6900;
	constexpr uint8_t   Prologue[10] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10 };
	constexpr uintptr_t CallerLo   = 0x141880000, CallerHi = 0x141960000;
	constexpr DWORD    MaxLinesPerSec = 40;
}
typedef uint64_t (__fastcall *SinkFn_t)(void*, uint32_t, const uint8_t*);
struct SinkRec { DWORD tick; uint32_t idx; uint32_t callerRva; uint8_t type; uint16_t flags; void* self; uint32_t bt[4]; uint8_t raw[0xB0]; };
static SinkFn_t o_Sink;
static void* volatile g_sinkTarget;   // hooked function (rebased), null when not installed
static void* volatile g_sinkObj;      // the Sahelan controller object we filter on
static volatile LONG g_sinkOn;
static SRWLOCK g_sinkLock = SRWLOCK_INIT;
static SinkRec g_sinkRing[Sink::RingSize];
static volatile uint32_t g_sinkHead, g_sinkTail;
static volatile LONG64 g_sinkCountOurs, g_sinkCountOther, g_sinkDropped;
// probe1: last TppSahelan2ActionController (+0x20 view) seen by the SetCommand detour; memory reads only.
static void* volatile g_lastSelf;
// probe4: SEH guard. Any exception inside OUR detour code (never the game's original call) clears pilot mode and
// asks the watchdog thread to disable every hook (MH_ALL_HOOKS) outside the faulting thread.
static volatile LONG g_faulted, g_pilotModeShadow;
static volatile LONG64 g_faultCount;
static volatile LONG* volatile g_pilotModePtr;   // set once g_pilotMode exists (declared later)
static void EnsureWatchdog();
static int FaultFilter() {
	if (g_pilotModePtr) InterlockedExchange(g_pilotModePtr, 0);
	InterlockedExchange(&g_faulted, 1);
	InterlockedIncrement64(&g_faultCount);
	return EXCEPTION_EXECUTE_HANDLER;
}

static void PushEvent(const char* text);
static uint64_t __fastcall h_Sink(void* self, uint32_t idx, const uint8_t* req) {
	uintptr_t ret = (uintptr_t)_ReturnAddress();
	if (self && idx == 0 && req && ret >= Rebase(Sink::CallerLo) && ret < Rebase(Sink::CallerHi)) {
		uint8_t t0 = 0;
		__try { t0 = req[Sink::ReqType]; } __except (EXCEPTION_EXECUTE_HANDLER) {}
		if (self != g_ctl || t0 == 18) {
			bool fresh = self != g_ctl;
			g_ctl = self; g_spawnTick = GetTickCount(); InterlockedExchange(&g_aiSeen, 0);
			if (fresh) InterlockedExchange(&g_pushSerial, -1);
			char ev[96]; sprintf_s(ev, "%lu SPAWN ctl=%p type=%u%s", GetTickCount(), self, t0, fresh ? " new" : "");
			PushEvent(ev); Log("%s", ev);
		}
	}
	if (g_gate && g_aiSeen && req && ret >= Rebase(Sink::CallerLo) && ret < Rebase(Sink::CallerHi)) {
		uint8_t t = 0;
		__try { t = req[Sink::ReqType]; } __except (EXCEPTION_EXECUTE_HANDLER) {}
		// type 3 from the walk module (MoveAi path-link jumps, return address in the chunk 0x1418D21A6..0x1418D2222)
		// is only dropped with gate bit 8; combat jumps are stopped earlier, at builder A (h_MoveReq3, gate bit 1).
		bool pathJump = ret >= Rebase(0x1418D21A6) && ret < Rebase(0x1418D2222);
		bool drop = ((g_gate & 2) && t < 32 && t != 3 && (g_gateTypes >> t & 1)) ||
			((g_gate & 8) && t == 3 && pathJump && GetTickCount() > g_jumpAllowUntil) ||
			((g_gate & 1) && t == 3 && !pathJump && GetTickCount() > g_jumpAllowUntil);
		if (t == 3 && !drop) {
			char ev[96]; sprintf_s(ev, "%lu JUMP cmd %s caller=+%llX", GetTickCount(), pathJump ? "PATH" : "OTHER", (unsigned long long)(ret - g_exeBase));
			PushEvent(ev);
		}
		if (drop) {
			InterlockedIncrement64(&g_gateDropped);
			char ev[96]; sprintf_s(ev, "%lu GATE drop cmd type=%u caller=+%llX", GetTickCount(), t, (unsigned long long)(ret - g_exeBase));
			PushEvent(ev);
			return 0;
		}
	}
	if (g_sinkOn && ret >= Rebase(Sink::CallerLo) && ret < Rebase(Sink::CallerHi)) {
		InterlockedIncrement64(&g_sinkCountOurs);
		SinkRec r{};
		r.tick = GetTickCount(); r.idx = idx; r.self = self;
		g_lastSelf = self;
		r.callerRva = (uint32_t)(ret - g_exeBase);
		{
			void* fr[6] = {};
			USHORT n = RtlCaptureStackBackTrace(1, 5, fr, nullptr);
			for (USHORT k = 1; k < n && k <= 4; k++) r.bt[k - 1] = (uint32_t)((uintptr_t)fr[k] - g_exeBase);
		}
		__try {
			if (req && Readable(req, Sink::ReqBytes)) {
				r.type = req[Sink::ReqType];
				r.flags = *(const uint16_t*)(req + Sink::ReqFlags);
				memcpy(r.raw, req, Sink::ReqBytes);  // whole request; layout varies by type
			}
		} __except (FaultFilter()) {}
		AcquireSRWLockExclusive(&g_sinkLock);
		if (g_sinkHead - g_sinkTail < Sink::RingSize) g_sinkRing[g_sinkHead++ % Sink::RingSize] = r;
		else InterlockedIncrement64(&g_sinkDropped);
		ReleaseSRWLockExclusive(&g_sinkLock);
	}
	else if (g_sinkOn) InterlockedIncrement64(&g_sinkCountOther);
	return o_Sink(self, idx, req);
}

static FILE* g_sinkFile;
static HANDLE g_sinkThread;
static DWORD WINAPI SinkFlushThread(LPVOID) {
	DWORD second = GetTickCount(); DWORD lines = 0;
	while (g_sinkOn) {
		Sleep(100);
		for (;;) {
			SinkRec r;
			AcquireSRWLockExclusive(&g_sinkLock);
			bool have = g_sinkTail != g_sinkHead;
			if (have) r = g_sinkRing[g_sinkTail++ % Sink::RingSize];
			ReleaseSRWLockExclusive(&g_sinkLock);
			if (!have) break;
			DWORD now = GetTickCount();
			if (now - second >= 1000) { second = now; lines = 0; }
			if (++lines > Sink::MaxLinesPerSec) { InterlockedIncrement64(&g_sinkDropped); continue; }
			if (g_sinkFile) {
				char hex[0xB0 * 2 + 1];
				for (size_t i = 0; i < Sink::ReqBytes; i++) sprintf_s(hex + i * 2, 3, "%02X", r.raw[i]);
				fprintf(g_sinkFile, "%lu idx=%u type=%u flags=%04X caller=+%X self=%p bt=+%X,+%X,+%X,+%X raw=%s\n",
					r.tick, r.idx, r.type, r.flags, r.callerRva, r.self, r.bt[0], r.bt[1], r.bt[2], r.bt[3], hex);
			}
		}
		if (g_sinkFile) fflush(g_sinkFile);
	}
	for (;;) {                            // final drain after stop
		SinkRec r;
		AcquireSRWLockExclusive(&g_sinkLock);
		bool have = g_sinkTail != g_sinkHead;
		if (have) r = g_sinkRing[g_sinkTail++ % Sink::RingSize];
		ReleaseSRWLockExclusive(&g_sinkLock);
		if (!have) break;
		if (g_sinkFile) fprintf(g_sinkFile, "%lu idx=%u type=%u flags=%04X caller=+%X (drained)\n", r.tick, r.idx, r.type, r.flags, r.callerRva);
	}
	return 0;
}

// Resolves the sink from the ctx captured by the move-request hooks; returns nullptr + reason on any doubt.
static void* SinkResolve(void** objOut, const char** why) {
	void* ctx; EnterCriticalSection(&g_snapLock); ctx = g_snap.ctx; LeaveCriticalSection(&g_snapLock);
	if (!ctx) { *why = "no ctx yet (no Sahelan move request seen since load)"; return nullptr; }
	if (!Readable((char*)ctx + Sahel::CtxRoot, 8)) { *why = "ctx unreadable"; return nullptr; }
	char* root = *(char**)((char*)ctx + Sahel::CtxRoot);
	if (!Readable(root + Sink::CtlFromRoot, 8)) { *why = "root unreadable"; return nullptr; }
	void* obj = *(void**)(root + Sink::CtlFromRoot);
	if (!Readable(obj, 8)) { *why = "controller unreadable"; return nullptr; }
	void** vt = *(void***)obj;
	if (!InExe(vt) || !Readable(vt, 8)) { *why = "controller vtable outside exe"; return nullptr; }
	uintptr_t fn = (uintptr_t)vt[0];
	for (uintptr_t v : Sink::Verified)
		if (fn == Rebase(v)) { *objOut = obj; *why = "ok"; return (void*)fn; }
	static char buf[96];
	sprintf_s(buf, "vtbl[0]=+%llX is not a verified SetCommand", (unsigned long long)(fn - g_exeBase));
	*why = buf; return nullptr;
}

// SahelanControl.StartSinkLog() -> ok, reason   (writes plugins\SahelanControl_sink.log)
static int l_StartSinkLog(lua_State* L) {
	if (g_sinkOn) { PB(L, 0); PS(L, "already running"); return 2; }
	void* obj = nullptr;  // sink2b: no object filter, callers are filtered instead
	void* fn = (void*)Rebase(Sink::Target);
	if (memcmp(fn, Sink::Prologue, sizeof Sink::Prologue) != 0) {
		const char* why = "sink prologue does not match the verified disassembly (already hooked or different build)";
		Log("Sink: refused: %s", why); PB(L, 0); PS(L, why); return 2;
	}
	if (g_sinkTarget && g_sinkTarget != fn) { PB(L, 0); PS(L, "different target than before; restart game"); return 2; }
	if (!g_sinkTarget) {
		MH_STATUS st = MH_CreateHook(fn, (void*)h_Sink, (void**)&o_Sink);
		if (st != MH_OK) { Log("Sink: MH_CreateHook failed: %s", MH_StatusToString(st)); PB(L, 0); PS(L, MH_StatusToString(st)); return 2; }
		g_sinkTarget = fn;
	}
	wchar_t path[MAX_PATH]{};
	GetModuleFileNameW(g_self, path, MAX_PATH);
	wchar_t* dot = wcsrchr(path, L'.');
	if (dot) wcscpy_s(dot, MAX_PATH - (dot - path), L"_sink.log");
	g_sinkFile = _wfsopen(path, L"w", _SH_DENYWR);
	if (g_sinkFile) fprintf(g_sinkFile, "# sink=+%llX obj=%p  fields: tickMs idx type flags caller(+RVA) raw(0xB0 hex)\n",
		(unsigned long long)((uintptr_t)fn - g_exeBase), obj);
	g_sinkObj = obj; g_sinkHead = g_sinkTail = 0;
	g_sinkCountOurs = g_sinkCountOther = g_sinkDropped = 0;
	InterlockedExchange(&g_sinkOn, 1);
	MH_STATUS st = MH_EnableHook(fn);
	if (st != MH_OK) { InterlockedExchange(&g_sinkOn, 0); Log("Sink: enable failed: %s", MH_StatusToString(st)); PB(L, 0); PS(L, MH_StatusToString(st)); return 2; }
	g_sinkThread = CreateThread(nullptr, 0, SinkFlushThread, nullptr, 0, nullptr);
	Log("Sink: started on +%llX obj=%p", (unsigned long long)((uintptr_t)fn - g_exeBase), obj);
	PB(L, 1); PS(L, "ok");
	return 2;
}

// SahelanControl.StopSinkLog(): disables the hook (one MH call), lets the flusher drain, closes the file
static int l_StopSinkLog(lua_State* L) {
	(void)L;
	if (!g_sinkOn) return 0;
	if (g_sinkTarget) MH_DisableHook(g_sinkTarget);
	Sleep(200);                          // let in-flight detour calls finish queueing
	InterlockedExchange(&g_sinkOn, 0);   // flusher drains what is queued, then exits
	if (g_sinkThread) { WaitForSingleObject(g_sinkThread, 3000); CloseHandle(g_sinkThread); g_sinkThread = nullptr; }
	if (g_sinkFile) { fprintf(g_sinkFile, "# stop ours=%lld other=%lld dropped=%lld\n", g_sinkCountOurs, g_sinkCountOther, g_sinkDropped);
		fclose(g_sinkFile); g_sinkFile = nullptr; }
	Log("Sink: stopped ours=%lld other=%lld dropped=%lld", g_sinkCountOurs, g_sinkCountOther, g_sinkDropped);
	return 0;
}

// SahelanControl.GetSinkLog() -> "on=.. ours=.. other=.. dropped=.."
static int l_GetSinkLog(lua_State* L) {
	char buf[128];
	sprintf_s(buf, "on=%ld ours=%lld other=%lld dropped=%lld", g_sinkOn, g_sinkCountOurs, g_sinkCountOther, g_sinkDropped);
	PS(L, buf);
	return 1;
}

// ---------------------------------------------------------------- r5 coverage probe
// Hooks every function listed in plugins/SahelanControl_hooks.txt ("<rva hex> <size hex> <flags>") with a
// counting thunk that leaves registers and stack untouched, so it is safe for any signature:
//   push rax; mov rax, slot; lock inc qword [rax]; mov [rax+8], rcx; mov [rax+16], rdx; pop rax; jmp [rip]; dq tramp
// A sampler thread writes per-100ms call deltas to plugins/SahelanControl_cov.bin; Lua can add marker events.
namespace Cov {
	constexpr uint32_t Magic = 0x564F4353; // 'SCOV'
	constexpr size_t   ThunkSize = 48;
	constexpr uint32_t RecCalls = 1, RecMark = 2;
}
struct CovSlot { volatile uint64_t count; uint64_t rcx, rdx, last; };
static CovSlot*   g_covSlots;
static uint8_t*   g_covThunks;
static uint32_t*  g_covRva;
static uint32_t   g_covN;
static bool*      g_covHooked;
static volatile LONG g_covOn;
static CRITICAL_SECTION g_covLock;
static FILE*      g_covFile;
static DWORD      g_covT0;

static void CovWrite(const void* p, size_t n) { if (g_covFile) fwrite(p, n, 1, g_covFile); }

static bool CovLoadList() {
	if (g_covN) return true;
	wchar_t path[MAX_PATH]{};
	GetModuleFileNameW(g_self, path, MAX_PATH);
	wchar_t* dot = wcsrchr(path, L'.');
	if (dot) wcscpy_s(dot, MAX_PATH - (dot - path), L"_hooks.txt");
	FILE* f = _wfsopen(path, L"r", _SH_DENYNO);
	if (!f) { Log("Coverage: no hook list next to the DLL"); return false; }
	static uint32_t rvas[8192]; uint32_t n = 0; char line[128];
	while (n < 8192 && fgets(line, sizeof line, f)) {
		unsigned rva = 0, size = 0;
		if (sscanf_s(line, "%x %x", &rva, &size) == 2 && rva) rvas[n++] = rva;
	}
	fclose(f);
	g_covSlots  = (CovSlot*)VirtualAlloc(nullptr, sizeof(CovSlot) * n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	// thunks, then one shared UNWIND_INFO, then n RUNTIME_FUNCTIONs, all in one block (RVA base = block start)
	size_t thunkBytes = Cov::ThunkSize * n, uwOff = thunkBytes, rfOff = (uwOff + 8 + 15) & ~(size_t)15;
	g_covThunks = (uint8_t*)VirtualAlloc(nullptr, rfOff + sizeof(RUNTIME_FUNCTION) * n, MEM_COMMIT | MEM_RESERVE,
		PAGE_EXECUTE_READWRITE);
	g_covRva    = new uint32_t[n];
	g_covHooked = new bool[n]();
	memcpy(g_covRva, rvas, n * 4);
	g_covN = n;
	if (g_covThunks) {
		// Unwind info so exception dispatch / stack walks that land inside a thunk unwind correctly:
		// prolog "push rax" (1 byte) = UWOP_ALLOC_SMALL 8. The "pop rax; jmp [rip]" tail is a valid epilogue.
		static const uint8_t uw[8] = { 0x01, 0x01, 0x01, 0x00, 0x01, 0x02, 0x00, 0x00 };
		memcpy(g_covThunks + uwOff, uw, sizeof uw);
		RUNTIME_FUNCTION* rf = (RUNTIME_FUNCTION*)(g_covThunks + rfOff);
		for (uint32_t i = 0; i < n; i++) {
			rf[i].BeginAddress = (DWORD)(i * Cov::ThunkSize);
			rf[i].EndAddress = (DWORD)(i * Cov::ThunkSize + 38);
			rf[i].UnwindData = (DWORD)uwOff;
		}
		if (!RtlAddFunctionTable(rf, n, (DWORD64)g_covThunks)) Log("Coverage: RtlAddFunctionTable failed");
	}
	Log("Coverage: %u targets in list", n);
	return g_covSlots && g_covThunks;
}

static void CovBuildThunk(uint32_t i, void* tramp) {
	uint8_t* t = g_covThunks + i * Cov::ThunkSize;
	uint64_t slot = (uint64_t)&g_covSlots[i], tr = (uint64_t)tramp;
	uint8_t code[] = {
		0x50,                                     // push rax
		0x48, 0xB8, 0,0,0,0,0,0,0,0,              // mov rax, imm64 (slot)
		0xF0, 0x48, 0xFF, 0x00,                   // lock inc qword [rax]
		0x48, 0x89, 0x48, 0x08,                   // mov [rax+8], rcx
		0x48, 0x89, 0x50, 0x10,                   // mov [rax+16], rdx
		0x58,                                     // pop rax
		0xFF, 0x25, 0,0,0,0,                      // jmp [rip+0]
		0,0,0,0,0,0,0,0                           // trampoline
	};
	memcpy(code + 3, &slot, 8);
	memcpy(code + sizeof code - 8, &tr, 8);
	memcpy(t, code, sizeof code);
}

static uint32_t CovInstall(uint32_t* failed, uint32_t* skipped) {
	uint32_t ok = 0; *failed = 0; *skipped = 0;
	for (uint32_t i = 0; i < g_covN; i++) {
		if (g_covHooked[i]) { ok++; continue; }
		uintptr_t target = g_exeBase + g_covRva[i];
		if (*(uint8_t*)target == 0xE9) { (*skipped)++; continue; } // someone else's hook (V_Framework / ours)
		CovBuildThunk(i, nullptr);
		void* tramp = nullptr;
		MH_STATUS st = MH_CreateHook((void*)target, g_covThunks + i * Cov::ThunkSize, &tramp);
		if (st != MH_OK) { (*failed)++; continue; }
		CovBuildThunk(i, tramp);
		g_covHooked[i] = true; ok++;
	}
	FlushInstructionCache(GetCurrentProcess(), g_covThunks, Cov::ThunkSize * g_covN);
	return ok;
}

static DWORD WINAPI CovThread(LPVOID) {
	uint64_t* prev = new uint64_t[g_covN]();
	for (uint32_t i = 0; i < g_covN; i++) prev[i] = g_covSlots[i].count;
	while (g_covOn) {
		Sleep(100);
		uint32_t t = GetTickCount() - g_covT0;
		EnterCriticalSection(&g_covLock);
		for (uint32_t i = 0; i < g_covN; i++) {
			uint64_t c = g_covSlots[i].count;
			if (c != prev[i]) {
				uint32_t rec[4] = { Cov::RecCalls, t, i, (uint32_t)(c - prev[i]) };
				CovWrite(rec, sizeof rec);
				uint64_t rcx = g_covSlots[i].rcx; CovWrite(&rcx, 8);
				prev[i] = c;
			}
		}
		if (g_covFile) fflush(g_covFile);
		LeaveCriticalSection(&g_covLock);
	}
	delete[] prev;
	return 0;
}

// SahelanControl.StartCoverage() -> hooked, failed, skipped (0,.. if already running or no list)
static int l_StartCoverage(lua_State* L) {
	if (g_covOn || !CovLoadList()) { p_pushnumber(L, 0); return 1; }
	uint32_t failed, skipped, ok = CovInstall(&failed, &skipped);
	wchar_t path[MAX_PATH]{};
	GetModuleFileNameW(g_self, path, MAX_PATH);
	wchar_t* dot = wcsrchr(path, L'.');
	if (dot) wcscpy_s(dot, MAX_PATH - (dot - path), L"_cov.bin");
	g_covFile = _wfsopen(path, L"wb", _SH_DENYWR);
	uint32_t hdr[3] = { Cov::Magic, 1, g_covN };
	CovWrite(hdr, sizeof hdr);
	CovWrite(g_covRva, g_covN * 4);
	g_covT0 = GetTickCount();
	MH_EnableHook(MH_ALL_HOOKS);
	InterlockedExchange(&g_covOn, 1);
	HANDLE th = CreateThread(nullptr, 0, CovThread, nullptr, 0, nullptr);
	if (th) CloseHandle(th);
	Log("Coverage: started, %u hooked, %u failed, %u skipped (already jmp)", ok, failed, skipped);
	p_pushnumber(L, ok); p_pushnumber(L, failed); p_pushnumber(L, skipped);
	return 3;
}

// SahelanControl.StopCoverage(): disables the coverage hooks (our other hooks stay on) and closes the file
static int l_StopCoverage(lua_State* L) {
	(void)L;
	if (!g_covOn) return 0;
	InterlockedExchange(&g_covOn, 0);
	// Queue + apply once: each MH_DisableHook call suspends every game thread, and doing that ~1800 times in a
	// row deadlocked the game (a suspended thread holding the heap lock). One freeze for all is safe.
	for (uint32_t i = 0; i < g_covN; i++)
		if (g_covHooked[i]) MH_QueueDisableHook((void*)(g_exeBase + g_covRva[i]));
	MH_ApplyQueued();
	Sleep(150);
	EnterCriticalSection(&g_covLock);
	if (g_covFile) { fclose(g_covFile); g_covFile = nullptr; }
	LeaveCriticalSection(&g_covLock);
	Log("Coverage: stopped");
	return 0;
}

// SahelanControl.Mark(text): timestamped marker in the coverage file (ground-truth labels)
static int l_Mark(lua_State* L) {
	if (!g_covOn) return 0;
	typedef const char* (__fastcall *tostr_t)(lua_State*, int, size_t*);
	const char* txt = ((tostr_t)Rebase(Addr::lua_tolstring))(L, 1, nullptr);
	char buf[120]{}; if (txt) strncpy_s(buf, txt, _TRUNCATE);
	uint32_t rec[3] = { Cov::RecMark, GetTickCount() - g_covT0, (uint32_t)strlen(buf) };
	EnterCriticalSection(&g_covLock);
	CovWrite(rec, sizeof rec); CovWrite(buf, rec[2]);
	LeaveCriticalSection(&g_covLock);
	return 0;
}

static DWORD WINAPI TraceThread(LPVOID) {
	wchar_t path[MAX_PATH]{};
	GetModuleFileNameW(g_self, path, MAX_PATH);
	wchar_t* dot = wcsrchr(path, L'.');
	if (dot) wcscpy_s(dot, MAX_PATH - (dot - path), L"_aitrace.bin");
	FILE* f = _wfsopen(path, L"wb", _SH_DENYWR);
	if (!f) { Log("Trace: cannot open output"); InterlockedExchange(&g_traceOn, 0); return 0; }
	TraceHeader h{ Trace::Magic, 1, (uint32_t)Trace::Bytes, g_tracePeriod };
	fwrite(&h, sizeof h, 1, f);
	Log("Trace: started, period %lu ms", g_tracePeriod);
	static TraceSample smp;
	g_traceCount = 0;
	while (g_traceOn && (LONG)(g_traceUntil - GetTickCount()) > 0) {
		void* ctx; void* ai; uint32_t idx;
		EnterCriticalSection(&g_snapLock);
		ctx = g_snap.ctx; ai = g_snap.ai; idx = g_snap.idx;
		LeaveCriticalSection(&g_snapLock);
		memset(&smp, 0, sizeof smp);
		smp.tickMs = GetTickCount();
		if (ai && Readable(ai, Trace::Bytes)) {
			memcpy(smp.ai, ai, Trace::Bytes);
			smp.valid = 1;
			if (ctx && Readable((char*)ctx + Sahel::CtxRoot, 8) && GetMechPos(ctx, idx, smp.mech)) smp.valid |= 2;
		}
		fwrite(&smp, sizeof smp, 1, f);
		g_traceCount++;
		Sleep(g_tracePeriod);
	}
	fclose(f);
	Log("Trace: stopped after %u samples", g_traceCount);
	InterlockedExchange(&g_traceOn, 0);
	return 0;
}

static void DoRelease() {
	if (!g_leasedObj) return;
	if (!Readable(g_leasedObj, 8) || !InExe(*(void**)g_leasedObj)) {
		Log("ReleasePad: leased object %p no longer valid, forgetting it", g_leasedObj);
		g_leasedObj = nullptr; g_leased2 = false; g_leaseMask = 0;
		return;
	}
	((PadRelease_t)VSlot(g_leasedObj, Pad::VtRelease))(g_leasedObj, Pad::Tag);
	if (g_leased2) ((PadRelease_t)VSlot(g_leasedObj, Pad::VtRelease2))(g_leasedObj, Pad::Tag);
	Log("ReleasePad obj=%p second=%d", g_leasedObj, g_leased2);
	g_leasedObj = nullptr; g_leased2 = false; g_leaseMask = 0;
}

// ---------------------------------------------------------------- lua api
static double OptNumber(lua_State* L, int i, double def) {
	int t = p_type(L, i);
	return (t == LUA_TNONE || t == LUA_TNIL) ? def : p_tonumber(L, i);
}


// ---------------------------------------------------------------- probe1 (read-only memory inspection)
static uint64_t RvaOrAbs(uint64_t v) { return InExe((void*)v) ? v - g_exeBase + Addr::ImageBase : v; }

// SahelanControl.ReadHex(addr, len<=200) -> hex string or "unreadable"
static int l_ReadHex(lua_State* L) {
	uintptr_t a = (uintptr_t)(uint64_t)p_tonumber(L, 1);
	int n = (int)p_tonumber(L, 2);
	if (n <= 0 || n > 200) n = 64;
	char buf[2 * 200 + 1] = {};
	if (!Readable((void*)a, n)) { PS(L, "unreadable"); return 1; }
	for (int i = 0; i < n; i++) sprintf_s(buf + 2 * i, 3, "%02X", ((const uint8_t*)a)[i]);
	PS(L, buf);
	return 1;
}

// SahelanControl.ReadQ(addr) -> number (qword as double; pointers < 2^53 are exact) or nil
static int l_ReadQ(lua_State* L) {
	uintptr_t a = (uintptr_t)(uint64_t)p_tonumber(L, 1);
	if (!Readable((void*)a, 8)) return 0;
	p_pushnumber(L, (double)*(const uint64_t*)a);
	return 1;
}

// SahelanControl.GetExeBase() -> number
static int l_GetExeBase(lua_State* L) { p_pushnumber(L, (double)g_exeBase); return 1; }

// SahelanControl.ProbeExecutor() -> string: controller -> TppSahelan2 -> executor chain, vtables as static VAs
static int l_ProbeExecutor(lua_State* L) {
	char buf[480]; int o = 0;
	char* self = (char*)g_lastSelf;
	auto q = [](const void* p) -> uint64_t { return Readable(p, 8) ? *(const uint64_t*)p : 0; };
	if (!self) { PS(L, "no controller seen yet (StartSinkLog first)"); return 1; }
	uint64_t ctlVt = q(self - 0x20), p58 = q(self + 0x38);
	uint64_t exec = p58 ? q((char*)p58 + 0x18) : 0, execVt = exec ? q((void*)exec) : 0;
	uint64_t s2 = p58 ? p58 - 0x18 : 0, s2Vt = s2 ? q((void*)s2) : 0;
	o += sprintf_s(buf + o, sizeof buf - o, "self=%p ctlVt=%llx p58=%llx s2=%llx s2Vt=%llx exec=%llx execVt=%llx",
		self, RvaOrAbs(ctlVt), p58, s2, RvaOrAbs(s2Vt), exec, RvaOrAbs(execVt));
	if (execVt && Readable((void*)execVt, 0x40))
		for (int i = 0; i < 8; i++) o += sprintf_s(buf + o, sizeof buf - o, " v%02X=%llx", i * 8, RvaOrAbs(((uint64_t*)execVt)[i]));
	PS(L, buf);
	return 1;
}


// ---------------------------------------------------------------- probe2: RequestMove logger (pass-through)
// The TppSahelan2 movement executor (*(TppSahelan2+0x30), vtable 0x1423A1610) plans navmesh paths in
// vtbl[0] = 0x140FA2430 (this, uint32 idx, const MoveRequest* 0xA0 bytes) -> void. The detour copies the request,
// records the return address, and calls the original with the same arguments. Only calls on one object are kept.
namespace Move {
	constexpr uintptr_t Target = 0x140FA2430;
	constexpr uint8_t   Prologue[10] = { 0x40, 0x55, 0x53, 0x56, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xC1 };
	constexpr size_t    ReqBytes = 0xA0;
	constexpr uint32_t  RingSize = 256;
}
typedef void (__fastcall *MoveFn_t)(void*, uint32_t, const uint8_t*);
struct MoveRec { DWORD tick; uint32_t idx; uint32_t callerRva; uint32_t caller2Rva; uint8_t raw[Move::ReqBytes]; };
static MoveFn_t o_Move;
// probe3 pilot goal. Mode bit 1 = A (upstream: rewrite goal record +0x40 after SahelanAi_SetMoveGoal),
// bit 2 = B (downstream: RequestMove gets a copy of the request with goal +0x10 replaced).
static volatile LONG g_pilotMode;
static float g_pilot[3];
static volatile LONG64 g_pilotA, g_pilotB;
static volatile LONG64 g_phaseCalls, g_phase3, g_replans;
static volatile LONG64 g_jumpA, g_jumpB, g_jumpBlocked;
static volatile LONG g_pilotSeq;           // bumped by every SetPilotGoal; UpdatePhase replans when it changes
static void* volatile g_moveTarget;
static void* volatile g_moveSelf;
static volatile LONG g_moveOn;
static SRWLOCK g_moveLock = SRWLOCK_INIT;
static MoveRec g_moveRing[Move::RingSize];
static volatile uint32_t g_moveHead, g_moveTail;
static volatile LONG64 g_moveCount, g_moveOther, g_moveDropped;
static FILE* g_moveFile;
static HANDLE g_moveThread;

static void* volatile g_sahelExec;     // probe6c: Sahelan TppNavigationController (root + 0x18)
static void* volatile g_sahelPlanner;  // *(exec + 0x30)
alignas(16) static uint8_t g_moveTpl[Move::ReqBytes];
static volatile LONG g_haveTpl;
static void __fastcall h_Move(void* self, uint32_t idx, const uint8_t* req) {
	alignas(16) uint8_t copy[Move::ReqBytes];
	const uint8_t* pass = req;
	if (self && self == g_sahelExec && idx == 0) {
		__try { if (req && Readable(req, Move::ReqBytes)) { memcpy(g_moveTpl, req, Move::ReqBytes); InterlockedExchange(&g_haveTpl, 1); } }
		__except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	__try {
		if ((g_pilotMode & 2) && self == g_moveSelf && req && Readable(req, Move::ReqBytes)) {
			memcpy(copy, req, Move::ReqBytes);
			memcpy(copy + 0x10, g_pilot, sizeof g_pilot);   // goal vec4 xyz; w kept
			pass = copy;
			InterlockedIncrement64(&g_pilotB);
		}
	} __except (FaultFilter()) { pass = req; }
	if (g_moveOn && self == g_moveSelf) {
		InterlockedIncrement64(&g_moveCount);
		MoveRec r{};
		r.tick = GetTickCount(); r.idx = idx;
		r.callerRva = (uint32_t)((uintptr_t)_ReturnAddress() - g_exeBase);
		void* frames[3] = {};
		if (RtlCaptureStackBackTrace(1, 2, frames, nullptr) >= 2) r.caller2Rva = (uint32_t)((uintptr_t)frames[1] - g_exeBase);
		__try { if (req && Readable(req, Move::ReqBytes)) memcpy(r.raw, req, Move::ReqBytes); } __except (FaultFilter()) {}
		AcquireSRWLockExclusive(&g_moveLock);
		if (g_moveHead - g_moveTail < Move::RingSize) g_moveRing[g_moveHead++ % Move::RingSize] = r;
		else InterlockedIncrement64(&g_moveDropped);
		ReleaseSRWLockExclusive(&g_moveLock);
	}
	else if (g_moveOn) InterlockedIncrement64(&g_moveOther);
	o_Move(self, idx, pass);
}

static DWORD WINAPI MoveFlushThread(LPVOID) {
	for (;;) {
		bool on = g_moveOn != 0;
		for (;;) {
			MoveRec r;
			AcquireSRWLockExclusive(&g_moveLock);
			bool have = g_moveTail != g_moveHead;
			if (have) r = g_moveRing[g_moveTail++ % Move::RingSize];
			ReleaseSRWLockExclusive(&g_moveLock);
			if (!have) break;
			if (g_moveFile) {
				char hex[Move::ReqBytes * 2 + 1];
				for (size_t i = 0; i < Move::ReqBytes; i++) sprintf_s(hex + i * 2, 3, "%02X", r.raw[i]);
				fprintf(g_moveFile, "%lu idx=%u caller=+%X caller2=+%X raw=%s\n", r.tick, r.idx, r.callerRva, r.caller2Rva, hex);
			}
		}
		if (g_moveFile) fflush(g_moveFile);
		if (!on) break;
		Sleep(100);
	}
	return 0;
}

// SahelanControl.StartMoveLog(execAddr) -> ok, reason   (writes plugins\SahelanControl_move.log)
static int l_StartMoveLog(lua_State* L) {
	if (g_moveOn) { PB(L, 0); PS(L, "already running"); return 2; }
	void* self = (void*)(uintptr_t)(uint64_t)p_tonumber(L, 1);
	if (!Readable(self, 8) || *(uintptr_t*)self != Rebase(0x1423A1610)) { PB(L, 0); PS(L, "object is not the movement executor (vtable mismatch)"); return 2; }
	void* fn = (void*)Rebase(Move::Target);
	if (!g_moveTarget) {
		if (memcmp(fn, Move::Prologue, sizeof Move::Prologue) != 0) { PB(L, 0); PS(L, "RequestMove prologue mismatch"); return 2; }
		MH_STATUS st = MH_CreateHook(fn, (void*)h_Move, (void**)&o_Move);
		if (st != MH_OK) { PB(L, 0); PS(L, MH_StatusToString(st)); return 2; }
		g_moveTarget = fn;
	}
	wchar_t path[MAX_PATH]{};
	GetModuleFileNameW(g_self, path, MAX_PATH);
	wchar_t* dot = wcsrchr(path, L'.');
	if (dot) wcscpy_s(dot, MAX_PATH - (dot - path), L"_move.log");
	g_moveFile = _wfsopen(path, L"w", _SH_DENYWR);
	if (g_moveFile) fprintf(g_moveFile, "# RequestMove +%llX self=%p  fields: tickMs idx caller caller2 raw(0xA0)\n",
		(unsigned long long)((uintptr_t)fn - g_exeBase), self);
	g_moveSelf = self; g_moveHead = g_moveTail = 0; g_moveCount = g_moveOther = g_moveDropped = 0;
	InterlockedExchange(&g_moveOn, 1);
	MH_STATUS st = MH_EnableHook(fn);
	if (st != MH_OK) { InterlockedExchange(&g_moveOn, 0); PB(L, 0); PS(L, MH_StatusToString(st)); return 2; }
	g_moveThread = CreateThread(nullptr, 0, MoveFlushThread, nullptr, 0, nullptr);
	Log("Move: started self=%p", self);
	PB(L, 1); PS(L, "ok");
	return 2;
}

// SahelanControl.StopMoveLog() -> "count=.. other=.. dropped=.."
static int l_StopMoveLog(lua_State* L) {
	InterlockedExchange(&g_pilotMode, g_pilotMode & ~2);
	if (g_moveOn) {
		if (g_moveTarget) MH_DisableHook(g_moveTarget);
		Sleep(200);
		InterlockedExchange(&g_moveOn, 0);
		if (g_moveThread) { WaitForSingleObject(g_moveThread, 3000); CloseHandle(g_moveThread); g_moveThread = nullptr; }
		if (g_moveFile) { fclose(g_moveFile); g_moveFile = nullptr; }
	}
	char buf[128];
	sprintf_s(buf, "count=%lld other=%lld dropped=%lld", g_moveCount, g_moveOther, g_moveDropped);
	Log("Move: stopped %s", buf);
	PS(L, buf);
	return 1;
}


// ---------------------------------------------------------------- probe3 Mode A: SahelanAi_SetMoveGoal post-hook
// 0x1418FEA90 (this rcx, idx edx, kind r8d, r9d byte, [rsp+0x28] ptr) -> al bool (verified from the caller at
// 0x1418FDFF4: sets r9d and a stack arg, then tests al). probe3 v1 declared it void with 3 args: broke the AI.
// The detour forwards six integer slots and returns rax unchanged. Writes goal record (*(*(this+0x70)+0x58))+8 + idx*0x130:
// +0x40 goal vec4, +0x60 heading, +0x68 mode 2. After the original returns we replace +0x40..+0x48 with the pilot
// point (only when idx < record count at +0x10 and the record was just set to mode 2).
namespace Goal {
	constexpr uintptr_t Target = 0x1418FEA90;
	constexpr uint8_t   Prologue[10] = { 0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x56 };
}
typedef uint64_t (__fastcall *GoalFn_t)(void*, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
static GoalFn_t o_Goal;
static void* volatile g_goalTarget;
static volatile LONG64 g_goalCalls;
static float g_lastAiGoal[3];

static float* GoalRecord(void* self, uint32_t idx) {
	char* rootp = (char*)self + 0x70;
	if (!Readable(rootp, 8)) return nullptr;
	char* r = *(char**)rootp;
	if (!r || !Readable(r + 0x58, 8)) return nullptr;
	char* holder = *(char**)(r + 0x58);
	if (!holder || !Readable(holder + 8, 0x10)) return nullptr;
	if (idx >= *(uint32_t*)(holder + 0x10)) return nullptr;
	char* rec = *(char**)(holder + 8) + (size_t)idx * 0x130;
	return Readable(rec, 0x130) ? (float*)rec : nullptr;
}

static volatile LONG64 g_goalOk, g_forced;
static void GatePre(void* self, uint32_t idx, uint8_t* bb) {
	if (!g_gate || idx != 0) return;
	__try {
		if (bb && Readable(bb, 16)) {
			uint8_t n = bb[8];
			bool atk = (n >= 0x0E && n <= 0x12) || n == 0x24;
			if (((g_gate & 2) && atk) || ((g_gate & 1) && n == 0x08 && !g_jumpAllow)) { bb[8] = 0x04; InterlockedIncrement64(&g_gateSteered); }
		}
		if ((g_gate & 4) && g_aimSet) {
			char* r = Readable((char*)self + 0x70, 8) ? *(char**)((char*)self + 0x70) : nullptr;
			if (r && Readable(r + Sahel::RootComp, 8)) {
				void* comp = *(void**)(r + Sahel::RootComp);
				if (comp && Readable(comp, 8) && InExe(*(void**)comp) && InExe(VSlot(comp, Sahel::CompGetVt))) {
					char* ai = (char*)CallCompGet(comp, idx);
					if (ai && Readable(ai + Sahel::Target, 16)) memcpy(ai + Sahel::Target, g_aim, sizeof g_aim);
				}
			}
			char* inst = Readable((char*)self + 0x78, 8) ? *(char**)((char*)self + 0x78) : nullptr;
			if (inst && Readable(inst + 0x140, 16)) memcpy(inst + 0x140, g_aim, sizeof g_aim);
			InterlockedIncrement64(&g_gateTargets);
		}
	} __except (FaultFilter()) {}
}
static LONG g_forcedSeq = -1;
static void* volatile g_goalSelf;   // SetMoveGoal this (walk AI); +0x70 = AI root
static volatile uint64_t g_aiStatePtr;   // SetMoveGoal 5th arg (per-instance AI state struct)
static uint64_t __fastcall h_Goal(void* self, uint64_t idx, uint64_t kind, uint64_t a4, uint64_t a5, uint64_t a6) {
	InterlockedIncrement64(&g_goalCalls);
	if ((uint32_t)idx == 0) { g_aiStatePtr = a5; g_goalSelf = self; }
	if ((uint32_t)idx == 0 && !g_aiSeen) {
		InterlockedExchange(&g_aiSeen, 1);
		char ev[96]; sprintf_s(ev, "%lu AI live %lu ms after spawn", GetTickCount(), GetTickCount() - g_spawnTick);
		PushEvent(ev); Log("%s", ev);
	}
	GatePre(self, (uint32_t)idx, (uint8_t*)a5);
	uint64_t ret = o_Goal(self, idx, kind, a4, a5, a6);
	GatePre(self, (uint32_t)idx, (uint8_t*)a5);
	__try {
		if ((uint8_t)ret) {                  // the AI set a goal this call
			InterlockedIncrement64(&g_goalOk);
			g_goalSelf = self;
			float* rec = GoalRecord(self, (uint32_t)idx);
			if (rec) {
				memcpy(g_lastAiGoal, (char*)rec + 0x40, sizeof g_lastAiGoal);
				if (g_pilotMode & 1) { memcpy((char*)rec + 0x40, g_pilot, sizeof g_pilot); InterlockedIncrement64(&g_pilotA); }
			}
			if (g_pilotMode & 8) g_forcedSeq = g_pilotSeq;   // the AI accepted a goal on its own this round
		}
		else if ((g_pilotMode & 8) && (uint32_t)idx == 0 && g_forcedSeq != g_pilotSeq) {
			float* rec = GoalRecord(self, (uint32_t)idx);
			char* inst = *(char**)((char*)self + 0x78);
			if (rec && inst && Readable(inst + 0x1D8, 4)) {
				memcpy((char*)rec + 0x40, g_pilot, sizeof g_pilot);
				*(int32_t*)((char*)rec + 0x68) = 2;
				*(int32_t*)(inst + 0x1D8) = 1;
				g_forcedSeq = g_pilotSeq;
				InterlockedIncrement64(&g_forced);
				ret = (ret & ~(uint64_t)0xFF) | 1;
			}
		}
	} __except (FaultFilter()) {}
	return ret;
}

// SahelanControl.StartGoalHook() -> ok, reason
static int l_StartGoalHook(lua_State* L) {
	EnsureWatchdog();
	void* fn = (void*)Rebase(Goal::Target);
	if (!g_goalTarget) {
		if (memcmp(fn, Goal::Prologue, sizeof Goal::Prologue) != 0) { PB(L, 0); PS(L, "SetMoveGoal prologue mismatch"); return 2; }
		MH_STATUS st = MH_CreateHook(fn, (void*)h_Goal, (void**)&o_Goal);
		if (st != MH_OK) { PB(L, 0); PS(L, MH_StatusToString(st)); return 2; }
		g_goalTarget = fn;
	}
	MH_STATUS st = MH_EnableHook(fn);
	Log("Goal: hook %s", MH_StatusToString(st));
	PB(L, st == MH_OK || st == MH_ERROR_ENABLED); PS(L, MH_StatusToString(st));
	return 2;
}

// SahelanControl.StopGoalHook()
static int l_StopGoalHook(lua_State* L) {
	(void)L;
	InterlockedExchange(&g_pilotMode, g_pilotMode & ~1);
	if (g_goalTarget) MH_DisableHook(g_goalTarget);
	return 0;
}

// SahelanControl.SetPilotGoal(x, y, z, mode)  mode bits: 1 = A (after SetMoveGoal), 2 = B (RequestMove copy),
// 4 = R (every UpdatePhase: write goal record, force a replan when the pilot goal changed)
static int l_SetPilotGoal(lua_State* L) {
	g_pilot[0] = (float)p_tonumber(L, 1); g_pilot[1] = (float)p_tonumber(L, 2); g_pilot[2] = (float)p_tonumber(L, 3);
	LONG mode = (LONG)p_tonumber(L, 4) & 127;
	InterlockedIncrement(&g_pilotSeq);
	InterlockedExchange(&g_pilotMode, mode);
	Log("Pilot: goal (%.1f,%.1f,%.1f) mode %ld", g_pilot[0], g_pilot[1], g_pilot[2], mode);
	return 0;
}

// SahelanControl.ClearPilotGoal()
static int l_ClearPilotGoal(lua_State* L) { (void)L; InterlockedExchange(&g_pilotMode, 0); Log("Pilot: cleared A=%lld B=%lld", g_pilotA, g_pilotB); return 0; }

// SahelanControl.GetPilotStats() -> string
static int l_GetPilotStats(lua_State* L) {
	char buf[200];
	sprintf_s(buf, "jumpA=%lld jumpB=%lld jblocked=%lld forced=%lld phase=%lld p3=%lld replans=%lld mode=%ld goalCalls=%lld ok=%lld A=%lld B=%lld moveCalls=%lld lastAiGoal=%.1f,%.1f,%.1f", g_jumpA, g_jumpB, g_jumpBlocked, g_forced, g_phaseCalls, g_phase3, g_replans, g_pilotMode, g_goalCalls, g_goalOk,
		g_pilotA, g_pilotB, g_moveCount, g_lastAiGoal[0], g_lastAiGoal[1], g_lastAiGoal[2]);
	PS(L, buf);
	return 1;
}



// ---------------------------------------------------------------- probe5: jump builder hooks
typedef uint64_t (__fastcall *JumpFn_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
static JumpFn_t o_JumpA, o_JumpB;
static void* volatile g_jumpTargetA; static void* volatile g_jumpTargetB;
static SRWLOCK g_evLock = SRWLOCK_INIT;
static char g_evRing[64][200];
static volatile uint32_t g_evHead, g_evTail;
static void PushEvent(const char* text) {
	AcquireSRWLockExclusive(&g_evLock);
	if (g_evHead - g_evTail < 64) { strncpy_s(g_evRing[g_evHead % 64], text, _TRUNCATE); g_evHead++; }
	ReleaseSRWLockExclusive(&g_evLock);
}
static void JumpEvent(char which, uintptr_t ret) {
	__try {
		void* fr[6] = {};
		USHORT n = RtlCaptureStackBackTrace(2, 4, fr, nullptr);
		char buf[200];
		sprintf_s(buf, "%lu JUMP %c caller=+%llX bt=+%llX,+%llX,+%llX blocked=%d", GetTickCount(), which,
			(unsigned long long)(ret - g_exeBase),
			(unsigned long long)(n > 0 ? (uintptr_t)fr[0] - g_exeBase : 0), (unsigned long long)(n > 1 ? (uintptr_t)fr[1] - g_exeBase : 0),
			(unsigned long long)(n > 2 ? (uintptr_t)fr[2] - g_exeBase : 0), (g_pilotMode & 16) ? 1 : 0);
		PushEvent(buf);
	} __except (FaultFilter()) {}
}
static uint64_t __fastcall h_JumpA(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
	InterlockedIncrement64(&g_jumpA);
	JumpEvent('A', (uintptr_t)_ReturnAddress());
	if (g_pilotMode & 16) { InterlockedIncrement64(&g_jumpBlocked); return 0; }
	return o_JumpA(a1, a2, a3, a4, a5, a6);
}
static uint64_t __fastcall h_JumpB(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
	InterlockedIncrement64(&g_jumpB);
	JumpEvent('B', (uintptr_t)_ReturnAddress());
	if (g_pilotMode & 16) { InterlockedIncrement64(&g_jumpBlocked); return 0; }
	return o_JumpB(a1, a2, a3, a4, a5, a6);
}
static bool HookOne(uintptr_t va, const uint8_t* pro, size_t n, void* det, void** orig, void* volatile* slot, const char* name) {
	void* fn = (void*)Rebase(va);
	if (!*slot) {
		if (memcmp(fn, pro, n) != 0) { Log("%s: prologue mismatch", name); return false; }
		if (MH_CreateHook(fn, det, orig) != MH_OK) { Log("%s: create failed", name); return false; }
		*slot = fn;
	}
	MH_STATUS st = MH_EnableHook(fn);
	Log("%s: hook %s", name, MH_StatusToString(st));
	return st == MH_OK || st == MH_ERROR_ENABLED;
}
// SahelanControl.StartJumpHooks() -> "A=ok B=ok"   (prologues from the retail exe, checked before hooking)
static const uint8_t JumpProA[8] = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18, 0x55 };
static const uint8_t JumpProB[8] = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18, 0x57 };
static int l_StartJumpHooks(lua_State* L) {
	EnsureWatchdog();
	bool a = true;   // builder A is covered by the MoveReq3 hook installed at load (probe6)
	bool b = HookOne(0x141937DA0, JumpProB, 8, (void*)h_JumpB, (void**)&o_JumpB, &g_jumpTargetB, "JumpB");
	char buf[64]; sprintf_s(buf, "A=%s B=%s", a ? "ok" : "no", b ? "ok" : "no");
	PS(L, buf);
	return 1;
}
// SahelanControl.StopJumpHooks()
static int l_StopJumpHooks(lua_State* L) {
	(void)L; InterlockedExchange(&g_pilotMode, g_pilotMode & ~16);
	if (g_jumpTargetA) MH_DisableHook(g_jumpTargetA);
	if (g_jumpTargetB) MH_DisableHook(g_jumpTargetB);
	return 0;
}
// SahelanControl.PilotMark(text)
static int l_PilotMark(lua_State* L) {
	typedef const char* (__fastcall *tostr5_t)(lua_State*, int, size_t*);
	const char* t = ((tostr5_t)Rebase(Addr::lua_tolstring))(L, 1, nullptr);
	char buf[200]; sprintf_s(buf, "%lu MARK %s", GetTickCount(), t ? t : "?");
	PushEvent(buf);
	return 0;
}

// ---------------------------------------------------------------- probe5: state sampler fields
static const char* BehName(uint8_t b) {
	switch (b) {
	case 0x00: return "none"; case 0x04: return "move"; case 0x05: return "transition"; case 0x08: return "jump";
	case 0x09: return "give-up"; case 0x0E: return "engage0(type14)"; case 0x0F: return "engage1(type14)";
	case 0x10: return "engage2"; case 0x11: return "engage3"; case 0x12: return "engage4"; case 0x1C: return "post-jump";
	case 0x24: return "fire-loop(type4)"; default: return "?";
	}
}
static const char* CmdName(uint8_t t) {
	switch (t) { case 0: return "none"; case 1: return "walk"; case 2: return "route"; case 3: return "jump"; case 4: return "fire-burst";
	case 14: return "target-action"; case 15: return "type15"; case 18: return "type18"; case 26: return "type26"; default: return "?"; }
}
static const char* DecName(uint8_t d) { switch (d) { case 2: return "default"; case 3: return "engage-target"; case 4: return "special"; case 5: return "override"; default: return "?"; } }
static float g_home[3]; static bool g_haveHome;
static int32_t g_goalMode;
static float fabs5(float v) { return v < 0 ? -v : v; }
struct StateSnap { uint8_t ai[16]; float target[4]; int32_t i1d8, i1dc, i1e0; int32_t gmode; uint8_t curT, pendT; uint16_t sflags; };
static StateSnap g_snapLast;
static bool ReadStateSnap(StateSnap* o) {
	memset(o, 0, sizeof *o);
	__try {
		uint64_t ai = g_aiStatePtr;
		if (ai && Readable((void*)ai, 16)) memcpy(o->ai, (void*)ai, 16);
		if (g_goalSelf && Readable((char*)g_goalSelf + 0x78, 8)) {
			char* inst = *(char**)((char*)g_goalSelf + 0x78);
			if (inst && Readable(inst, 0x210)) {
				memcpy(o->target, inst + 0x140, 16);
				o->i1d8 = *(int32_t*)(inst + 0x1D8); o->i1dc = *(int32_t*)(inst + 0x1DC); o->i1e0 = *(int32_t*)(inst + 0x1E0);
			}
		}
		o->gmode = g_goalMode;
		char* ctl = (char*)g_lastSelf;
		if (ctl && Readable(ctl + 0x48, 8)) {
			char* slot = *(char**)(ctl + 0x48);
			if (slot && Readable(slot, 0x180)) { o->curT = (uint8_t)slot[0xAA]; o->pendT = (uint8_t)slot[0xB0 + 0xAA]; o->sflags = *(uint16_t*)(slot + 0x16A); }
		}
	} __except (FaultFilter()) { return false; }
	return true;
}
static void WriteStateAndEvents(FILE* f, DWORD now, const float* mech) {
	for (;;) {
		char line[200];
		AcquireSRWLockExclusive(&g_evLock);
		bool have = g_evTail != g_evHead;
		if (have) { memcpy(line, g_evRing[g_evTail % 64], sizeof line); g_evTail++; }
		ReleaseSRWLockExclusive(&g_evLock);
		if (!have) break;
		fprintf(f, "%s\n", line);
	}
	StateSnap sn;
	if (!ReadStateSnap(&sn)) return;
	StateSnap a = sn, b = g_snapLast;
	// target moves every frame while the player moves: compare it coarsely (1 m)
	bool tgt = fabs5(a.target[0] - b.target[0]) > 1 || fabs5(a.target[2] - b.target[2]) > 1;
	a.target[0] = b.target[0]; a.target[1] = b.target[1]; a.target[2] = b.target[2]; a.target[3] = b.target[3];
	if (tgt || memcmp(&a, &b, sizeof a) != 0) {
		char hex[33];
		for (int k = 0; k < 16; k++) sprintf_s(hex + 2 * k, 3, "%02X", sn.ai[k]);
		if (!g_haveHome && (mech[0] != 0 || mech[2] != 0)) { memcpy(g_home, mech, sizeof g_home); g_haveHome = true; }
		float dt = sqrtf((mech[0] - sn.target[0]) * (mech[0] - sn.target[0]) + (mech[2] - sn.target[2]) * (mech[2] - sn.target[2]));
		float dh = g_haveHome ? sqrtf((mech[0] - g_home[0]) * (mech[0] - g_home[0]) + (mech[2] - g_home[2]) * (mech[2] - g_home[2])) : 0;
		fprintf(f, "%lu STATE dec=%u:%s beh=%02X:%s next=%02X:%s cmd=%u:%s mech=%.1f,%.1f,%.1f target=%.1f,%.1f,%.1f dTarget=%.1f dHome=%.1f gate=%ld ai=%s i1d8=%d i1e0=%d gmode=%d fl=%04X\n",
			now, sn.ai[5], DecName(sn.ai[5]), sn.ai[7], BehName(sn.ai[7]), sn.ai[8], BehName(sn.ai[8]), sn.curT, CmdName(sn.curT),
			mech[0], mech[1], mech[2], sn.target[0], sn.target[1], sn.target[2], dt, dh, g_gate, hex, sn.i1d8, sn.i1e0, sn.gmode, sn.sflags);
		g_snapLast = sn;
	}
}

// ---------------------------------------------------------------- probe4: watchdog, panic, 20 Hz pilot sampler
// AI root = *(walkAi+0x70) (from SetMoveGoal) or *(controller+0x38) (from SetCommand). root+0x10 TppGameObjectInfo
// (+0x20 vec4 positions per instance), root+0x58 TppSahelan2StateCollector (+8 records 0x130, +0x10 count).
static HANDLE g_watchThread;
static volatile LONG g_sampleOn;
static FILE* g_pilotFile;
static float g_mech[4], g_goalNow[4];

static char* AiRoot() {
	char* r = nullptr;
	if (g_goalSelf && Readable((char*)g_goalSelf + 0x70, 8)) r = *(char**)((char*)g_goalSelf + 0x70);
	if (!r && g_lastSelf && Readable((char*)g_lastSelf + 0x38, 8)) r = *(char**)((char*)g_lastSelf + 0x38);
	return (r && Readable(r, 0x60)) ? r : nullptr;
}

static bool ReadMechState() {    // idx 0 only
	bool ok = false;
	__try {
		char* r = AiRoot();
		if (!r) return false;
		char* info = *(char**)(r + 0x10);
		if (info && Readable(info + 0x20, 8)) {
			char* pos = *(char**)(info + 0x20);
			if (pos && Readable(pos, 16)) { memcpy(g_mech, pos, 16); ok = true; }
		}
		char* holder = *(char**)(r + 0x58);
		if (holder && Readable(holder + 8, 0x10) && *(uint32_t*)(holder + 0x10) > 0) {
			char* rec = *(char**)(holder + 8);
			if (rec && Readable(rec, 0x130)) { memcpy(g_goalNow, rec + 0x40, 16); g_goalMode = *(int32_t*)(rec + 0x68); }
		}
	} __except (FaultFilter()) { ok = false; }
	return ok;
}

static void DisableAllHooks(const char* why) {
	InterlockedExchange(&g_pilotMode, 0);
	MH_STATUS st = MH_DisableHook(MH_ALL_HOOKS);
	Log("PANIC (%s): all hooks disabled: %s; faults=%lld", why, MH_StatusToString(st), g_faultCount);
}

static DWORD WINAPI WatchThread(LPVOID) {
	DWORD lastSample = 0;
	for (;;) {
		Sleep(10);
		if (g_faulted) { InterlockedExchange(&g_faulted, 0); DisableAllHooks("exception in detour"); }
		// panic chord: Ctrl+Shift+End while the game window is in front
		HWND fg = GetForegroundWindow(); DWORD pid = 0; GetWindowThreadProcessId(fg, &pid);
		if (pid == GetCurrentProcessId() && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
			(GetAsyncKeyState(VK_END) & 0x8000)) { DisableAllHooks("Ctrl+Shift+End"); Sleep(500); }
		DWORD now = GetTickCount();
		if (g_sampleOn && now - lastSample >= 50) {
			lastSample = now;
			bool ok = ReadMechState();
			if (!ok && g_pilotMode) { InterlockedExchange(&g_pilotMode, 0); Log("Pilot: cleared (mech/AI root gone)"); }
			if (g_pilotFile) WriteStateAndEvents(g_pilotFile, now, g_mech);
			if (ok && g_pilotFile)
				fprintf(g_pilotFile, "%lu mech=%.2f,%.2f,%.2f goal=%.2f,%.2f,%.2f gmode=%d pilot=%ld:%.2f,%.2f,%.2f ok=%lld A=%lld\n",
					now, g_mech[0], g_mech[1], g_mech[2], g_goalNow[0], g_goalNow[1], g_goalNow[2], g_goalMode,
					g_pilotMode, g_pilot[0], g_pilot[1], g_pilot[2], g_goalOk, g_pilotA);
			if (g_pilotFile && now % 1000 < 50) fflush(g_pilotFile);
		}
	}
}
static void EnsureWatchdog() {
	g_pilotModePtr = &g_pilotMode;
	if (!g_watchThread) g_watchThread = CreateThread(nullptr, 0, WatchThread, nullptr, 0, nullptr);
}

// SahelanControl.Panic(): clear pilot mode and disable every hook now
static int l_Panic(lua_State* L) { (void)L; DisableAllHooks("Lua Panic()"); return 0; }

// SahelanControl.StartSampler() -> ok   (20 Hz mech/goal log to plugins\SahelanControl_pilot.log)
static int l_StartSampler(lua_State* L) {
	EnsureWatchdog();
	if (!g_pilotFile) {
		wchar_t path[MAX_PATH]{};
		GetModuleFileNameW(g_self, path, MAX_PATH);
		wchar_t* dot = wcsrchr(path, L'.');
		if (dot) wcscpy_s(dot, MAX_PATH - (dot - path), L"_pilot.log");
		g_pilotFile = _wfsopen(path, L"w", _SH_DENYWR);
	}
	InterlockedExchange(&g_sampleOn, 1);
	PB(L, g_pilotFile != nullptr);
	return 1;
}

// SahelanControl.StopSampler()
static int l_StopSampler(lua_State* L) {
	(void)L; InterlockedExchange(&g_sampleOn, 0); Sleep(100);
	if (g_pilotFile) { fflush(g_pilotFile); fclose(g_pilotFile); g_pilotFile = nullptr; }
	return 0;
}

// SahelanControl.GetMechState() -> "mx,my,mz,gx,gy,gz,gmode,pilotMode,faults" or "none"
static int l_GetMechState(lua_State* L) {
	EnsureWatchdog();
	if (!ReadMechState()) { PS(L, "none"); return 1; }
	char buf[200];
	sprintf_s(buf, "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%ld,%lld,%u,%u,%u,%u", g_mech[0], g_mech[1], g_mech[2], g_goalNow[0], g_goalNow[1],
		g_goalNow[2], g_goalMode, g_pilotMode, g_faultCount, g_snapLast.ai[5], g_snapLast.ai[7], g_snapLast.ai[8], g_snapLast.curT);
	PS(L, buf);
	return 1;
}


// ---------------------------------------------------------------- probe4b mode R: SahelanWalk_UpdatePhase pre-hook
// 0x1418D1DD0 (this rcx, idx edx, phase r8d, char* status r9) -> void, virtual; ABI checked with fast_xrefs abi
// (reads rcx rdx r8 r9, no stack args, no return value). Walk state = *(this+0x80) + idx*0x160: +0x00 u16 path
// handle, +0x151 flags (0x80 = path active). AI root = *(this+0x78). In phase 3 with 0x80 clear, UpdatePhase compares
// the goal record (+0x40) with the mech position and, if out of radius, cancels the old path and calls RequestPath.
// Mode R: before the original runs, write the pilot goal into the goal record; if the pilot goal changed since the
// last replan, clear 0x80 so this very call replans.
namespace Phase {
	constexpr uintptr_t Target = 0x1418D1DD0;
	constexpr uint8_t   Prologue[10] = { 0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41 };
}
typedef uint64_t (__fastcall *PhaseFn_t)(void*, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
static PhaseFn_t o_Phase;
static void* volatile g_phaseTarget;
static LONG g_planSeq = -1;

static void PhasePre(void* self, uint32_t idx, uint32_t phase) {
	__try {
		if (!(g_pilotMode & 4) || idx != 0) return;
		char* r = *(char**)((char*)self + 0x78);
		if (!r || !Readable(r + 0x58, 8)) return;
		char* holder = *(char**)(r + 0x58);
		if (!holder || !Readable(holder + 8, 0x10) || idx >= *(uint32_t*)(holder + 0x10)) return;
		char* rec = *(char**)(holder + 8) + (size_t)idx * 0x130;
		if (!Readable(rec, 0x130)) return;
		memcpy(rec + 0x40, g_pilot, sizeof g_pilot);
		if (phase != 3) return;
		InterlockedIncrement64(&g_phase3);
		LONG seq = g_pilotSeq;
		if (seq != g_planSeq) {
			char* walk = *(char**)((char*)self + 0x80);
			if (walk && Readable(walk + (size_t)idx * 0x160, 0x160)) {
				uint8_t* fl = (uint8_t*)(walk + (size_t)idx * 0x160 + 0x151);
				*fl = (uint8_t)(*fl & 0x7F);
				g_planSeq = seq;
				InterlockedIncrement64(&g_replans);
			}
		}
	} __except (FaultFilter()) {}
}

static uint64_t __fastcall h_Phase(void* self, uint64_t idx, uint64_t phase, uint64_t a4, uint64_t a5, uint64_t a6) {
	InterlockedIncrement64(&g_phaseCalls);
	PhasePre(self, (uint32_t)idx, (uint32_t)phase);
	return o_Phase(self, idx, phase, a4, a5, a6);
}

// SahelanControl.StartPhaseHook() -> ok, reason
static int l_StartPhaseHook(lua_State* L) {
	EnsureWatchdog();
	void* fn = (void*)Rebase(Phase::Target);
	if (!g_phaseTarget) {
		if (memcmp(fn, Phase::Prologue, sizeof Phase::Prologue) != 0) { PB(L, 0); PS(L, "UpdatePhase prologue mismatch"); return 2; }
		MH_STATUS st = MH_CreateHook(fn, (void*)h_Phase, (void**)&o_Phase);
		if (st != MH_OK) { PB(L, 0); PS(L, MH_StatusToString(st)); return 2; }
		g_phaseTarget = fn;
	}
	MH_STATUS st = MH_EnableHook(fn);
	Log("Phase: hook %s", MH_StatusToString(st));
	PB(L, st == MH_OK || st == MH_ERROR_ENABLED); PS(L, MH_StatusToString(st));
	return 2;
}

// SahelanControl.StopPhaseHook()
static int l_StopPhaseHook(lua_State* L) {
	(void)L; InterlockedExchange(&g_pilotMode, g_pilotMode & ~4);
	if (g_phaseTarget) MH_DisableHook(g_phaseTarget);
	return 0;
}


// ---------------------------------------------------------------- probe6: jump events, gate API, auto-arm
static void JumpEvent6(const char* what, uintptr_t ret) {
	__try {
		void* fr[5] = {};
		USHORT n = RtlCaptureStackBackTrace(2, 3, fr, nullptr);
		char buf[200];
		sprintf_s(buf, "%lu JUMP %s caller=+%llX bt=+%llX,+%llX,+%llX", GetTickCount(), what, (unsigned long long)(ret - g_exeBase),
			(unsigned long long)(n > 0 ? (uintptr_t)fr[0] - g_exeBase : 0), (unsigned long long)(n > 1 ? (uintptr_t)fr[1] - g_exeBase : 0),
			(unsigned long long)(n > 2 ? (uintptr_t)fr[2] - g_exeBase : 0));
		PushEvent(buf);
	} __except (EXCEPTION_EXECUTE_HANDLER) {}
}
// probe6c: direct steering
namespace Direct {
	constexpr uintptr_t StartPath = 0x141CC7500;   // PathPlanner_StartPath(planner, idx, goal*)
	constexpr uint8_t   StartPathPro[11] = { 0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x60 };
	constexpr uintptr_t OnMesh = 0x141C85400;      // (navWorld, pos*) -> 1 when the point is on a loaded navmesh
	constexpr uint8_t   OnMeshPro[11] = { 0x48, 0x89, 0x6C, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20 };
	constexpr uintptr_t ExecVtbl = 0x1423A1610;
}
typedef uint64_t (__fastcall *Fn4_t)(void*, uint64_t, uint64_t, uint64_t);
static Fn4_t o_StartPath, o_OnMesh;
static void* volatile g_startPathTarget; static void* volatile g_onMeshTarget;
static __declspec(thread) int t_forceDirect;
static volatile LONG64 g_directPaths, g_instantMoves;
static LONG g_instantSeq = -1;
static DWORD g_instantTick, g_ptrTick;

static void RefreshSahelPtrs() {
	DWORD now = GetTickCount();
	if (g_sahelExec && now - g_ptrTick < 500) return;
	g_ptrTick = now;
	__try {
		char* r = AiRoot();
		void* e = r ? *(void**)(r + 0x18) : nullptr;
		if (e && Readable(e, 0x40) && *(uintptr_t*)e == Rebase(Direct::ExecVtbl)) {
			void* p = *(void**)((char*)e + 0x30);
			g_sahelPlanner = (p && Readable(p, 0x20)) ? p : nullptr;
			if (g_sahelExec != e) { g_sahelExec = e; Log("Direct: Sahelan executor %p planner %p", e, g_sahelPlanner); }
		}
		else { g_sahelExec = nullptr; g_sahelPlanner = nullptr; }
	} __except (FaultFilter()) {}
}

static uint64_t __fastcall h_StartPath(void* self, uint64_t idx, uint64_t goal, uint64_t a4) {
	bool direct = false;
	if ((g_pilotMode & 32) && self && self == g_sahelPlanner) {
		__try {
			char* slots = *(char**)((char*)self + 8);
			if (slots && Readable(slots + (uint32_t)idx * 16, 16)) slots[(uint32_t)idx * 16 + 10] &= ~8;
			direct = true;
		} __except (FaultFilter()) { direct = false; }
	}
	if (direct) { t_forceDirect = 1; InterlockedIncrement64(&g_directPaths); }
	uint64_t r = o_StartPath(self, idx, goal, a4);
	t_forceDirect = 0;
	return r;
}
static uint64_t __fastcall h_OnMesh(void* world, uint64_t pos, uint64_t a3, uint64_t a4) {
	if (t_forceDirect) return 0;
	return o_OnMesh(world, pos, a3, a4);
}

static void InstantMove() {
	if (!(g_pilotMode & 64) || !g_haveTpl || !g_sahelExec || !o_Move || g_pushSerial != -1) return;
	LONG seq = g_pilotSeq;
	DWORD now = GetTickCount();
	if (seq == g_instantSeq || now - g_instantTick < 150) return;
	alignas(16) uint8_t req[Move::ReqBytes];
	__try {
		if (!ReadMechState()) return;
		memcpy(req, g_moveTpl, sizeof req);
		memcpy(req + 0x00, g_mech, 12);
		memcpy(req + 0x10, g_pilot, 12);
		float* rec = g_goalSelf ? GoalRecord(g_goalSelf, 0) : nullptr;
		if (rec) memcpy((char*)rec + 0x40, g_pilot, 12);
		g_instantSeq = seq; g_instantTick = now;
		o_Move(g_sahelExec, 0, req);
		InterlockedIncrement64(&g_instantMoves);
	} __except (FaultFilter()) {}
}

static bool HookAt(uintptr_t va, const uint8_t* pro, size_t n, void* det, void** orig, void* volatile* slot, const char* name) {
	void* fn = (void*)Rebase(va);
	if (!*slot) {
		if (memcmp(fn, pro, n) != 0 && *(uint8_t*)fn != 0xE9) { Log("%s: prologue mismatch, not hooked", name); return false; }
		MH_STATUS st = MH_CreateHook(fn, det, orig);
		if (st != MH_OK) { Log("%s: create %s", name, MH_StatusToString(st)); return false; }
		*slot = fn;
	}
	MH_STATUS st = MH_EnableHook(fn);
	Log("%s: hook %s", name, MH_StatusToString(st));
	return st == MH_OK || st == MH_ERROR_ENABLED;
}
static void StartDirectHooks() {
	HookAt(Move::Target, Move::Prologue, sizeof Move::Prologue, (void*)h_Move, (void**)&o_Move, &g_moveTarget, "RequestMove");
	HookAt(Direct::StartPath, Direct::StartPathPro, sizeof Direct::StartPathPro, (void*)h_StartPath, (void**)&o_StartPath, &g_startPathTarget, "StartPath");
	HookAt(Direct::OnMesh, Direct::OnMeshPro, sizeof Direct::OnMeshPro, (void*)h_OnMesh, (void**)&o_OnMesh, &g_onMeshTarget, "OnMeshTest");
}

// probe7: native commands through the AI's own PushCommand / PopCommand (slot 0), run from the Update hook
namespace Cmd {
	constexpr uintptr_t Push = 0x1418BFA70;
	constexpr uint8_t   PushPro[12] = { 0x48, 0x89, 0x5C, 0x24, 0x20, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83 };
	constexpr uintptr_t Pop = 0x1418BF580;
	constexpr uint8_t   PopPro[12] = { 0x4C, 0x8B, 0xC9, 0x8B, 0xC2, 0x48, 0x8D, 0x0C, 0x40, 0x48, 0xC1, 0xE1 };
	constexpr size_t Slots = 0x48, SlotBytes = 0x180, Flags = 0x16A, Serial = 0x170;
	enum Kind { None = 0, Weapon = 1, Jump = 2 };
}
typedef int  (__fastcall *PushFn_t)(void*, uint64_t, const uint8_t*);
typedef void (__fastcall *PopFn_t)(void*, uint64_t, int);
static PushFn_t p_Push; static PopFn_t p_Pop;
struct Weapon { const char* name; int type, variant, cut; };
static Weapon g_weapons[8] = {
	{ "none", 0, 0, 0 }, { "sword", 14, 0, 0 }, { "gatling", 12, 0, 1 }, { "flame", 16, 0, 1 },
	{ "missiles", 15, 0, 0 }, { "grenade", 19, 0, 0 }, { "stamp", 13, 0, 0 }, { "sword2", 14, 1, 0 } };
static SRWLOCK g_injLock = SRWLOCK_INIT;
alignas(16) static uint8_t g_injReq[Sink::ReqBytes];
static int g_injKind, g_injSlot, g_injCut; static volatile LONG g_injPending;
alignas(16) static uint8_t g_pushReq[Sink::ReqBytes];
static int g_pushKind, g_pushSlot, g_pushCut;
static DWORD g_pushTick, g_cancelTick;
static volatile DWORD g_holdUntil;
static volatile LONG64 g_pushes, g_pops, g_repeats, g_lost, g_cancels, g_openCancels;

static bool StartCmdInjection() {
	if (p_Push && p_Pop) return true;
	void* pu = (void*)Rebase(Cmd::Push); void* po = (void*)Rebase(Cmd::Pop);
	if (memcmp(pu, Cmd::PushPro, sizeof Cmd::PushPro) != 0 || memcmp(po, Cmd::PopPro, sizeof Cmd::PopPro) != 0) {
		Log("Cmd: Push/Pop prologue mismatch, weapons/jump disabled"); return false;
	}
	p_Push = (PushFn_t)pu; p_Pop = (PopFn_t)po;
	Log("Cmd: PushCommand %p PopCommand %p", pu, po);
	return true;
}
static char* CtlSlot0(void* ctl) {
	if (!ctl || !Readable((char*)ctl + Cmd::Slots, 8)) return nullptr;
	char* slot = *(char**)((char*)ctl + Cmd::Slots);
	return (slot && Readable(slot, Cmd::SlotBytes)) ? slot : nullptr;
}
// native stop request (type 1, +0x00 = 1), sent through the controller's own SetCommand (vtbl[0], verified)
static bool SendStop(void* ctl) {
	alignas(16) uint8_t r[Sink::ReqBytes] = {};
	*(uint32_t*)r = 1; r[0xAA] = 1;
	void* fn = VSlot(ctl, 0);
	if ((uintptr_t)fn != Rebase(Sink::Target)) return false;
	((SinkFn_t)fn)(ctl, 0, r);
	return true;
}
static void BuildCmd(uint8_t* r, int type, int variant, const float* p, float yaw) {
	memset(r, 0, Sink::ReqBytes);
	memcpy(r, p, 12); *(float*)(r + 0x0C) = 1.0f;
	switch (type) {
	case 4:  break;
	case 12: {   // VulcunAttackAction: (float)u32 goal-record +0xB0 / +0xB4, like the native builder 0x141934C40
		memset(r, 0, 0x20);
		float* rec = g_goalSelf ? GoalRecord(g_goalSelf, 0) : nullptr;
		uint32_t a = rec ? *(uint32_t*)((char*)rec + 0xB0) : 0, b = rec ? *(uint32_t*)((char*)rec + 0xB4) : 0;
		*(float*)(r + 0x10) = (float)a; *(float*)(r + 0x18) = (float)b;
		r[0xA8] = 0x04;
		char ev[96]; sprintf_s(ev, "%lu VULCAN rec=%u,%u%s", GetTickCount(), a, b, rec ? "" : " (no record)"); PushEvent(ev);
		break; }
	case 16: memset(r, 0, 0x20); *(uint16_t*)r = 0xFFFF; r[0xA8] = 0x04; break;   // FlameAttackAction (0x1418DF950)
	case 19: {   // GrenadeAttackAction, native builder 0x1418E4C80: aim vec4, +0x10 u16 0xFFFF, +0x14 launcher 0/3
		uint32_t l = g_grenadeLauncher >= 0 ? (uint32_t)g_grenadeLauncher : ((InterlockedIncrement(&g_grenadeAlt) & 1) ? 0u : 3u);
		*(uint32_t*)(r + 0x10) = 0xFFFF; *(uint32_t*)(r + 0x14) = l; r[0xA8] = 0x04;
		char ev[64]; sprintf_s(ev, "%lu GRENADE launcher=%u", GetTickCount(), l); PushEvent(ev);
		break; }
	case 3:  *(float*)(r + 0x10) = yaw; r[0xA8] = 0x04; break;
	case 15: memcpy(r + 0x80, p, 12); r[0xA8] = 0x04; r[0xA9] = 0x01; break;
	default: *(uint32_t*)(r + 0x10) = (uint32_t)variant; r[0xA8] = 0x04; break;   // 14 and discovery types
	}
	r[0xAA] = (uint8_t)type;
}
static void QueueCmd(const uint8_t* r, int kind, int slot, int cut, DWORD holdMs) {
	AcquireSRWLockExclusive(&g_injLock);
	memcpy(g_injReq, r, sizeof g_injReq);
	g_injKind = kind; g_injSlot = slot; g_injCut = cut;
	g_holdUntil = GetTickCount() + holdMs;
	InterlockedExchange(&g_injPending, 1);
	ReleaseSRWLockExclusive(&g_injLock);
}
static void EndPush(void* ctl, const char* why) {
	int serial = g_pushSerial;
	if (serial == -1) return;
	p_Pop(ctl, 0, serial);
	InterlockedExchange(&g_pushSerial, -1);
	InterlockedIncrement64(&g_pops);
	if (g_pushCut) SendStop(ctl);
	char ev[120]; sprintf_s(ev, "%lu POP type=%u slot=%d serial=%d after %lu ms (%s)", GetTickCount(), g_pushReq[0xAA], g_pushSlot, serial, GetTickCount() - g_pushTick, why);
	PushEvent(ev);
}
static void DoPush(void* ctl, const char* what) {
	int serial = p_Push(ctl, 0, g_pushReq);
	InterlockedExchange(&g_pushSerial, serial);
	g_pushTick = GetTickCount();
	InterlockedIncrement64(&g_pushes);
	char ev[160]; sprintf_s(ev, "%lu PUSH %s type=%u v=%u slot=%d serial=%d at %.1f,%.1f,%.1f", g_pushTick, what, g_pushReq[0xAA],
		*(uint32_t*)(g_pushReq + 0x10), g_pushSlot, serial, *(float*)g_pushReq, *(float*)(g_pushReq + 4), *(float*)(g_pushReq + 8));
	PushEvent(ev);
}
static void CommandTick() {
	void* ctl = g_ctl;
	if (!ctl || !p_Push) return;
	DWORD now = GetTickCount();
	__try {
		char* slot = CtlSlot0(ctl);
		if (!slot) return;
		uint16_t fl = *(uint16_t*)(slot + Cmd::Flags);
		uint8_t cur = (uint8_t)slot[0xAA];
		if (g_injPending && g_aiSeen) {
			AcquireSRWLockExclusive(&g_injLock);
			bool same = g_pushSerial != -1 && g_pushKind == Cmd::Weapon && g_injKind == Cmd::Weapon && g_injSlot != 0 && g_injSlot == g_pushSlot;
			if (!same) { memcpy(g_pushReq, g_injReq, sizeof g_pushReq); }
			int kind = g_injKind, wslot = g_injSlot, cut = g_injCut;
			InterlockedExchange(&g_injPending, 0);
			ReleaseSRWLockExclusive(&g_injLock);
			if (!same) {                      // same held weapon: the hold window was already extended
				if (g_pushSerial != -1) { int c = g_pushCut; g_pushCut = 0; EndPush(ctl, "replaced"); g_pushCut = c; }
				g_pushKind = kind; g_pushSlot = wslot; g_pushCut = cut;
				DoPush(ctl, kind == Cmd::Jump ? "JUMP" : "WEAPON");
			}
			return;
		}
		if (g_pushSerial != -1) {
			if (!(fl & 0x100) || *(int*)(slot + Cmd::Serial) != g_pushSerial) {
				InterlockedExchange(&g_pushSerial, -1); InterlockedIncrement64(&g_lost);
				char ev[80]; sprintf_s(ev, "%lu PUSH lost (flags=%04X)", now, fl); PushEvent(ev);
				return;
			}
			DWORD age = now - g_pushTick;
			bool finished = (fl & 0x10) && !(fl & 0x80) && age > 250;
			bool held = (LONG)(g_holdUntil - now) > 0;
			if (g_pushKind == Cmd::Jump) { if (finished || age > 6000) EndPush(ctl, finished ? "landed" : "timeout"); return; }
			if (held && finished) { int s0 = g_pushSerial; p_Pop(ctl, 0, s0); InterlockedExchange(&g_pushSerial, -1); InterlockedIncrement64(&g_repeats); DoPush(ctl, "REPEAT"); return; }
			if (!held && (g_pushCut || finished || age > 15000)) EndPush(ctl, g_pushCut ? "released" : finished ? "finished" : "timeout");
			return;
		}
		if (g_injPending) return;          // waiting for the AI to go live
		if (now - g_cancelTick < 300 || (fl & 0x100) || (fl & 0x10)) return;
		if (g_cfgSkipOpening && !g_aiSeen && now - g_spawnTick < 45000 && (cur == 18 || cur == 14) && g_openCancels < 40) {
			if (SendStop(ctl)) {
				g_cancelTick = now; InterlockedIncrement64(&g_openCancels);
				char ev[96]; sprintf_s(ev, "%lu OPENING cancel type=%u flags=%04X t+%lu ms", now, cur, fl, now - g_spawnTick); PushEvent(ev);
			}
		}
		else if ((g_gate & 2) && g_aiSeen && cur < 32 && cur != 1 && cur != 3 && (g_gateTypes >> cur & 1) && now - g_cancelTick > 500) {
			if (SendStop(ctl)) {
				g_cancelTick = now; InterlockedIncrement64(&g_cancels);
				char ev[96]; sprintf_s(ev, "%lu CANCEL running AI type=%u flags=%04X", now, cur, fl); PushEvent(ev);
			}
		}
	} __except (FaultFilter()) {}
}
static void AimArg(lua_State* L, int i, float out[3]) {
	if (p_type(L, i) == 3 /* LUA_TNUMBER */) {
		out[0] = (float)p_tonumber(L, i); out[1] = (float)p_tonumber(L, i + 1); out[2] = (float)p_tonumber(L, i + 2);
	}
	else memcpy(out, g_aim, 12);
}
// SahelanControl.PilotFire(slot, holdMs[, x, y, z]) -> string. Re-send while the key is held (extends the hold).
static int l_PilotFire(lua_State* L) {
	int slot = (int)p_tonumber(L, 1);
	DWORD hold = (DWORD)OptNumber(L, 2, 300);
	if (slot < 1 || slot > 7 || !p_Push || !g_ctl) { PS(L, !p_Push ? "no Push/Pop" : !g_ctl ? "no controller yet" : "bad slot"); return 1; }
	const Weapon& w = g_weapons[slot];
	float aim[3]; AimArg(L, 3, aim);
	alignas(16) uint8_t r[Sink::ReqBytes];
	BuildCmd(r, w.type, w.variant, aim, 0);
	QueueCmd(r, Cmd::Weapon, slot, w.cut, hold);
	char buf[96]; sprintf_s(buf, "%s type=%d v=%d cut=%d", w.name, w.type, w.variant, w.cut); PS(L, buf);
	return 1;
}
// SahelanControl.PilotCommand(type, variant, holdMs, cut[, x, y, z]) -> string   (discovery: any type, slot 0 of the table)
static int l_PilotCommand(lua_State* L) {
	int type = (int)p_tonumber(L, 1), variant = (int)p_tonumber(L, 2);
	DWORD hold = (DWORD)OptNumber(L, 3, 300); int cut = (int)OptNumber(L, 4, 0);
	if (type <= 0 || type > 40 || !p_Push || !g_ctl) { PS(L, "refused"); return 1; }
	float aim[3]; AimArg(L, 5, aim);
	alignas(16) uint8_t r[Sink::ReqBytes];
	BuildCmd(r, type, variant, aim, 0);
	QueueCmd(r, Cmd::Weapon, 0, cut, hold);
	char buf[64]; sprintf_s(buf, "cmd type=%d v=%d hold=%lu cut=%d", type, variant, hold, cut); PS(L, buf);
	return 1;
}
// SahelanControl.SetWeapon(slot, type, variant, cut)
static int l_SetWeapon(lua_State* L) {
	int slot = (int)p_tonumber(L, 1);
	if (slot >= 1 && slot <= 7) {
		g_weapons[slot].type = (int)p_tonumber(L, 2); g_weapons[slot].variant = (int)p_tonumber(L, 3); g_weapons[slot].cut = (int)p_tonumber(L, 4);
		Log("SetWeapon %d (%s) = type %d v %d cut %d", slot, g_weapons[slot].name, g_weapons[slot].type, g_weapons[slot].variant, g_weapons[slot].cut);
	}
	return 0;
}
// SahelanControl.PilotJumpNow(x, y, z) -> string: native type-3 jump pushed directly (AI combat jumps stay blocked)
static int l_PilotJumpNow(lua_State* L) {
	if (!p_Push || !g_ctl) { PS(L, "no controller yet"); return 1; }
	float land[3]; AimArg(L, 1, land);
	float yaw = 0;
	if (ReadMechState()) yaw = atan2f(land[0] - g_mech[0], land[2] - g_mech[2]);   // [I] same convention as camera yaw
	alignas(16) uint8_t r[Sink::ReqBytes];
	BuildCmd(r, 3, 0, land, yaw);
	QueueCmd(r, Cmd::Jump, 0, 0, 0);
	char buf[96]; sprintf_s(buf, "jump to %.1f,%.1f,%.1f yaw %.2f", land[0], land[1], land[2], yaw); PS(L, buf);
	return 1;
}
// SahelanControl.SkipOpening(on)
static int l_SkipOpening(lua_State* L) { g_cfgSkipOpening = p_toboolean(L, 1) ? 1 : 0; return 0; }

// probe8: Dominion decision state machine (Arch doc 8.6-8.8)
namespace Decide {
	constexpr uintptr_t H0 = 0x1418FE270;        // decision-0 handler (this, idx, event, bb, a5)
	constexpr uint8_t   H0Pro[10] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20 };
	constexpr uintptr_t Override = 0x1418FC460;  // decision 2 -> 5 test (this, idx, bb) -> al
	constexpr uint8_t   OverridePro[11] = { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57 };
	constexpr size_t    InstBytes = 0x210, InstOverrideDone = 0x202;
}
static Fn6_t o_H0;
static Fn4_t o_Override;
static void* volatile g_h0Target; static void* volatile g_overrideTarget;
static int g_cfgIntroSkip = 1, g_cfgKillOverride = 1;
static volatile LONG g_idleHold;
static volatile LONG64 g_introSkips, g_idleHolds, g_overrideKills;
static DWORD g_decideEvTick;
static uint64_t __fastcall h_H0(void* self, uint64_t idx, uint64_t ev, uint64_t bb, uint64_t a5, uint64_t a6) {
	uint64_t r = o_H0(self, idx, ev, bb, a5, a6);
	if ((uint32_t)ev != 3 || !bb) return r;
	__try {
		uint8_t* b = (uint8_t*)bb;
		if (Readable(b, 16) && b[5] == 1) {
			const char* what = nullptr;
			if (g_idleHold) { b[5] = 0; InterlockedIncrement64(&g_idleHolds); what = "IDLE-HOLD decision 0 kept"; }
			else if (g_cfgIntroSkip) { b[5] = 2; InterlockedIncrement64(&g_introSkips); what = "INTRO-SKIP decision 0 -> 2"; }
			DWORD now = GetTickCount();
			if (what && now - g_decideEvTick > 5000) {
				g_decideEvTick = now;
				char e[96]; sprintf_s(e, "%lu %s (idx %u, t+%lu ms after spawn)", now, what, (uint32_t)idx, now - g_spawnTick);
				PushEvent(e); Log("%s", e);
			}
		}
	} __except (FaultFilter()) {}
	return r;
}
static uint64_t __fastcall h_Override(void* self, uint64_t idx, uint64_t bb, uint64_t a4) {
	if (!g_cfgKillOverride) return o_Override(self, idx, bb, a4);
	__try {
		char* inst = Readable((char*)self + 0x78, 8) ? *(char**)((char*)self + 0x78) : nullptr;
		char* p = inst ? inst + (size_t)(uint32_t)idx * Decide::InstBytes + Decide::InstOverrideDone : nullptr;
		if (p && Readable(p, 1)) *p = 1;
	} __except (FaultFilter()) {}
	InterlockedIncrement64(&g_overrideKills);
	return 0;   // al = 0: no decision 5
}
// ---------------------------------------------------------------- probe10b: Phase Locking (from V_Framework & RE)
namespace PhaseForce {
	constexpr uintptr_t PreUpdate    = 0x1418FF8E0;
	constexpr uint8_t   PreUpdatePro[10] = { 0x48, 0x89, 0x6C, 0x24, 0x40, 0x4C, 0x89, 0x7C, 0x24, 0x48 };
	constexpr uintptr_t StepTable    = 0x142C69AB0; // Sahelan_PhaseSneakAiImpl_StepFuncsTable
	constexpr size_t    StepEntrySize = 16;
	constexpr int64_t   BaseAdj      = -0x20;
}
typedef void (__fastcall *PreUpdate_t)(void*, uint32_t, int64_t*);
typedef void (__fastcall *StepFunc_t)(void*, uint32_t, uint32_t, void*, void*);
static PreUpdate_t o_PreUpdate;
static void* volatile g_preUpdateTarget;
static volatile LONG g_forcedPhase = 3;    // default Phase 3 (Combat) when active
static volatile LONG g_forcePhaseOn = 1;   // default active phase lock for Option B free roam
static volatile LONG g_currentPhase = -1;  // live inspected phase
static volatile LONG64 g_phaseSwaps;

static void __fastcall h_PreUpdate(void* self, uint32_t slot, int64_t* aiContext) {
	if (o_PreUpdate) o_PreUpdate(self, slot, aiContext);
	if (!aiContext || !*aiContext) return;
	__try {
		uint8_t* knowledge = (uint8_t*)(*aiContext);
		if (Readable(knowledge, 8)) {
			g_currentPhase = (LONG)knowledge[4];
			if (g_forcePhaseOn && g_forcedPhase >= 0 && g_forcedPhase <= 7) {
				uint8_t cur = knowledge[4];
				uint8_t forced = (uint8_t)g_forcedPhase;
				if (cur != forced) {
					auto tableBase = (uint8_t*)Rebase(PhaseForce::StepTable);
					if (tableBase && Readable(tableBase, 0x80)) {
						auto curStep = *(StepFunc_t*)(tableBase + (size_t)cur * PhaseForce::StepEntrySize);
						int32_t curOff = *(int32_t*)(tableBase + (size_t)cur * PhaseForce::StepEntrySize + 8);
						auto forcedStep = *(StepFunc_t*)(tableBase + (size_t)forced * PhaseForce::StepEntrySize);
						int32_t forcedOff = *(int32_t*)(tableBase + (size_t)forced * PhaseForce::StepEntrySize + 8);
						uint8_t* baseThis = (uint8_t*)self + PhaseForce::BaseAdj;
						if (curStep) curStep(baseThis + curOff, slot, 1 /* kStepProcExit */, knowledge, nullptr);
						if (forcedStep) forcedStep(baseThis + forcedOff, slot, 0 /* kStepProcEnter */, knowledge, nullptr);
						knowledge[4] = forced;
						InterlockedIncrement64(&g_phaseSwaps);
					}
				}
			}
		}
	} __except (FaultFilter()) {}
}

static void StartPhaseForceHook() {
	HookAt(PhaseForce::PreUpdate, PhaseForce::PreUpdatePro, sizeof PhaseForce::PreUpdatePro, (void*)h_PreUpdate, (void**)&o_PreUpdate, &g_preUpdateTarget, "PhaseSneakAiImpl_PreUpdate");
}

// ---------------------------------------------------------------- probe10b: Eye Lamp Color Tuning
namespace EyeLamp {
	constexpr uintptr_t UpdateColor    = 0x1418C51B0;
	constexpr uint8_t   UpdateColorPro[10] = { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20 };
}
typedef void (__fastcall *UpdateColor_t)(void*, int32_t);
static UpdateColor_t o_UpdateColor;
static void* volatile g_updateColorTarget;
static volatile LONG g_eyeCustomOn = 0;
static volatile LONG g_discoOn = 0;
static float g_eyeR = 1.0f, g_eyeG = 0.0f, g_eyeB = 0.0f, g_eyeA = 1.0f;
static float g_discoSpeed = 2.0f;
static volatile LONG64 g_eyeColorCalls;

static void ApplyEyePreset(const char* p) {
	if (!p) return;
	if (_stricmp(p, "red") == 0) { g_eyeR = 1.0f; g_eyeG = 0.05f; g_eyeB = 0.05f; g_eyeA = 1.0f; g_eyeCustomOn = 1; g_discoOn = 0; }
	else if (_stricmp(p, "blue") == 0) { g_eyeR = 0.05f; g_eyeG = 0.6f; g_eyeB = 1.0f; g_eyeA = 1.0f; g_eyeCustomOn = 1; g_discoOn = 0; }
	else if (_stricmp(p, "white") == 0) { g_eyeR = 1.0f; g_eyeG = 1.0f; g_eyeB = 1.0f; g_eyeA = 1.0f; g_eyeCustomOn = 1; g_discoOn = 0; }
	else if (_stricmp(p, "purple") == 0) { g_eyeR = 0.85f; g_eyeG = 0.1f; g_eyeB = 1.0f; g_eyeA = 1.0f; g_eyeCustomOn = 1; g_discoOn = 0; }
	else if (_stricmp(p, "amber") == 0) { g_eyeR = 1.0f; g_eyeG = 0.65f; g_eyeB = 0.0f; g_eyeA = 1.0f; g_eyeCustomOn = 1; g_discoOn = 0; }
	else if (_stricmp(p, "green") == 0) { g_eyeR = 0.1f; g_eyeG = 1.0f; g_eyeB = 0.2f; g_eyeA = 1.0f; g_eyeCustomOn = 1; g_discoOn = 0; }
	else if (_stricmp(p, "disco") == 0) { g_discoOn = 1; g_eyeCustomOn = 0; }
	else { g_eyeCustomOn = 0; g_discoOn = 0; }
}

static void __fastcall h_UpdateColor(void* self, int32_t slot) {
	float outR = 0.0f, outG = 0.0f, outB = 0.0f, outA = 1.0f;
	bool haveColor = false;
	if (g_discoOn) {
		float hue = (float)(GetTickCount() % 10000) / 1000.0f * g_discoSpeed;
		float h = hue - floorf(hue);
		int i = (int)(h * 6.0f);
		float f = h * 6.0f - (float)i;
		switch (i % 6) {
			case 0: outR = 1.0f; outG = f; outB = 0.0f; break;
			case 1: outR = 1.0f - f; outG = 1.0f; outB = 0.0f; break;
			case 2: outR = 0.0f; outG = 1.0f; outB = f; break;
			case 3: outR = 0.0f; outG = 1.0f - f; outB = 1.0f; break;
			case 4: outR = f; outG = 0.0f; outB = 1.0f; break;
			default: outR = 1.0f; outG = 0.0f; outB = 1.0f - f; break;
		}
		haveColor = true;
	} else if (g_eyeCustomOn) {
		outR = g_eyeR; outG = g_eyeG; outB = g_eyeB; outA = g_eyeA;
		haveColor = true;
	}
	if (self && haveColor) {
		__try {
			uint8_t* base = *(uint8_t**)((uintptr_t)self + 0x48);
			int32_t baseSlot = *(int32_t*)((uintptr_t)self + 0x58);
			if (base && Readable(base, 0x400)) {
				int64_t offset = (int64_t)(slot - baseSlot) * 0x60 + 0x20;
				float* color = (float*)(base + offset);
				if (Readable(color, 16)) {
					color[0] = outR; color[1] = outG; color[2] = outB; color[3] = outA;
					InterlockedIncrement64(&g_eyeColorCalls);
				}
			}
		} __except (FaultFilter()) {}
	}
	if (o_UpdateColor) o_UpdateColor(self, slot);
}

static void StartEyeColorHook() {
	HookAt(EyeLamp::UpdateColor, EyeLamp::UpdateColorPro, sizeof EyeLamp::UpdateColorPro, (void*)h_UpdateColor, (void**)&o_UpdateColor, &g_updateColorTarget, "UpdateEyeLampColor");
}

// ---------------------------------------------------------------- probe10b: Target Acquisition Diversion (Protects Player)
namespace TargetAcquire {
	constexpr uintptr_t Target       = 0x1418FEDB0; // SahelanAi_AcquirePlayerTarget
	constexpr uint8_t   Prologue[10] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x18 };
}
typedef void (__fastcall *AcquireTarget_t)(void*, uint32_t);
static AcquireTarget_t o_AcquireTarget;
static void* volatile g_acquireTargetPtr;
static volatile LONG g_divertTarget = 1;     // default 1: divert target away from Snake
static volatile LONG64 g_targetDiverts;

static void __fastcall h_AcquireTarget(void* self, uint32_t slot) {
	if (o_AcquireTarget) o_AcquireTarget(self, slot);
	if (!g_divertTarget) return;
	__try {
		float targetPos[4];
		if (g_aimSet) {
			targetPos[0] = g_aim[0]; targetPos[1] = g_aim[1]; targetPos[2] = g_aim[2]; targetPos[3] = 1.0f;
		} else {
			// Decoy position 2000m underground so mech never targets Snake
			targetPos[0] = 0.0f; targetPos[1] = -2000.0f; targetPos[2] = 0.0f; targetPos[3] = 1.0f;
		}
		char* inst = *(char**)((char*)self + 0x78) + (size_t)slot * 0x210;
		if (inst && Readable(inst + 0x140, 16)) {
			memcpy(inst + 0x140, targetPos, 16);
		}
		char* r = *(char**)((char*)self + 0x70);
		if (r && Readable(r + Sahel::RootComp, 8)) {
			void* comp = *(void**)(r + Sahel::RootComp);
			if (comp && Readable(comp, 8) && InExe(*(void**)comp) && InExe(VSlot(comp, Sahel::CompGetVt))) {
				char* ai = (char*)CallCompGet(comp, slot);
				if (ai && Readable(ai + Sahel::Target, 16)) {
					memcpy(ai + Sahel::Target, targetPos, 16);
					InterlockedIncrement64(&g_targetDiverts);
				}
			}
		}
	} __except (FaultFilter()) {}
}

static void StartTargetAcquireHook() {
	HookAt(TargetAcquire::Target, TargetAcquire::Prologue, sizeof TargetAcquire::Prologue, (void*)h_AcquireTarget, (void**)&o_AcquireTarget, &g_acquireTargetPtr, "AcquirePlayerTarget");
}

static void StartDecideHooks() {
	HookAt(Decide::H0, Decide::H0Pro, sizeof Decide::H0Pro, (void*)h_H0, (void**)&o_H0, &g_h0Target, "Decision0");
	HookAt(Decide::Override, Decide::OverridePro, sizeof Decide::OverridePro, (void*)h_Override, (void**)&o_Override, &g_overrideTarget, "OverrideTest");
}
// SahelanControl.SetIntroSkip(bool) / SetKillOverride(bool) / SetIdleHold(bool)
static int l_SetIntroSkip(lua_State* L) { g_cfgIntroSkip = p_toboolean(L, 1) ? 1 : 0; Log("IntroSkip %d", g_cfgIntroSkip); return 0; }
static int l_SetKillOverride(lua_State* L) { g_cfgKillOverride = p_toboolean(L, 1) ? 1 : 0; Log("KillOverride %d", g_cfgKillOverride); return 0; }
// SahelanControl.SetGrenadeLauncher(n): 0 or 3 pins the launcher, -1 alternates
static int l_SetGrenadeLauncher(lua_State* L) { InterlockedExchange(&g_grenadeLauncher, (LONG)p_tonumber(L, 1)); return 0; }
static int l_SetIdleHold(lua_State* L) { InterlockedExchange(&g_idleHold, p_toboolean(L, 1) ? 1 : 0); Log("IdleHold %ld", g_idleHold); return 0; }

// probe6b: per-frame gate at TppSahelan2ActionController::Update(this = controller + 0x20)
namespace Frame {
	constexpr uintptr_t Target = 0x1418BFD30;
	constexpr uint8_t   Prologue[9] = { 0x40, 0x53, 0x56, 0x41, 0x55, 0x48, 0x83, 0xEC, 0x30 };
}
typedef uint64_t (__fastcall *FrameFn_t)(void*, uint64_t, uint64_t, uint64_t);
static FrameFn_t o_Frame;
static void* volatile g_frameTarget;
static volatile LONG64 g_frameCalls;
static uint64_t __fastcall h_Frame(void* self, uint64_t a2, uint64_t a3, uint64_t a4) {
	InterlockedIncrement64(&g_frameCalls);
	if ((g_gate & 16) && g_goalSelf && g_aiStatePtr) GatePre(g_goalSelf, 0, (uint8_t*)g_aiStatePtr);
	if (g_goalSelf) { RefreshSahelPtrs(); InstantMove(); }
	CommandTick();
	return o_Frame(self, a2, a3, a4);
}
static bool StartFrameHook() {
	void* fn = (void*)Rebase(Frame::Target);
	if (!g_frameTarget) {
		if (memcmp(fn, Frame::Prologue, sizeof Frame::Prologue) != 0) { Log("Frame: Update prologue mismatch, frame gate unavailable"); return false; }
		MH_STATUS st = MH_CreateHook(fn, (void*)h_Frame, (void**)&o_Frame);
		if (st != MH_OK) { Log("Frame: create %s", MH_StatusToString(st)); return false; }
		g_frameTarget = fn;
	}
	MH_STATUS st = MH_EnableHook(fn);
	Log("Frame: Update hook %s", MH_StatusToString(st));
	return st == MH_OK || st == MH_ERROR_ENABLED;
}

// SahelanControl.SetNavWorld(useDefault) -> string   (affects mechs created afterwards)
static int l_SetNavWorld(lua_State* L) {
	SetNavWorld(p_toboolean(L, 1) ? NavWorld::Default : NavWorld::Sahelan, "lua");
	char buf[64]; sprintf_s(buf, "navPatch=%ld", g_navPatch); PS(L, buf); return 1;
}

// SahelanControl.SetGate(bits[, typeMask]) -> "gate=.. types=.."
static int l_SetGate(lua_State* L) {
	LONG bits = (LONG)p_tonumber(L, 1) & 31;
	if ((bits & 16) && !g_frameTarget) StartFrameHook();
	if (p_type(L, 2) == 3 /* LUA_TNUMBER */) InterlockedExchange(&g_gateTypes, (LONG)(uint32_t)p_tonumber(L, 2));
	InterlockedExchange(&g_gate, bits);
	char buf[96]; sprintf_s(buf, "%lu GATE set gate=%ld types=%08lX", GetTickCount(), bits, g_gateTypes);
	PushEvent(buf); Log("%s", buf);
	PS(L, buf + 0);
	return 1;
}
// SahelanControl.SetAimPoint(x, y, z)
static int l_SetAimPoint(lua_State* L) {
	g_aim[0] = (float)p_tonumber(L, 1); g_aim[1] = (float)p_tonumber(L, 2); g_aim[2] = (float)p_tonumber(L, 3);
	InterlockedExchange(&g_aimSet, 1);
	return 0;
}
// SahelanControl.PilotJump(x, y, z): let the AI's next jump through, landing at (x,y,z)
static int l_PilotJump(lua_State* L) {
	g_jumpLand[0] = (float)p_tonumber(L, 1); g_jumpLand[1] = (float)p_tonumber(L, 2); g_jumpLand[2] = (float)p_tonumber(L, 3);
	InterlockedExchange(&g_jumpAllow, 1);
	char buf[96]; sprintf_s(buf, "%lu PILOTJUMP armed land=%.1f,%.1f,%.1f", GetTickCount(), g_jumpLand[0], g_jumpLand[1], g_jumpLand[2]);
	PushEvent(buf);
	return 0;
}
// SahelanControl.GetGateStats() -> string
static int l_GetGateStats(lua_State* L) {
	char buf[640];
	sprintf_s(buf, "gate=%ld types=%08lX dropped=%lld steered=%lld targets=%lld jumpsBlocked=%lld jumpAllow=%ld frames=%lld navPatch=%ld direct=%lld instant=%lld tpl=%ld", g_gate, g_gateTypes,
		g_gateDropped, g_gateSteered, g_gateTargets, g_gateJumps, g_jumpAllow, g_frameCalls, g_navPatch, g_directPaths, g_instantMoves, g_haveTpl);
	size_t n = strlen(buf);
	sprintf_s(buf + n, sizeof buf - n, " aiSeen=%ld push=%lld pop=%lld repeat=%lld lost=%lld cancel=%lld openCancel=%lld pushing=%ld skip=%d inj=%d",
		g_aiSeen, g_pushes, g_pops, g_repeats, g_lost, g_cancels, g_openCancels, g_pushSerial, g_cfgSkipOpening, p_Push != nullptr);
	n = strlen(buf);
	sprintf_s(buf + n, sizeof buf - n, " introSkip=%lld idleHold=%lld overrideKill=%lld cfg=%d/%d/%ld hooks=%d/%d",
		g_introSkips, g_idleHolds, g_overrideKills, g_cfgIntroSkip, g_cfgKillOverride, g_idleHold, g_h0Target != nullptr, g_overrideTarget != nullptr);
	PS(L, buf);
	return 1;
}

static int l_StartGoalHook(lua_State* L);
static int l_StartSinkLog(lua_State* L);
static int l_StartSampler(lua_State* L);
static int l_StartPhaseHook(lua_State* L);
static DWORD WINAPI AutoArmThread(LPVOID) {
	int autoarm = 1; long gate = 0; unsigned long types = (unsigned long)g_gateTypes;
	wchar_t path[MAX_PATH]{};
	GetModuleFileNameW(g_self, path, MAX_PATH);
	wchar_t* dot = wcsrchr(path, L'.');
	if (dot) wcscpy_s(dot, MAX_PATH - (dot - path), L"_gate.txt");
	FILE* f = _wfsopen(path, L"r", _SH_DENYNO);
	if (f) {
		char line[200] = {};
		if (fgets(line, sizeof line, f)) {
			const char* p;
			if ((p = strstr(line, "autoarm="))) autoarm = atoi(p + 8);
			if ((p = strstr(line, "gate="))) gate = strtol(p + 5, nullptr, 0);
			if ((p = strstr(line, "types="))) types = strtoul(p + 6, nullptr, 0);
			if ((p = strstr(line, "skipopening="))) g_cfgSkipOpening = atoi(p + 12);
			if ((p = strstr(line, "introskip="))) g_cfgIntroSkip = atoi(p + 10);
			if ((p = strstr(line, "killoverride="))) g_cfgKillOverride = atoi(p + 13);
			if ((p = strstr(line, "idlehold="))) g_idleHold = atoi(p + 9);
			if ((p = strstr(line, "forcedphase="))) { g_forcedPhase = atoi(p + 12); g_forcePhaseOn = (g_forcedPhase >= 0) ? 1 : 0; }
			if ((p = strstr(line, "diverttarget="))) g_divertTarget = atoi(p + 13);
			if ((p = strstr(line, "eyepreset="))) {
				char pr[32] = {}; sscanf_s(p + 10, "%31s", pr, (unsigned)sizeof pr);
				ApplyEyePreset(pr);
			}
		}
		fclose(f);
	}
	Log("AutoArm: autoarm=%d gate=%ld types=%08lX skipopening=%d introskip=%d killoverride=%d idlehold=%ld (config %s)", autoarm, gate, types, g_cfgSkipOpening, g_cfgIntroSkip, g_cfgKillOverride, g_idleHold, f ? "found" : "absent");
	if (!autoarm) return 0;
	Sleep(3000);
	l_StartGoalHook(nullptr);
	l_StartPhaseHook(nullptr);
	l_StartSinkLog(nullptr);
	l_StartSampler(nullptr);
	InterlockedExchange(&g_gateTypes, (LONG)types);
	StartFrameHook();
	StartDirectHooks();
	StartCmdInjection();
	StartDecideHooks();
	StartPhaseForceHook();
	StartEyeColorHook();
	StartTargetAcquireHook();
	InterlockedExchange(&g_gate, gate & 31);
	char buf[96]; sprintf_s(buf, "%lu AUTOARM gate=%ld types=%08lX", GetTickCount(), gate & 31, types);
	PushEvent(buf);
	Log("AutoArm: armed");
	return 0;
}

// SahelanControl.GetInfo() -> string
static int l_GetInfo(lua_State* L) {
	const char* why; void* obj = GetPadObject(&why);
	char buf[200];
	sprintf_s(buf, "%s pad=%s leased=%d mask=%08X second=%d navPatch=%ld", SC_VERSION, why,
		g_leasedObj != nullptr, g_leaseMask, g_leased2, g_navPatch);
	(void)obj;
	PS(L, buf);
	return 1;
}

// SahelanControl.LeasePad([mask=0xFFFFFFFF], [second=true]) -> bool, reason
static int l_LeasePad(lua_State* L) {
	uint32_t mask = (uint32_t)(int64_t)OptNumber(L, 1, 4294967295.0);
	int t2 = p_type(L, 2);
	bool second = (t2 == LUA_TNONE || t2 == LUA_TNIL) ? true : p_toboolean(L, 2) != 0;
	const char* why; void* obj = GetPadObject(&why);
	if (!obj) { PB(L, 0); PS(L, why); return 2; }
	if (g_leasedObj) DoRelease();
	((PadLease_t)VSlot(obj, Pad::VtLease))(obj, Pad::Tag, mask);
	if (second) ((PadLease_t)VSlot(obj, Pad::VtLease2))(obj, Pad::Tag, 1);
	g_leasedObj = obj; g_leased2 = second; g_leaseMask = mask;
	Log("LeasePad obj=%p mask=%08X second=%d", obj, mask, second);
	PB(L, 1); PS(L, "ok");
	return 2;
}

// SahelanControl.ReleasePad() -> bool
static int l_ReleasePad(lua_State* L) {
	bool had = g_leasedObj != nullptr;
	DoRelease();
	PB(L, had);
	return 1;
}

// SahelanControl.GetXInput([pad=0]) -> connected, buttons, lx, ly, rx, ry, lt, rt
// Native fallback if the lease blanks PlayerVars.*Direct. Sticks normalised to -1..1, triggers 0..1.
static int l_GetXInput(lua_State* L) {
	DWORD idx = (DWORD)OptNumber(L, 1, 0);
	XINPUT_STATE s{};
	if (idx > 3 || XInputGetState(idx, &s) != ERROR_SUCCESS) { PB(L, 0); return 1; }
	const XINPUT_GAMEPAD& g = s.Gamepad;
	PB(L, 1);
	p_pushnumber(L, g.wButtons);
	p_pushnumber(L, g.sThumbLX / 32767.0);
	p_pushnumber(L, g.sThumbLY / 32767.0);
	p_pushnumber(L, g.sThumbRX / 32767.0);
	p_pushnumber(L, g.sThumbRY / 32767.0);
	p_pushnumber(L, g.bLeftTrigger / 255.0);
	p_pushnumber(L, g.bRightTrigger / 255.0);
	return 8;
}

// SahelanControl.GetSahelanAi() -> string, empty until a move request has been seen
static int l_GetSahelanAi(lua_State* L) {
	EnterCriticalSection(&g_snapLock);
	SahelSnap c = g_snap;
	LeaveCriticalSection(&g_snapLock);
	if (!c.calls2 && !c.calls3) { PS(L, ""); return 1; }
	char buf[400];
	sprintf_s(buf, "t=%d idx=%u age=%lu c2=%u c3=%u ovr=%u ai=%d mech=%.1f,%.1f,%.1f goal=%.1f,%.1f,%.1f f=%.2f "
		"tgt=%.1f,%.1f,%.1f node=%u:%.1f,%.1f,%.1f fl=%04X goalOn=%d",
		c.type, c.idx, GetTickCount() - c.tick, c.calls2, c.calls3, c.overrides, c.ai != nullptr,
		c.mech[0], c.mech[1], c.mech[2], c.goal[0], c.goal[1], c.goal[2], c.param,
		c.target[0], c.target[1], c.target[2], c.nodeId, c.node[0], c.node[1], c.node[2], c.flags, (int)g_goalOn);
	PS(L, buf);
	return 1;
}

// SahelanControl.StartAiTrace([secs=60], [periodMs=100]) -> bool
// Samples aiData[0..0x400] + mech position to plugins/SahelanControl_aitrace.bin (overwritten each start).
static int l_StartAiTrace(lua_State* L) {
	if (g_traceOn) { PB(L, 0); return 1; }
	DWORD secs = (DWORD)OptNumber(L, 1, 60), period = (DWORD)OptNumber(L, 2, 100);
	if (secs > Trace::MaxSecs) secs = Trace::MaxSecs;
	if (period < 16) period = 16;
	g_tracePeriod = period;
	g_traceUntil = GetTickCount() + secs * 1000;
	InterlockedExchange(&g_traceOn, 1);
	HANDLE t = CreateThread(nullptr, 0, TraceThread, nullptr, 0, nullptr);
	if (!t) { InterlockedExchange(&g_traceOn, 0); PB(L, 0); return 1; }
	CloseHandle(t);
	PB(L, 1);
	return 1;
}

// SahelanControl.StopAiTrace() / GetAiTrace() -> "on=<0|1> samples=<n>"
static int l_StopAiTrace(lua_State* L) { (void)L; InterlockedExchange(&g_traceOn, 0); return 0; }
static int l_GetAiTrace(lua_State* L) {
	char buf[64];
	sprintf_s(buf, "on=%ld samples=%u", g_traceOn, g_traceCount);
	PS(L, buf);
	return 1;
}

// SahelanControl.SetMoveGoal(x, y, z): from now on the move-request hooks replace aiData+0x260
static int l_SetMoveGoal(lua_State* L) {
	g_goal[0] = (float)p_tonumber(L, 1); g_goal[1] = (float)p_tonumber(L, 2); g_goal[2] = (float)p_tonumber(L, 3);
	g_goalOn = true;
	Log("SetMoveGoal (%.1f,%.1f,%.1f)", g_goal[0], g_goal[1], g_goal[2]);
	return 0;
}

// SahelanControl.ClearMoveGoal(): stop overriding
static int l_ClearMoveGoal(lua_State* L) {
	(void)L;
	g_goalOn = false;
	Log("ClearMoveGoal");
	return 0;
}

// SahelanControl.ForcePhase(phase) -> sets phase lock (0..7, or -1 to release)
static int l_ForcePhase(lua_State* L) {
	int ph = (int)p_tonumber(L, 1);
	if (ph >= 0 && ph <= 7) {
		g_forcedPhase = ph;
		g_forcePhaseOn = 1;
		Log("ForcePhase: locked to phase %d", ph);
		PS(L, "locked");
	} else {
		g_forcePhaseOn = 0;
		Log("ForcePhase: released (vanilla AI)");
		PS(L, "released");
	}
	return 1;
}

// SahelanControl.GetPhase() -> current, forced, swaps
static int l_GetPhase(lua_State* L) {
	p_pushnumber(L, (double)g_currentPhase);
	p_pushnumber(L, g_forcePhaseOn ? (double)g_forcedPhase : -1.0);
	p_pushnumber(L, (double)g_phaseSwaps);
	return 3;
}

// SahelanControl.SetDivertTarget(bool) -> enable/disable player targeting protection
static int l_SetDivertTarget(lua_State* L) {
	g_divertTarget = p_toboolean(L, 1) ? 1 : 0;
	Log("DivertTarget %ld", g_divertTarget);
	PB(L, g_divertTarget);
	return 1;
}

// SahelanControl.SetEyeColor(r, g, b[, a])
static int l_SetEyeColor(lua_State* L) {
	g_eyeR = (float)p_tonumber(L, 1);
	g_eyeG = (float)p_tonumber(L, 2);
	g_eyeB = (float)p_tonumber(L, 3);
	g_eyeA = (float)OptNumber(L, 4, 1.0);
	g_eyeCustomOn = 1;
	g_discoOn = 0;
	char buf[80]; sprintf_s(buf, "EyeColor set: %.2f, %.2f, %.2f, %.2f", g_eyeR, g_eyeG, g_eyeB, g_eyeA);
	Log("%s", buf); PS(L, buf);
	return 1;
}

// SahelanControl.SetEyePreset(name) -> "red", "blue", "white", "purple", "amber", "green", "vanilla"
static int l_SetEyePreset(lua_State* L) {
	typedef const char* (__fastcall *tostr_t)(lua_State*, int, size_t*);
	const char* p = p_type(L, 1) == 4 ? ((tostr_t)Rebase(Addr::lua_tolstring))(L, 1, nullptr) : "";
	ApplyEyePreset(p);
	char buf[64]; sprintf_s(buf, "Preset: %s", p ? p : ""); Log("%s", buf); PS(L, buf);
	return 1;
}

// SahelanControl.SetDisco(enabled[, speed])
static int l_SetDisco(lua_State* L) {
	g_discoOn = p_toboolean(L, 1) ? 1 : 0;
	g_discoSpeed = (float)OptNumber(L, 2, 2.0);
	g_eyeCustomOn = 0;
	char buf[64]; sprintf_s(buf, "Disco %s (speed %.1f)", g_discoOn ? "ON" : "OFF", g_discoSpeed);
	Log("%s", buf); PS(L, buf);
	return 1;
}

// SahelanControl.GetEyeStatus() -> customOn, discoOn, colorCalls
static int l_GetEyeStatus(lua_State* L) {
	p_pushboolean(L, g_eyeCustomOn);
	p_pushboolean(L, g_discoOn);
	p_pushnumber(L, (double)g_eyeColorCalls);
	return 3;
}

// SahelanControl.GetWeapon(slot) -> name, type, variant, cut
static int l_GetWeapon(lua_State* L) {
	int slot = (int)p_tonumber(L, 1);
	if (slot >= 1 && slot <= 7) {
		PS(L, g_weapons[slot].name);
		p_pushnumber(L, g_weapons[slot].type);
		p_pushnumber(L, g_weapons[slot].variant);
		p_pushnumber(L, g_weapons[slot].cut);
		return 4;
	}
	return 0;
}

static const luaL_Reg g_lib[] = {
	{ "GetInfo",    l_GetInfo },
	{ "LeasePad",   l_LeasePad },
	{ "ReleasePad", l_ReleasePad },
	{ "GetXInput",  l_GetXInput },
	{ "GetSahelanAi",  l_GetSahelanAi },
	{ "SetMoveGoal",   l_SetMoveGoal },
	{ "ClearMoveGoal", l_ClearMoveGoal },
	{ "StartAiTrace",  l_StartAiTrace },
	{ "StopAiTrace",   l_StopAiTrace },
	{ "GetAiTrace",    l_GetAiTrace },
	{ "StartCoverage", l_StartCoverage },
	{ "StopCoverage",  l_StopCoverage },
	{ "Mark",          l_Mark },
	{ "StartSinkLog",  l_StartSinkLog },
	{ "StopSinkLog",   l_StopSinkLog },
	{ "GetSinkLog",    l_GetSinkLog },
	{ "ReadHex",       l_ReadHex },
	{ "ReadQ",         l_ReadQ },
	{ "GetExeBase",    l_GetExeBase },
	{ "ProbeExecutor", l_ProbeExecutor },
	{ "StartMoveLog",  l_StartMoveLog },
	{ "StopMoveLog",   l_StopMoveLog },
	{ "StartGoalHook", l_StartGoalHook },
	{ "StopGoalHook",  l_StopGoalHook },
	{ "SetPilotGoal",  l_SetPilotGoal },
	{ "ClearPilotGoal", l_ClearPilotGoal },
	{ "GetPilotStats", l_GetPilotStats },
	{ "Panic",         l_Panic },
	{ "StartSampler",  l_StartSampler },
	{ "StopSampler",   l_StopSampler },
	{ "GetMechState",  l_GetMechState },
	{ "StartPhaseHook", l_StartPhaseHook },
	{ "StopPhaseHook", l_StopPhaseHook },
	{ "StartJumpHooks", l_StartJumpHooks },
	{ "StopJumpHooks", l_StopJumpHooks },
	{ "PilotMark",     l_PilotMark },
	{ "SetGate",       l_SetGate },
	{ "SetAimPoint",   l_SetAimPoint },
	{ "PilotJump",     l_PilotJump },
	{ "GetGateStats",  l_GetGateStats },
	{ "SetNavWorld",   l_SetNavWorld },
	{ "PilotFire",     l_PilotFire },
	{ "PilotCommand",  l_PilotCommand },
	{ "SetWeapon",     l_SetWeapon },
	{ "PilotJumpNow",  l_PilotJumpNow },
	{ "SkipOpening",   l_SkipOpening },
	{ "SetIntroSkip",  l_SetIntroSkip },
	{ "SetKillOverride", l_SetKillOverride },
	{ "SetIdleHold",   l_SetIdleHold },
	{ "SetGrenadeLauncher", l_SetGrenadeLauncher },
	{ "ForcePhase",       l_ForcePhase },
	{ "GetPhase",         l_GetPhase },
	{ "SetDivertTarget",  l_SetDivertTarget },
	{ "SetEyeColor",      l_SetEyeColor },
	{ "SetEyePreset",     l_SetEyePreset },
	{ "SetDisco",         l_SetDisco },
	{ "GetEyeStatus",     l_GetEyeStatus },
	{ "GetWeapon",        l_GetWeapon },
	{ nullptr, nullptr }
};

static void RegisterLib(lua_State* L) {
	p_FoxLuaRegisterLibrary(L, "SahelanControl", g_lib);
	p_settop(L, -2); // FoxLuaRegisterLibrary leaves the library table on the stack
	Log("Registered SahelanControl library in lua_State %p", L);
}

// Fallback for Lua: package.loadlib("plugins\\SahelanControl.dll", "luaopen_SahelanControl")()
extern "C" __declspec(dllexport) int luaopen_SahelanControl(lua_State* L) {
	if (!p_FoxLuaRegisterLibrary) return 0;
	p_FoxLuaRegisterLibrary(L, "SahelanControl", g_lib);
	return 1;
}

// ---------------------------------------------------------------- hooks
static void __fastcall h_SetLuaFunctions(lua_State* L) {
	o_SetLuaFunctions(L);
	if (g_leasedObj) { Log("Lua state rebuilt while leased, dropping lease"); DoRelease(); }
	g_player = nullptr; // force recapture for the new mission state
	RegisterLib(L);
}

static void __fastcall h_SetupWeaponInfo(void* self, void* a2, uint32_t a3, void* a4) {
	if (self && Readable((char*)self + 8, 8)) {
		void* p = *(void**)((char*)self + 8);
		if (p && p != g_player) { g_player = p; Log("Captured player %p (SetupWeaponInfo this=%p)", p, self); }
	}
	o_SetupWeaponInfo(self, a2, a3, a4);
}

// ---------------------------------------------------------------- init
static bool StartsWithJmp(uintptr_t a) { return *(uint8_t*)a == 0xE9; }

static bool Hook(const char* name, uintptr_t va, void* detour, void** orig) {
	void* target = (void*)Rebase(va);
	uint8_t* b = (uint8_t*)target;
	Log("%-16s %p bytes %02X %02X %02X %02X %02X", name, target, b[0], b[1], b[2], b[3], b[4]);
	MH_STATUS s = MH_CreateHook(target, detour, orig);
	if (s == MH_OK) s = MH_EnableHook(target);
	if (s != MH_OK) { Log("  hook FAILED: %s", MH_StatusToString(s)); return false; }
	Log("  hooked");
	return true;
}

static DWORD WINAPI InitThread(LPVOID) {
	// IHHook loads plugins alphabetically, so we load before V_FrameWork.dll. V_Framework validates
	// prologues before hooking, so chain each target only after V_Framework has hooked it. The targets
	// are independent: V_Framework hooks SetLuaFunctions early but SetupWeaponInfo from a feature module
	// that can install later, and waiting for both made us miss the boot-time SetLuaFunctions call (r1).
	MH_STATUS s = MH_Initialize();
	if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) { Log("MH_Initialize failed: %s", MH_StatusToString(s)); return 0; }
	{ HANDLE at = CreateThread(nullptr, 0, AutoArmThread, nullptr, 0, nullptr); if (at) CloseHandle(at); }

	// Sahelan move-request builders: V_Framework doesn't hook them, so hook right away (warn if someone did).
	static const uintptr_t sahelTargets[] = { Addr::SahelanMoveReqType3, Addr::SahelanMoveReqType2 };
	for (uintptr_t va : sahelTargets)
		if (StartsWithJmp(Rebase(va))) Log("WARNING: %p already starts with a jmp (another hook?)", (void*)Rebase(va));
	Hook("SahelanMoveReq3", Addr::SahelanMoveReqType3, (void*)h_MoveReq3, (void**)&o_MoveReq3);
	Hook("SahelanMoveReq2", Addr::SahelanMoveReqType2, (void*)h_MoveReq2, (void**)&o_MoveReq2);

	struct Target { const char* name; uintptr_t va; void* detour; void** orig; DWORD limitMs; bool done; };
	Target targets[] = {
		{ "SetLuaFunctions", Addr::SetLuaFunctions, (void*)h_SetLuaFunctions, (void**)&o_SetLuaFunctions, 3000, false },
		{ "SetupWeaponInfo", Addr::SetupWeaponInfo, (void*)h_SetupWeaponInfo, (void**)&o_SetupWeaponInfo, 30000, false },
	};
	for (DWORD waited = 0; ; waited += 1) {
		bool vfw = GetModuleHandleW(L"V_FrameWork.dll") != nullptr;
		bool pending = false;
		for (Target& t : targets) {
			if (t.done) continue;
			bool ready = vfw && StartsWithJmp(Rebase(t.va));
			if (ready || waited >= t.limitMs) {
				Log("%s: V_FrameWork %s, jmp=%d, after %lu ms", t.name, vfw ? "present" : "absent", ready, waited);
				Hook(t.name, t.va, t.detour, t.orig);
				t.done = true;
			}
			else pending = true;
		}
		if (!pending) break;
		Sleep(1);
	}
	Log("Init done");
	return 0;
}

namespace NavWorld {
	constexpr uintptr_t Movabs = 0x1418BAE48;   // 48 B8 <imm64 StrCode64("sahelan")>  in TppSahelan2::Setup
	constexpr uint8_t   Orig[10] = { 0x48, 0xB8, 0xBF, 0x33, 0x48, 0x9F, 0x62, 0x7D, 0x00, 0x00 };
}
static void ReadConfigEarly() {
	wchar_t path[MAX_PATH]{};
	GetModuleFileNameW(g_self, path, MAX_PATH);
	wchar_t* dot = wcsrchr(path, L'.');
	if (dot) wcscpy_s(dot, MAX_PATH - (dot - path), L"_gate.txt");
	FILE* f = _wfsopen(path, L"r", _SH_DENYNO);
	if (!f) return;
	char line[200] = {};
	if (fgets(line, sizeof line, f)) { const char* p = strstr(line, "navworld="); if (p) g_cfgNavWorld = atoi(p + 9); }
	fclose(f);
}
static void SetNavWorld(uint64_t hash, const char* why) {
	uint8_t* p = (uint8_t*)Rebase(NavWorld::Movabs);
	if (p[0] != 0x48 || p[1] != 0xB8) { InterlockedExchange(&g_navPatch, -1); Log("NavWorld: movabs not found (%s)", why); return; }
	uint64_t cur; memcpy(&cur, p + 2, 8);
	if (cur != NavWorld::Sahelan && cur != NavWorld::Default) {
		InterlockedExchange(&g_navPatch, -1);
		Log("NavWorld: unexpected immediate %016llX, not patching (%s)", (unsigned long long)cur, why);
		return;
	}
	DWORD old;
	if (!VirtualProtect(p + 2, 8, PAGE_EXECUTE_READWRITE, &old)) { Log("NavWorld: VirtualProtect failed"); return; }
	memcpy(p + 2, &hash, 8);
	VirtualProtect(p + 2, 8, old, &old);
	FlushInstructionCache(GetCurrentProcess(), p, 10);
	InterlockedExchange(&g_navPatch, hash == NavWorld::Default ? 1 : 0);
	Log("NavWorld: Sahelan nav world -> %s (%016llX) [%s]; takes effect for mechs created from now on",
		hash == NavWorld::Default ? "\"\" default map world" : "\"sahelan\" arena world", (unsigned long long)hash, why);
}

static bool CheckGameVersion() {
	wchar_t path[MAX_PATH]{};
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	wchar_t* slash = wcsrchr(path, L'\\');
	if (!slash) return false;
	wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"version_info.txt");
	FILE* f = nullptr;
	if (_wfopen_s(&f, path, L"rb") || !f) { Log("version_info.txt unreadable"); return false; }
	char buf[128]{};
	fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	Log("version_info: %s", buf);
	return strstr(buf, "Tpp_steam_mst_en_day3900") != nullptr;
}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
		g_self = mod;
		DisableThreadLibraryCalls(mod);
		InitializeCriticalSection(&g_logLock);
		InitializeCriticalSection(&g_snapLock);
		InitializeCriticalSection(&g_covLock);
		OpenLog();
		Log(SC_VERSION " loaded");

		g_exeBase = (uintptr_t)GetModuleHandleW(nullptr);
		auto dos = (IMAGE_DOS_HEADER*)g_exeBase;
		auto nt = (IMAGE_NT_HEADERS64*)(g_exeBase + dos->e_lfanew);
		g_exeEnd = g_exeBase + nt->OptionalHeader.SizeOfImage;
		Log("exe base %p size %08X", (void*)g_exeBase, nt->OptionalHeader.SizeOfImage);

		if (!CheckGameVersion()) { Log("Unsupported game build, staying inactive"); return TRUE; }

		p_settop      = (lua_settop_t)Rebase(Addr::lua_settop);
		p_tonumber    = (lua_tonumber_t)Rebase(Addr::lua_tonumber);
		p_toboolean   = (lua_toboolean_t)Rebase(Addr::lua_toboolean);
		p_type        = (lua_type_t)Rebase(Addr::lua_type);
		p_pushnumber  = (lua_pushnumber_t)Rebase(Addr::lua_pushnumber);
		p_pushboolean = (lua_pushboolean_t)Rebase(Addr::lua_pushboolean);
		p_pushstring  = (lua_pushstring_t)Rebase(Addr::lua_pushstring);
		p_FoxLuaRegisterLibrary = (FoxLuaRegisterLibrary_t)Rebase(Addr::FoxLuaRegisterLibrary);

		ReadConfigEarly();
		if (g_cfgNavWorld) SetNavWorld(NavWorld::Default, "load");
		else { InterlockedExchange(&g_navPatch, 2); Log("NavWorld: disabled by config (navworld=0)"); }

		HANDLE t = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
		if (t) CloseHandle(t);
	}
	return TRUE;
}
