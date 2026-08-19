# Render backend migration order

How the methods on `IRenderBackend` should reach upstream, and in what order.

## Why this document exists

Upstream's `IRenderBackend` (TheSuperHackers/GeneralsGameCode PR #2613) grows by an
explicit rule, stated in the interface header itself:

> A method appears here once a caller routes through it, not in anticipation of one.

That rule is right for a single-implementation interface, and this repository does not
propose changing it. But it leaves a scheduling question unanswered: **in what order do
callers migrate, and what does each one drag in with it?** Upstream's interface currently
carries 12 methods — device and frame lifecycle. This repository's carries 56, because a
second backend has to satisfy every path the engine actually renders through. The gap is
not a disagreement about design; it is 44 methods' worth of migration that nobody has
sequenced.

This document sequences it, from measured call sites rather than from intuition.

## The central finding: the drawing core is not separable

Counting distinct files that call each method through the backend pointer, across
`Core/`, `Generals/Code/` and `GeneralsMD/Code/`:

| method | files |
|---|---|
| `Set_Shader` | 49 |
| `Set_Texture` | 48 |
| `Set_Index_Buffer` | 45 |
| `Set_Transform` | 44 |
| `Draw_Triangles` | 44 |
| `Set_Vertex_Buffer` | 43 |
| `Set_Material` | 42 |
| `Apply_Render_State_Changes` | 26 |
| `Get_Transform` | 13 |
| `Invalidate_Cached_Render_States` | 10 |
| `Clear` | 9 |
| `Set_World_Identity` | 8 |
| *(31 further methods)* | 7 or fewer |

The top seven are not seven independent migrations. Of the 43 files that issue a draw
call, **31 use all seven and 40 use at least five**. A file that renders anything needs to
bind a vertex buffer, bind an index buffer, set a texture, set a material, set a shader,
set a transform, and draw. Migrating one such caller therefore moves seven methods at
once, and there is no ordering of those seven that lets a caller compile in between.

This is worth stating plainly because a reasonable reading of the upstream rule — one
method per commit, as its caller arrives — cannot be satisfied here. The first real
drawing caller to migrate brings the whole cluster. The rule still holds; the unit is
just larger than one method.

## Proposed slices

Each slice is independently reviewable, leaves the tree building, and is ordered so that
no slice depends on a later one.

**1. Drawing core (7 methods).** `Set_Vertex_Buffer`, `Set_Index_Buffer`, `Set_Texture`,
`Set_Material`, `Set_Shader`, `Set_Transform`, `Draw_Triangles`. Migrate with one small,
self-contained caller — `pointgr.cpp` or `dynamesh.cpp` — rather than with a large one, so
the reviewable diff is the interface change and not the caller. Every later slice assumes
this one.

**2. Render-state application.** `Apply_Render_State_Changes`, `Set_World_Identity`,
`Set_View_Identity`, `Get_Transform`, `Invalidate_Cached_Render_States`. These follow the
core immediately. `dx8renderer.cpp` is the natural caller for the first two: its full
requirement is `Apply_Render_State_Changes`, `Set_World_Identity`, `Set_Alpha_Reference`,
`Set_Light_Environment` and six of the seven core methods — it binds and sets state but
never calls `Draw_Triangles` itself. Note it therefore also needs `Set_Alpha_Reference`
from slice 4, which is a small enough exception to pull forward rather than to reorder
around.

**3. Dynamic buffer staging.** `Stage_Dynamic_Vertices`, `Stage_Dynamic_Indices`,
`Set_Index_Buffer_Index_Offset`. Narrow (one to three callers) but a prerequisite for any
backend that cannot mirror DX8's raw lock semantics, so it should not be left to last.
Callers: `sortingrenderer.cpp`, `dynamesh.cpp`.

**4. Lighting and fog.** `Set_Light`, `Disable_Light`, `Set_Fog`, `Set_Alpha_Reference`.
Small, self-contained, independent of slices 3 and 5.

**5. Render targets and surfaces.** `Create_Render_Target`, `Set_Render_Target_With_Z`,
`Is_Render_To_Texture`, `Capture_Backbuffer`, `Read_Back_Buffer`,
`Set_Projection_Transform_With_Z_Bias`, `Draw_Screen_Filter_Quad`. The screen-effect and
shadow paths. `W3DShaderManager.cpp` is the heaviest single consumer in the tree.

**6. Stencil and shader constants.** `Has_Stencil`, `Set_Vertex_Shader`,
`Set_Pixel_Shader`, `Set_Vertex_Shader_Constant`, `Set_Pixel_Shader_Constant`,
`Set_Shadow_Map`, `Get_Shadow_Map`. Last because the volumetric-shadow path is the least
settled, both here and upstream.

`Gpu_Profile_Marker` is diagnostic and can accompany any slice or none.

## A note on dropping parameters

Slices 1 and 2 both touch methods whose signatures were shaped by DX8. Where a parameter
looks unused, the population to check is every caller in the engine, not only the ones
already routed through the interface — the two come apart, and the second is the one that
determines whether a parameter can go.

Worked example, from adopting upstream `77b2768d` into this repository: dropping
`uselimit` from `Set_Gamma` is safe against the routed callers, since `WW3D::Set_Gamma`
always passes `true`. It is not safe against the engine: `W3DDisplay.cpp` passes `false`
in both `Generals` (line 501) and `GeneralsMD` (line 582). Those still reach
`DX8Wrapper::Set_Gamma` statically today, so nothing is broken now — but the interface
method they would migrate to cannot express what they ask for, and the limiting would
switch on silently at migration time. That commit is therefore not carried here. Reported
upstream on PR #2613.

## Status

Adopted from PR #2613 so far: `4d2aeeb0` (fixed-width shader ids), `8acd5f0d` (widened
draw and index ranges), `bdd6d938` (removed the back-buffer accessor). Declined:
`77b2768d`, for the reason above.

The counts in this document were produced by scanning call sites through the backend
pointer across `Core/`, `Generals/Code/` and `GeneralsMD/Code/`, and are expected to
change as callers migrate. They are a snapshot for sequencing, not a maintained figure.
