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
#include "D3D11Backend.h"

#include <cstdio>
#include <cstdlib>
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
	std::snprintf(name, sizeof(name), "W3DNEXT_%s", suffix);
	const char * value = std::getenv(name);
	if (value != nullptr) {
		return value;
	}

	// Legacy zpower-tree form, kept so existing harness scripts keep working.
	std::snprintf(name, sizeof(name), "ZP_%s", suffix);
	return std::getenv(name);
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
	FILE * f = std::fopen(path != nullptr ? path : "d3d11_backend.log", "a");
	if (f != nullptr) {
		std::fputs(line, f);
		std::fputc('\n', f);
		std::fclose(f);
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

	if (s_useD3D11Backend) {
		g_renderBackend = new D3D11Backend();
		RB_Log_Line("[RenderBackend] constructed D3D11Backend (-gfxBackend d3d11)");
	} else {
		g_renderBackend = new DX8Backend();
		RB_Log_Line("[RenderBackend] constructed DX8Backend (default path)");
	}
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
