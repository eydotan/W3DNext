# W3DNext — Renderer Port Design

Deep-dive on replacing the DirectX 8 backend behind `DX8Wrapper`. This is the keystone task: it's the prerequisite for a 64-bit build *and* the foundation for the graphics remaster.

---

## The one fact that determines everything

**The WW3D2 renderer is a fixed-function engine.** The evidence, gathered from `Core/Libraries/Source/WWVegas/WW3D2`:

- It configures the **fixed-function transform & lighting pipeline**: `SetTransform` (world/view/proj), `SetLight`/`LightEnable`, `SetMaterial`, and render states like `D3DRS_LIGHTING`, `D3DRS_AMBIENT`, `D3DRS_*MATERIALSOURCE`, `D3DRS_FOGENABLE`/`FOGTABLEMODE`.
- It uses the **fixed-function multitexture combiner**: `D3DTSS_COLOROP`, `D3DTSS_ALPHAOP`, `D3DTSS_COLORARG1/2`, `D3DTSS_TEXCOORDINDEX`, `D3DTSS_TEXTURETRANSFORMFLAGS` across up to 8 stages.
- It uses **FVF vertex formats**, not vertex declarations: `D3DFVF_XYZ`, `NORMAL`, `DIFFUSE`, `SPECULAR`, `TEX1..TEX8`, and crucially `D3DFVF_XYZB4 | D3DFVF_LASTBETA_UBYTE4` — i.e. **fixed-function indexed vertex blending (GPU skinning)**.
- `ShaderClass` / `SHD_INIT_SHADERS` is **Westwood's material-state abstraction, not GPU shaders.** `SHD_Init_Shaders` is a stub. There is essentially **no HLSL** in the codebase.

**Why this matters:** Direct3D 11 and Vulkan have **no fixed-function pipeline at all.** Every one of the features above must be re-expressed as programmable shaders + constant buffers + state objects. The port is therefore *not* a 1:1 API translation — it's writing a **fixed-function-emulation layer**. That realization changes the recommended strategy (below).

The good news from the earlier scout still holds: the raw D3D8 surface is confined to ~10 files (`dx8wrapper`, `texture`, `dx8caps`, `formconv`, `dx8fvf`, `ww3dformat`, `sortingrenderer`, `dx8webbrowser`), all behind the `DX8Wrapper` / `DX8CALL` abstraction. The other ~63 WW3D2 files and the entire game call *through* that wrapper.

---

## The D3D8 API surface to replace

Grouped by concept, with the D3D11 destination and the catch.

| D3D8 (current) | Used for | D3D11 equivalent | Catch |
|---|---|---|---|
| `CreateDevice`, `Reset`, `TestCooperativeLevel` | device + lost-device | `D3D11CreateDeviceAndSwapChain`; resize handles loss | No "lost device" model — simpler |
| `CreateVertexBuffer` / `CreateIndexBuffer`, `SetStreamSource`, `SetIndices` | geometry | `ID3D11Buffer` + `IASetVertexBuffers`/`IASetIndexBuffer` | Map/Unmap instead of Lock |
| **FVF** (`SetVertexShader(fvf)`) | vertex layout | `ID3D11InputLayout` | Must translate each FVF → input-element array (`dx8fvf.*` is the place) |
| `DrawIndexedPrimitive` | draw | `DrawIndexed` | Primitive-type + count→index-count math |
| `SetTransform`/`GetTransform` | **FF transform** | constant buffer `cbPerFrame/cbPerObject` | Must write a VS that consumes it |
| `SetLight`/`LightEnable`, `SetMaterial` | **FF lighting** | constant buffer + VS/PS lighting code | Reimplement D3D8's lighting equation |
| `SetRenderState(D3DRS_*)` | blend/depth/stencil/raster/fog | `ID3D11{Blend,DepthStencil,Rasterizer}State` | Group ~60 states into immutable state objects, cached |
| `SetTextureStageState(D3DTSS_*)` | **FF texture combiner** | pixel-shader logic | Hardest part — see below |
| `CreateTexture`/`CreateImageSurface`, `SetTexture`, `UpdateTexture`, `CopyRects` | textures | `ID3D11Texture2D` + SRV, `UpdateSubresource`, `CopyResource` | Sampler state splits out of TSS |
| `SetRenderTarget`, `GetBackBuffer`, `CreateAdditionalSwapChain`, `Present`, `Clear` | targets | RTV/DSV, `OMSetRenderTargets`, `IDXGISwapChain::Present`, `Clear*View` | Straightforward |
| `SetGammaRamp`, `SetClipPlane`, `SetViewport` | misc | post-process / SV_ClipDistance / `RSSetViewports` | Gamma → post pass |
| `SetVertexShaderConstant`/`SetPixelShaderConstant`, `Create/SetPixel/VertexShader` | the few real shaders | HLSL + constant buffers | Mostly unused; FF dominates |

### The hard part: the texture-stage combiner
`D3DTSS_COLOROP/ALPHAOP` with args (`D3DTSS_COLORARG1/2`, `TEXCOORDINDEX`, `TEXTURETRANSFORMFLAGS`) is a **configurable per-stage blend tree** — the thing pixel shaders replaced. Two ways to emulate:

1. **Übershader + state hash:** one big HLSL pixel shader parameterized by the active stage config, packed into a constant buffer; branch/loop over stages. Simplest to get correct.
2. **Shader permutation cache:** hash the stage state, generate/compile a specialized PS on first use, cache it. Faster at runtime, more machinery.

Recommend (1) first, optimize to (2) only if profiling demands.

### The other notable part: GPU skinning
`D3DFVF_XYZB4 | LASTBETA_UBYTE4` is fixed-function indexed vertex blending — 4 bone weights + a packed bone-index. In D3D11 this becomes a skinning vertex shader reading a bone-matrix constant buffer/structured buffer. Self-contained, well-understood.

---

## Two strategic paths (pick deliberately)

### Path A — d3d8to9 / D3D9 backend (fast, low-risk, modest payoff)
Direct3D **9 still has the fixed-function pipeline**, and has a native 64-bit runtime. Dropping in the `d3d8to9` shim (or porting `DX8Wrapper` to D3D9 directly) preserves every FF feature with little new shader work, and **gets you a 64-bit build**.

- Effort: low–moderate. Mostly type/ABI plumbing, no FF reimplementation.
- Payoff: 64-bit, runs on modern Windows cleanly, can layer dgVoodoo/DXVK for upscaling/AA. But the engine is still fundamentally "2003 fixed-function" — limited remaster ceiling.
- Best if: your priority order is *stability + 64-bit first, visuals later.*

### Path B — D3D11 (or Vulkan) backend with FF-emulation (the remaster path) — recommended for our goals
Rewrite the `DX8Wrapper` backend onto D3D11, implementing the FF-emulation layer above.

- Effort: high. The combiner übershader + lighting + state-object caching is real work (estimate a few focused weeks for parity, then ongoing).
- Payoff: once the FF layer exists, **you own the shaders** — that's the doorway to HDR, PBR, real shadows, post-processing, the whole Phase 2 remaster. And it's natively 64-bit.
- Best if: visual modernization is a core goal (it is, per the project brief).

**Pragmatic combined plan:** do **A first** to unlock 64-bit and a stable modern baseline quickly, *then* **B** as the remaster foundation. A is not wasted — it de-risks the build/run loop and gives you a reference to diff against while building B.

---

## Concrete implementation plan (Path B)

1. **Define a backend interface.** Extract the D3D8 calls in `dx8wrapper.h/.cpp` behind an abstract `IGfxBackend` (device, buffers, textures, state, draw, present). Keep the existing D3D8 impl as `GfxBackendD3D8` so nothing breaks. *This is the single most valuable first commit — it makes everything after it incremental and testable.*
2. **State translation tables.** Map `D3DRS_*`/`D3DTSS_*`/FVF into your own enums (start in `formconv.*`, `dx8fvf.*`, `ww3dformat.*`). These are mostly mechanical lookup tables.
3. **`GfxBackendD3D11` skeleton:** device, swap chain, RTV/DSV, clear, present. Get a clear-to-color frame on screen.
4. **Geometry + input layouts:** buffers, FVF→`InputLayout`, `DrawIndexed`. Render untextured geometry.
5. **FF transform + a basic VS/PS:** constant buffers for world/view/proj; flat-shaded triangles.
6. **Textures + samplers:** single-texture path, then the **combiner übershader** for multitexture.
7. **FF lighting + material + fog** in the shaders.
8. **Skinning** (`XYZB4`) VS path.
9. **State objects** (blend/depth/stencil/raster) with a cache keyed by the legacy state vector.
10. **Parity pass:** A/B against the D3D8 (or D3D9) backend frame-by-frame; fix combiner/blend mismatches. Then delete `dx8.cmake`'s 32-bit constraint and build x64.

---

## Where each change lands

- `Core/Libraries/Source/WWVegas/WW3D2/dx8wrapper.{h,cpp}` — the backend interface + both implementations.
- `dx8fvf.*`, `formconv.*`, `ww3dformat.*` — format/FVF/state translation tables.
- `texture.cpp`, `dx8caps.cpp` — texture creation + capability reporting.
- `sortingrenderer.cpp` — draw submission (alpha-sorted) — verify against new draw path.
- `cmake/dx8.cmake` + the `CMakeLists.txt` 32-bit guard (`CMAKE_SIZEOF_VOID_P EQUAL 4`) — relax once x64 backend is in.
- New: `WW3D2/Backend/` for `GfxBackendD3D11` + `WW3D2/Shaders/` for the HLSL FF-emulation set.

---

## Bottom line

The renderer is small in surface (~10 files) but deep in *semantics* — it's a fixed-function engine, so a modern backend means emulating fixed-function in shaders, not translating calls. Step 1 (the backend interface extraction) is low-risk and high-leverage; do that regardless of which path you pick. For the parent project's remaster ambitions, Path B (D3D11 + FF-emulation) is the real target, ideally after a quick Path-A pass to bank a 64-bit build early.
