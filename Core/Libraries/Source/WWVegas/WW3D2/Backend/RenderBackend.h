/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// Backend-agnostic access point for the global IRenderBackend instance.
// Engine-side code should include this header (not IRenderBackend.h or
// DX8Backend.h directly) to use the backend.

#pragma once

#include "IRenderBackend.h"

// The active rendering backend. Set by Init_Render_Backend() and cleared by
// Shutdown_Render_Backend(); never null between those two calls.
extern IRenderBackend * g_renderBackend;

// Select which concrete backend Init_Render_Backend() constructs. Default (never
// called, or called with false) is the DX8 reference backend - the byte-identical
// default game path. Passing true selects the D3D11 backend (RENDERER_PORT.md
// step 10). Must be called BEFORE Init_Render_Backend(). Lives in Core so the
// Generals layer (which owns the -gfxBackend flag / TheGlobalData) can drive the
// choice without Core depending on GlobalData.
void Set_Use_D3D11_Backend(bool use);

// True when the constructed g_renderBackend is the D3D11 backend. Lets callers
// that must keep a byte-identical raw-D3D8-device fast path on the default
// backend (e.g. W3DShaderManager's custom terrain shaders) route their binds
// through g_renderBackend only when the D3D11 backend is the one drawing.
// False before Init_Render_Backend() / after Shutdown_Render_Backend().
bool Is_D3D11_Backend_Active();

// RAII pair of Gpu_Profile_Marker calls, for render entry points with early
// returns (the terrain Render methods). Emits `begin_label` on construction and
// `end_label` on destruction; both must be string literals. No-op unless the
// active backend profiles (see IRenderBackend::Gpu_Profile_Marker).
struct RenderBackendGpuSpan
{
	const char * m_endLabel;
	RenderBackendGpuSpan(const char * begin_label, const char * end_label)
		: m_endLabel(end_label)
	{
		if (g_renderBackend != nullptr) g_renderBackend->Gpu_Profile_Marker(begin_label);
	}
	~RenderBackendGpuSpan()
	{
		if (g_renderBackend != nullptr) g_renderBackend->Gpu_Profile_Marker(m_endLabel);
	}
};

// Create the render backend. Must be called after the render device is ready.
void Init_Render_Backend();

// Destroy the render backend (calls its Shutdown() first). Must be called
// before the render device is released. Safe to call if init never ran.
void Shutdown_Render_Backend();
