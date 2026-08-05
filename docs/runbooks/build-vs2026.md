# Building W3DNext on Visual Studio 2026 (VS v18)

First-time build notes for a machine that has VS2026 but has never built this repo.
The game is a **32-bit (Win32)** target; the preferred preset is `win32-vcpkg`
(Release), which the publisher (`zpublish.bat`) ships.

## Prerequisites this repo needs but VS2026 may not install by default

VS2026 Community ships the MSVC compiler (`cl.exe`, toolset 14.5x) but **not**
these, which the build requires:

| Missing piece | Symptom if absent | Install |
|---|---|---|
| **CMake** | `cmake: command not found` | `winget install -e --id Kitware.CMake` |
| **Ninja** (generator is Ninja Multi-Config) | configure can't find a generator | `winget install -e --id Ninja-build.Ninja` |
| **C++ ATL** | `fatal error C1083: Cannot open include file: 'atlbase.h'` (in the PCH) | VS Installer → Modify VS2026 → Individual components → **C++ ATL for latest build tools (x86 & x64)** |

### Installing the ATL component from the CLI (the gotchas)

The GUI (VS Installer → Modify → tick ATL) is the reliable path. If you script it,
**both** of these bite:

1. **Spaces in `--installPath` get truncated** — `setup.exe` receives
   `C:\Program Files\...` unquoted and reads it as `C:\Program` →
   *"An installed product matching the following parameters cannot be found."*
   Pass the path as a **single verbatim-quoted string**, not a short (8.3) path
   (the short path doesn't match the registered instance either).
2. **`--quiet` can't elevate** → `Exit Code: 5007`. Use an already-elevated
   context (e.g. launch from a VS Installer that's already open) or `--passive`
   with real elevation.

Working invocation (targets the v18 instance by its registered long path):

```powershell
Start-Process 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\setup.exe' `
  -ArgumentList 'modify --installPath "C:\Program Files\Microsoft Visual Studio\18\Community" --add Microsoft.VisualStudio.Component.VC.ATL --passive --norestart' `
  -Verb RunAs
```

## Build

From an **x86** VS2026 developer environment (so `cl.exe` targets Win32 and
`VCPKG_ROOT` is set) with CMake + Ninja on PATH:

```
cmake --preset win32-vcpkg
cmake --build build\win32-vcpkg --config Release --target z_generals
```

`z_generals` → `build\win32-vcpkg\GeneralsMD\Release\generalszh.exe` (Zero Hour).
Enter the env with `Launch-VsDevShell.ps1 -Arch x86 -HostArch amd64`.

## Toolchain-specific build breaks already fixed in-tree

- **zlib `Byte` clash** (`W3DDisplay.cpp`, PNG screenshot encoder): vcpkg's zlib
  is not built with `Z_PREFIX`, so its `zconf.h` does a real
  `typedef unsigned char Byte`, colliding with the engine's `typedef char Byte`
  (C2371). Fixed by `#define __MACTYPES__` around the `#include <zlib.h>` (zlib
  gates that typedef behind `__MACTYPES__`).
- **C4189/C4101 as errors in Release** (`BaseTypeCore.h`): the project elevates
  "unused local" warnings to errors via `#pragma warning(error: ...)`. STRATAGEM's
  `DEBUG_LOG`-only locals are unused in Release (DEBUG_LOG is a no-op there) and
  trip `/WX`. The pragmas are now guarded `#ifndef NDEBUG` — errors in Debug,
  disabled in Release.


## LAN multiplayer

Confirmed LAN-compatible when both machines run the **same** published build
(both machines running identical bytes; the base netcode is stock
TheSuperHackers lockstep, STRATAGEM ships disabled).
