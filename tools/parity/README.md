# Parity capture harness — DX8-vs-D3D11 frame-by-frame A/B (step 10)

This directory drives the game to produce comparable frame captures and hands
them to the `w3d_parity_diff` oracle (built from `Core/Tests/parity/`). Together
they implement RENDERER_PORT.md **step 10**: *"A/B against the D3D8 (or D3D9)
backend frame-by-frame; fix combiner/blend mismatches."*

> **Status (2026-07-15).** The D3D11 backend is **not yet selectable** — nothing
> in the running game constructs it, and there is no backend-select command-line
> flag. So a *real* DX8-vs-D3D11 comparison cannot run today. What is built and
> self-validated now is the tooling: the diff oracle (proven against synthetic
> pairs, see `Core/Tests/parity/README.md`) and this capture harness. When step
> 10 wires the backend, the A/B is a two-command procedure with no tooling change.

## Files

| File | Purpose |
|------|---------|
| `capture-frames.ps1` | Launch the game along a fixed frame schedule; collect its F9 PNGs into `scrshots\parity\<backend>_<stamp>\`. |
| `_build_diff.bat` | Build the `w3d_parity_diff` oracle in the pinned VS2022 x86 / vcpkg toolchain. |
| `README.md` | This file. |

## How the capture is (near-)deterministic

The game already emits F9 PNG screenshots — `W3DDisplay::takeScreenShot` writes
`zp_screenshot_NNN.png` (8-bit truecolor, single zlib IDAT) into
`%W3DNEXT_SCRSHOTS%`. The `-stratagemShot` auto-capture mode boots straight into
an AI skirmish and fires those captures on a **frame schedule driven from the
deterministic lockstep path** (`StratagemBrain`: warm-up 150 ticks, then every
240 ticks; count = `-stratagemShots`, default 16) — *not* wall-clock. So the same
map + same build captures the same frames at the same simulation ticks. That
frame-locking is what makes a backend A/B meaningful: only the *renderer* differs
between the two runs, and the frame indices line up.

### The fixed-camera hook for step 10

Two levels of determinism, cheapest first:

1. **`-stratagemShot` (used here).** Frame-scheduled captures of an AI skirmish.
   Good enough for a same-map A/B when the simulation is seed-stable; the camera
   is whatever the skirmish boot sets up.
2. **`-replay <file>` (the ideal, wire in step 10).** The engine already has a
   `-replay` command (`parseReplay`, `CommandLine.cpp`). A replay is byte-identical
   input playback → deterministic simulation **and** camera → truly exact
   frame-by-frame captures. For the authoritative parity gate, record one short
   replay with camera movement, then play it back once per backend. **This is the
   precise hook point:** extend the frame-scheduled capture (or add a per-tick
   capture) inside the replay-playback path so both backends screenshot the same
   replay ticks. No new determinism is invented — it rides the existing lockstep
   replay guarantee.

Do **not** fake determinism the engine doesn't provide: if a run is not
seed/replay-locked, expect small cross-backend deltas and use a tolerance
(`--tol`, below) rather than asserting exact equality.

## The backend-select flag (does not exist yet — step 10 TODO)

`capture-frames.ps1 -Backend d3d11` only *labels the output dir* today. To make it
real, step 10 must:

1. Construct the D3D11 backend behind a runtime switch (RENDERER_PORT.md steps 3–9).
2. Add a command-line flag to pick it. Following the repo's own flag pattern
   (`CommandLine.cpp` — e.g. `parseNavalSandbox`, `{ "-navalSandbox", ... }`), a
   natural spelling is **`-gfxBackend d3d11`** (default `dx8`). Register it in the
   `TheCommandLineParams` table and store it on `GlobalData`.
3. Pass it through here: `-ExtraArgs '-gfxBackend','d3d11'`.

Until then the D3D11 column is empty by construction — this harness does not
pretend otherwise.

## A/B procedure (step 10)

```powershell
# 1. Build the diff oracle (once):
tools\parity\_build_diff.bat

# 2. Reference capture on the current DX8 backend:
$ref = tools\parity\capture-frames.ps1 -GameDir "D:\Games\Zero Hour" -Backend dx8 |
       Select-Object -Last 1

# 3. (Step 10, once the flag exists) capture on D3D11, same map:
$new = tools\parity\capture-frames.ps1 -GameDir "D:\Games\Zero Hour" -Backend d3d11 `
         -ExtraArgs '-gfxBackend','d3d11' | Select-Object -Last 1

# 4. Diff frame-by-frame. Same-backend sanity: --tol 0 (exact). Cross-backend:
#    a small epsilon absorbs combiner/blend rounding.
$diff = "build\win32-vcpkg-debug\Core\Debug\w3d_parity_diff.exe"
& $diff $ref $new --tol 3 --diff-out scrshots\parity\delta.png
# exit 0 = PARITY-DIR PASS, 2 = FAIL (a frame exceeded tolerance).
```

**Same-backend determinism check first.** Before trusting a cross-backend verdict,
capture the DX8 backend *twice* and diff at `--tol 0`; it must be `PARITY PASS
maxdelta=0` (or the capture itself is non-deterministic and the A/B is moot). Then
run DX8-vs-D3D11 with a small tolerance and drive `maxdelta`/`over` toward zero by
fixing combiner/blend mismatches — the numeric target step 10 optimizes against.
