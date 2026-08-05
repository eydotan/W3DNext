# Upstream recon: TheSuperHackers render-backend interface work

Recon 2026-07-15 for our `IGfxBackend` extraction (branch `renderer-d3d11`).
Sources: upstream Discussion #1575 ("Migrate to DX9"), PR #2613
(`chore(ww3d2): add IRenderBackend Interface`, author bobtista, OPEN, 30 comments),
and the actual headers from `bobtista/GeneralsGameCode`, branch
`bobtista/feat/render-backend-interface-skeleton`.

## Context: upstream strategy (Discussion #1575)

- JohnsterID's plan: Phase 1 DX8→DX9 in place; Phase 2 extract a `RenderDevice`
  interface; Phase 3 OpenGL/Vulkan backends. Maintainer xezon wants DX8 kept
  working (GenTool only works with DX8) and flagged that raw D3D8 types leak
  through `DX8Wrapper` today ("Texture should be abstracted").
- bobtista argued against dual compile-time DX8/DX9 toggles (stephanmeesters'
  `#ifdef DX9` macro branch) — instead: **interface first, DX8 stays the default
  pass-through reference**, alternate backends on a branch until 1:1 visual +
  performance parity vs master. His long-term target is **bgfx**, not DX9.
- Related: fbraz3/GeneralsX (SDL3 + DXVK) exists as a full-fat divergent fork.

## (a) Their interface shape — PR #2613

Files: `WW3D2/IRenderBackend.h` (158 L), `WW3D2/Backend/DX8Backend.{h,cpp}`
(pure forwarder to the `DX8Wrapper` static facade), `WW3D2/Backend/RenderBackend.{h,cpp}`
(global `IRenderBackend* g_renderBackend` + `Init_/Shutdown_Render_Backend()`,
constructed/destroyed from `Do_Onetime_Device_Dependent_Inits/Shutdowns`).

- **Granularity: high-level, semantic.** Only DX8Wrapper methods that traffic in
  W3D types are virtualized (~45 methods): `Begin_Scene/End_Scene(flip)`,
  `Clear`, `Set_Viewport`, `Set_Vertex_Buffer/Set_Index_Buffer` (static + dynamic
  overloads), `Set_Shader(ShaderClass&)`, `Set_Material(VertexMaterialClass*)`,
  `Set_Texture(stage, TextureBaseClass*)`, `Apply_Render_State_Changes`,
  `Apply_Default_State`, `Invalidate_Cached_Render_States`,
  `Set_Transform(TransformKind, Matrix4x4/Matrix3D)`, identity-transform
  helpers, `Set_Projection_Transform_With_Z_Bias`, `Set_Light/Set_Ambient/
  Set_Fog/Set_Light_Environment`, `Draw_Triangles` (x2) / `Draw_Strip`,
  render-target + shadow-map methods, `Set_Gamma`, device-loss/stencil/
  back-buffer queries. Names deliberately match DX8Wrapper's so a caller
  migration is a mechanical `DX8Wrapper::X(...)` → `g_renderBackend->X(...)`.
- **Resource handle style: existing W3D classes as handles.** No new opaque
  handle types — `VertexBufferClass*`, `TextureBaseClass*`, `SurfaceClass*`,
  `TextureClass* Create_Render_Target(...)`. Legacy D3D shader handles cross as
  "opaque `unsigned long`" (`Set_Vertex_Shader(unsigned long)`), and shader
  constants as `(int reg, const void* data, int count)`.
- **FVF / raw render-state / texture-stage state do NOT cross the boundary.**
  The boundary is `ShaderClass`/`VertexMaterialClass`/texture-per-stage plus the
  `Apply_Render_State_Changes` deferred-flush model; raw `D3DRS_*`/`D3DTSS_*`/
  FVF stay inside the backend. D3D8-typed entry points (`_Get_D3D_Device8`,
  `_Create_DX8_Texture`, …) remain as DX8Backend-specific escape hatches on the
  static DX8Wrapper. In review, bobtista sketched the next step (local bgfx
  branch): a backend-neutral fixed-function state cache keyed by the same
  ordinals + typed semantic setters, named methods for the raw-device cases,
  and `Create_Legacy_Pixel_Shader(kind)` enum-hatch for hand-written bytecode
  (the water shader) instead of exposing a device pointer.
- **Caps plan** (review, not yet code): narrow virtuals with safe defaults —
  `Supports_Texture_Format(WW3DFormat)`, `Get_Texture_Limits()`, etc.;
  `DX8Caps` never exposed directly.
- **Known review warts** (Greptile/xezon, still open): `TransformKind` pins
  D3D8 ABI values (`RB_TRANSFORM_WORLD = 256`) into the abstract enum; missing
  `override` specifiers; unguarded `g_renderBackend->Shutdown()` null deref on
  an error-recovery path; `unsigned short` polygon/vertex counts inherited from
  DX8-era limits; unresolved question whether `g_renderBackend` should live on
  the `WW3D` class (blocked by Core vs Generals/GeneralsMD layering).

## (b) What to borrow / what to avoid

Borrow:
1. **The cut line.** Virtualize only W3D-typed methods; keep `ShaderClass` /
   `VertexMaterialClass` / texture-stage as the semantic state currency and the
   `Apply_Render_State_Changes` deferred flush as the boundary contract. This
   matches our RENDERER_PORT.md step 1 and is exactly where a D3D11
   FF-emulation layer wants to sit (it sees intent, not D3DTSS ordinals).
2. **Name-compatible methods** for mechanical caller migration; W3D resource
   classes as handles (no premature opaque-handle system).
3. **The three-file split**: pure interface header (fwd-decls only, includable
   anywhere) / concrete backend under `Backend/` / tiny access-point header that
   callers include instead of concrete headers. Lifecycle pair hooked into the
   existing one-time device init/shutdown. Clean, low-blast-radius.
4. **Their parity doctrine**: DX8 stays the runnable reference implementation;
   new backend A/Bs against it for 1:1 visual output before switch-over.
5. **Caps-as-narrow-virtuals** plan and the `Create_Legacy_Pixel_Shader(kind)`
   named-enum hatch for the water shader bytecode.

Avoid:
1. **D3D ABI values in abstract enums** (`RB_TRANSFORM_WORLD = 256`). Use
   sequential enum values; map to `D3DTS_*` inside the D3D8 backend only.
2. **Escape hatches via the static DX8Wrapper facade.** Fine for their
   zero-behavior-change PR #1, but it means callers still bind to DX8 statics;
   for a real second backend every such call site is a hidden porting TODO.
   We should inventory `_Get_D3D_Device8`-style call sites up front.
3. **`unsigned long` shader handles / raw `void*` constant blobs** — carries the
   D3D8 handle model forward; our D3D11 path wants typed shader/CB objects.
4. **A bare global `g_renderBackend`** without ownership; and don't inherit
   `unsigned short` counts into a new interface.
5. **Their scope**: PR #2613 is scaffolding + one call site; it does not touch
   FVF translation, state objects, or the combiner — all of our hard work
   (RENDERER_PORT.md steps 2–9) remains ours regardless.

## (c) Parity / testing approach discussed

No automated harness. Their bar: Windows CI builds + "game launches and runs a
Skirmish round identically"; discussion-level agreement on keeping the upgrade
on a branch until it matches master 1:1 in visual output and performance. We
can beat this cheaply: scripted-camera frame captures diffed DX8 vs new backend
(we already have F9 PNG screenshots) as a machine oracle.

## (d) Divergence risk if they land theirs

Moderate and mostly nominal, not structural. PR #2613 is open, actively
reviewed (May 30 → Jun 4 review rounds), unmerged as of 2026-07-15; direction
(interface + DX8 reference) is maintainer-aligned, so some form likely lands.
If we ship `IGfxBackend` with different names/splits, future upstream merges
into this tree will conflict in `dx8wrapper.cpp` call sites and `CMakeLists.txt`.
Cheap insurance: adopt their names (`IRenderBackend`, `DX8Backend`,
`Backend/` dir, `g_renderBackend`, method names) and their file layout, then
extend with our D3D11 backend + typed shader handles on top. Their bgfx
ambition (bobtista, still local-only) would diverge from our D3D11
FF-emulation path, but that fight is at the backend-implementation layer, not
the interface — the shared interface is the hedge.
