# Backlog — W3DNext

Open work on the D3D11 render backend and the upstream collaboration.
Rows are added by wrap-up sweeps so an idea never dies in a transcript.

**Status vocabulary:** `open` (scoped, ready to pick up) · `discuss` (needs a decision
before it can be scoped — the row states the question) · `blocked` (waiting on
something external) · `done` (kept briefly with the commit that closed it).

**`promise` flag** = committed to publicly, so it's an obligation to a collaborator,
not just an intention.

Split out of the zpower backlog on 2026-08-19, when the renderer was separated from
the personal Zero Hour project into its own repository. Row ids are carried over
unchanged so older commit messages and notes still resolve.

---

## Renderer / D3D11

| id | status | item |
|----|--------|------|
| R1 | open | **Apply the menu black-sprite fix.** Root cause confirmed in `94fce45`: the terrain shoreline dest-alpha pass sets `D3DRS_COLORWRITEENABLE` to alpha-only via a raw `Set_DX8_Render_State` the backend never consumes (`D3D11States.cpp:196` hardcodes `COLOR_WRITE_ENABLE_ALL`), so it paints black RGB; the water plane loses its DESTALPHA/INVDESTALPHA override the same way. Diagnosis landed, fix did not. |
| R2 | open | **Port bobtista's write-time buffer capture model.** Fixes the raw-lock VB mirror gap (in-game waves) and the per-bind re-upload cost in one change; audit BUGs 4/16. Named as the top engineering item since 08-06, never scoped into steps. |
| R3 | done | **`Set_Index_Buffer` base offsets widened to `unsigned int`** — closed by adopting upstream's own commit `8acd5f0d` in `6a6f766f` (branch `upstream/adopt-2613-bucket1`), not a parallel edit. DX8 forwards narrow with an explicit cast. Still to curate onto `w3dnext-main`. |
| R4 | done | **`SurfaceClass * Get_Back_Buffer(unsigned int)` dropped** from the interface — closed by adopting upstream's `bdd6d938` in `6a6f766f`. No caller anywhere in the tree reached it through the interface. Still to curate onto `w3dnext-main`. |
| R5 | discuss | **Menu fps gap: 14–26 vs DX8's steady 30.** Believed to be the no-cache upload paths that R2 addresses — does R2 close it, or is this separate work? Decide after R2. |
| R6 | done | **Adoption curated onto `w3dnext-main`** as `5998f798` — clean cherry-pick; all three edits verified present in the public branch's `IRenderBackend.h`, `D3D11Backend.{h,cpp}` and `D3D11Backend_W3D.cpp`. |
| R7 | done | **`Set_Gamma` `uselimit` finding reported upstream** — posted to PR #2613 (comment 5346173271). His `77b2768d` drops the parameter as dead; this tree has live callers passing `false` (`W3DDisplay.cpp:501` Generals, `:582` GeneralsMD), so it is unmigrated rather than unused. Commit declined locally. |
| R8 | open | **Adopt `2bbc25f6` (lifetime split) + `c03cc182` (factory selection)** from PR #2613 — decided 2026-08-19: keep the full 56-method interface, but align structure. **Skip `eeb27db8` indefinitely** — it deletes `RenderBackend.cpp`, which here also holds `s_useD3D11Backend`, `Is_D3D11_Backend_Active()`, `RB_Log_Line` and `W3DNext_GetEnv` (the smoke target resolves that one differently from the game). **Never take `93ae331e`** — it deletes the 44 drawing methods our D3D11 backend implements. |
| R9 | done | **The public repo's smoke test links again** — fixed in `500e86fe`, pushed to `eydotan/W3DNext` main. `w3d_d3d11_smoke` was failing on `W3DNext_GetEnv` (defined in ww3d2's `RenderBackend.cpp`, which that target deliberately does not link); the definition now compiles into `D3D11Backend.cpp` under `#ifndef W3DNEXT_D3D11_W3D_TU`, the same split the file already uses for its W3D-typed virtuals. Was pre-existing, not caused by the PR #2613 adoption. |
| R11 | open | **Adopt the restored `Set_Gamma` `uselimit` from upstream.** The finding reported on PR #2613 was accepted — bobtista replied 2026-08-19 18:58Z, *"Good call - restored uselimit to IRenderBackend::Set_Gamma in the latest push"*. `77b2768d` was declined locally in `6a6f766f` for exactly this reason; upstream's successor commit no longer drops the parameter, so the divergence is closed and the newer commit can be taken. Re-check the PR head, then adopt. |
| R12 | open | **Reply on PR #2613 — the thread is awaiting us.** `tools\github-inbox-check.ps1 -Hours 8` exits 2 on it. A short acknowledgement of the restored parameter is owed; posting is public, so it needs a green light first. |
| R10 | open | **Watch for PR #2613 merging into upstream `main`.** While it is open the interface is a moving target — `77b2768d` would have broken our gamma path had we taken it. Once it merges the target stops moving and adopting more of it gets cheaper, which is the one condition that would revisit R8's "stay full" decision. Currently OPEN, 21 commits, head `77b2768d` (2026-08-18). |

## Collaboration / upstream

| id | status | item |
|----|--------|------|
| C1 | open `promise` | **Build the "every effect on one map" test map**, offered to `_irelle` in the drafted reply — a single map exercising each effect so backend regressions are visible at a glance. Offered alongside the existing frame-compare harness. |
| C2 | open `promise` | **Contribute capture/compare tooling for multi-GPU parity.** All current parity numbers are one NVIDIA machine, so they prove "matches DX8 here", not everywhere; `_irelle` flagged effects that break on AMD but not NVIDIA. Needs others capturing on AMD/Intel. |
| C3 | done | **Slicing answered** — `docs/architecture/backend-migration-order.md` on the public repo (`ed3aef96`) sequences the 44-method gap into six reviewable slices, from measured call sites. Key finding: the seven-method drawing core is not separable (31 of 43 drawing files use all seven), so the first drawing caller migrates the whole cluster — upstream's one-method-per-caller rule holds, but the unit is larger than one method. |
| C4 | open | **Wire the bgfx/DX8-dependency auditor as a real fail-on-increase ratchet.** Upstream agreed in principle but deferred until the first caller-migration slice gives it a baseline to count against. Revisit when that lands. |
| C5 | discuss | **Ask TheSuperHackers admins for a read-only bot** for the Discord channel — the only ToS-clean headless route. Raised 08-09, never decided; weigh against simply reading manually via `window-peek.ps1`. |
| C6 | open | **Seed the newly-enabled W3DNext Discussions** with a first topic (roadmap or backend-parity), so the surface isn't empty when someone follows the README there. |
| C7 | discuss | **Should R3/R4/C1/C2 become public GitHub issues on W3DNext?** They're public promises; issues would make them visible to collaborators and match the "GitHub is the channel" stance. Currently tracked only here, privately. |

## Release

| id | status | item |
|----|--------|------|
| P1 | discuss | **v0.2.0 = flipping D3D11 to default.** Criteria named in conversation (artifact fixed, menu fps parity, wave gap closed, clean runs on other machines) but never written into a milestone. R1/R2 are two of them. |
| P2 | blocked | **Maximize-window feature** (`feature/maximizable-window`, unmerged): awaiting a click-accuracy verdict, plus a stretch-vs-increment-2 call. Blocked on a user decision. |
| P3 | open | **Verify the weekly automation actually fired.** The Release CI run and the first triage digest were both due 2026-08-10; neither has been confirmed green. |
| P5 | open | **Retire the obsolete flight-recorder trap.** `C:\ZeroPowerRun\generalszh.exe` is still the 08-05 recorder build; the artifact it hunted is root-caused, so the instrumented binary and `debug/menu-black-recorder` branch can be wound down. |
| P6 | blocked | **Discord account ban appeal** filed 2026-08-14 with Trust & Safety. Two drafted replies wait on it (`memory/pending-discord-replies.md`). No action until Discord responds; do not create a second account. |
