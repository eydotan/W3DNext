# D3D11 backend: real-game bring-up stub-gap inventory

Recon for RENDERER_PORT.md **step 10, phase 0** (branch `renderer-d3d11`). Goal:
with the D3D11 backend *selectable* in the real game, enumerate which
currently-stubbed / partial `IRenderBackend` methods the real bring-up path hits,
so the rest of step 10 can be scoped. **This is a work-list, not a claim the game
renders.** No correct first frame was produced or attempted.

## What was wired (the only behavioral change)

- **Selection flag, default OFF.** `GlobalData::m_gfxBackendD3D11` (default `FALSE`).
  Set by `-gfxBackend d3d11` (alias `-d3d11`) in `CommandLine.cpp`
  (`parseGfxBackend`/`parseD3D11`, registered in `paramsForStartup`).
- **Backend construction switch.** `Init_Render_Backend()`
  (`Core/.../WW3D2/Backend/RenderBackend.cpp`) constructs `DX8Backend` when the
  flag is off (byte-identical default path) and `D3D11Backend` when on. Because
  Core cannot see `TheGlobalData`, the choice is pushed in via a Core setter
  `Set_Use_D3D11_Backend(bool)`, called from `W3DDisplay::init` immediately before
  `WW3D::Init(ApplicationHWnd)`.
- **HWND + size:** *no new plumbing needed.* The existing call site
  `dx8wrapper.cpp:398` already does
  `g_renderBackend->Initialize(_Hwnd, ResolutionWidth, ResolutionHeight)` inside
  `Do_Onetime_Device_Dependent_Inits()` (after the DX8 device is up). `_Hwnd` is
  the `ApplicationHWnd` passed to `WW3D::Init`; `ResolutionWidth/Height` are the
  chosen backbuffer size. For `DX8Backend`, `Initialize` is an inherited no-op;
  for `D3D11Backend`, it creates the device/swapchain/depth/pipeline on that HWND.
- **Default-path safety:** with the flag absent, `Init_Render_Backend()` runs the
  exact original `new DX8Backend()` line and logs
  `"[RenderBackend] constructed DX8Backend (default path)"`. The D3D11 branch is
  never reached. Verified: full game build clean, D3D11 smoke test still PASS.

## Stub instrumentation

`D3D11_STUB()` (was `((void)0)`) now expands to `D3D11_Trace_Once(__FUNCTION__,
"stub")`, which records each distinct method the FIRST time it fires, in first-hit
order, to env `ZP_D3D11_LOG` (else `d3d11_backend.log` in CWD) + `OutputDebugString`.
Records-only bind points (`Set_Vertex_Buffer(VertexBufferClass*)`,
`Set_Index_Buffer(IndexBufferClass*)`, dynamic-IB overload,
`Set_Index_Buffer_Index_Offset`, `Set_Texture`, `Set_Light_Environment`) and the
`Draw_Triangles`/`Draw_Strip` early-return (no VB/IB uploaded) also trace, tagged
`partial` / `no-op`, so the log reflects the *real* gaps, not only the pure stubs.
Only fires when `D3D11Backend` is the live backend (flag on) — zero effect on the
DX8 default path.

## Method used: STATIC trace (original recon — now superseded by the dynamic run below)

> **UPDATE 2026-07-16:** the environment blocker described here is fixed and a real
> dynamic run has since been captured — see **"Dynamic run (2026-07-16)"** below. The
> static inference in this section held up; it is kept for provenance.

A dynamic run was attempted twice (`generalszh.exe -win -d3d11 -ignoreAsserts`,
staged into the Steam Zero Hour dir, time-boxed 75 s). Both aborted at an asset-init
assertion **before render-device creation**:

```
GetStringFromRegistry - looking in SOFTWARE\Electronic Arts\EA Games\Generals for key InstallPath
ASSERTION FAILURE: Be 1337! Go install Generals!
```

This build requires the base **Generals** install (registry `InstallPath`) which is
absent in this environment; `-ignoreAsserts` does not suppress this particular abort.
Execution never reached `WW3D::Set_Render_Device` → `Do_Onetime_Device_Dependent_Inits`
→ `Init_Render_Backend`, so no in-game stub log could be captured. The instrumentation
is in place and will emit the log on a machine with the base install. The inventory
below is therefore derived by **static trace** of the `g_renderBackend->` call sites
cross-referenced against the `D3D11Backend` method bodies (an explicitly acceptable
fallback for this recon). The device-init slice itself is independently exercised
green by the D3D11 smoke test.

### Static bring-up call path

```
W3DDisplay::init
  -> Set_Use_D3D11_Backend(TheGlobalData->m_gfxBackendD3D11)      [new]
  -> WW3D::Init(ApplicationHWnd) -> WW3D::Set_Render_Device
       -> DX8Wrapper::Set_Render_Device (creates the DX8 device)
       -> DX8Wrapper::Do_Onetime_Device_Dependent_Inits            [dx8wrapper.cpp:370]
            -> Init_Render_Backend()          -> new D3D11Backend() + log      [REAL]
            -> g_renderBackend->Initialize(_Hwnd, ResW, ResH)      [REAL: device/swapchain/depth/pipeline]
  -> per-frame: DX8Wrapper::Begin_Scene / Clear / End_Scene        [DX8 statics — NOT routed through g_renderBackend]
       scene render (RTS3DScene/RTS2DScene) drives the backend:
         DX8RigidFVFCategoryContainer::Render (dx8renderer.cpp)    [static meshes — the bulk]
           g_renderBackend->Set_Shader / Set_Material / Set_Texture
           g_renderBackend->Set_Vertex_Buffer(VertexBufferClass*)  [records only]
           g_renderBackend->Set_Index_Buffer(IndexBufferClass*)    [records only]
           g_renderBackend->Apply_Render_State_Changes             [REAL, but sees default vector]
           g_renderBackend->Draw_Triangles                        [no-op: m_indexBuffer == null]
         SortingRendererClass::Insert/Flush (sortingrenderer.cpp)  [sorted/transparent]
           g_renderBackend->Set_Vertex_Buffer(DynamicVBAccessClass&)  [STUB]
           g_renderBackend->Set_Index_Buffer(DynamicIBAccessClass&)   [records offset only]
```

Note: `Begin_Scene`/`End_Scene`/`Clear`/`Flip_To_Primary`/`Set_Viewport` are **not**
routed through `g_renderBackend` anywhere in the tree — they are called on the
`DX8Wrapper` static facade directly. So with D3D11 selected, the DX8 device still
owns the actual frame; the D3D11 backend receives only the draw/state/geometry
virtuals, and its own swapchain is never presented. Filling the stubs below is
necessary but not sufficient for a visible frame — the present path must also be
routed through the backend in a later step.

## Dynamic run (2026-07-16) — the static prediction, now confirmed live

The environment blocker from the static section is **resolved**: an
`HKCU\SOFTWARE\Electronic Arts\EA Games\Generals\InstallPath` value pointing at the
Steam Zero Hour folder gets the Debug build past the `"Be 1337! Go install Generals!"`
asset-init assert. A dynamic bring-up run was then performed and **the in-game stub
log was captured for real** — replacing the static inference below.

### How it was run (recon harness, not a code change)

- **Exe:** `build/win32-vcpkg-debug/GeneralsMD/Debug/generalszh.exe` (Debug, run
  **in place** — its own dir supplies `zlibd1.dll`; nothing staged/copied into Steam).
- **CWD:** the Steam ZH folder (so the game finds its `*ZH.big` assets).
- **Flags:** `-win -noaudio -quickstart -ignoreAsserts` (baseline) and the same
  `+ -d3d11` (D3D11 run). `-quickstart` = `-nologo -noshellmap -noshellanim`, so the
  shell reaches the 2D main menu without loading a 3D shell map. `-ignoreAsserts`
  keeps mod-content asserts from popping a modal that blocks an unattended run.
- **Time-box:** `Start-Process -PassThru` → poll `HasExited` to a 60–75 s deadline →
  `Stop-Process -Force`. Never a foreground/blocking run. All processes killed after.
- **Log sink:** `ZP_D3D11_LOG` set to an absolute scratch path (so the trace is
  captured cleanly, not left in the Steam dir). The engine `DebugLogFileD.txt` path
  is the **exe** dir (`GetModuleFileName`), not CWD; this build did not emit one
  (no `DEBUG_LOGGING` text log — it uses the `ReleaseCrashInfo.txt` + minidump
  mechanism instead, which is what surfaced the crash below).

### First blocker found + cleared (NOT an engine bug)

The first boot attempts crashed **before render-device init** — not on the registry
assert (that's fixed) but on **mod content**: the Steam folder has the WIP
`mod/overlay` INIs installed under `Data\INI\Overrides\`, and one of them aborts INI
parse:

```
Release Crash at Thu Jul 16 10:20:05 2026
; Reason Error parsing INI file 'Data\INI\Overrides\ZeroPowerUSABurtonOverwatch.ini' (Line: 'CommandButton Command_ZPBurtonOverwatch')
```

That file self-documents `!! VERIFICATION STATUS: AUTHORED, NOT ENGINE-TESTED !!`.
This is unrelated to the renderer. To get a clean render-path recon the `Overrides`
dir was **temporarily moved aside and restored afterward** (verified: all 7 override
files back in place, no `reconbak` left). With it moved aside, both runs booted
clean past render init.

### DX8 baseline — BOOTS TO A RENDERED FRAME (observed)

`generalszh.exe -win -noaudio -quickstart -ignoreAsserts` ran the full 75 s window
with no crash and no new minidump. `ZP_D3D11_LOG` confirmed the render path:

```
[RenderBackend] constructed DX8Backend (default path)
```

A mid-run screenshot shows the **fully rendered Zero Hour main menu** (SOLO PLAY /
MULTIPLAYER / LOAD / OPTIONS / CREDITS / EXIT GAME, live FPS counter `30[30]`,
"Debug: Load Map" button; magenta shell backdrop because `-noshellmap`). So the
registry fix genuinely unblocks boot: DX8 reaches a real first frame. *(Observed via
screenshot, not inferred.)*

### D3D11 run — backend constructs, device comes up, frame is BLACK (observed)

Same launch `+ -d3d11`, 75 s box, no crash, no minidump. Captured `ZP_D3D11_LOG`
**in full** (this is the whole file, 7 lines):

```
[RenderBackend] constructed D3D11Backend (-gfxBackend d3d11)
[D3D11 hit #1] D3D11Backend::Set_Vertex_Buffer (partial: records bind-state, no GPU work)
[D3D11 hit #2] D3D11Backend::Set_Index_Buffer (partial: records bind-state, no GPU work)
[D3D11 hit #3] D3D11Backend::Set_Texture (partial: records bind-state, no GPU work)
[D3D11 hit #4] D3D11Backend::Set_Material (stub)
[D3D11 hit #5] D3D11Backend::Set_Shader (stub)
[D3D11 hit #6] D3D11Backend::Draw_Triangles (no-op: no engine VB/IB uploaded (Draw_Triangles skipped))
```

What this establishes (observed):
- `Init_Render_Backend()` took the **D3D11 branch** and constructed `D3D11Backend`.
- The game did **not crash and stayed alive the full 75 s**, so `Initialize()`
  (device / swapchain / depth / pipeline on the game HWND) **succeeded** — the D3D11
  device/swapchain came up on real hardware. *(Inferred from "no crash + draw calls
  arriving," not from a device-created log line — there is no such line.)*
- The bring-up path then drives the backend exactly as the static trace predicted:
  bind VB, bind IB, bind texture, set material, set shader, issue a triangle draw —
  which **early-returns because no engine VB/IB was uploaded**, so nothing is drawn.
- The mid-run screenshot of the D3D11 window is **solid black** — no menu, no
  geometry. Consistent with every `Draw_Triangles` no-opping. *(Observed.)* Note this
  also means the DX8 menu is **not** visible in D3D11 mode even though the present
  path was expected to stay on the DX8 device — the D3D11 swapchain appears to own
  the HWND and presents an empty/black buffer. The exact present ownership is
  **inferred, not proven** (no present-path trace); resolving it is part of the
  separate "route present through the backend" step, not this recon.

### Static prediction vs. dynamic reality — reconciliation

| Static predicted (groups A+B) | Fired live? | Note |
|---|---|---|
| `Set_Vertex_Buffer` (VB #2 + dynamic #3) | ✅ hit #1 | trace keys on `__FUNCTION__` (no signature), so the `VertexBufferClass*` and `DynamicVBAccessClass&` overloads **collapse to one line** — can't tell from the log which/both fired |
| `Set_Index_Buffer` (IB #1 + dynamic #4) | ✅ hit #2 | same overload-collapse caveat |
| `Set_Texture` (#7) | ✅ hit #3 | |
| `Set_Material` (#6) | ✅ hit #4 | |
| `Set_Shader` (#5) | ✅ hit #5 | |
| `Draw_Triangles` no-op | ✅ hit #6 | confirmed the hard gate: the draw bails because IB/VB are record-only |

- **The static prediction matched reality** — every group-A/B method it named fired,
  and the predicted `Draw_Triangles` no-op is exactly where the frame dies. No
  surprise method appeared.
- **Fired but ordering differs slightly:** the live first-hit order is VB → IB →
  Texture → Material → Shader, whereas the static call-path sketch listed
  Shader/Material before the geometry binds. Cosmetic (per-mesh bind order); the set
  is identical.
- **Predicted but did NOT fire on this path:** `Draw_Strip` (the menu path uses
  triangle lists only), and **all of group C** — `Set_Render_Target_With_Z`,
  `Create_Render_Target`, `Get_Back_Buffer`, shadow-map, gamma, vertex/pixel-shader.
  Their absence confirms the scoping claim that group C is off the boot/menu path
  (no RTT/shadows/F9-surface exercised by the shell). `Apply_Render_State_Changes`,
  `Set_Transform`, `Set_Light_Environment` etc. also don't appear because they are
  **real** methods that don't trace — expected, not a gap.

**Bottom line of the dynamic run:** the must-fill set is confirmed live and is
exactly the small cohesive core the static pass predicted — the four
`Set_Vertex_Buffer`/`Set_Index_Buffer` geometry-upload overloads (#1–#4, the hard
gate at `Draw_Triangles`) then `Set_Shader`/`Set_Material`/`Set_Texture` (#5–#7).
Nothing outside groups A+B is needed to get the menu's geometry drawing; group C
stays a later, independent concern. Separately, the black D3D11 window shows the
present path also needs routing through the backend (already flagged as a distinct
step below).

## Ranked stub-gap inventory

Difficulty: trivial / moderate / hard. "Bad no-op default" = what a missing impl
does today (crash vs silent-wrong-render).

### A. Blocks the first frame (must-fill to render anything through D3D11)

| # | Method | Kind | Used by (bring-up) | What it must do | Bad no-op today | Difficulty |
|---|--------|------|--------------------|-----------------|-----------------|------------|
| 1 | `Set_Index_Buffer(IndexBufferClass*, offset)` | partial (records only) | dx8renderer static meshes (all meshes) | Create/bind the `ID3D11Buffer` index buffer from the W3D IB bytes | silent-wrong: `m_indexBuffer` stays null → **every `Draw_Triangles`/`Draw_Strip` early-returns → nothing renders** | moderate |
| 2 | `Set_Vertex_Buffer(VertexBufferClass*, stream)` | partial (records only) | dx8renderer static meshes | Create/bind the `ID3D11Buffer` VB + FVF→input-layout from the W3D VB bytes (the real work already exists in `Upload_Vertices`, just needs the W3D bytes) | silent-wrong: no geometry bound | moderate |
| 3 | `Set_Vertex_Buffer(DynamicVBAccessClass&)` | **STUB** | sortingrenderer, pointgr, dynamesh, GUI/particles | Upload + bind dynamic VB (map/discard ring) | silent-wrong: dynamic/sorted/particle/2D geometry never drawn | moderate |
| 4 | `Set_Index_Buffer(DynamicIBAccessClass&, offset)` | partial (records offset only) | sortingrenderer + dynamic paths | Upload + bind dynamic IB | silent-wrong: pairs with #3 | moderate |

Rationale for the ordering: #1 is the hard gate — because the IB is only recorded,
`m_indexBuffer` is null and the already-real `Draw_Triangles` bails, so *no draw
call ever executes* regardless of everything else. #1–#4 together are the geometry-
upload integration (RENDERER_PORT.md step 4 wired into the `VertexBufferClass` /
`IndexBufferClass` / dynamic-access classes). None crash; they silently render
nothing.

### B. Blocks a *correct* frame (needed once geometry draws, not to boot)

| # | Method | Kind | Used by | What it must do | Bad no-op today | Difficulty |
|---|--------|------|---------|-----------------|-----------------|------------|
| 5 | `Set_Shader(ShaderClass&)` | **STUB** | dx8renderer, matpass, pointgr, line3d, dynamesh, ringobj, sphereobj, seglinerenderer, streakRender, sortingrenderer (hot) | Translate `ShaderClass` → the typed blend/depth/cull + combiner setters (`Set_Blend_*`, `Set_Depth_*`, `Set_Texture_Stage_*`) so `Apply_Render_State_Changes` binds the right state | silent-wrong: draws use the default opaque state + pass-through combiner (no alpha blend, wrong depth, vertex-diffuse only) | moderate |
| 6 | `Set_Material(VertexMaterialClass*)` | **STUB** (records nothing) | same hot set | Extract diffuse/ambient/emissive/opacity + `D3DMCS_*` sources → `Set_Material_Params`; drive `Set_Lighting_Enable`/lights | silent-wrong: flat/untinted, no FF lighting | moderate |
| 7 | `Set_Texture(stage, TextureBaseClass*)` | partial (records only) | dx8renderer, matpass, pointgr, dynamesh, ringobj, sphereobj, seglinerenderer, streakRender | Create/bind SRV + sampler from the W3D texture pixels (real work exists in `Upload_Texture_RGBA`, needs the W3D bytes) | silent-wrong: everything untextured | moderate–hard |

`Apply_Render_State_Changes`, `Set_Transform`/`Get_Transform`,
`Set_World/View_Identity`, `Set_Ambient`, `Set_Fog`, `Set_Light_Environment`
(records) already function; they just operate on default/empty inputs until #5–#7
feed them.

### C. Needed for correctness later (specific features; not on the menu/first-frame path)

| Method | Kind | Feature | Bad no-op today | Difficulty |
|--------|------|---------|-----------------|------------|
| `Set_Render_Target_With_Z` | STUB | texproject / shadow / render-to-texture | silent-wrong: RTT effects absent | hard |
| `Create_Render_Target` | STUB (returns null) | RTT allocation | potential-crash if a caller derefs the null target | hard |
| `Is_Render_To_Texture` | STUB (returns false) | RTT query | mostly safe (reports "not RTT") | trivial |
| `Set_Shadow_Map` / `Get_Shadow_Map` | STUB | shadow-map pass | silent-wrong: no shadows | moderate |
| `Set_Vertex_Shader` / `Set_Pixel_Shader` | STUB | the few real D3D8 shaders (e.g. water) | silent-wrong on those materials | moderate |
| `Set_Vertex_Shader_Constant` / `Set_Pixel_Shader_Constant` | STUB | ditto | silent-wrong | moderate |
| `Set_Gamma` | STUB | gamma ramp (skipped when `m_displayGamma==1.0`, the default) | silent-wrong gamma | trivial |
| `Get_Back_Buffer(num)` | STUB (returns null) | F9 screenshot / surface queries | **potential-crash** if a caller derefs the null surface | moderate |
| `Get_Shader(ShaderClass&)` | STUB | shader read-back (not observed on the bring-up path) | silent-wrong | trivial |

## Bottom line for scoping step 10

The must-fill-to-boot set is small and cohesive: **geometry upload for the four
`Set_Vertex_Buffer`/`Set_Index_Buffer` overloads (#1–#4)** — that alone moves the
D3D11 backend from "every draw is a no-op" to "draws execute". Correctness then
comes from **`Set_Shader` + `Set_Material` + `Set_Texture` (#5–#7)**, all moderate
and all with the GPU-side machinery already built (`Upload_Vertices`,
`Upload_Texture_RGBA`, the state cache, the combiner/lighting setters) — the gap is
purely reading the bytes/params out of the W3D handle classes. RTT/shadows/legacy
shaders (group C) are independent later features. Separately, the frame present path
(`Begin_Scene`/`Clear`/`End_Scene`) is not yet routed through `g_renderBackend`, so
routing that is an additional step beyond filling these stubs.

---

## World path (2026-07-16) — map-load recon on D3D11

First attempt to take the D3D11 backend past the menu into a loaded map
(branch `renderer-d3d11`, commit 252fc43). Vehicle: **`-navalSandbox`** (AI-vs-AI
water demo — boots straight into a skirmish, windowed, runs until killed; the
lightest deterministic in-world path; runs as Instance:02). Flags:
`-win -noaudio -ignoreAsserts -navalSandbox [-gfxBackend d3d11]`, Steam CWD,
2560x1440 client.

### DX8 baseline: fully in-world

Captured at t=80 s (game timer 00:01:13): textured terrain with cliffs/grass/
roads/railway, tree models, a supply-dock structure, fog-of-war shroud, full HUD
(control bar, minimap, army roster, debug money/XP overlay). Capture (scratch):
`...\scratchpad\world_dx8.png`. Crash oracle unchanged (ReleaseCrashInfo.txt
mtime 10:41:47, dumps 11 -> 11).

### D3D11: reproducible CRASH at world entry — no world frame exists

Two runs, both died with **exit 0xC0000005 (access violation)** during/just
after map load, before any capturable world frame:

- Run 1: crashed ~30 s in; CrashDumps 11 -> 13
  (`CrashMZ-20260716-172833-44e19bb-pid21400.dmp` 550 KB +
  `CrashFZ-...` 760 MB full dump).
- Run 2 (poll-capture every 5 s from t=10 s): window still blank at t=10 s,
  process gone by t=15 s; CrashDumps 13 -> 14.

So on the world path the honest description is: **nothing renders — the game
crashes before presenting a world frame.** The menu path (same binary, same
backend) is stable; the crash is specific to map-load/world rendering.

### What the traces show fired in-world that the menu never hit

`ZP_D3D11_LOG` (one-shot stub trace), run 1 — the menu run recorded *zero*
stub hits, the world run adds exactly two:

```
[D3D11 hit #1] D3D11Backend::Set_Light_Environment      (partial: records bind-state, no GPU work)
[D3D11 hit #2] D3D11Backend::Set_Index_Buffer_Index_Offset (partial: records bind-state, no GPU work)
```

`ZP_D3D11_DRAWLOG` (per-draw log), run 1 — 4,768 lines / 68 `Begin_Scene`
frames before death:

- Menu/loading frames: alpha-blended (`blend=1 src=SRCALPHA dst=INVSRCALPHA`),
  `depthWrite=0`, `start=0` — the familiar 2D composite.
- Final frames: **real 3D world draws** — opaque (`blend=0`), `depthTest=1
  write=1 func=LESSEQUAL`, textured, with **non-zero index start offsets**
  (`start=936..5658`) and mesh-sized batches (up to 1,248 polys) — i.e. the
  packed static-mesh path (many meshes sharing one IB, addressed via
  `Set_Index_Buffer_Index_Offset`) began executing, then the process died
  mid-frame.

### Stub/fallthrough inventory for the world path, ranked by visual impact

1. **The crash itself — RESOLVED 2026-07-17** (see "World-entry crash: root cause
   + fix" below). The earlier inference (index-offset draw math / RTT null
   read-back) was **wrong**: the true root cause was the sorting renderer
   capturing a null VB/material out of `DX8Wrapper::render_state`, which the
   D3D11 backend never populated. The crash-dump stack (analyzed with cdb +
   matching PDBs) settled it.
2. **DXT textures unimplemented** (`Set_Texture` decodes only A8R8G8B8/X8R8G8B8
   + 16-bit families; DXT falls through): terrain/unit/tree textures are DXT in
   this game, so once the crash is fixed expect a largely **untextured,
   vertex-colored world**. Highest visual-impact non-crash gap.
3. **`Set_Light_Environment` records-only**: world objects light via
   `LightEnvironmentClass` (menu never uses it). Untextured + unlit = flat
   silhouettes.
4. **Shadows / RTT still stubbed** (`Set_Render_Target_With_Z`,
   `Create_Render_Target` -> null, `Set_Shadow_Map`): no shadows; null-target
   deref remains a live crash risk on the water/RTT path of this map.
5. **Vertex/pixel shader entry points stubbed**: the water surface on naval maps
   uses the D3D8 shader path — expect missing/wrong water even after 2–4.

### Corrections to earlier sections

- Section B listed `Set_Texture` as "records only": since the W3D texture-upload
  slice (`D3D11Backend_W3D.cpp`) it *does* upload uncompressed surfaces; the
  remaining gap is compressed (DXT) formats — reflected in rank 2 above.
- The menu path now hits **no** stubs at all (empty one-shot trace), confirming
  groups A/B of the original inventory are filled for 2D.

---

## World-entry crash: root cause + fix (2026-07-17)

### Faulting stack (cdb, both `44e19bb` minidumps — identical)

```
ExceptionCode c0000005, read at 0x24, eax=0 (null this)
generalszh!VertexBufferClass::Get_Vertex_Count+0xa      <- this == nullptr
generalszh!SortingRendererClass::Insert_Triangles+0x213  (sortingrenderer.cpp:248)
generalszh!DX8PolygonRendererClass::Render_Sorted+0x117
generalszh!DX8TextureCategoryClass::Render+0x968
generalszh!DX8RigidFVFCategoryContainer::Render+0x114
generalszh!DX8MeshRendererClass::Flush+0xc7
generalszh!RTS3DScene::flushOccludedObjectsIntoStencil+0x733
generalszh!RTS3DScene::Render ... WW3D::Render ... GameClient::update
```

A second, same-family crash surfaced once the first was fixed (caught live under
cdb): `VertexMaterialClass::Get_Lighting` on a **null material** in the sorting
renderer's `Apply_Render_State` (`Flush_Sorting_Pool`), via
`W3DSmudgeManager::render -> SortingRendererClass::Flush`.

### Root cause

The sorted/transparent path (`SortingRendererClass`) snapshots the current
engine bind state with `DX8Wrapper::Get_Render_State()` at insert time and
re-applies it at flush time. `DX8Backend` keeps that record populated because
its bind methods forward into `DX8Wrapper::Set_*`. The D3D11 backend recorded
binds **only in its own members**, so `DX8Wrapper::render_state` stayed empty:
`vertex_buffers[0] == null`, `material == null`. The `WWASSERT(vertex_buffer)`
that would have caught it is disabled by the required `-ignoreAsserts`, so the
first sorted world draw (water/trees/smudges — the menu never sorts)
null-derefed. Menu stability was never evidence of world stability.

### Fix (commit on `renderer-d3d11`)

- `D3D11Backend_W3D.cpp`: the engine-facing bind methods now **mirror into
  DX8Wrapper's render_state record exactly as DX8Backend does** — both
  `Set_Vertex_Buffer` overloads, both `Set_Index_Buffer` overloads,
  `Set_Index_Buffer_Index_Offset`, `Set_Shader`, `Set_Material`, `Set_Texture`,
  `Set_Light_Environment` (the last two moved from D3D11Backend.cpp into the
  W3D TU; smoke-link stubs remain `#ifndef ZP_D3D11_W3D_TU`). Buffer uploads run
  BEFORE the mirror so the engine ref taken by the record doesn't block the
  WriteLock read; a consecutive re-bind of the still-recorded buffer skips
  re-upload (content-correct: its bytes are already current).
- `sortingrenderer.cpp`: defensive null-guards in addition (not instead) —
  `Insert_Triangles`/`Insert_VolumeParticle` drop a node whose captured VB/IB is
  null; `Apply_Render_State` tolerates a null material.

### Post-fix evidence (2026-07-17)

- Two consecutive D3D11 `-navalSandbox` runs: **alive at 110 s kill** (crash was
  100% repro at ~13-30 s before), CrashDumps count and `ReleaseCrashInfo.txt`
  mtime unchanged across both, in-world frame captured at t=86 s
  (2560x1440 PrintWindow of the game HWND).
- What renders (observed, run2/run3 captures): recognizable world viewport —
  textured-ish terrain tiles with cliff/grass/road surfaces, tree billboards,
  fog-of-war shroud, full HUD (control bar, minimap, army roster, timer, FPS
  counter). Visually wrong as expected: large black shroud/lighting regions,
  many surfaces flat or mis-lit (DXT textures still unimplemented, no shadows,
  no water shader — `Set_Vertex_Shader`/`Set_Pixel_Shader` are now the ONLY
  stub hits in the world one-shot trace).
- `w3d_d3d11_smoke`: all 16 asserts PASS after the change.
- DX8 control run, same flags: boots to world, alive at 110 s, crash oracle
  clean (mirroring is D3D11-only code; DX8 path untouched).
- Known correctness gap left open (documented, not a crash): the sorting
  flush re-applies each node's world/view via `DX8Wrapper::_Set_DX8_Transform`,
  which the D3D11 backend never sees — sorted geometry may draw with the
  last-set D3D11 transforms. Routing that through the backend is a later
  parity step.

---

## DXT/BC texture support + visible fallbacks (2026-07-17)

Closes the rank-2 world gap above ("DXT textures unimplemented"). Commit on
`renderer-d3d11`.

### What was built

- **Native BC upload** (`D3D11Backend::Upload_Texture_BC`, D3D11Backend.cpp -
  smoke-test-reachable): takes raw compressed blocks + row pitch, creates a
  `DXGI_FORMAT_BC1/BC2/BC3_UNORM` immutable texture + SRV + sampler, binds to
  the stage slot. Requires block-aligned (multiple-of-4, >= 4) dimensions per
  the D3D11 BC rules; single mip, same as the RGBA path.
- **`Set_Texture` (D3D11Backend_W3D.cpp) DXT path**: `LockRect(READONLY)` on
  the managed-pool DXT surface returns the raw blocks with `Pitch` = bytes per
  block row - exactly D3D11's `SysMemPitch` - so DXT1->BC1, DXT2/3->BC2,
  DXT4/5->BC3 upload **natively, no CPU decode, lossless** (strategy A; the
  compressed bytes were cleanly accessible, so the CPU-decode fallback B was
  never needed).
- **Visible fallbacks** (no more silent black through an unbound SRV):
  - *Unknown format* -> `Upload_Fallback_Texture`: loud 4x4 magenta/black
    checker - wrong-but-visible, self-announcing on screen.
  - *Known format but unreadable bytes* (LockRect fails: default-pool /
    GPU-only surfaces, e.g. the shroud dst texture) ->
    `Upload_Neutral_Texture`: 4x4 all-WHITE, the multiplicative identity, so
    modulate passes degrade to a no-op instead of corrupting the frame.
    (First tried magenta for this case too: the fog-of-war shroud pass painted
    the entire terrain magenta/black - the multiply identity is the right
    degradation for unreadable content.)
  - Every fallback logs one `[D3D11 texture-fallback] format=..(fourcc) WxH
    (reason)` line per distinct format+reason to `ZP_D3D11_LOG`.

### Oracle evidence

- Smoke: new BC1 assert (hand-crafted solid-red DXT1 block, 4-color opaque
  mode, white-diffuse modulate quad; tolerance +/-8 per channel for decoder
  latitude - measured decode was exact 255,0,0). Negative control
  (`SMOKE_NEG_CONTROL_BC` expecting green) went red on exactly that assert,
  then reverted + grep-verified. **All 17 asserts PASS** (16 prior + BC1).
- In-world A/B at t=86 s, `-navalSandbox`, 2560x1440, crash oracle clean
  (dumps 12->12, ReleaseCrashInfo mtime unchanged) on every launch:
  near-black fraction of the terrain viewport (top 77% of the client area,
  4px sampling):
  - pre-fix D3D11 baseline: **72.3%** (world_d3d11_run2.png, game timer 0:49)
    / 81.5% (run3)
  - post-fix D3D11: **66.6%** (world_d3d11_dxt3.png, game timer 0:54 - more
    shroud creep than the 0:49 baseline, i.e. biased against the fix)
  - DX8 control: **25.8%** (world_dx8_dxtctrl.png)
  - What visibly changed: roads, railway, supply-dock crates, tree canopies
    and ground stones now render their real DXT textures (matching the DX8
    control). The interim magenta run also proved the fallback works: 3.4% of
    the viewport lit up magenta where unreadable textures were sampled.

### Honest residual (why D3D11 is still 66.6% vs DX8's 25.8%) - NOT DXT gaps

1. **Terrain base texture never reaches the backend.** The terrain system
   (`W3DShaderManager.cpp`, e.g. lines 202/341/578/795) binds textures
   directly on the raw D3D8 device (`_Get_D3D_Device8()->SetTexture(0,tex)`)
   and uses D3D8 pixel shaders (`Set_Vertex_Shader`/`Set_Pixel_Shader` are
   the only remaining stub hits in the world trace). The D3D11 stage-0 SRV is
   therefore stale/empty for terrain draws -> the black/white tile patchwork.
   The pre-fix frames *looked* textured only because those draws sampled
   whatever texture the engine-routed path had last uploaded - stale-SRV
   garbage that happened to resemble terrain. Fix = route the shader-manager
   texture binds + shader paths through the backend (group C).
2. **GPU-composed textures are unreadable at bind time** (shroud dst 64x128
   R5G6B5, written via `_Copy_DX8_Rects` from a sysmem source): now a white
   no-op (logged); correct content needs that copy mirrored into the backend.
3. Shadows / water / `Set_Light_Environment` lighting - unchanged, documented
   above.

---

Update 2026-07-19 (screen-filter round): the screen-filter family
(ScreenBWFilter / ScreenMotionBlurFilter / ScreenCrossFadeFilter) no longer
draws on the raw device under D3D11 — postRender routes through
`IRenderBackend::Capture_Backbuffer` + `Draw_Screen_Filter_Quad`
(monochrome.nvp emulated as a combiner-ubershader post-op). The two
`Set_Vertex_Shader`/`Set_Pixel_Shader` stub hits in the world trace are NOT
filter-family (they fire identically in a no-filter run); remaining raw
DrawPrimitiveUP sites are W3DSmudge, W3DVolumetricShadow and
W3DProfilerFrameCapture. Crossfade FB_MASK's dest-alpha mask pass is
approximated (one-shot DEBUG_LOG when hit).
