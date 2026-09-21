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

// Render backend global owner. Holds the single g_renderBackend pointer and
// constructs/destroys the concrete backend instance.

#include "RenderBackend.h"
#include "DX8Backend.h"
#if W3DNEXT_HAS_D3D11
#include "D3D11Backend.h"
#endif

// <Utility/stdio_adapter.h> rather than <cstdio>: VC6 puts none of the C library
// in namespace std, and its STLport <cstdio> collides with the adapter's own
// vsnprintf shim. The adapter is the tree-wide answer to both, so the calls below
// are unqualified. (The D3D11 twin of this helper in D3D11Backend.cpp keeps
// std:: - that translation unit never reaches the VC6 toolchain.)
#include <Utility/stdio_adapter.h>
#include <stdlib.h>
#include <windows.h>

IRenderBackend * g_renderBackend = nullptr;

// Backend selection, driven by the Generals-side -gfxBackend flag before the
// device-dependent inits run (see Set_Use_D3D11_Backend). Default false keeps the
// DX8 reference backend so the default game path is byte-identical.
static bool s_useD3D11Backend = false;

void Set_Use_D3D11_Backend(bool use)
{
	s_useD3D11Backend = use;
}

const char * W3DNext_GetEnv(const char * suffix)
{
	if (suffix == nullptr) {
		return nullptr;
	}

	char name[128];

	// Preferred, project-named form.
	snprintf(name, sizeof(name), "W3DNEXT_%s", suffix);
	const char * value = getenv(name);
	if (value != nullptr) {
		return value;
	}

	// Legacy zpower-tree form, kept so existing harness scripts keep working.
	snprintf(name, sizeof(name), "ZP_%s", suffix);
	return getenv(name);
}

namespace
{
// Recon-only diagnostic sink, self-contained (Core must not depend on the
// Generals debug log). Appends one line to the file named by env W3DNEXT_D3D11_LOG
// (else "d3d11_backend.log" in the process CWD) and mirrors to OutputDebugString,
// so the selected-backend line is capturable both in-game and from a smoke run.
void RB_Log_Line(const char * line)
{
	const char * path = W3DNext_GetEnv("D3D11_LOG");
	FILE * f = fopen(path != nullptr ? path : "d3d11_backend.log", "a");
	if (f != nullptr) {
		fputs(line, f);
		fputc('\n', f);
		fclose(f);
	}
	OutputDebugStringA(line);
	OutputDebugStringA("\n");
}
}

bool Is_D3D11_Backend_Active()
{
	return s_useD3D11Backend && g_renderBackend != nullptr;
}

void Init_Render_Backend()
{
	if (g_renderBackend != nullptr) {
		return;
	}

#if W3DNEXT_HAS_D3D11
	if (s_useD3D11Backend) {
		g_renderBackend = new D3D11Backend();
		RB_Log_Line("[RenderBackend] constructed D3D11Backend (-gfxBackend d3d11)");
		return;
	}
#else
	// The vc6 presets compile no D3D11 backend at all (no <unordered_map>, no
	// D3D11 SDK), so -gfxBackend d3d11 cannot be honoured there. Clear the flag
	// so Is_D3D11_Backend_Active stays honest, say so in the log, and draw with
	// DX8 rather than leaving g_renderBackend null.
	if (s_useD3D11Backend) {
		s_useD3D11Backend = false;
		RB_Log_Line("[RenderBackend] D3D11 backend not built in this configuration - using DX8Backend");
	}
#endif

	g_renderBackend = new DX8Backend();
	RB_Log_Line("[RenderBackend] constructed DX8Backend (default path)");
}

void Shutdown_Render_Backend()
{
	// Null-guarded so error-recovery paths that shut down without a matching
	// init don't dereference a null backend.
	if (g_renderBackend == nullptr) {
		return;
	}

	g_renderBackend->Shutdown();
	delete g_renderBackend;
	g_renderBackend = nullptr;
}
