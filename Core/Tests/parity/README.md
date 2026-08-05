# w3d_parity_diff — PNG frame-diff parity oracle

A standalone console oracle for RENDERER_PORT.md **step 10** (DX8-vs-D3D11
frame-by-frame A/B). It decodes two PNG frames (or two directories of frames) and
emits a machine-readable verdict with a nonzero exit on FAIL. The capture side and
the full A/B procedure live in `tools/parity/README.md`.

> **Scope.** This validates the **diff tool**. A real DX8-vs-D3D11 backend
> comparison is **deferred to step 10**, when the D3D11 backend becomes selectable
> — nothing constructs it in the running game yet. Do not read the green self-test
> as a backend comparison; it is a comparison of the *tool* against known inputs.

## What it computes

For two same-sized images (else an immediate dimension-mismatch FAIL):

- **exact-equal?** — `maxdelta == 0`.
- **maxdelta** — max absolute per-channel delta over all pixels (0…255).
- **mae** — mean absolute error over all channel samples.
- **over / total** — count of pixels whose max-channel delta exceeds the tolerance,
  out of `width*height`; `--list N` prints the first N exceeding coordinates.
- **PASS/FAIL** — PASS iff dimensions match and `over == 0`.

Verdict line (grep-friendly):

```
PARITY <PASS|FAIL> maxdelta=N mae=X.XXXX over=K/total <label>
```

Directory mode adds a rollup line `PARITY-DIR <PASS|FAIL> (N frames)`.

**Exit codes:** `0` = PASS, `2` = FAIL, `1` = self-test failure / usage error.

## Input format

Consumes exactly what `W3DDisplay::takeScreenShot` writes: 8-bit truecolor
(PNG color type 2), single zlib IDAT, filter-0 scanlines, `zp_screenshot_NNN.png`.
The decoder is a bit broader for robustness — 8-bit grayscale/RGBA and all five
scanline filters, non-interlaced — but everything normalizes to RGB before the
diff. Palette / interlaced / non-8-bit inputs are rejected with a clear error
rather than guessed. Inflate is delegated to zlib (vcpkg `ZLIB::ZLIB` under the
`win32-vcpkg-*` presets; the bundled `cmake/zlib.cmake` is the fallback).

## Usage

```
w3d_parity_diff <refA.png> <newB.png> [options]   compare two frames
w3d_parity_diff <dirA> <dirB> [options]           compare matching *.png
w3d_parity_diff --selftest [--tmp <dir>]          run the synthetic oracle

  --tol N          per-pixel max-channel tolerance (default 0 = exact)
  --list N         list up to N pixels exceeding tolerance (default 16)
  --diff-out P     write an amplified delta-visualization PNG
  --amp N          delta amplification for --diff-out (default 8)
  --with-negcontrol  (selftest) also run the RED negative-control case
```

### Tolerance model

- **Same-backend determinism** → `--tol 0` (exact). Two DX8 captures of a
  seed/replay-locked run must be byte-identical.
- **Cross-backend (DX8 vs D3D11)** → a small epsilon (`--tol 2..4`) absorbs
  rounding in the combiner/blend shader emulation. Step 10 drives `maxdelta`/`over`
  toward zero by fixing mismatches; the tolerance is the moving pass bar, not a
  free pass.

## Build

```
tools\parity\_build_diff.bat
# or, in a vcvarsall x86 env with the vcpkg toolchain:
cmake --build build\win32-vcpkg-debug --config Debug --target w3d_parity_diff
```

Output: `build\win32-vcpkg-debug\Core\Debug\w3d_parity_diff.exe`.
CMake target: `w3d_parity_diff` (wired via `Core/Tests/CMakeLists.txt`, alongside
`d3d11_smoke`). Links only zlib — no WWVegas library, no game/backend impact.

## Self-test — the tool's own green oracle

`--selftest` round-trips image pairs through **real on-disk PNG files in the
game's exact format** (encode → decode → diff), so it exercises the real decode
path, then asserts:

| Case | Construction | Asserted verdict |
|------|--------------|------------------|
| 1 identical | gradient vs itself | PASS, `maxdelta=0`, `over=0` |
| 2 one-pixel +1 | one green channel +1 | `maxdelta=1`, `over(tol0)=1`, `over(tol1)=0` |
| 3 fully inverted | black vs white | `maxdelta=255`, `over=all` |
| 4 dim mismatch | 8×8 vs 8×9 | FAIL (dims don't match) |

```
> w3d_parity_diff --selftest
...
=== selftest: 9 checks, 0 failed ===
SELFTEST PASS      (exit 0)
```

### Negative control (falsification)

`--with-negcontrol` adds a case that feeds two **known-different** images
(black vs white) but asserts `maxdelta=0` / `over=0`. It **must go RED** — that is
the point: it proves the assertion machinery detects differences instead of
rubber-stamping. The plant is grep-visible in the source
(`grep -n "NEGATIVE CONTROL\|WRONG-expect-0" w3d_parity_diff.cpp`).

```
> w3d_parity_diff --selftest --with-negcontrol
...
  [FAIL] NC.maxdelta(WRONG-expect-0)  got=255 expected=0
=== selftest: 11 checks, 2 failed ===
SELFTEST FAIL      (exit 1)
```

Green without the flag, red with it — the tool is falsified, not just asserted.
```
