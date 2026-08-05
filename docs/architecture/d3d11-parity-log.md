# D3D11 backend: DX8-vs-D3D11 parity A/B log

Running log of *real* (in-game, not synthetic) parity comparisons between the
default DX8 backend and the D3D11 backend (`-gfxBackend d3d11`), per
RENDERER_PORT.md step 10. Oracle: `w3d_parity_diff` (`Core/Tests/parity/`).

---

## 2026-07-16 — first real A/B: main menu (branch `renderer-d3d11`, commit 252fc43)

### Method

- Build: `build/win32-vcpkg-debug/GeneralsMD/Debug/generalszh.exe` (Debug),
  CWD = Steam Zero Hour install, flags `-win -noaudio -quickstart -ignoreAsserts`
  (+ `-gfxBackend d3d11` for the D3D11 run).
- `-ignoreAsserts` is required in this environment: without it the Debug build
  parks on an `ASSERTION FAILURE: MissingCursor data\cursors\SCCPointer.ANI`
  dialog before creating the render device (the file exists on disk,
  case-insensitively — the assert itself is spurious here) and every capture is
  black. Diagnosed by capturing the assert dialog itself.
- Capture: `PrintWindow(hwnd, hdc, PW_CLIENTONLY|PW_RENDERFULLCONTENT)` of the
  verified game HWND (class `Game Window`, PID-matched to the launched process),
  ~25 s after launch, both runs at the same client size **1440x1080**. Client-only
  capture → no title-bar crop needed. Time-boxed and killed; never foregrounded.
- Crash oracle before/after every launch: `ReleaseCrashInfo.txt` mtime
  (2026-07-16 10:41:47) and CrashDumps count (11) both **unchanged** across both
  menu runs.
- Steam `Data\INI\Overrides` was left **in place** (7 INIs incl. the fixed Burton
  INI and the new `ZeroPowerCommandoLimit.ini`) — the game booted clean to the
  main menu with them, so this run doubles as the Overrides boot test: **PASS**.

Captures (session scratchpad, not committed):

- DX8:   `<scratchpad>menu_dx8.png`
- D3D11: `...\scratchpad\menu_d3d11.png`
- Diff viz: `...\scratchpad\menu_diff.png`

### Verdict lines (verbatim)

```
w3d_parity_diff menu_dx8.png menu_d3d11.png --tol 4:
PARITY FAIL maxdelta=255 mae=2.8078 over=236922/1555200

w3d_parity_diff menu_dx8.png menu_d3d11.png --tol 0:
PARITY FAIL maxdelta=255 mae=2.8078 over=301286/1555200
```

I.e. 15.2% of pixels differ by >4, 19.4% by >0; mean absolute error 2.81/255.
**FAIL is the expected honest result** — the captures are wall-clock-shifted
frames of a live menu with a debug FPS/timestamp overlay; the number is the
baseline to drive down, not a defect list by itself.

### Where the differences are (from the diff viz)

Ranked by area/intensity in `menu_diff.png`:

1. **Vertical grid stripes inside the six menu buttons** (magenta bands in the
   diff): on D3D11 the background grid texture shows *through* the button fill —
   the button interior is not fully opaque. Visible directly in the D3D11
   capture. Likely alpha-blend/combiner mismatch on the button fill draw.
   Biggest genuine backend difference.
2. **The outer "ruler/grid" border frame** of the shell screen: dense per-texel
   deltas around the whole screen edge — texture sampling/filtering or
   coordinate half-texel difference on the tiled border texture.
3. **GENERALS ZERO:HOUR logo**: strong edge deltas across the whole logo —
   consistent with filtering/rounding on a high-contrast texture, not a missing
   element (logo present and correctly placed on both).
4. **Button borders + all text edges** (menu labels, "Debug: Load Map" badge):
   1-px edge halos — subpixel text/edge rasterization differences.
5. **FPS/timestamp overlay (top-left)**: differs by construction (wall-clock
   text, `17:24:19` vs `17:25:13`) — a known non-backend diff source; it alone
   guarantees maxdelta=255. Future runs should mask this region or disable the
   overlay.
6. **Central panel field**: pure black in the diff — the large flat purple
   region is pixel-identical. The 2D composite fix in 252fc43 holds.

### D3D11 stub trace during the menu run

`ZP_D3D11_LOG` one-shot trace recorded **only** the construction line
(`[RenderBackend] constructed D3D11Backend`) — zero stub/partial hits on the
whole boot-to-menu path. The menu path is now fully on real code.

### Follow-ups for the next A/B

- Mask (or disable) the debug FPS/timestamp overlay before diffing.
- Chase finding 1 (button-fill translucency) first — it is the only
  structural difference; 2–4 look like sampling/rounding classes.
- For frame-locked captures use the `-stratagemShot` schedule or a `-replay`
  playback per `tools/parity/README.md` instead of wall-clock settle delays.

---

## 2026-07-19 — finding 1 (button-fill translucency) re-check: NO LONGER REPRODUCES (commit cb6cea6 + diagnostics)

### Method

- New window/focus-independent capture primitive: `ZP_D3D11_FRAMEDUMP=<prefix>`
  makes the D3D11 backend dump its own backbuffer (staging copy) to
  `<prefix>_fNNN.ppm` at frames 300/600/900, logging mean luminance to
  `ZP_D3D11_LOG`. Motivation: PrintWindow returns a black client area for BOTH
  backends when the game is unfocused, and screen grabs need a verified
  foreground window — this session's desktop held a foreground lock (Hyper-V
  VM), so every windowed capture path was unreliable. The dump reads the real
  render target and cannot lie about focus. Note: while unfocused the game
  clock pauses, so dump frames are STATIC (f300/f600 meanlum identical 86.17)
  — good for menu A/B, useless for animation.
- DX8 control: focused screen grab (CopyFromScreen after verified
  SetForegroundWindow), 1440x1080 client. Focused ⇒ shellmap ANIMATES ⇒
  whole-frame diffs vs the frozen D3D11 dump are animation-polluted; only
  static regions are comparable.
- New per-draw diagnostics (committed): ZP_D3D11_DRAWLOG lines now carry
  ShaderClass bits + combiner stage-0 ops/args + bound texture format/name;
  ZP_D3D11_LOG gains one-shot `[D3D11 tex-alpha]` (level-0 alpha min/max per
  texture) when the draw log is on.

### Evidence

Per-draw recon (D3D11 menu): the six button draws come from atlas
`scsmshelluserinterface512_001.tga` (A8R8G8B8, aMin=0 aMax=255 — correct
atlas alpha) in exactly two states, both semantically matching DX8's
`ShaderClass::Apply` mapping:

```
4190x shader=0x000984b7 stages=1 MODULATE(TEXTURE,DIFFUSE) c+a, blend SRCALPHA/INVSRCALPHA
 651x shader=0x000884b7 stages=0 (TEXTURING_DISABLE -> diffuse passthrough), same blend
```

Button-interior mean RGB (340x44 interior crops, static art regions), DX8
focused grab vs D3D11 f900 dump:

```
SOLO     DX8=(48,45,44) D3D11=(46,44,42) sumdiff=5
MULTI    DX8=(54,50,47) D3D11=(51,48,44) sumdiff=8
LOAD     DX8=(59,55,50) D3D11=(59,56,50) sumdiff=1
OPTIONS  DX8=(65,62,57) D3D11=(65,61,56) sumdiff=2
CREDITS  DX8=(57,54,49) D3D11=(56,52,48) sumdiff=4
EXIT     DX8=(53,51,47) D3D11=(52,49,45) sumdiff=5
```

The 2026-07-16 finding (background grid bleeding through fills — interior
shifts far above these) does not reproduce: interiors match within noise, and
both backends show the same by-design faint grid inside the semi-transparent
fills. Attribution is not bisected; the fix almost certainly rode the texture
upload/bind work landed since e02cde3 (9deca75 native BC + uncompressed decode,
0527d5c shader-manager bind routing). Whole-frame masked stats are NOT
comparable to the 07-16 baseline (animation pollution, above) and are recorded
only for method honesty: tol4 over=667330/1539460 (43.35%), dominated by
smoke/fire/jet animation deltas.

### Remaining menu items

- Findings 2-4 (border/logo/text edge classes) still open, unquantified this
  round (animation pollution; needs same-source A/B — e.g. a DX8-side
  framedump twin or a frozen-clock focused capture).
- For exact frame-matched menu A/Bs: run BOTH sides unfocused via framedump
  once a DX8 equivalent exists, or freeze the shellmap.

## 2026-07-19 — first FRAME-MATCHED menu A/B via in-engine framedumps (DX8 twin added)

Method change: both backends now dump their own backbuffer (`ZP_DX8_FRAMEDUMP` /
`ZP_D3D11_FRAMEDUMP`, PPM at flip frames 300/600/900), and `w3d_parity_diff`
gained PPM input + `--mask x,y,w,h` (selftest cases 5/6 cover both; planted
negative control still goes red: 15/15 PASS plain, 17-check FAIL with
`--with-negcontrol`). This supersedes BOTH prior menu rounds:

- The 07-16 PrintWindow round (`tol4 over=236922/1555200` = 15.2%) — wall-clock
  shifted frames + a pre-texture-work D3D11 render (large flat regions, fewer
  textured pixels at risk), not comparable.
- The 07-19 button-fill round's whole-frame stats (`tol4 43.35%`) — animation
  polluted (focused DX8 grab vs frozen unfocused D3D11 dump), recorded with that
  caveat at the time.

Capture protocol that made frames comparable (all four learned this session):

1. **DX8 presents ONLY while focused** — the DX8 render loop stops flipping when
   the window is unfocused (framedumps never fire; two silent 5-min runs), while
   D3D11 keeps presenting. Both runs get `AppActivate` right after boot.
2. **The shell background artwork is randomized per boot** (nuclear-plant vs
   snow-fortress art observed) — cross-boot whole-frame A/B is meaningless
   without `-noshellmap`; with it both backends draw the same static wallpaper.
3. Frame-matched = same flip index (f300/600/900) + focus from boot on both.
4. Masks exclude the by-construction-different regions: FPS/clock overlay
   (`--mask 0,0,560,90`) and the backend badge (`--mask 1240,1020,200,60`).

Command: `-win -noaudio -quickstart -noshellmap -ignoreAsserts` (+`-dx11`),
1440x1080. Mean luminance tracks across backends: f300 113.17 vs 112.84, f900
79.95 vs 79.66. f300==f600 exactly on both (static menu -> per-backend render
is deterministic; capture determinism holds).

### Verdict lines (verbatim, f900)

```
unmasked (NC reference):
PARITY FAIL maxdelta=255 mae=8.3766 over=674742/1555200
masked overlay+badge, tol 4:
PARITY FAIL maxdelta=246 mae=7.9214 over=647078/1555200
masked overlay+badge, tol 0:
PARITY FAIL maxdelta=246 mae=7.9214 over=1392778/1555200
```

(f300/f600 masked tol4: over=631556/1555200, maxdelta=239, mae=8.1641 — both
frames byte-identical per backend.)

### Diagnosis: the remaining diff is dominated by a HALF-PIXEL offset

The masked diff viz is an edge-enhanced ghost of the entire background art —
every texture edge halos, flat interiors are black. Shift tests on the f900
pair (central-crop over-fraction at tol 4):

```
integer +1,+1: 0.4396   integer -1,-1: 0.6150   (no integer shift collapses it)
bilinear +0.5,+0.5 resample of D3D11: 0.4413 -> 0.2886
```

A post-hoc bilinear resample cannot fully recover (it blurs), so a 35% drop
from a half-pixel nudge is strong evidence: **the D3D11 output is offset
(-0.5,-0.5) px relative to DX8** — the D3D8/D3D9 half-pixel rasterization
convention (pixel centers at integer coords for screen-space quads) is not
replicated in the D3D11 backend (D3D10+ centers at half-integers). This one
class plausibly accounts for the 07-16 findings 2-4 (border ruler, logo, text
edge halos) in one shot.

### Follow-ups

- Fix the half-pixel convention in the D3D11 2D/screen-space path (projection
  or VS nudge), then re-run this exact A/B — expect the masked tol4 number to
  collapse; this log's numbers are the before-baseline.
- The 07-16 15.2% number should not be quoted as a best-known-parity figure;
  this round's 41.6% masked tol4 is the honest frame-matched baseline.

---

## 2026-07-19 — half-pixel convention FIXED: menu at 99.78% byte-identical (this commit)

### Sign experiment first (banked f900 pair, before touching code)

The previous round's "bilinear +0.5,+0.5 resample" phrasing was directionally
ambiguous, so the sign was settled empirically on the banked f900 dumps with
explicit semantics (output(x,y) = sample(D3D11, x+s, y+s); content moves -s),
central-crop over-fraction at tol 4:

```
baseline                          0.4261
sample(+0.5,+0.5)  content LEFT/UP   0.5496
sample(-0.5,-0.5)  content RIGHT/DOWN 0.2971   <- winner
blur-only control (no net shift)  0.4461
```

D3D11 content must move RIGHT/DOWN — i.e. the D3D11 output sat 0.5px LEFT/UP
of DX8, so the correction is clip.x += w/W, clip.y -= w/H, the OPPOSITE sign
of the classic "D3D9 on D3D10" formula (this engine's screen-space coords were
authored against D3D8's mapping; the blur control proves the previous round's
gain was shift, not blur).

### Fix

`D3D11Backend::Update_Constant_Buffer` folds the half-pixel offset into the
uploaded WVP rows (post-projection, scaled by clip.w), using the live viewport
extent tracked from `Set_Viewport`/backbuffer bind — one place, covers every
draw (2D + 3D share the matrix). Zero per-vertex cost, DX8 path untouched.

Smoke pins (new, 20/20 total): quads HX/HY map an 8x8 two-tone texture 1:1
texel-per-pixel through a LINEAR sampler; the sample pixel reads the 50/50
boundary blend (127,0,127) ONLY with the correct offset — no offset OR flipped
sign both read pure blue (0,0,255). NC demonstrated live: a deliberate
sign-flip build printed
`SMOKE FAIL: halfpix-X(444,12) got RGBA(0,0,255,255)` (and same for Y), then
the restored build passed 20/20.

### Frame-matched masked menu A/B (f900, method as previous round)

New capture wrinkle discovered: the ZP mod randomizes the menu WALLPAPER art
per boot (three arts observed) — `-noshellmap` does NOT pin it. Runs are
paired by re-rolling boots until a wallpaper-identity check (MAE < 15 on a
UI-free patch) passes; matched on attempt 8. Also fixed a stale-dump trap in
the capture loop (a leftover f900.ppm satisfies the wait instantly — delete
`<prefix>_f*.ppm` before every run).

```
before (95a959d, NC baseline):  tol4 over=647078/1555200 (41.6%)  mae=7.9214
after  (this commit):           tol4 over=  3417/1555200 (0.22%)  mae=0.0820
                                tol0 over=  3417/1555200 — identical to tol4
```

tol0 == tol4 means every pixel outside the residual is BYTE-IDENTICAL across
backends. The residual 3417 px all lie in the menu-button column
(x[957..1360], y[194..648], ~7.5 px/row) — button edge/text AA class, the
last open menu item. The wallpaper patch itself diffs at MAE 0.00.

### In-world sanity (navalSandbox, f2700 framedumps via new ZP_FRAMEDUMP_FRAMES)

`ZP_FRAMEDUMP_FRAMES` (comma list, shared by both dump twins) now overrides
the hardcoded 300/600/900 so in-world runs dump past the load screen. Both
backends at game clock 00:01:27, same scene/shroud shape, crash oracle clean
(dumps 12->12, rci mtime unchanged, ALIVE-AT-KILL=True all runs):
near-black (lum<32, rows 8-80%) D3D11 32.0% vs DX8 26.4% — the known
shroud-coverage residual, not disturbed by the offset (pre-fix round read
29.8% vs 26.5% on a slightly different metric/game time).

### Supersedes

- The 41.6% masked tol4 baseline is retired; best-known menu parity is now
  0.22% tol4 / 99.78% byte-identical.
- 07-16 findings 2-4 (border ruler, logo, text-edge halos) are CLOSED as the
  half-pixel class, as conjectured. Remaining open menu item: the 3417-px
  button edge/text AA residual.

---

## Round 6 (2026-07-19, screen-filter path) — mission BW fade works on D3D11

The last known stub family: the W3DShaderManager screen filters (mission BW
fade, motion blur, crossfade) drew via raw `DrawPrimitiveUP` + raw pixel
shaders on the dead D3D8 device, so every filter effect was simply absent
under D3D11 (world rendered normally, fades never appeared).

### What changed

- `IRenderBackend::Capture_Backbuffer` + `Draw_Screen_Filter_Quad`
  (default no-ops; DX8 path byte-untouched): the D3D11 backend snapshots the
  just-rendered backbuffer (`CopyResource`) instead of the DX8
  render-to-texture redirect, and draws the legacy XYZRHW quads through the
  FF-emulation pipeline (screen-space ortho; the half-pixel fold applies
  automatically). The BW filter's `monochrome.nvp` pixel shader is emulated
  by a `gMonoEnable` post-op in the combiner ubershader (same dp3/mul/lrp).
- All four filters branched: BW (full), motion blur (full incl. additive /
  alpha ghost quads; capture skipped on scene-skip frames so the DX8
  "last rendered scene" semantics hold), crossfade CIRCLE (stage-1 mask
  modulate; FB_MASK mode approximated — no dest-alpha mask pass, logged
  once), default filter (dead upstream — preRender returns FALSE since the
  MSAA bugfix).
- `-zpBWFilter <logicframe>` (GeneralsMD): deterministic trigger mirroring
  `ScriptActions::doBlackWhiteMode` — the oracle hook.

### Oracle (grayscale-ness, honestly labeled)

Frame-exact cross-backend diffs of a transient fade aren't practical (game
clocks drift on focus loss), so the machine oracle is **viewport saturation**
`mean(|R-G|+|G-B|)` over the framedump viewport crop (satmetric.py), fade
fully on (trigger frame 60, fade 30, dumps f1800/f2400):

```
D3D11 -zpBWFilter:  0.452 / 0.452   (f1800/f2400 — monochrome)
DX8   -zpBWFilter:  0.452 / 0.452   (positive control — DX8 path + trigger work)
D3D11 no filter:   32.219 / 32.206  (negative control — colorful, 71x separation)
```

Crash oracle all three boots: CrashDumps 12->12, rci mtime 07/17 09:54:08
unchanged, ALIVE-AT-KILL=True. ZP_D3D11_LOG: stub inventory identical with
and without the filter (only the two pre-existing non-filter
`Set_Vertex_Shader`/`Set_Pixel_Shader` hits) — the filter path runs on real
code, no new stubs.

Smoke: 22/22 incl. new `filterGray` (capture + RHW quad + monochrome post-op
end-to-end: quad-B color (128,64,32) -> gray 80, the exact dp3 value) and
`filterKeep` (RHW screen mapping pinned — no spill outside the quad). NC
demonstrated live: the first run asserted a wrong expected region and went
RED (filterGray got 80 where 105-for-magenta was claimed), then the region
was corrected — the assert bites.

### Remaining known gaps after this round

- Menu: the 3417-px button edge/text AA residual (round 5).
- World: shroud coverage residual (32.0% vs 26.4% near-black).
- Crossfade FB_MASK dest-alpha mask pass (demo-hotkey-only; approximated).
- W3DSmudge / W3DVolumetricShadow / W3DProfilerFrameCapture raw
  DrawPrimitiveUP sites — separate families, not screen filters.

---

## 2026-07-20 — first real-play findings (user skirmish) + perf measurement

The first human play session on the D3D11 backend (Debug build, 1600x900
windowed, skirmish vs GLA AI) produced three reports; this round chases all
three. Captures via the framedump twins (focus-independent), naval sandbox,
frame-matched f2700 unless stated.

### 1. Buildings/units too dark — FIXED (34d856f)

User confirmed with `-dx8` vs `-dx11` on the SAME build that DX8 renders them
correctly, isolating it to the D3D11 lighting path.

Root cause: `D3D11Backend::Set_Light_Environment` recorded the environment
pointer but never unpacked it into `cbLighting`, so every lit mesh drew with
`numLights=0` (ambient/emissive only). Terrain was less affected because its
custom shaders carry their own lighting.

    NC-preLightFix-D3D11   meanlum= 82.98 nearblack= 4.11%
    NC-preLightFix-DX8     meanlum= 94.35 nearblack= 1.06%
    POSTFIX-D3D11          meanlum= 86.51 nearblack= 3.94%
    POSTFIX-DX8            meanlum= 94.35 nearblack= 1.06%  (control unchanged)

Gap 11.37 -> 7.84 (~31% closed). The DX8 control is byte-stable across runs.

### 2. "Black parts inside the terrain" — DIAGNOSED, NOT FIXED

Not a lighting defect and not the shroud *content* path (fixed in 8981795).
Side-by-side of the f2700 pair shows the D3D11 shroud boundary is **hard and
blocky with near-black wedges cutting into visible terrain**, where the DX8
boundary is a smooth gradient over tens of pixels. This is the same defect the
aggregate already hinted at (D3D11 near-black fraction consistently ABOVE the
DX8 control) and is what a player reads as "black patches in the terrain".

Ruled out this round: sampler filtering — the copy-shadow shroud upload already
passes `linear=true, wrap=false` (CLAMP), matching the shroud's own
`FILTER_TYPE_DEFAULT` / `TEXTURE_ADDRESS_CLAMP`.

Still open, in suspicion order: the shroud pass's combiner op/blend translation
(a hard cutoff where DX8 modulates), and the texgen texture-matrix mapping
placing cell centers differently so interpolation happens across the wrong
texels. Next step is a DRAWLOG diff of the shroud draw's combiner + blend state
against the DX8 device state for the same draw.

### 3. "Very laggy" — MEASURED, root cause found, fix NOT landed

Both backends render their own FPS counter into the backbuffer, so the
framedumps ARE the measurement (same Debug build, same scene, same frame index):

    D3D11  f2700:  12[30]     <- 12 fps against a 30 cap
    DX8    f2700:  30[30]     <- at the cap

Root cause: `Set_Texture` LockRect'd the source surface and created a **new**
`ID3D11Texture2D` + SRV + sampler on EVERY bind — no cache. Measured with an
experimental cache in place: ~101k binds per 600 frames, i.e. **~170 full
texture uploads per frame**.

An uploaded-texture cache keyed on the source `IDirect3DTexture8*` plus a
content version (the CopyRects mirror counter, so the every-frame shroud still
re-uploads) takes this to a **99.4% hit rate**:

    [D3D11 texcache] f1800 hits/600f=94429  uploads/600f=618 entries=253
    [D3D11 texcache] f2400 hits/600f=101105 uploads/600f=601 entries=254

**But it is not sound and is therefore gated OFF** (`ZP_D3D11_TEXCACHE=1` to
enable). The engine frees and reallocates textures, so a later texture can land
on a freed pointer and take a false cache hit; enabling it renders the world as
striped garbage (`scratchpad/texcache_corruption_f2700.png`). The code and the
measurement are kept in place because the *diagnosis* is solid — what it needs
is a real identity/eviction hook (a texture-destruction callback or a
per-texture serial issued at creation), not a raw pointer key. That is the
single highest-value perf item on the backend.

Caveat for future perf A/Bs: frame-INDEXED dumps are not game-time-matched
across builds of different speed — a faster build reaches f2700 at an earlier
game time and a darker, more-shrouded scene. Compare by game clock, not frame.

### 4. Screenshot feature wrote pure black under D3D11 — FIXED

`W3DDisplay::takeScreenShot` captured through `DX8Wrapper::_Get_DX8_Back_Buffer`
— the D3D8 device, which is dead under D3D11 — so every in-game screenshot
(F9, and the `-stratagemShot` harness) wrote a pure-black PNG. Added
`IRenderBackend::Read_Back_Buffer` (top-down RGB24 CPU readback, default false
so DX8 keeps its legacy surface copy) and routed the PNG path through it.

    zp_screenshot_001.png  1440x1080 meanlum=64.08 nonblack=97.30%
    zp_screenshot_002.png  1440x1080 meanlum=64.10 nonblack=97.00%

NC: the pre-fix build wrote all-black PNGs (meanlum 0) — the reported defect.
The captures show a correct textured world with the D3D11 badge, and double as
the regression check that the default (cache-off) build renders cleanly.

### Harness fixes this round

- The capture wait loop killed the game as soon as the dump file was *created*,
  truncating it mid-write (a 2560x1440 P6 is ~11 MB; one leg landed at 4.5 MB
  and silently skewed a comparison). It now waits for the size to stop growing.
- A live game instance (the user's own session, or a sibling session's boot)
  owns the D3D device; launching into contention fails with the misleading
  "Please make sure you have DirectX 8.1 or higher installed" dialog. Runs now
  wait for other `generals*` processes to exit instead of racing them — and
  never kill them.

---

## 2026-07-21 — texcache identity fix VERIFIED on a second machine (147f5cc + this commit)

The 147f5cc teleport checkpoint carried the sound texture-cache rewrite
(key = never-reused `TextureBaseClass::Get_ID()`, version = `D3DGeneration`
bumped on every `D3DTexture` mutation, destructor eviction as hygiene; ON by
default, `ZP_D3D11_TEXCACHE=0` disables) but was banked unverified. This round
builds and verifies it on a different machine (<machine-2>/Win11, non-vcpkg
`win32-debug` preset, 2560x1440).

### Build portability (non-vcpkg preset, fixed this commit)

Two latent breaks the vcpkg preset had masked — on any machine where
`find_package(ZLIB)` fails, BOTH Compression and the parity tool fell back to
`cmake/zlib.cmake`, and the second `add_library(libzlib)` is a hard configure
error (now guarded `if(NOT TARGET libzlib)`); and `w3d_parity_diff` called
`compressBound`, which the bundled zlib 1.1.4 predates (now a local
`len + len/1000 + 64` bound).

### Oracles (naval sandbox, framedump twins, f1800/f2700, crash-free all legs)

Smoke: 23 checks PASS incl. the new `texCacheIdentity` (same-key/version hits;
bumped-generation, foreign-id and evicted keys all MISS — each miss case IS the
old aliasing bug).

Cache activity (ZP_D3D11_LOG, cache ON): hit rate ≥99.9% with the eviction
churn witness firing —

    [D3D11 texcache] f600  hits/600f=28507 uploads/600f=67 evictions/600f=4  entries=63
    [D3D11 texcache] f2400 hits/600f=64048 uploads/600f=0  evictions/600f=0  entries=85

Same-machine fps A/B (baked FPS counter in the f2700 dumps; NC = same build,
`ZP_D3D11_TEXCACHE=0`, hits/uploads all 0 in its log):

    D3D11 cache OFF (NC):  16[30]
    D3D11 cache ON:        19[30]
    DX8 control:           30[30]

World renders correctly with the cache on (no striped garbage — the old
pointer-keyed corruption does not reproduce); shroud residual unchanged
(near-black 36.6% vs DX8 32.7% at f2700, the known open boundary defect).

### Verdict

The cache is SOUND and stays default-ON (+3 fps here, and it removes the
~170 uploads/frame pathology that cost the slower machine 12→much worse).
But upload spam is no longer the dominant cost: 16 fps with the cache fully
off means the remaining 19-vs-30 gap lives elsewhere (draw-path CPU overhead —
state translation, per-draw constant-buffer updates — is the natural suspect).
"Very laggy" therefore stays OPEN with a new profiling target, and the perf
item graduates from "texture uploads" to "per-draw overhead".

---

## 2026-07-25 — texcache: sound and fast, but the fps win did NOT materialise

Continuation of the 2026-07-20 perf finding (§3, "very laggy": D3D11 12 fps vs
DX8's 30 cap — `Set_Texture` re-uploaded every texture on every bind). The
uploaded-texture cache that shipped gated OFF as unsound was re-keyed and turned
ON by default in `147f5cc`; this entry records what the **GPU run measured**, in
the console session, replacing the projection that stood here before.

### What changed (the v1 aliasing bug is closed by construction)

- **Key** = `TextureBaseClass::Get_ID()` — process-monotonic, never reused. v1
  keyed on the raw `IDirect3DTexture8*`, which the engine reallocates at a freed
  address → a stale entry served as a hit → the striped-garbage world
  (`scratchpad/texcache_corruption_f2700.png`). Distinct textures can no longer
  collide on a key.
- **Version** = `Get_D3D_Generation()`, bumped at every mutation of the
  `D3DTexture` pointer (ctor, `~TextureBaseClass`, `Invalidate`,
  `Set_D3D_Base_Texture`, `Load_Locked_Surface`, `Poke_Texture`). An entry cannot
  outlive the bytes it was made from.
- **Evict** on `~TextureBaseClass`; hygiene only — because ids never repeat, a
  missed eviction can only leak, never alias.
- **Cacheability** conservative: `POOL_MANAGED` + non-procedural only. The
  GPU-composed shroud dst and render targets re-upload per bind as before.

### Measured (navalSandbox, Debug x86, 2560×1440, console session)

**The cache works.** Counters from the cache-ON run:

```
[D3D11 texcache] f600  hits/600f=28521 uploads/600f=67 evictions/600f=4 entries=63
[D3D11 texcache] f1200 hits/600f=27096 uploads/600f=1  evictions/600f=0 entries=64
```

Evictions > 0 in the first window is the churn witness — textures were actually
freed and reallocated, the exact condition v1 aliased under, and nothing broke.

**The fps win did not.** On-screen fps with the cache ON measured **11–14**
against DX8's 30 cap on the same build and scene. The previous version of this
entry projected 12 → ~30. **That projection is retracted: eliminating ~101k
uploads/600f bought roughly 1–2 fps.** Texture upload was not the bottleneck.

This independently corroborates the 2026-07-21 second-machine entry above,
which reached the same verdict from a cleaner measurement (same-machine A/B,
fps baked into the framedumps, cache-off negative control): **OFF 16[30] → ON
19[30], DX8 control 30[30]** — +3 fps, not +18. Two machines, two harnesses,
same conclusion: the cache is worth keeping and is not where the frames went.
That entry's call to graduate the perf item from "texture uploads" to
"per-draw overhead" stands, and the drift finding below is the sharper lead.
Whatever is costing D3D11 its other ~16 fps is still unidentified — see the
terrain finding below, which is the strongest lead.

Caveat on the ~101k baseline: it comes from the earlier instrumented run, not
re-measured here. A cache-OFF run reports `hits=0 uploads=0` because the
counters only increment on the cache path — useful as a negative control (it
correctly fails the PERF check) but it is not a raw upload count.

### NEW FINDING — D3D11 image DRIFTS over hundreds of frames (not the cache)

Diffing frames of the *same* run, camera parked, `w3d_parity_diff --tol 2`:

```
ADJACENT frames (D3D11 toggle, f900 vs f901)  mae=0.0059  over=186/3686400      (0.005%)
SAME-RUN temporal (DX8,       f900 vs f1500)  mae=0.8682  over=85231/3686400    (2.3%)
SAME-RUN temporal (D3D11 ON,  f900 vs f1500)  mae=12.2593 over=1878926/3686400  (51.0%)
SAME-RUN temporal (D3D11 OFF, f900 vs f1500)  mae=17.7601 over=1954927/3686400  (53.0%)
DX8 vs D3D11-ON               (f1500)         mae=18.7317 over=1964666/3686400  (53.3%)
```

Read those top two rows together — they are the whole finding. **Consecutive
D3D11 frames are effectively identical** (186 pixels of 3.7M), so there is no
per-frame flicker or noise. Yet across 600 frames D3D11 moves 51% of pixels
where DX8 moves 2.3%. The divergence is therefore **progressive drift**, not
instability: the image walks away from where it started over hundreds of frames.
Reproduces with the cache ON and OFF alike, so the cache does not cause it.

An earlier version of this entry called it "frame-unstable" on the strength of
the 600-frame number alone, before adjacent frames had ever been measured. That
was wrong, and it would have sent the investigation hunting for per-frame noise.
The adjacent-frame measurement only became available because the toggle oracle
needed it for a different purpose.

Visually the D3D11 terrain is purple, noisy and high-contrast where DX8 is
smooth and light. This is the top open D3D11 defect and the most likely home of
the missing ~16 fps. Next probe: dump a dense frame ladder (e.g. 300/600/900/
1200/1500) and find whether the drift is monotonic (accumulating state — a
leaked/never-reset render state, a texture-LOD or mip-streaming path that only
D3D11 advances) or converges to a plateau.

### Why the cross-run A/B oracle was replaced

The original correctness oracle diffed a cache-ON run against a cache-OFF run
and expected near-identity. It cannot work: two runs differ by mae ~19 over ~54%
of pixels — the same magnitude as a DX8-vs-D3D11 diff — so there is no signal
left to detect corruption with. Replaced by an **in-process toggle**:
`ZP_D3D11_TEXCACHE_TOGGLE=1` consults the cache only on even flip frames, and the
harness dumps two **adjacent** frames (900, 901), one rendered with the cache and
one without, from a single process. One frame apart the scene is nearly static,
so stale or aliased bytes have nowhere to hide. Each framedump log line now
records `texcache=on|off`, and the oracle refuses to pass unless the pair
actually straddles both states.

### Gate status

`scripts/verify_texcache.ps1` — SMOKE (identity contract, check R) and CRASH pass;
PERF passes with a cache-OFF negative control that correctly goes RED; CORRECT is
the rebuilt toggle oracle with a DX8-vs-D3D11 known-different negative control.
The 2026-07-25 first run returned a false FAIL on CORRECT: `w3d_parity_diff`
prints its `PARITY` summary *before* an "exceeding pixels" detail line and the
script read `Select-Object -Last 1`, so the verdict never parsed. Fixed.

**GREEN as of 2026-07-25 23:33 (`texcache_verify_20260725-233309`, exit 0):**

```
[PASS] SMOKE    texCacheIdentity: same-key/ver hits; bumped-generation, foreign-id and evicted all MISS - OK
[PASS] PERF     last window: hits=27096 uploads=1 evictions=0; NC=RED (cache-off correctly fails)
[PASS] CORRECT  states=off/on  mae=0.01 over=0.0%; NC=RED (dx8-vs-d3d11 mae=18.73 correctly fails)
[INFO] SANITY   mae=18.7317 over=1964666/3686400
[PASS] CRASH    dx8, d3d11_on, d3d11_off, d3d11_tog all alive-at-kill and dump-clean
VERDICT: PASS - texcache is sound, fast, and non-corrupting.
```

The toggle engaged (`states=off/on`, straddle asserted, so the comparison is not
vacuous) and the ON/OFF pair one frame apart differed by 186 pixels of 3,686,400.
The cache is non-corrupting by measurement, not by argument.

What this run does NOT establish: that the cache is worth its complexity. It is
sound and it eliminates the uploads, but the fps it was built to buy did not
appear (see above). Keep it — it is correct and costs nothing — but the perf
work has moved to the drift finding.

### RETRACTED — the "progressive drift" finding (2026-07-26)

The section above ("D3D11 image DRIFTS over hundreds of frames") does not
survive its own follow-up probe. `scripts/probe_drift.ps1` dumped a frame
ladder (300/600/900/1200/1500) for **both** backends in one run each and diffed
it three ways. DX8 is the control the original finding never had.

```
                                    mae per 300-frame step
  D3D11 consecutive   f300->600  22.07   600->900  17.89   900->1200  19.91   1200->1500  19.73
  DX8   consecutive   f300->600  15.75   600->900   1.60   900->1200   9.46   1200->1500   9.65

  D3D11 vs f600 (post-transient)   f900 17.89   f1200 20.38   f1500 18.00
  DX8   vs f600 (post-transient)   f900  1.60   f1200  9.23   f1500  2.22

  DX8 vs D3D11, SAME frame index   f300 21.11  f600 18.03  f900 18.86  f1200 19.84  f1500 18.97
```

Three reasons the finding fails:

1. **Nothing accumulates.** Accumulating state was the entire hypothesis, and it
   predicts that distance to a fixed reference grows with time. D3D11's distance
   to f600 goes 17.89 -> 20.38 -> 18.00 over 900 frames: bounded and
   non-monotonic. There is no accumulation to find.
2. **DX8 comes back.** DX8 is far from f600 at f1200 (9.23) and near it again at
   f1500 (2.22). That is an oscillation — animated scene content, near-certainly
   the water in `-navalSandbox` — not drift, in either backend.
3. **The 51%-vs-2.3% contrast was a single sample pair.** DX8's self-temporal
   series ranges 1.60 to 9.65 mae (25% to 52% of pixels) depending purely on
   which pair you sample. The original entry took one DX8 pair (f900 vs f1500)
   that happened to land near a matched phase of that cycle, read its 2.3% as
   "DX8's baseline", and compared it to one D3D11 pair. Four pairs later, DX8
   moves up to 52% of pixels too.

And the measurement that explains all of the above: **the cross-backend gap is
flat at ~19 mae / 54% of pixels at every frame index**, f300 through f1500. Every
temporal number ever measured here — D3D11 self (17.89-22.07), DX8-vs-D3D11
(18.03-21.11) — sits inside that band. The temporal experiments were
re-measuring the static parity gap. `probe_drift.ps1` now asserts this
explicitly and refuses to report a temporal finding whose spread does not exceed
the static gap.

What survives, weakly: DX8 reaches 1.60 mae against itself while D3D11 never
drops below 17.89 in four samples. With n=4 and the animation phase
uncontrolled, that is a hint, not a finding.

**The real defect, restated:** a *time-independent* DX8-vs-D3D11 parity gap of
~19 mae over ~54% of pixels — the purple, noisy, high-contrast terrain already
noted visually, present identically at every frame index. It needs no temporal
machinery to reproduce or to chase, and it remains the most likely home of the
missing ~16 fps.

Method note for the next probe: the first rung of any ladder is unusable as a
reference. Both backends move ~54% of pixels between f300 and f600 — a startup
transient (fade-in / camera settle) that saturates the metric. Reference from
the second rung onward, and never read a temporal number without the
same-frame cross-backend gap beside it.

### ROOT CAUSE — the D3D11 backend uploads no mipmaps (2026-07-26)

The "static parity gap" left over after the drift retraction is not a shading,
lighting or tint defect. It is a **missing mip chain**, and it is almost
certainly also the missing ~16 fps.

Measured on the same f900 pair, terrain window only (UI/shroud excluded):

```
  raw mae                                      34.39
  best rigid alignment  dx=+7 dy=-4            26.94   (22% of the gap)
  per-tile shift across a 3x3 grid    dx +5..+8, dy -3..-5   (small gradient)
  aligned per-channel means   DX8 101.4/100.1/78.6   D3D11 94.6/90.5/70.8
  aligned + best per-channel gain applied      28.39   (WORSE than 26.94)
```

The per-channel gain making the residual *worse* is the tell: a tint hypothesis
predicts it collapses. The gap is not chromatic, it is **spatial-frequency** —
visible immediately once the pair is aligned and magnified: DX8 terrain is
smooth and heavily filtered, D3D11 terrain is sharp, aliased and high-contrast.
The earlier "purple, noisy, high-contrast" note read that aliasing as a colour
defect; D3D11 is not tinting the rock, it is rendering it un-mip-mapped.

Confirmed in code, not inferred from the image. Every `CreateTexture2D` in
`D3D11Backend.cpp` sets `MipLevels = 1`, and there is no `GenerateMips` call in
the file. Both texture-upload paths - RGBA (~L873) and BCn (~L965) - hand a
single `D3D11_SUBRESOURCE_DATA` (level 0 only) to an `IMMUTABLE` resource. The
samplers meanwhile request `D3D11_FILTER_MIN_MAG_MIP_LINEAR` with
`MaxLOD = D3D11_FLOAT32_MAX`: the backend *asks* for trilinear mip filtering
against resources that have exactly one level, so every sample lands on level 0
regardless of minification.

Why this is the fps lead too: minified terrain sampling level 0 has almost no
texture-cache locality - adjacent screen pixels land on widely separated texels.
That is a bandwidth/cache-miss cost that no amount of upload elision can touch,
which is exactly why the texcache (which fixed the *upload count*, 101k -> 1 per
600f) bought no frames. Uploads were never the bottleneck; *sampling* is.

Next: upload the full mip chain. The DX8 source surfaces already carry one
(`IDirect3DTexture8::GetLevelCount`), so this is a plumbing change in the two
upload paths - allocate `MipLevels = levels`, fill one `D3D11_SUBRESOURCE_DATA`
per level - not a content change. Note it interacts with the texcache: the cache
key is versioned per surface mutation and must invalidate on a level-count
change too. Predicted oracle: terrain mae collapses toward the DX8 reference AND
fps rises; if mae collapses but fps does not, the fps gap is elsewhere and this
was "only" a correctness fix.

### Mip upload landed: real correctness win, predicted fps win FALSIFIED (2026-07-26)

Both upload paths now hand D3D11 the full source mip chain
(`Upload_Texture_{RGBA,BC}_Mips`, `MipLevels = count`, one subresource per
level, gathered from `IDirect3DTexture8::GetLevelCount`). The single-level entry
points remain as wrappers for the smoke oracle.

**Mips are demonstrably live**, not merely wired: the D3D11 terrain is no longer
razor-sharp, and it now shows visible rectangular mip seams where adjacent
terrain tiles select different levels. That artifact is itself the evidence -
level selection is happening.

Correctness moved, on the same probe/ladder as the pre-fix run:

```
  DX8-vs-D3D11, same frame   BEFORE  21.11 18.03 18.86 19.84 18.97   (f300..f1500)
                             AFTER   17.71 17.77 16.45 17.81 16.78
```

~13% better at f900 (18.86 -> 16.45). Real, and visible side by side - but an
improvement, NOT the collapse toward DX8 that was predicted.

**And the fps prediction was wrong.** The stated oracle was "terrain mae
collapses AND fps rises"; the recorded prediction was that un-mipped minified
sampling was the home of the missing ~16 fps.

```
  D3D11 pre-fix   14 fps      D3D11 post-fix   11 fps      DX8   30 fps  (cap)
```

It went DOWN. Caveat, stated because the numbers are weak: n=1 per config, and
this metric is known noisy across runs and machines (this machine has read
11-14 for D3D11; the second machine read 16 OFF / 19 ON for the same build).
So "mips made it slower" is NOT established - the honest reading is only that
**the predicted fps win did not appear**, and the un-mipped-sampling hypothesis
for the missing ~16 fps is unsupported. Keep the change on correctness grounds.

That also retires the last hypothesis this line of investigation produced. The
fps gap is not upload count (texcache, 101k -> 1, no frames) and not mip
sampling (this change, no frames). It has never been measured directly - every
attempt so far has inferred it from image diffs. The next move should be a
PROFILE (GPU timestamp queries per pass, or a CPU-side frame breakdown), not
another pixel experiment.

New defect opened by this change: the mip seams above. Adjacent terrain tiles
picking visibly different levels means LOD is being computed per small tile
rather than continuously - DX8 blends the same tiles smoothly. Suspect the
terrain draws each tile with its own texture/sampler rather than one atlas.

### Hardware provenance — and a retraction of the fps numbers above (2026-07-26)

**Every fps number in this log predating this entry is unsafe to compare against
any other**, because the log never stamped which machine produced it and at
least three boxes are involved. Reconstructed provenance:

| number | box | trustworthy? |
|---|---|---|
| "12 fps vs DX8's 30" (texcache motivation) | earlier machine | not comparable to anything below |
| "OFF 16 -> ON 19" (texcache 2nd-machine check) | 2nd machine (<machine-2>) | not comparable |
| "11-14 fps" | Spooky | **contaminated, see below** |
| pre-mip 14 fps / post-mip 11 fps | Spooky | **RETRACTED** |

**The 14 -> 11 "fps regression" attributed to the mip upload is withdrawn.**
Spooky was concurrently running VTOL VR plus a CatFight analysis - a
GPU-saturating parallel load - across both runs, which were ~20 minutes apart.
The measured drop is fully explained by contention and is not evidence about
mips either way.

Worse, the control was structurally incapable of catching this. DX8 read 30 in
both runs and that was taken as proof conditions matched. **DX8 is capped at 30**:
it has headroom, so it pins to its ceiling whether the GPU is idle or half
consumed, while the uncapped D3D11 absorbs the entire contention. A control that
cannot move when the feared variable moves is not a control. Any future
reference for a timing measurement must be verified to have no ceiling in the
regime being measured.

What is NOT affected: every pixel/parity result in this log. Frame dumps are
keyed by LOGIC frame index and the game is fixed-timestep, so f900 is the same
world state regardless of how long it took to render (both runs' in-game clocks
read 00:00:27.23). The drift retraction, the missing-mipmap root cause, and the
mip fix's correctness win (same-frame mae 18.86 -> 16.45, mip seams visible)
all stand on that basis.

**Consequence for the open work: the "missing ~16 fps" target is unestablished.**
It was derived on machines whose load state was never recorded, one of them
running VR in parallel. Do not aim at 16. Re-baseline on a machine confirmed
quiet (<machine-1>) via `scripts/probe_fps.ps1`, whose `d3d11_mips_off` arm IS
that baseline.

Standing rule from here: any timing run records hostname + GPU + a
confirmed-quiet check in its artifact, uses ONE env-toggled binary rather than
two builds, takes n>=3 per config, and claims a difference only when the two
spreads are DISJOINT.

### First trustworthy fps baseline — quiet <machine-1>: mips COST ~3.6 fps (2026-07-26)

First execution of `scripts/probe_fps.ps1`, on a machine verified quiet, per the
standing rule above. Every fps number below is the first in this log stamped
with its hardware and load state.

**Provenance:** host `<machine-1>`, NVIDIA GeForce RTX 5060 Ti (driver
32.0.15.9186), Windows 11. Quiet check: nvidia-smi 12% util / 2.3 GB pre-run
(desktop only — no VR, no games, no analysis jobs; process list captured in the
artifact), 1% util post-run, zero generals* processes at start. Interactive
console session. Commit `1ad67f5`, ONE Debug binary env-toggled
(`ZP_D3D11_MIPS`), staged as `generalszh_fpsprobe.exe`. Built with the
**non-vcpkg `win32-debug` preset** (VS 2026 / MSVC 14.51 x86 — this box has no
VS 2022/vcpkg); irrelevant to validity since every comparison is internal to
this one binary, but future cross-build comparisons must note it. Window
f300->f900 at 2560x1440, `-navalSandbox`.

Two invocations pooled: `-Repeats 3` (`fps_probe_20260726-105122`) plus a
`-Repeats 1` supplemental (`fps_probe_20260726-110623`) because the very first
run of the session lost its sample to shader-compile warm-up (reached f300,
never f900 inside the 420 s timeout; all later runs ride the driver shader
cache). Raw samples:

| config | samples (fps) | n | min | median | max | spread |
|---|---|---|---|---|---|---|
| d3d11_mips_on | 16.12, 16.13, 16.49 | 3 | 16.12 | 16.13 | 16.49 | 0.37 |
| d3d11_mips_off | 19.75, 19.76, 19.83, 19.28 | 4 | 19.28 | 19.76 | 19.83 | 0.55 |
| dx8 | 29.38, 29.40, 29.40, 29.40 | 4 | 29.38 | 29.40 | 29.40 | 0.02 |

Both built-in falsifiers passed in both invocations: every D3D11 run logged the
mips state its config asked for (`mips=on`/`mips=off` on the dump line), and
dx8 landed on its ~30 cap (29.40), so the parser is sane.

**Findings:**

1. **The mip upload (56d5329) DOES cost frames.** on [16.12–16.49] vs off
   [19.28–19.83] are DISJOINT: median 16.13 vs 19.76 = **~3.6 fps (~18%)
   slower with mips**. The conservative bound (closest edges) is 2.79 fps.
   This is the finding Spooky's contaminated 14->11 claim gestured at; it is
   now demonstrated on a quiet box. The correctness win stands (mae 18.86 ->
   16.45); the cost is now a measured engineering trade, not a rumor.
2. **The real D3D11 baseline is established:** pre-fix behaviour (mips_off) =
   **~19.8 fps** on quiet <machine-1>; current behaviour (mips_on) = ~16.1 fps.
   DX8 pins its 30 cap as always — which still says nothing about uncapped DX8
   speed, so "the gap to DX8" remains unmeasurable by this method (DX8 has
   headroom at its ceiling; see the control lesson above).
3. Noise floor on this quiet box is tiny (spread <= 0.55 fps across pooled
   invocations vs a 3.6 fps effect) — retroactive confirmation that the
   machine-load confound, not backend variance, is what poisoned every earlier
   number.

Next for perf: GPU timestamp queries per pass (profile directly; stop inferring
fps causes from image diffs). The mip cost is the first candidate to profile —
likely texture-bandwidth/filtering bound in the terrain pass.

### GPU timestamp profiler: the frame is NOT draw-bound, and the swapchain is not the thief (2026-07-26)

Built the per-span GPU profiler (`ZP_D3D11_GPUPROF=1`): D3D11 timestamp +
disjoint queries in an 8-frame polled ring inside the backend, engine-side
markers (`3d` after drawViews, `ui` after mouse, `tail2d` before End_Render,
`endscene` at End_Scene entry, `present` after Present, `terrain_in`/`terrain`
RAII-bracketing the terrain Render), averaged per-span lines to the
ZP_D3D11_LOG sink every 300 flips. Same quiet <machine-1> box as the baseline
above; runs at 2560x1440, `-navalSandbox`, warm-up launch discarded (the first
launch of a freshly staged exe reproducibly stalls - third occurrence).

Read carefully: a D3D11 GPU timestamp gap includes any time the GPU sat IDLE
waiting for CPU submission or a present handshake, so a span measures "GPU
timeline between markers", not pure draw execution. That caveat turned out to
be the finding.

2x2 sweep, swap model (`ZP_D3D11_FLIP=1` flip-model vs default single-buffered
blt DISCARD) x mips (on/off), 300-frame averages, `swap=`/`mips=` logged per
line by the binary itself (falsifiers), meanlum parity across swap models
exact (60.17/59.89 on, 59.73-59.77/59.43-59.45 off):

| arm | frame period (t_ms f300->f900) | 3d | terrain | ui | tail2d+endscene | present span | total |
|---|---|---|---|---|---|---|---|
| blt, mips on | 59.7 ms (16.8 fps) | 4.6-4.8 | 2.0-2.5 | 0.6-0.9 | ~0.1 | 50.7-52.4 | 58.4-60.5 |
| blt, mips off | 48.9 ms (20.5 fps) | 3.8-4.7 | 1.8-2.3 | 0.7-0.9 | ~0.1 | 40.2-43.5 | 48.0-50.2 |
| flip, mips on | 59.7 ms (16.8 fps) | 3.9-4.7 | 2.1-2.7 | 0.7-0.9 | ~0.1 | 3.0-11.7 | 11.3-19.1 |
| flip, mips off | 48.8 ms (20.5 fps) | 3.8-4.7 | 1.8-2.2 | 0.7-0.9 | ~0.1 | 36.6-41.8 | 43.3-49.6 |

**Findings:**

1. **The swapchain model is NOT the fps bottleneck - hypothesis refuted.** fps
   is bit-identical across blt and flip (16.8/16.8 and 20.5/20.5). The
   flip-model toggle (`ZP_D3D11_FLIP=1`) stays in the tree as a diagnostic.
2. **The frame is not draw-bound.** All visible GPU draw work - 3d scene,
   terrain, UI, 2D tail - sums to <= 8 ms of a 49-60 ms frame in every arm.
   ~40-50 ms/frame is GPU-idle time whose apparent location (inside the
   present span for blt and flip/mips-off; in the unprofiled inter-frame gap
   for flip/mips-on) shifts with configuration, i.e. it is starvation, not
   execution.
3. **The ~11 ms/frame mip cost is NOT GPU sampling in the draws.** terrain
   moved 2.2 -> 2.5 ms (+0.3) with mips on; the other draw spans are flat. The
   cost lives in the dark region.
4. **It is not cached-upload traffic either:** `[D3D11 texcache]` counters are
   equal in all four arms (hits/600f=28508-28514, uploads/600f=67,
   evictions/600f=4).

**Open: the mip-cost mechanism.** Remaining suspects, in order: (a) per-bind
CPU in the NON-cacheable upload path (procedural / default-pool / shroud
textures re-upload every bind, are invisible to the texcache counters, and
with mips on pay the full LockRect-chain + larger CreateTexture2D per bind);
(b) driver residency/paging against the larger texture memory; (c) present
interplay. Next instrumentation: count + byte-size + wall-time the
non-cacheable upload path per frame - it is the last unmeasured suspect. The
same counters would also bound how much of the 40-50 ms dark region is
D3D11-backend CPU at all, vs engine CPU the DX8 path pays too (DX8 fits the
whole shared engine frame in <= 33 ms at its cap).

## Upload profiler: the non-cacheable path IS the thief - 41 ms/frame of CPU uploads (2026-07-26)

**Instrumentation (this commit):** CPU-side wall-time counters in
`D3D11Backend::Set_Texture` - every bind that reaches an actual upload
accounts count + bytes + QPC wall-ms into one of four buckets (`nc` =
non-cacheable, re-uploads every bind; `miss` = cacheable first upload;
`shadow` = copy-shadow shroud path; `fallback` = neutral/magenta), timed from
just after the cache check to upload completion (LockRect chain + CPU decode +
CreateTexture2D/SRV/sampler). Emitted as `[D3D11 uploadprof]` every 300 flips,
gated on the same `ZP_D3D11_GPUPROF=1`, with the `mips=`/`swap=` falsifiers
stamped.

**Provenance:** <machine-1>, RTX 5060 Ti, 2560x1440, quiet (12% pre / 5% post,
desktop composition only), Debug build `win32-debug` preset, blt swapchain
(swap refuted last entry), discarded warm-up launch, steady-state window
f301-f900. Artifacts: `build\win32-debug\gpuprof_20260726-132649`.

**Steady state (f600/f900 emits, identical to 3 digits):**

| arm      | frame period | nc count | nc bytes    | nc CPU wall | miss | shadow  |
|----------|--------------|----------|-------------|-------------|------|---------|
| mips on  | ~59.7 ms     | 46.0/f   | 34.6 MB/f   | 41.0-41.2 ms/f | 0  | 0.63 ms/f |
| mips off | ~48.8 ms     | 46.0/f   | 26.6 MB/f   | 31.3-31.6 ms/f | 0  | 0.60 ms/f |

**Findings:**

1. **The mip-cost mechanism is CONVICTED: suspect (a), the non-cacheable
   per-bind upload path.** Mips on adds +8.0 MB/frame and +9.6 ms/frame of
   CPU wall time in that path - against a measured frame-period delta of
   10.7 ms (59.7 - 48.8). ~90% of the mip cost is this one code path.
2. **Per-byte cost is IDENTICAL across arms: 1.21 ms/MB both.** The mip cost
   is purely the extra bytes (full chain = +30%) through the same path - no
   per-level overhead, no driver-residency effect needed. Suspects (b)/(c)
   are dismissed as primary.
3. **The 40-50 ms dark region is EXPLAINED: it is this CPU time.** 41 ms/frame
   of Set_Texture upload work on the render thread while the GPU starves -
   matching the gpuprof finding that no draw span contains the cost. The
   D3D11 backend is CPU-bound on texture re-upload, full stop.
4. **The counters self-validate:** `miss` decays to exactly 0 in steady state
   (the cacheable world set uploads once - matches texcache uploads/600f=67
   unchanged); the f300 window still shows warm-up misses (0.2/f). A
   regression that made cacheable textures re-upload would light `miss` up.

**The 46/frame:** avg ~750 KB each - these are the procedural / POOL_DEFAULT
textures excluded from the cache by design (see the CACHEABILITY comment in
Set_Texture). 46 binds/frame re-uploading ~27-35 MB is the entire perf story:
fix THIS and the projected frame is ~19 ms + draws, i.e. the 30 fps cap.

**Next (fix, not measurement):** identify the 46 by name (one log line with
texture name + pool + procedural flag on the nc path), then either (i) extend
the cache with dirty-tracking for procedural surfaces (generation bump on
Unlock instead of blanket exclusion), or (ii) mirror them into persistent
D3D11 textures updated via UpdateSubresource on actual change. Expected win:
~31-41 ms/frame -> D3D11 should hit the 30 fps cap in both mips arms.

## FIX: create-once procedural textures cached - D3D11 reaches the DX8 cap (2026-07-26)

**Change:** `TextureBaseClass::Is_Procedural_Cacheable()` opt-in - a procedural
texture whose bytes are written only between creation and first bind (and whose
every refresh path release-and-recreates under a fresh never-reused id) may
declare itself cacheable. Opted in: `TerrainTextureClass` (both ctors),
`AlphaTerrainTextureClass` (aliases the base terrain texture's D3D object - it
was re-uploading the SAME 2048x1024 bytes under a second cache key),
`AlphaEdgeTextureClass`, `W3DTreeBuffer::W3DTreeTextureClass`. Lifecycle
audited: getTerrainTexture()/getFlatTexture()/W3DTerrainBackground/W3DTreeBuffer
all REF_PTR_RELEASE + new on every refresh; in-game nothing rewrites them in
place. Mid-life in-place writers (video buffers, radar) stay excluded and
re-upload per bind. Kill switch: `ZP_D3D11_TEXCACHE_PROC=0` reverts procedural
textures to per-bind upload.

**Provenance:** <machine-1>, RTX 5060 Ti, 2560x1440, quiet (1-8% GPU), Debug
`win32-debug`, warm-up launch discarded. Artifacts:
`build\win32-debug\gpuprof_20260726-135327` (uploadprof) and
`fps_probe_20260726-135658` (oracle). SMOKE PASS exit 0 (incl.
texCacheIdentity negative controls).

**uploadprof, steady state (was -> is, mips on):**
nc 34,581 KB/f -> 960 KB/f (-97%); nc wall 41.0 ms/f -> 1.49 ms/f;
texcache entries 63 -> 69 (+6 = the newly cached composites), hits/600f
28.5k -> 32.3k. miss still decays to exactly 0 (self-check holds).

**fps oracle (n=3 per config, f300->f900):**

| config         | min    | median | max    | spread |
|----------------|--------|--------|--------|--------|
| d3d11 mips on  | 26.74* | 29.36  | 29.36  | 2.62   |
| d3d11 mips off | 29.34  | 29.36  | 29.36  | 0.02   |
| dx8            | 29.36  | 29.38  | 29.38  | 0.02   |

*one transient low sample; the other two runs sit at 29.29/29.36.

**Findings:**

1. **The fps gap is CLOSED: D3D11 median 29.36 vs DX8 29.38 - a 0.02 fps
   residual against a morning baseline of 16.1.** Both renderers now sit at
   the engine's ~30 fps logic cap.
2. **The mip cost is GONE:** mips on/off overlap completely (29.36 vs 29.36
   median). The full-chain upload correctness win (mae 18.86->16.45) is now
   free.
3. **Visual parity holds:** pre-vs-post whole-frame MAE 11.9 is BELOW the
   pre-vs-pre run-to-run noise floor (15.4); the in-game counter reads 29[30]
   (was 17[30]) on an otherwise identical frame. In-image before/after:
   `build\win32-debug\prefix_f900.png` / `fixed_f900.png`.
4. Remaining uploadprof residue (nc 0.96 MB/f = the transient 64x64 decal
   churn + shadow 0.32 MB/f shroud) costs ~2.1 ms/f CPU - immaterial at the
   cap; revisit only if a future map/scene pushes it up.

**Perf story complete:** no-mips (16 fps, wrong) -> mips fixed (12->16 fps,
correct but slow) -> texcache (16 fps, cache sound but nc path unmeasured) ->
uploadprof convicts nc path (41 ms/f CPU) -> create-once composites cached ->
**29.4 fps, at cap, correct.** Open cosmetic defect unchanged: mip seams
between terrain tiles (pre-existing, tracked separately).

## Open defect: disabled command-bar buttons render un-greyed (2026-07-27)

Found in live play (first user session on the capped build): build buttons
whose prerequisites are missing (e.g. Strategy Center absent) render at full
brightness instead of greyed. They correctly do NOT respond to clicks - the
game logic is fine, so this is display-only. ZH draws a disabled button as the
same image modulated by a grey draw-color; suspect the D3D11 2D/UI path drops
that per-draw color modulation (combiner arg ignored -> texture drawn as-is).
Repro: skirmish, any faction, before building the prerequisite structure -
compare command bar vs dx8. Not yet instrumented.

## Open defect: helicopter main rotor not rendered (2026-07-27)

Second live-play find: Comanche top/main rotor is absent (static blades on
other axes render, with house color). ZH rotors use a special-cased material
(rotating blade geometry swapped for an alpha/additive blur disc when
spinning); suspect an unhandled blend/combiner state in the D3D11 3D path
that resolves to invisible rather than the magenta unknown-format fallback.
Repro: build a Comanche, parked or flying - top rotor missing vs dx8.

## Open defects: invisible projectiles / particle effects (2026-07-27)

Live-play finds three and four, same suspected family as the rotor (things
that render INVISIBLE rather than magenta under D3D11):
- **Comanche rockets not visible in flight** (impacts still resolve - logic
  fine, projectile render missing).
- **RPG/bazooka launch smoke absent** (missile-trail / muzzle particle
  systems not rendered).
Suspect the particle-system / additive-alpha draw path (point sprites or
sorted translucent pass) is not reaching the screen in the D3D11 backend.
Together with the rotor these likely share one root cause in the translucent/
effects pass. Repro: any Comanche or RPG infantry attack, compare vs dx8.

## FIXED: disabled command-bar buttons render un-greyed (2026-07-27)

Root cause was closely related to the suspicion but not a dropped combiner
arg: the disabled state doesn't use a grey draw-color at all. With
WIN_STATUS_USE_OVERLAY_STATES, W3DPushButton draws the disabled image with
`Display::DRAW_IMAGE_GRAYSCALE`, and `Render2DClass::Render()` (GeneralsMD
render2d.cpp) implements that mode by bypassing the backend abstraction:
raw `DX8Wrapper::Set_DX8_Texture_Stage_State` calls build a two-stage DOT3
luminance trick (stage 0 MULTIPLYADD -> 0.502+0.502*tex with TFACTOR
0x80A5CA8E alpha-replicated, stage 1 DOTPRODUCT3 against 4*(TFACTOR-0.5)).
The D3D11 backend only mirrors the texgen stage states
(D3D11Backend_W3D.cpp), so the overrides evaporated and the image drew
full-color through the default MODULATE(TEXTURE, DIFFUSE=white) combiner -
exactly the observed defect.

**Fix** (same RB_Mirror self-gated-hook idiom as texgen):

- `D3D11Backend::Set_Grayscale_Override(bool)` - drives the combiner's
  existing monochrome post-op (built for the BW screen filter) with the DX8
  trick's effective weights: rgb -> dot(rgb, (0.295, 0.587, 0.114)), alpha
  passthrough (monoFade alpha lane 0). The ~0.004 constant term of the DX8
  arithmetic is under 1/255 and dropped.
- `RB_Mirror_Grayscale2D(bool)` (dx8wrapper.h / D3D11Backend_W3D.cpp) -
  no-op while DX8 draws.
- `Render2DClass::Render()` brackets the grayscale draw with it; the DX8
  path is byte-for-byte unchanged.

**Oracle (machine, with negative control):** w3d_d3d11_smoke checks S/T -
a pure-red quad drawn under the override reads back gray(75,75,75,255) =
round(255*0.295) exactly; an identical draw after clearing the override
reads back pure red (proves the override both applies and un-applies; an
unfixed backend renders BOTH quads red and check S goes RED).

    gsOn(260,115) RGBA(75,75,75,255) == DX8-trick luminance of red - OK
    gsOff(260,165) RGBA(255,0,0,255) == pure red, override cleared - OK
    SMOKE PASS (exit 0)

In-game confirmation pending next play session (skirmish, dozer selected,
missing-prerequisite build buttons grey vs dx8). Perf: the hook adds one
boolean compare per 2D Render() call and only touches cbCombiner state on
actual grayscale draws (command bar disabled buttons) - no measurable frame
cost mechanism; fps probe rerun folded into the step-⑤ coverage sweep.

### Lead for the invisible rotor/rockets/smoke family (2026-07-27, UNVERIFIED)

Scoping pass, mechanism labeled INFERENCE until a framedump A/B confirms:
translucent particles/streaks route through SortingRendererClass
(pointgr.cpp:984 Insert_Triangles when sorting is on). At flush,
`Apply_Render_State` (sortingrenderer.cpp:368) re-applies the captured
shader/material/textures through g_renderBackend BUT re-applies WORLD/VIEW
via `DX8Wrapper::_Set_DX8_Transform` (:379-380) - the raw DX8 device only.
Under D3D11 the backend never sees those transforms, so every sorted draw
runs with whatever world/view the last opaque object left behind ->
geometry rasterizes anywhere-but-right, i.e. INVISIBLE rather than magenta.
Fits all three defects (rockets, RPG smoke, rotor blur disc = sorted
translucent). Second flush path at :630 (Set_Render_State) needs the same
audit. Next action: route the flush transforms through
g_renderBackend->Set_Transform (mind the D3DMATRIX row-vector vs Matrix4x4
transpose - RB_Get_Backend_Transform shows the convention), then framedump
A/B on a battle scene with particles.

## FIXED: sorted-translucent family - invisible particles/smoke (2026-07-27)

The scoping lead above CONFIRMED, with a second half found during
implementation. Two breaks, one per direction:

1. **Capture:** `D3D11Backend::Set_Transform` never mirrored WORLD/VIEW into
   DX8Wrapper's render_state record, so
   `SortingRendererClass::Insert_Triangles` captured stale matrices into
   every node's sorting_state (garbage sort keys AND garbage flush state).
2. **Apply:** the flush's `Apply_Render_State` re-applied WORLD/VIEW via
   `DX8Wrapper::_Set_DX8_Transform` - raw D3D8 device only; the D3D11
   backend drew every sorted node with whatever transforms the last opaque
   object left bound.

**Fix** (both directions, DX8 path byte-untouched):
- `Mirror_Transform_To_Wrapper` (D3D11Backend.h, real body in
  D3D11Backend_W3D.cpp, no-op stub for the smoke build via ZP_D3D11_W3D_TU):
  D3D11Backend::Set_Transform / Set_World_Identity / Set_View_Identity now
  keep the wrapper record true (wrapper WORLD/VIEW cases are record-only, no
  device call, no recursion).
- `RB_Mirror_World_View_Transform` (dx8wrapper.h `_Set_DX8_Transform`,
  same self-gated idiom as the texture-matrix mirror): forwards WORLD/VIEW
  device-funnel sets into the backend. Also covers W3DWater's raw sets.

**Oracle - three-way framedump A/B** (`scripts/ab_sorted_translucents.ps1`,
-stratagemShot skirmish, logic frames 1800/2700, all three legs at game
clock 00:01:27.23; the PRE-FIX leg is the negative control):

    dx8_truth      : power-plant smoke, construction dust, decals VISIBLE
    d3d11_prefix   : all of them ABSENT (the defect, reproduced on tape)
    d3d11_postfix  : all of them BACK

    crop mae vs dx8 (f2700):  construction-dust 28.10 -> 9.65
                              powerplant-smoke  19.38 -> 16.85
    whole-frame mae vs dx8:   11.89 -> 11.79 (no regression elsewhere)
    fps over f1800->f2700:    prefix 29.09 / postfix 29.12 / dx8 29.54

Dumps + PNGs: `build\win32-debug\ab_sorted_rotor\`. Comanche rotor and
rocket sprites are the same family (no Comanche in this scene); their
specific confirmation rides the GA plan's final live-eyeball step. Residual
meanlum gap (d3d11 ~72 vs dx8 ~63) is the pre-existing shroud-coverage
item, unchanged by this fix.

## Terrain "mip seams" round 1 (2026-07-27): re-diagnosed as a BLEND-PASS artifact

Three same-scene D3D11 legs against the dx8_truth framedump (stratagemShot
f2700, ab_sorted_rotor dir), sampler policy varied one lever per leg:

| leg | sampler | terrain-crop mae | whole-frame mae |
|---|---|---|---|
| postfix | blanket trilinear (old) | 14.15 | 12.15 |
| mipfix2 | DX8 filter tables mirrored | 14.38 | 12.13 |
| bias05  | tables + LODBIAS -0.5     | 15.16 | 12.87 |

What the tape shows:
- Mirroring the DX8 tables (new `TextureFilterClass::_Get_Resolved_Mip_Filter`
  -> `RenderBackendMipFilter` on the upload samplers; under the live
  TEXTURE_FILTER_BILINEAR option everything resolves to mip POINT, and
  MIP_LEVELS_1 alias textures to level-0-only) made the terrain UNIFORMLY one
  level softer - the sharp/blurry mip patchwork collapsed, confirming
  level-selection was half the story. KEPT: it is what DX8 actually applies.
- LODBIAS -0.5 (env lever `ZP_D3D11_LODBIAS`, default 0, kept for experiments)
  brought terrain to sharp level 0 - and REVEALED the real residual: dark
  RECTANGULAR cells of a different tile appearance that DX8 renders as soft
  feathered mottling. Went red on mae; default stays 0.
- **Re-diagnosis:** the rectangular patches follow the terrain's BLENDED-TILE
  cells. The defect is the terrain blend pass (AlphaTerrainTextureClass -
  "prevents seams between blended tiles") rendering hard/opaque under D3D11
  where DX8 feathers. The mip-level story was a layer on top of it.

Next action (open): compare the blend pass under D3D11 vs DX8 on a seam crop -
alpha decode of the A1R5G5B5 atlas (1-bit alpha feathered by bilinear
sampling on DX8), the second-pass blend/combiner state, and whether the
blend-edge draw reaches the backend with blending enabled at all.

## FIXED: terrain blend pass - the .pso path was live with a stubbed pixel shader (2026-07-28)

Round 1's third suspect was right, with a twist. OBSERVED (one-shot log line
`[D3D11 terrain] pso path disabled -> 2-stage FF (mirrored)` appears in the
post-fix leg and nothing terrain-ish before it): under D3D11 the terrain was
driven by **TerrainShaderPixelShader** - the modern GPU passes the chipset
gate, `terrain.pso` loads and `CreatePixelShader` succeeds on the still-live
D3D8 device - but its `SetPixelShader` is a raw device call the D3D11 backend
stubs. The backend drew ONE pass with the default Set_Shader combiner:
base atlas, UV set 0, no blend contribution at all. The "dark rectangular
cells" were blended-tile cells showing their base tile hard where DX8's
pixel shader lerps the second (blend-shape) UV set in.

**Fix** (both halves DX8-byte-untouched):
- `TerrainShaderPixelShader::init` / `TerrainShader8Stage::init` fail under
  D3D11 so the FF **TerrainShader2Stage** path drives terrain (the .pso path
  cannot work while Set_Pixel_Shader is a stub; the 8-stage cascade exceeds
  the combiner emulation).
- `RB_Mirror_Terrain_FF_Pass` (dx8wrapper.h + D3D11Backend_W3D.cpp, called
  from TerrainShader2Stage::set/reset): mirrors each pass's raw D3DTSS/D3DRS
  pokes - which the backend otherwise never sees - into the typed combiner +
  blend state. Pass 0 opaque MODULATE; pass 1 MODULATE color+alpha with
  SRCALPHA/INVSRCALPHA (the blend pass); pass 2 texture-only color with
  DESTCOLOR/ZERO (cloud/noise), 2 stages under NOISE12.

**Oracle** (ab_sorted_translucents.ps1, terrain crop (540,330,940,470) f2700
vs dx8_truth; negative control = same-HEAD pre-fix leg):

    terrain-crop mae vs dx8:  terrain_nc 14.38 (== round-1 mipfix2, defect
                              reproduced on tape) -> terrain_fix 5.60
    whole-frame mae vs dx8:   12.17 -> 6.15  (previous floor was ~11.8)
    meanlum f2700:            72.29 -> 65.17 (dx8 ~63; most of the standing
                              "shroud coverage" meanlum gap was THIS)
    smoke:                    PASS (25 checks incl. grayscale S/T; the "13" previously logged here was stale - the harness prints 25)
    fps (t_ms f1800->f2700):  nc 28.2 / fix 27.3 / fix2 27.5 - the 2-pass
                              FF path re-draws terrain once more than the
                              single-pass pso path; watch in step 7 endurance.

Crops: `build\win32-debug\ab_sorted_rotor\*terraincrop*.png` - nc shows the
hard rectangles, fix shows dx8-style feathered mottling.

Also new: `ZP_FRAMEDUMP_EVERY` / `ZP_FRAMEDUMP_FROM` / `ZP_FRAMEDUMP_TO` on
the D3D11 dumper - cadence dumps for video assembly / flicker hunts,
additive to ZP_FRAMEDUMP_FRAMES, off unless set.

## Open defect RECLASSIFIED: "black/pink flicker" = dev-harness overlays (2026-07-28)

The user saw black/pink flicker during a live -d3d11 session; first magenta
scans of headless dumps found nothing. The user's eyeball on the step-4
video (terrain_fix_f1500-3000.mp4, ~00:13) found it, and the recalibrated
scan (below) confirms ON TAPE - both artifacts are **-stratagemShot dev
overlays, not rendering bugs**:

1. **Pink**: the auto-capture screenshot toast - magenta thumbnail strip +
   "Screen Captured to 'zp_screenshot_NNN.png'" caption, firing every ~240
   frames (`updateStratagemShotCapture`, StratagemBrain.cpp:133; skirmish
   construction GameEngine.cpp:770). Thumbnails render dim magenta
   ~(139,11,143).
2. **Black**: the StratagemBrain influence heatmap - dark box with red/blue
   blobs (`debugDrawInfluence`, StratagemBrain.cpp:138, enabled by
   -stratagemShot via GameEngine.cpp:1129).

**Scanner recalibration (the miss was real):** the first magenta rule only
knew the backend's PURE fallback (255,0,255) (`r,b>200, g<60`) and could not
see the ~55%-brightness toast. Rule now reports two classes - PURE-FALLBACK
(rendering bug) vs dim-overlay (dev UI) - and is falsified both ways: fires
on all 6 toast frames of the step-4 video (~2850 px each), silent on the
745 clean frames.

**Residual open question (do NOT close until step 10):** whether the user's
original live-play sighting was these same overlays from the dev-mode
staged exe. Resolved definitively at steps 9/10 when ZeroPowerDevMode goes
OFF and the live eyeball runs clean.

## Step-5 coverage sweep (2026-07-28)

All legs on the step-4 build (staged exe = 9a67cf47..., HEAD 873e6c7):

| leg | result |
|---|---|
| fps probe (probe_fps.ps1, f300-900, 3 runs/config) | d3d11 mips_on 23.99/26.37/26.37 · mips_off 24.02/29.25/29.45 · dx8 29.49/29.49/24.20 - spreads overlap (noise floor ~5 fps; one slow-mode run hit EVERY config incl. dx8). No demonstrated mips or terrain-2nd-pass cost; verdict.txt banked in fps_probe_20260728-004723. Watch again at step 7 endurance. |
| menu/shell parity (no -stratagemShot, f300/600/900) | d3d11-vs-dx8 whole-frame MAE 0.13/0.14/0.14 - the 2D path is pixel-equal. |
| texture-format fallback census (every leg log) | ZERO "unhandled format" / "neutral white" lines; only benign shroud copy-shadow binds + the terrain pso-disabled notice. No format gaps exercised. |
| magenta/black anomaly scan (63 sweep frames + 751 video frames) | 0 PURE-FALLBACK hits anywhere. Dim-overlay hits: the 6 stratagemShot toast frames (expected, see reclassification above) and menu f600 - which fires IDENTICALLY on dx8 (41 px both) = shared shell content, not a backend artifact. |
| deferred | naval_shot.ps1 (gameplay-tier oracle, dx8-only harness, hardcoded other build dir) - not a renderer-coverage leg; revisit if water parity becomes a step-10 item. |

## Step-6 Release build + step-7 endurance (2026-07-28)

**Step 6.** `cmake --preset win32` (Ninja Multi-Config, build/win32) at d293ff0:
build exit 0, Release smoke PASS (25 checks, exit 0; earlier "13/13" was a stale count). Release A/B leg
(rel_terrain): terrain-crop mae **5.60** / whole-frame **6.14** vs dx8_truth -
the terrain fix carries to Release bit-for-bit (debug read 5.60/6.15).

**Step 7.** 9 interleaved Release runs (dx8 x3 / d3d11 mips_on x3 / mips_off
x3), each 3600 frames, two fps windows per run (W1 f300-900, W2 f2700-3600),
WS sampled every 20s. Box NOT quiet (VTOLVR live at ~1 core the whole time -
the interleave makes contamination symmetric; cap-hits are conclusive even in
a noisy box):

    dx8            W1 29.91/29.88/29.91  W2 29.88/29.84/29.66  ws~348MB flat
    d3d11 mips_on  W1 29.68/29.77/29.72  W2 29.69/29.69/26.42  ws~387MB flat
    d3d11 mips_off W1 29.79/29.81/29.56  W2 29.68/29.72/29.71  ws~387MB flat

**VERDICT: PASS.** Release d3d11 holds the cap band (>=29.56 in 11 of 12
windows) within ~0.2 fps of the same-box dx8 anchor; the single 26.42 is the
same slow-mode class that hit dx8 itself in the step-5 probe. No W1->W2
degradation trend; ws_last == ws_max in every run (no leak signal; d3d11
+40MB static footprint). The debug-build "~0.9 fps second-pass cost" does
not exist on Release, and mips on/off are indistinguishable at the cap.
Raw table: build\win32-debug\endurance7\results.txt.

## FIXED: harness runs grabbed the pointer + stole focus (2026-07-28)

Probe legs locked a Chrome-Remote-Desktop user out machine-wide: windowed
GAMEPLAY captures the cursor by default (CursorCaptureMode_Default includes
EnabledInWindowedGame, Mouse.h) -> Win32Mouse::capture() ClipCursor-pins the
pointer to the auto-launched game window, and CreateWindow's WS_VISIBLE +
SetFocus/SetForegroundWindow made every leg steal the desktop.

**Fix** (dev-harness-only; normal play byte-identical): new Core global
`TheZPUnattendedHarness` (Mouse.h/Mouse.cpp - lives in Core because vanilla
Generals' GlobalData lacks the harness fields), set by ZH WinMain when
-stratagemShot/-navalShot/-navalSandbox parsed. Gates:
- `Mouse::canCapture()` returns false -> no ClipCursor/capture ever;
- window creation strips WS_VISIBLE (WS_VISIBLE activates at CreateWindow,
  which defeated the first attempt - oracle caught it: fg 32/38 samples),
  shows SW_SHOWNOACTIVATE at HWND_BOTTOM, skips SetFocus/SetForegroundWindow.
- NOT minimized: W3DDisplay::draw() early-returns under IsIconic, so
  SW_SHOWMINNOACTIVE would kill rendering + every framedump. Focus item was
  downgraded to best-effort; the no-activate variant landed because it stayed
  trivial.

**Oracle** (GetClipCursor + GetForegroundWindow sampled 2s from a second
process, P/Invoke):
- harness leg, fixed build: 38 samples, clip_violations=0,
  fg_game_samples=0 (first attempt read fg=32/38 -> WS_VISIBLE found);
- NC normal -win launch: fg_game_samples=1 (takes foreground at launch,
  discriminates); clip NC reads 0 at the MENU by design (default mode only
  clips in-GAME) - the pre-fix in-game clip is attested by the default-mode
  code path + the user's actual lockouts during probe legs;
- rendering untouched: rel_nograb leg mae 5.60/6.14 (bit-identical), run
  entirely unfocused at HWND_BOTTOM - unfocused sim+render confirmed;
  Release smoke PASS.

## FIXED: alpha test unimplemented in the D3D11 backend (2026-07-28)

ShaderClass carries an alpha-test bit that DX8's ShaderClass::Apply programs
as D3DRS_ALPHATEST* (ref 0x60 GREATEREQUAL; inverted to 0x9f LESSEQUAL under
INVSRCALPHA source blend). The D3D11 backend had NO alpha-test path at all -
every alpha-tested cutout drew its full quad. Fix: cbCombiner gains
alphaTest{Enable,LessEqual,Ref}; FFPixel discards on the post-combiner
alpha; Set_Shader mirrors ShaderClass::Apply's exact ref/func logic.
Oracle: construction-scaffold region (760,440,1080,540) f2700 MAE vs dx8
16.42 -> 10.09; whole-frame 6.14 -> 5.69 (new floor); smoke PASS; NC =
same-HEAD pre-fix leg. Residual scaffold delta + the untouched regions are
the bypass-audit items below.

## CONFIRMED: trees frozen under D3D11 + adjacent-frame motion oracle (2026-07-28)

Spooky's finding reproduced on this box with a cloud-immune spec (the first
attempt normalized against a far-away road and cloud drift polluted the
ratio; the amplified diff image settled it - d3d11 canopies are BLACK frozen
silhouettes masking the cloud motion):

    canopy (150,45,195,90) vs adjacent ground (45,150,140,195), f1800->f1830:
    dx8  ratio 2.17  MOVING   (negative control)
    d3d11 ratio 0.91 FROZEN   (defect on tape)

Tooling: scripts/frame_oracle.py (motion/mae/magenta + selftest fixture NC).
Root cause class: W3DTreeBuffer uploads sway constants via raw
SetVertexShaderConstant(9+i) and draws with a custom vertex shader
(SetVertexShader) - all stubs under D3D11; the FF path renders the trees
statically. Fix pending (below).

## AUDIT: raw _Get_D3D_Device8() bypasses in shipped game code (2026-07-28)

Every raw device call is a silent no-op-or-invisible under D3D11 (raw draws
land on the never-presented D3D8 device; raw states never reach the backend).
Counts per file, classified; WorldBuilder/GUIEdit/tools excluded (not in
generalszh.exe). "LIVE" = exercised in a normal skirmish.

| file (sites) | what the raw calls do | live? | expected D3D11 symptom | status |
|---|---|---|---|---|
| W3DTreeBuffer (14) | sway/topple VS constants, custom tree VS/PS, stage states | LIVE | trees frozen (CONFIRMED on tape, motion oracle); topple/crush anim frozen too | P1 - fix in flight |
| W3DShaderManager (58) | .pso shader binds + DrawPrimitiveUP for road/water/cloud + screen filters | LIVE | terrain was this class (fixed 873e6c7); screen filters already have zpD3D11 branches; ROAD pso path still raw -> the user's road-join/end-cap defects are this mechanism (verify per-fix) | P1 - roads next |
| W3DProjectedShadow (5 + raw draws) | decal shadow flush: raw SetStreamSource/DrawIndexedPrimitive | LIVE (m_useShadowDecals default) | ALL unit/tree shadow decals invisible under D3D11 (brightness delta contributor) | P2 |
| W3DVolumetricShadow (7, ~300 raw states + draws) | stencil shadow volumes, raw everything | LIVE where volumes enabled | volumetric shadows invisible | P2 |
| W3DWater (37) | raw stage states, PS constants, some raw draws | LIVE on water maps (not this scene) | water surface wrong/missing effects | P2 (naval deferred anyway) |
| W3DSmudge (2 live) | heat-haze quads via raw device | LIVE with jets/fires | heat distortion missing | P3 |
| W3DSnow (4) | point-sprite snowfall raw draw | snow maps only | no snowfall | P3 |
| W3DMouse (3) | D3D hardware cursor surfaces | live play only | HW cursor path dead (GDI cursor still shows windowed) | P3 |
| W3DScene (2) | custom-pass color-write mask read/write | occasional passes | alpha-mask pass state leak | P3 - probe when hit |
| HeightMap (6) | PRE_TRANSFORM_VERTEX non-T&L path | dead (#ifdef) | none | benign |
| ww3d/W3DShroud/BaseHeightMap/W3DDisplay (4) | TestCooperativeLevel queries | live | none (queries) | benign |
| dx8vertex/indexbuffer (5) | CreateVertex/IndexBuffer on the D3D8 device | live | none - by design (D3D8 buffers are the CPU-readable source the backend uploads from) | benign |
| W3DProfilerFrameCapture (4) / dx8webbrowser (1) | dev profiler / EA browser | dev/dead | none | benign |

Open row - supply-pad emblem (zbsupplydk.tga): drawlog shows the backend
drawing it with a plain OPAQUE shader word (0x0009441b, no blend, no alpha
test) while dx8 renders it transparent; texture is fourcc DXT2
(premultiplied BC2 - transparent texels are literally black). The shader
categorization code is shared, so WHERE the states diverge is not yet
proven. Related: TBBib.tga never appears in 345k logged draws (bibs never
render under D3D11?). Both under active triage with the grass-rectangle
patches (props) and road joins.

## GA'd pre-ship-gate round (2026-07-28, second half)

### FIXED: roads (1492059) + shadow decals (010b3be)

Roads: RB_Mirror_Road_FF_Pass mirrors RoadShader2Stage pass 0; roadnoise2.pso
gated; NOISE12 forced to 1 pass under D3D11 (BLENDCURRENTALPHA +
ALPHAREPLICATE inexpressible in the combiner - roads render base+cloud, no
lightmap modulation, on NOISE12 maps only). Oracle: road_left 12.55->8.82,
road_right 14.63->9.46, road_dirt_gate 12.23->6.54, whole-frame 5.60->4.34.
Shadow decals: CPU-twin lock shims + dynamic-buffer flush; tree-base shadow
blobs restored on tape, trees_topleft 3.12->2.41. Volumetric (stencil) and
runtime SHADOW_PROJECTION gated off under D3D11 = the engine's shipped
UseShadowVolumes=No look; the stencil port needs backend stencil + colorwrite
state (RenderStateVector has neither) - THE remaining P1-visible gap.

### Emblem row NARROWED, bib row CLOSED (twin drawlogs, 2026-07-28)

New tool: ZP_DX8_DRAWLOG env - DX8 twin of the D3D11 drawlog (logs applied
shader word + stage-0 texture per draw from DX8Wrapper::Draw). One paired run
(build\win32-debug\emblem_probe\{dx8,d3d11}_draws.log) proved:
- zbsupplydk.tga draws IDENTICALLY on both backends: same 23-poly/47-vert
  mesh, same opaque word 0x0009441b (src ONE dst ZERO, depth LESSEQUAL+write,
  no alpha test), same neighboring draw sequence. State divergence is
  DISPROVEN - the black quad must come from texture-CONTENT divergence: DXT2
  uploads natively as BC2 (premultiplied RGB = black where alpha~0) while
  DX8's runtime does something else with DXT2 at load (un-premultiplying
  conversion suspected, NOT proven). Next probe: dump the uploaded texel
  content for this one texture on both backends, or extract zbsupplydk from
  TexturesZH.big and inspect its alpha plane.
- TBBib.tga: draws on NEITHER backend (0 hits in both logs) - "bibs missing
  under d3d11" is vanilla behavior on this scene, row CLOSED not-a-defect.
- Grass rectangles: RESOLVED by the alpha-test fix - region 5.81->2.99 with
  only a small speckle residual; scaffold residual similarly down 26.81->14.48
  (lattice open like dx8; remaining delta is the gated volumetric darkening).

## STEP 9 - SHIP GATE (2026-07-28)

ZeroPowerDevMode default flipped OFF (GlobalData.cpp - retail construction
speed, no dev overlays; dev builds opt back in via ZeroPowerDevMode=Yes INI).

Release build (cmake --preset win32, Release): BUILD_EXIT=0, smoke PASS (25
checks, exit 0), staged exe md5 8D7EFE7F60AFB9175F5542B6B67AA630.

Full green sweep on the retail Release exe, fresh dx8-vs-d3d11 pair
(build\win32-debug\gate9, -stratagemShot legs; harness overlays present on
BOTH sides by design - the influence heatmap + capture toast are forced by
-stratagemShot itself, not dev mode, so they cancel in the A/B and are absent
from real play):

    terrain preset mae (1280x720-pinned):  3.03   <- best yet; dev-mode
    whole-frame f2700:                     3.49      overlays were inflating
    whole-frame f1800:                     3.38      all previous numbers
    magenta scan:                          0 anomalies in 4 frames
    motion oracle f1800->f1830 canopy:     ratio 2.17 MOVING (== dx8's own
                                           NC value; control_delta 2.87)

Retail NC (plain launch, no -stratagemShot, dev OFF): backend badge ABSENT
from the top-left strip (bright-px check: only the pre-existing fps/clock
stats row remains - that row's gating predates this chain; eyeball at step
10), no heatmap, no toasts. Badge-in-harness positive control: "D3D11" text
present top-left in d3d11_gate9_f2700.

fps probe (Release, -Config Release, 2 repeats per config; run
fps_probe_20260728-134233 - the first attempt, fps_probe_20260728-114133,
self-invalidated when foreground activity stalled 3 of 4 D3D11 runs;
quiet-box rerun below is the banked result):

    d3d11_mips_on   n=2  min= 29.45  median= 29.63  max= 29.81  spread= 0.37
    d3d11_mips_off  n=2  min= 29.84  median= 29.85  max= 29.86  spread= 0.02
    dx8             n=2  min= 29.86  median= 29.86  max= 29.86  spread= 0.00
    READING: mips on [29.45-29.81] and off [29.84-29.86] are DISJOINT - a
    real difference of 0.22 fps (mips COST frames).

D3D11 is at DX8 parity (29.6-29.9 vs 29.86 on the 30 fps logic cap); the
mip-upload chain costs a measurable but negligible 0.22 fps median on this
scene.

Known-open after the gate (all ledgered above with evidence): volumetric
stencil shadows + runtime SHADOW_PROJECTION (engine's UseShadowVolumes=No
look until the backend grows stencil/colorwrite state), supply-pad emblem
(content/upload divergence, state divergence disproven), NOISE12 road second
pass (lightmap-modulation approximation), water/smudge/snow/mouse (P2-P3,
not exercised on this scene).

## STEP 10 - USER LIVE EYEBALL (2026-07-28, 16:14)

PASSED. User launched the ship-gate retail Release (md5-verified
8D7EFE7F60AFB9175F5542B6B67AA630, staged side-by-side as
generalszh_eyeball.exe - the generalszh.exe already in the Steam dir is a
different binary, md5 2176AFBA..., left untouched) and reported "looks
good!" against the checklist: buttons, rotor/rockets/smoke, terrain blends,
feathered roads, tree sway/shadows, no flicker, no CRD lockout, no retail
badge.

DECIDED (16:15, user GL): fps/clock stats row SHIPS AS-IS - original-game
behavior, not a regression; its gating predates this chain.

The step 1-10 ship-gate chain is COMPLETE. Pushed: origin/renderer-d3d11 ==
166650d (verified remote SHA == local HEAD).
