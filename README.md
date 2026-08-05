# W3DNext

A **Direct3D 11 renderer backend** for the GPL-released *Command & Conquer: Generals — Zero Hour* engine, replacing the original DirectX 8 renderer.

The W3D engine (Westwood 3D / WW3D2) is a fixed-function-era renderer: FVF vertex formats, the multitexture cascade, fixed-function transform & lighting, indexed vertex blending for GPU skinning. D3D11 has none of that, so this project implements a **fixed-function-emulation layer** — HLSL shaders, constant buffers, and state objects that reproduce the DX8 pipeline's behavior behind the engine's existing `DX8Wrapper` abstraction. The game code above the wrapper is unchanged.

## Status

**Working.** The D3D11 backend plays real skirmish games at frame-rate parity with the original DX8 path (29.36 vs 29.38 FPS on the internal fps-capped benchmark scene; both backends sit at the engine's frame cap). Menu rendering is frame-matched to 99.78% byte-identical against DX8. The backend ships **off by default** — the game starts on the original DX8 renderer unless you opt in.

Run with the new backend:

```
generalszh.exe -d3d11
```

(`-dx11` is an alias; `-dx8` forces the original backend and wins if both are given.)

Known open defects and the full DX8-vs-D3D11 A/B history are documented in [`docs/architecture/d3d11-parity-log.md`](docs/architecture/d3d11-parity-log.md). The port design deep-dive — what the DX8 surface is and how each piece maps to D3D11 — is [`RENDERER_PORT.md`](RENDERER_PORT.md).

## Building

Windows, 32-bit target. You need:

- **Visual Studio 2022 or later** with the MSVC toolset and **C++ ATL** (x86) — see [`docs/runbooks/build-vs2026.md`](docs/runbooks/build-vs2026.md) for VS2026-specific notes and the ATL install gotchas
- **CMake** and **Ninja** (the generator is Ninja Multi-Config)

```
cmake --preset win32-vcpkg
cmake --build build/win32-vcpkg --config Release --target z_generals
```

The Debug config and other presets are listed in `CMakePresets.json`. As with all Generals source ports, you need a retail installation of *Zero Hour* for the game data — **no game assets are included in or distributed with this repository.**

## Lineage

- Electronic Arts released the *Generals / Zero Hour* source under GPL v3: [electronicarts/CnC_Generals_Zero_Hour](https://github.com/electronicarts/CnC_Generals_Zero_Hour)
- [TheSuperHackers/GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode) modernized it to build with current toolchains — this project builds on their tree
- **W3DNext** (this repository) adds the Direct3D 11 backend

## License

GPL v3, with Electronic Arts' additional terms under GPL section 7 — see [`LICENSE.md`](LICENSE.md), carried verbatim from EA's source release.

In particular: this project claims **no** right, title, or interest in "Command & Conquer" or any other Electronic Arts trademark, and no affiliation or association with Electronic Arts Inc. This is a modified version of the GPL-released source and is marked as such. No game assets (models, textures, audio, INI data) are included.

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md). Renderer work is reviewed against the parity discipline used so far: changes to the D3D11 backend should come with an A/B observation against the DX8 path (screenshot pair, frame-dump diff, or fps measurement) rather than an assertion that it looks right.
