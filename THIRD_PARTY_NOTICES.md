# Third-party notices

## Bundled code

### MinHook (src/SahelanControl/minhook)
```
  MinHook - The Minimalistic API Hooking Library for x64/x86
  Copyright (C) 2009-2017 Tsuda Kageyu.
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
  TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
  PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
  OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### Hacker Disassembler Engine 64 (src/SahelanControl/minhook/hde, part of MinHook)
```
 Hacker Disassembler Engine 64 C
 Copyright (c) 2008-2009, Vyacheslav Patkov.
 All rights reserved.
```
Distributed with MinHook under MinHook's BSD 2-Clause terms above.

## Not bundled (required or optional at runtime)
- **Infinite Heaven** and **IHHook** by TinManTex (Infinite Heaven r267 fork by caplag). Not included; install them
  from Nexus Mods. The `lua/mod/modules/InfBossTppSahelan2.lua` module uses Infinite Heaven's boss-event API.
- **V_Framework** by Yazed0071 (MIT). Not included. ControlSally chains its eye-lamp hook after V_Framework's when both
  are installed, and some hook addresses and structure layouts were first identified by V_Framework's address tables.
- **SnakeBite Mod Manager**. Not included.

## Game
Metal Gear Solid V: The Phantom Pain is (c) Konami Digital Entertainment. This repository contains no game files,
executables or extracted assets. The `.mgsv` spawn package is built from the player's own game files
(`tools/build_sahelan_unbound.py`).
