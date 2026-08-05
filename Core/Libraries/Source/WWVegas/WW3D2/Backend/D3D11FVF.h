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

// FVF -> D3D11 input-layout translation (RENDERER_PORT.md step 2, geometry
// slice). Maps a legacy Direct3D 8 FVF code (D3DFVF_XYZ | NORMAL | DIFFUSE |
// SPECULAR | TEX1..TEX8 and the position variants) onto the array of
// D3D11_INPUT_ELEMENT_DESC + byte stride that ID3D11Device::CreateInputLayout
// wants.
//
// This translation unit deliberately depends on <d3d11.h> only - NOT on d3d8.h
// or any WW3D2 header - so it compiles both inside the ww3d2 library and inside
// the standalone d3d11_smoke test (which links neither). The FVF flag values it
// needs are stable public D3D8 constants, redefined locally with a W3D_FVF_
// prefix to keep the file d3d8-header-free.

#pragma once

#include <d3d11.h>

// ---------------------------------------------------------------------------
// Legacy FVF flag subset. Values match the D3DFVF_* constants from d3d8.h.
// ---------------------------------------------------------------------------
enum
{
	W3D_FVF_XYZ            = 0x002, // untransformed position (3 floats)
	W3D_FVF_XYZRHW        = 0x004, // pre-transformed position (4 floats)
	W3D_FVF_XYZB1         = 0x006,
	W3D_FVF_XYZB2         = 0x008,
	W3D_FVF_XYZB3         = 0x00a,
	W3D_FVF_XYZB4         = 0x00c,
	W3D_FVF_XYZB5         = 0x00e,
	W3D_FVF_POSITION_MASK = 0x00e,

	W3D_FVF_NORMAL        = 0x010,
	W3D_FVF_PSIZE         = 0x020,
	W3D_FVF_DIFFUSE       = 0x040,
	W3D_FVF_SPECULAR      = 0x080,

	W3D_FVF_TEXCOUNT_MASK  = 0xf00,
	W3D_FVF_TEXCOUNT_SHIFT = 8,

	// Reinterprets the LAST blend value of an XYZBn position as a packed UBYTE4
	// bone-index rather than a float weight - i.e. fixed-function indexed vertex
	// blending / GPU skinning. Only XYZB4 | LASTBETA_UBYTE4 occurs in this engine
	// (3 float weights + 4 packed indices); see FVFInfoClass in dx8fvf.cpp.
	W3D_FVF_LASTBETA_UBYTE4 = 0x1000,
};

// The maximum number of input elements a single FVF can generate:
// position + blendweight + blendindices + normal + psize + diffuse + specular
// + 8 texture coordinate sets. Sized generously so the missing-semantic aliases
// the backend appends (NORMAL / COLOR0 / TEXCOORD0-1 / BLENDWEIGHT / BLENDINDICES)
// never overflow the array.
enum { W3D_FVF_MAX_INPUT_ELEMENTS = 16 };

// Result of translating one FVF. elements[0..num_elements) is ready to hand to
// ID3D11Device::CreateInputLayout; stride is the byte size of one vertex (the
// per-stream stride for IASetVertexBuffers).
struct D3D11InputLayoutDesc
{
	D3D11_INPUT_ELEMENT_DESC elements[W3D_FVF_MAX_INPUT_ELEMENTS];
	unsigned int num_elements;
	unsigned int stride;
};

// Translate a legacy FVF into a D3D11 input-element array + stride.
// Returns true on success. Returns false only for an FVF with no position bits
// (the one shape that has no sensible input layout); out is left zeroed then.
//
// Element ordering and byte offsets exactly follow the DX8 vertex memory layout
// (position, [blend], normal, diffuse, specular, texcoord0..N) so the same raw
// vertex bytes the engine already produces feed straight into D3D11.
bool FVF_To_Input_Layout(unsigned int fvf, D3D11InputLayoutDesc & out);

// Convenience: just the byte stride for an FVF (0 if the FVF has no position).
unsigned int FVF_Vertex_Stride(unsigned int fvf);
