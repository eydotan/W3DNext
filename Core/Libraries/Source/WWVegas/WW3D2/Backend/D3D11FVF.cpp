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

// FVF -> D3D11 input-layout translation. See D3D11FVF.h for the contract and
// the reason this file stays free of d3d8/WW3D2 headers.

#include "D3D11FVF.h"

namespace
{

// Append one input element and advance the running byte offset.
void Push_Element(
	D3D11InputLayoutDesc & out,
	const char * semantic_name,
	unsigned int semantic_index,
	DXGI_FORMAT format,
	unsigned int byte_size)
{
	D3D11_INPUT_ELEMENT_DESC & e = out.elements[out.num_elements++];
	e.SemanticName = semantic_name;
	e.SemanticIndex = semantic_index;
	e.Format = format;
	e.InputSlot = 0;
	e.AlignedByteOffset = out.stride;
	e.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	e.InstanceDataStepRate = 0;
	out.stride += byte_size;
}

// Number of floats in texture-coordinate set n, decoded from the FVF's
// D3DFVF_TEXCOORDSIZE* bits (2 bits per set, starting at bit 16). The DX8
// default (bits == 0) is a 2D coordinate.
unsigned int Texcoord_Float_Count(unsigned int fvf, unsigned int n)
{
	const unsigned int bits = (fvf >> (16 + n * 2)) & 0x3;
	switch (bits) {
	case 0: return 2; // D3DFVF_TEXTUREFORMAT2 (default)
	case 1: return 3; // D3DFVF_TEXTUREFORMAT3
	case 2: return 4; // D3DFVF_TEXTUREFORMAT4
	case 3: return 1; // D3DFVF_TEXTUREFORMAT1
	}
	return 2;
}

DXGI_FORMAT Float_Count_To_Format(unsigned int count)
{
	switch (count) {
	case 1: return DXGI_FORMAT_R32_FLOAT;
	case 2: return DXGI_FORMAT_R32G32_FLOAT;
	case 3: return DXGI_FORMAT_R32G32B32_FLOAT;
	case 4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
	}
	return DXGI_FORMAT_R32G32_FLOAT;
}

void Zero_Out(D3D11InputLayoutDesc & out)
{
	out.num_elements = 0;
	out.stride = 0;
	for (unsigned int i = 0; i < W3D_FVF_MAX_INPUT_ELEMENTS; ++i) {
		out.elements[i].SemanticName = nullptr;
		out.elements[i].SemanticIndex = 0;
		out.elements[i].Format = DXGI_FORMAT_UNKNOWN;
		out.elements[i].InputSlot = 0;
		out.elements[i].AlignedByteOffset = 0;
		out.elements[i].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		out.elements[i].InstanceDataStepRate = 0;
	}
}

} // namespace

bool FVF_To_Input_Layout(unsigned int fvf, D3D11InputLayoutDesc & out)
{
	Zero_Out(out);

	const unsigned int position = fvf & W3D_FVF_POSITION_MASK;
	if (position == 0) {
		return false; // no position - not a drawable vertex layout
	}

	// --- Position (and, for XYZBn, the blend weights that ride with it) ------
	// Byte layout mirrors FVFInfoClass (dx8fvf.cpp): position occupies 3 floats,
	// each blend value one more float. XYZRHW is a pre-transformed float4. We emit
	// a single POSITION element covering the position floats; the blend values
	// (GPU skinning) become BLENDWEIGHT (+ BLENDINDICES when LASTBETA_UBYTE4) so
	// the byte stride matches the DX8 vertex exactly and the skinning VS can read
	// weights + packed bone indices.
	if (position == W3D_FVF_XYZRHW) {
		Push_Element(out, "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 4 * 4);
	} else {
		Push_Element(out, "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 3 * 4);

		// XYZBn: n blend values follow the position. D3DFVF_XYZB1==0x006 (n=1) up
		// to XYZB5==0x00e (n=5); n == (code >> 1) - 2.
		if (position >= W3D_FVF_XYZB1 && position <= W3D_FVF_XYZB5) {
			const unsigned int nbetas = (position >> 1) - 2;

			if (fvf & W3D_FVF_LASTBETA_UBYTE4) {
				// Indexed vertex blending: the last beta's 4 bytes are a packed
				// UBYTE4 bone-index, not a float weight. So (nbetas-1) explicit
				// float weights (the implicit last weight = 1 - their sum) then a
				// 4-byte BLENDINDICES. For XYZB4|LASTBETA_UBYTE4: 3 floats + UBYTE4,
				// exactly the FVFInfoClass "3*float + DWORD" (28 bytes before NORMAL).
				const unsigned int weight_floats = nbetas - 1;
				if (weight_floats > 0) {
					Push_Element(
						out, "BLENDWEIGHT", 0,
						Float_Count_To_Format(weight_floats),
						weight_floats * 4);
				}
				Push_Element(out, "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 4);
			} else if (nbetas > 0) {
				// Non-indexed weighted blending: all n betas are float weights.
				Push_Element(
					out, "BLENDWEIGHT", 0,
					Float_Count_To_Format(nbetas),
					nbetas * 4);
			}
		}
	}

	// --- Normal --------------------------------------------------------------
	if (fvf & W3D_FVF_NORMAL) {
		Push_Element(out, "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 3 * 4);
	}

	// --- Point size (rare; keep the stride honest) ---------------------------
	if (fvf & W3D_FVF_PSIZE) {
		Push_Element(out, "PSIZE", 0, DXGI_FORMAT_R32_FLOAT, 4);
	}

	// --- Diffuse / specular colors -------------------------------------------
	// D3DCOLOR is 0xAARRGGBB, i.e. bytes B,G,R,A in memory - exactly
	// DXGI_FORMAT_B8G8R8A8_UNORM, which the shader reads back as (R,G,B,A).
	if (fvf & W3D_FVF_DIFFUSE) {
		Push_Element(out, "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 4);
	}
	if (fvf & W3D_FVF_SPECULAR) {
		Push_Element(out, "COLOR", 1, DXGI_FORMAT_B8G8R8A8_UNORM, 4);
	}

	// --- Texture coordinate sets ---------------------------------------------
	const unsigned int tex_count =
		(fvf & W3D_FVF_TEXCOUNT_MASK) >> W3D_FVF_TEXCOUNT_SHIFT;
	for (unsigned int i = 0; i < tex_count; ++i) {
		const unsigned int floats = Texcoord_Float_Count(fvf, i);
		Push_Element(out, "TEXCOORD", i, Float_Count_To_Format(floats), floats * 4);
	}

	return true;
}

unsigned int FVF_Vertex_Stride(unsigned int fvf)
{
	D3D11InputLayoutDesc desc;
	if (!FVF_To_Input_Layout(fvf, desc)) {
		return 0;
	}
	return desc.stride;
}
