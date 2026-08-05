# Naming: the `ZP_` / `ZeroPower` residue and the 2026-08-05 rename

## Where the old names came from

The D3D11 backend was developed inside a separate mod tree, **zpower** ("Zero
Power"), and later extracted into this repository as a standalone renderer
project. The extraction renamed the *project* — README, docs, executable — but
not the *identifiers*, so the code kept a `ZP_` / `ZeroPower` / `zp` prefix that
names a project W3DNext has nothing to do with. Symbols with no renderer
relevance came along for the ride (the shell wallpaper rotator, the BW
screen-filter test hook).

This document records the sweep that fixed it, and the two things deliberately
left alone.

## What changed

| Before | After |
|---|---|
| `ZP_D3D11_LOG`, `ZP_FRAMEDUMP_FRAMES`, … (env) | `W3DNEXT_D3D11_LOG`, `W3DNEXT_FRAMEDUMP_FRAMES`, … |
| `ZeroPowerDevMode` (INI key) | `W3DNextDevMode` |
| `TheZPUnattendedHarness` | `TheW3DNextUnattendedHarness` |
| `m_zpDevMode`, `m_zpBWFilterAtFrame` | `m_w3dNextDevMode`, `m_w3dNextBWFilterAtFrame` |
| `zpQuad`, `zpSway`, `s_zpWallpapers`, … (locals/statics) | `w3dNextQuad`, `w3dNextSway`, `s_w3dNextWallpapers`, … |
| `ZP_Decal_VB_Lock`, `ZP_Dump_DX8_Back_Buffer` | `W3DNext_Decal_VB_Lock`, `W3DNext_Dump_DX8_Back_Buffer` |
| `zp_screenshot_NNN.png` (screenshot leafname) | `w3dnext_screenshot_NNN.png` |

The `W3DNEXT_` env prefix was not invented here — `W3DNEXT_SCRSHOTS`
(`W3DDisplay.cpp`) already used it. The sweep makes the rest consistent with it.

## Back-compat: the old names still work

Both user-facing surfaces keep a fallback, so existing harness scripts, shell
profiles, and dev INIs do not break:

- **Env knobs.** All diagnostic knobs are read through `W3DNext_GetEnv(suffix)`
  (`Backend/RenderBackend.{h,cpp}`), which tries `W3DNEXT_<suffix>` first and
  falls back to `ZP_<suffix>`. Callers pass the *unprefixed* suffix, so no call
  site names either prefix.
- **INI key.** `GlobalData::s_GlobalDataFieldParseTable` carries both
  `W3DNextDevMode` and `ZeroPowerDevMode`, pointing at the same field.

The fallbacks are a courtesy, not a contract — prefer the new names.

## Verification (2026-08-05, win32-vcpkg Release, retail Zero Hour install)

Build: `cmake --build build\win32-vcpkg --config Release --target z_generals`
exit 0. Then, against the rebuilt `generalszh.exe`:

**Env knob, both spellings.** Log sink redirected per launch; the line only
lands at the custom path if the variable was actually read.

    W3DNEXT_D3D11_LOG=<path>  ->  [RenderBackend] constructed D3D11Backend (-gfxBackend d3d11)
    ZP_D3D11_LOG=<path>       ->  [RenderBackend] constructed D3D11Backend (-gfxBackend d3d11)
    NC: no -d3d11 flag        ->  [RenderBackend] constructed DX8Backend (default path)

The NC proves the line discriminates rather than always printing.

**INI key.** An additive `Data\INI\GameData\W3DNextDevMode.ini` (`GameData` /
`W3DNextDevMode = Yes` / `END`) enabled dev mode, so the backend badge drew —
the badge is gated on that field, so its appearance *is* the proof the renamed
key parsed:

    with the .ini, -d3d11   ->  badge reads "D3D11"
    with the .ini, no flag  ->  badge reads "DX8"
    NC: .ini removed        ->  top-left corner empty, no badge

The NC rules out dev mode having been on for some other reason. The override
file was deleted afterwards; the install is back to its prior state.

**Caught by the build, worth remembering:** `ZP_D3D11_W3D_TU` looked like
another env knob but is a compile-time TU guard, set via
`target_compile_definitions` in `WW3D2/CMakeLists.txt`. Renaming it in the
sources alone left the guard permanently false, so `D3D11Backend.cpp` and
`D3D11Backend_W3D.cpp` both emitted the same method bodies — 11× LNK2005 at
link. The define has to move with the macro.

## Deliberately NOT renamed

- **`ZeroPowerMenu.tga` / `ZeroPowerMenu<N>` (`Shell.cpp`).** These are *asset
  filenames* looked up on disk, not identifiers. W3DNext ships no assets, so the
  art they name lives in the zpower install; renaming the string would break
  that lookup and fix nothing. They stay until the wallpaper feature is either
  removed from this tree or given W3DNext art of its own.
- **`docs/architecture/d3d11-parity-log.md` and `d3d11-realgame-stub-gap.md`.**
  These are dated evidence records — A/B measurements, gate verdicts, env
  invocations as they were actually run. Rewriting the names inside them would
  make the record describe commands that were never executed. Each carries a
  banner pointing here instead.
