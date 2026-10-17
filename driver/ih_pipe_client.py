"""IHHook named-pipe client that gets past the 512-byte message limit.

IHHook's PipeServer reads at most 512 bytes per message (BUFSIZE), and IH splits each message on '|'
(InfExtToMgsv.ProcessCommands), so a raw DoScript payload must stay short and contain no '|'.

    from ih_pipe_client import IHPipe
    with IHPipe() as ih:
        ih.run(big_lua)                         # direct if it fits, chunked otherwise
        ih.run(big_lua, mode="file")            # write live_payload.lua, loadfile() it in game
        print(ih.eval("return vars.missionCode"))

Every payload runs under pcall in game; errors come back as replies tagged 'SCErr'.
Chunk mode keeps no state between calls except the global _SCPB, cleared after each run.

GOTCHA: Fox's _G has a __newindex that silently drops new globals ('X=1' in one DoScript is nil in the
next). Persist state with rawset(_G,'X',v), or as fields of an existing table (InfCore.X=v).
"""
import ctypes
import itertools
import msvcrt
import sys
import time
from ctypes import wintypes
from pathlib import Path

PIPE_IN = r"\\.\pipe\mgsv_in"
PIPE_OUT = r"\\.\pipe\mgsv_out"
MAX_MSG = 500  # BUFSIZE 512, minus a margin for the terminator
PAYLOAD_FILE = Path(__file__).with_name("live_payload.lua")

_k32 = ctypes.WinDLL("kernel32", use_last_error=True)
_k32.PeekNamedPipe.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p,
                               ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
_k32.PeekNamedPipe.restype = wintypes.BOOL

# Runs the string in local s under pcall, reports load/runtime errors on the out pipe.
_EXEC = ("local f,e=loadstring(s) if f then local ok,r=pcall(f) e=not ok and r end "
         "if e then IHH.QueuePipeOutMessage('0'..string.char(124)..'SCErr'..string.char(124)..tostring(e)) end")

_APPEND = "_SCPB[#_SCPB+1]="  # _SCPB is created with rawset, so plain reads/field writes work


def lua_quote(text: str) -> str:
    """Lua 5.1 single-quoted literal with no '|' and nothing that can break the message framing."""
    out = []
    for b in text.encode("utf-8"):
        c = chr(b)
        if c in "\\'":
            out.append("\\" + c)
        elif 32 <= b < 127 and c != "|":
            out.append(c)
        else:
            out.append(f"\\{b:03d}")
    return "'" + "".join(out) + "'"


class IHPipe:
    def __init__(self):
        self.w = open(PIPE_IN, "wb", buffering=0)
        self.r = open(PIPE_OUT, "rb", buffering=0)
        self._rh = msvcrt.get_osfhandle(self.r.fileno())
        self._ids = itertools.count(1)
        self._tags = itertools.count(1)
        self._sync()

    def _sync(self, attempts: int = 20, streak: int = 3):
        """IHHook keeps one PipeOutThread alive per past client until it fails a write, and they all race
        to pop the same queue, so each stale thread eats one reply before dying. Ping until `streak`
        replies in a row get through, which means the stale threads are (very likely) gone."""
        ok = 0
        for _ in range(attempts):
            try:
                self.eval("return 1", timeout=0.6)
                ok += 1
                if ok >= streak:
                    return
            except TimeoutError:
                ok = 0
        raise ConnectionError("no reply from game over the pipe (paused, loading, or IH not running?)")

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def close(self):
        self.w.close()
        self.r.close()

    # ------------------------------------------------------------ send
    def send(self, cmd: str, *args: str):
        msg = "|".join([str(next(self._ids)), cmd, *args]).encode("utf-8")
        if len(msg) > MAX_MSG:
            raise ValueError(f"pipe message is {len(msg)} bytes (max {MAX_MSG})")
        self.w.write(msg)

    def _direct(self, lua: str) -> str:
        return f"local s={lua_quote(lua)} {_EXEC}"

    def run(self, lua: str, mode: str = "auto"):
        """mode: 'auto' (direct if it fits, else chunk), 'chunk', or 'file'."""
        if mode == "file":
            PAYLOAD_FILE.write_text(lua, encoding="utf-8")
            path = PAYLOAD_FILE.as_posix()
            self.send("DoScript",
                      f"local f,e=loadfile('{path}') if f then local ok,r=pcall(f) e=not ok and r end "
                      "if e then IHH.QueuePipeOutMessage('0'..string.char(124)..'SCErr'..string.char(124)..tostring(e)) end")
            return
        direct = self._direct(lua)
        if mode == "auto" and len(direct.encode()) + 16 <= MAX_MSG:
            self.send("DoScript", direct)
            return
        self.send("DoScript", "rawset(_G,'_SCPB',{})")
        for piece in self._chunks(lua):
            self.send("DoScript", f"{_APPEND}{piece}")
        self.send("DoScript", f"local s=table.concat(_SCPB) rawset(_G,'_SCPB',nil) {_EXEC}")

    def _chunks(self, lua: str):
        budget = MAX_MSG - len("99999|DoScript|" + _APPEND)
        piece = ""
        for ch in lua:
            q = lua_quote(piece + ch)
            if len(q) > budget:
                yield lua_quote(piece)
                piece = ch
            else:
                piece += ch
        if piece:
            yield lua_quote(piece)

    # ------------------------------------------------------------ receive
    def _available(self) -> int:
        n = wintypes.DWORD(0)
        if not _k32.PeekNamedPipe(self._rh, None, 0, None, ctypes.byref(n), None):
            raise OSError(ctypes.get_last_error(), "PeekNamedPipe failed")
        return n.value

    def recv(self, timeout: float = 1.0):
        """All replies that arrive within timeout, as lists of '|' fields."""
        out, end = [], time.monotonic() + timeout
        while time.monotonic() < end:
            if self._available():
                raw = self.r.read(4096).decode("utf-8", "replace").rstrip("\0")
                out.extend(m.split("|") for m in raw.split("\0") if m)
            else:
                time.sleep(0.02)
        return out

    def eval(self, lua_body: str, timeout: float = 2.0, mode: str = "auto", retries: int = 0):
        """Run a body that returns a value; return tostring(value), raising on an in-game error.
        retries > 0 re-sends on a lost reply, so only use it for side-effect-free bodies."""
        for attempt in range(retries + 1):
            try:
                return self._eval_once(lua_body, timeout, mode)
            except TimeoutError:
                if attempt == retries:
                    raise

    def _eval_once(self, lua_body: str, timeout: float, mode: str):
        tag = f"SCR{next(self._tags)}"
        self.run(f"local function F() {lua_body} end local v=F() "
                 f"IHH.QueuePipeOutMessage('0'..string.char(124)..'{tag}'..string.char(124)..tostring(v))", mode)
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            for fields in self.recv(0.1):
                if len(fields) >= 3 and fields[1] == tag:
                    return "|".join(fields[2:])
                if len(fields) >= 3 and fields[1] == "SCErr":
                    raise RuntimeError("in-game Lua error: " + "|".join(fields[2:]))
        raise TimeoutError(f"no reply for {tag}")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    with IHPipe() as ih:
        print("mission:", ih.eval("return vars.missionCode"))
        big = "local t={} for i=1,50 do t[#t+1]='x|'..i end return #table.concat(t)" + " " * 600
        print("chunked |-safe payload:", ih.eval(big))
        print("file mode:", ih.eval("return 'loadfile ok'", mode="file"))
