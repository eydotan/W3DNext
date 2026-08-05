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

// Legacy render-state vector -> D3D11 desc translation + object cache
// (RENDERER_PORT.md step 9). This .cpp owns the <d3d11.h> dependency (the header
// stays d3d11-free); it maps the RB_* semantic enums onto D3D11_BLEND / _OP /
// _COMPARISON_FUNC / _CULL_MODE / _FILL_MODE and creates the three immutable
// state objects. Like D3D11Backend.cpp / D3D11FVF.cpp it compiles both into the
// ww3d2 library and into the standalone d3d11_smoke test.

#include "D3D11States.h"

#include <d3d11.h>

namespace
{

// --- RB_* -> D3D11 translation (all D3D ABI values confined to this file) -----

D3D11_BLEND To_D3D11_Blend_Color(RenderBackendBlendFactor f)
{
	switch (f) {
	case RB_BLEND_ZERO:        return D3D11_BLEND_ZERO;
	case RB_BLEND_ONE:         return D3D11_BLEND_ONE;
	case RB_BLEND_SRCALPHA:    return D3D11_BLEND_SRC_ALPHA;
	case RB_BLEND_INVSRCALPHA: return D3D11_BLEND_INV_SRC_ALPHA;
	case RB_BLEND_SRCCOLOR:    return D3D11_BLEND_SRC_COLOR;
	case RB_BLEND_INVSRCCOLOR: return D3D11_BLEND_INV_SRC_COLOR;
	case RB_BLEND_DESTALPHA:   return D3D11_BLEND_DEST_ALPHA;
	case RB_BLEND_INVDESTALPHA:return D3D11_BLEND_INV_DEST_ALPHA;
	case RB_BLEND_DESTCOLOR:   return D3D11_BLEND_DEST_COLOR;
	case RB_BLEND_INVDESTCOLOR:return D3D11_BLEND_INV_DEST_COLOR;
	default:                   return D3D11_BLEND_ONE;
	}
}

// The alpha channel cannot use a *_COLOR blend factor in D3D11 (the debug layer
// rejects it), so the color-based factors fold onto their alpha equivalents for
// the SrcBlendAlpha/DestBlendAlpha slots. ZERO/ONE/SRC_ALPHA/INV_SRC_ALPHA/
// DEST_ALPHA/INV_DEST_ALPHA pass through unchanged.
D3D11_BLEND To_D3D11_Blend_Alpha(RenderBackendBlendFactor f)
{
	switch (f) {
	case RB_BLEND_SRCCOLOR:    return D3D11_BLEND_SRC_ALPHA;
	case RB_BLEND_INVSRCCOLOR: return D3D11_BLEND_INV_SRC_ALPHA;
	case RB_BLEND_DESTCOLOR:   return D3D11_BLEND_DEST_ALPHA;
	case RB_BLEND_INVDESTCOLOR:return D3D11_BLEND_INV_DEST_ALPHA;
	default:                   return To_D3D11_Blend_Color(f);
	}
}

D3D11_BLEND_OP To_D3D11_Blend_Op(RenderBackendBlendOp op)
{
	switch (op) {
	case RB_BLENDOP_ADD:        return D3D11_BLEND_OP_ADD;
	case RB_BLENDOP_SUBTRACT:   return D3D11_BLEND_OP_SUBTRACT;
	case RB_BLENDOP_REVSUBTRACT:return D3D11_BLEND_OP_REV_SUBTRACT;
	case RB_BLENDOP_MIN:        return D3D11_BLEND_OP_MIN;
	case RB_BLENDOP_MAX:        return D3D11_BLEND_OP_MAX;
	default:                    return D3D11_BLEND_OP_ADD;
	}
}

D3D11_COMPARISON_FUNC To_D3D11_Cmp(RenderBackendCmpFunc f)
{
	switch (f) {
	case RB_CMP_NEVER:       return D3D11_COMPARISON_NEVER;
	case RB_CMP_LESS:        return D3D11_COMPARISON_LESS;
	case RB_CMP_EQUAL:       return D3D11_COMPARISON_EQUAL;
	case RB_CMP_LESSEQUAL:   return D3D11_COMPARISON_LESS_EQUAL;
	case RB_CMP_GREATER:     return D3D11_COMPARISON_GREATER;
	case RB_CMP_NOTEQUAL:    return D3D11_COMPARISON_NOT_EQUAL;
	case RB_CMP_GREATEREQUAL:return D3D11_COMPARISON_GREATER_EQUAL;
	case RB_CMP_ALWAYS:      return D3D11_COMPARISON_ALWAYS;
	default:                 return D3D11_COMPARISON_LESS_EQUAL;
	}
}

// FrontCounterClockwise stays FALSE (D3D11 default), so front faces are
// clockwise-wound - the D3D8 convention. Culling CW faces (D3DCULL_CW) therefore
// maps to CULL_FRONT, and CCW to CULL_BACK.
D3D11_CULL_MODE To_D3D11_Cull(RenderBackendCullMode c)
{
	switch (c) {
	case RB_CULL_NONE: return D3D11_CULL_NONE;
	case RB_CULL_CW:   return D3D11_CULL_FRONT;
	case RB_CULL_CCW:  return D3D11_CULL_BACK;
	default:           return D3D11_CULL_NONE;
	}
}

D3D11_FILL_MODE To_D3D11_Fill(RenderBackendFillMode f)
{
	return (f == RB_FILL_WIREFRAME) ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
}

} // namespace

// ----------------------------------------------------------------------------
// RenderStateVector
// ----------------------------------------------------------------------------

RenderStateVector::RenderStateVector()
	: blendEnable(false)
	, srcBlend(RB_BLEND_ONE)
	, dstBlend(RB_BLEND_ZERO)
	, blendOp(RB_BLENDOP_ADD)
	, depthEnable(true)
	, depthWrite(true)
	, depthFunc(RB_CMP_LESSEQUAL)
	// Bring-up: cull NONE, matching both the Initialize-time rasterizer and the
	// Set_Shader translation (which forces CULL_NONE until the DX8-vs-D3D11
	// winding convention is settled). The DX8 default is CULL_CW; restore that
	// here when per-shader culling is enabled, so the draw-time state flush
	// doesn't silently cull geometry that never goes through Set_Shader.
	, cullMode(RB_CULL_NONE)
	, fillMode(RB_FILL_SOLID)
{
}

unsigned int RenderStateVector::Blend_Key() const
{
	// blendEnable:1 | srcBlend:4 | dstBlend:4 | blendOp:3 (well under 32 bits).
	// When blending is OFF the src/dst/op fields are irrelevant to the created
	// object, so they are masked out - every "blend off" vector collapses onto a
	// single cached passthrough object regardless of its stale factor fields.
	if (!blendEnable) {
		return 0u;
	}
	unsigned int k = 1u;
	k |= (static_cast<unsigned int>(srcBlend) & 0xF) << 1;
	k |= (static_cast<unsigned int>(dstBlend) & 0xF) << 5;
	k |= (static_cast<unsigned int>(blendOp)  & 0x7) << 9;
	return k;
}

unsigned int RenderStateVector::Depth_Key() const
{
	// depthEnable:1 | depthWrite:1 | depthFunc:3.
	unsigned int k = depthEnable ? 1u : 0u;
	k |= (depthWrite ? 1u : 0u) << 1;
	k |= (static_cast<unsigned int>(depthFunc) & 0x7) << 2;
	return k;
}

unsigned int RenderStateVector::Raster_Key() const
{
	// cullMode:2 | fillMode:1.
	unsigned int k = static_cast<unsigned int>(cullMode) & 0x3;
	k |= (static_cast<unsigned int>(fillMode) & 0x1) << 2;
	return k;
}

// ----------------------------------------------------------------------------
// D3D11StateCache
// ----------------------------------------------------------------------------

ID3D11BlendState * D3D11StateCache::Get_Blend_State(ID3D11Device * device, const RenderStateVector & rs)
{
	if (device == nullptr) {
		return nullptr;
	}
	const unsigned int key = rs.Blend_Key();
	std::unordered_map<unsigned int, ID3D11BlendState *>::iterator it = m_blend.find(key);
	if (it != m_blend.end()) {
		return it->second; // cache hit - the whole point of step 9
	}

	D3D11_BLEND_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.AlphaToCoverageEnable = FALSE;
	desc.IndependentBlendEnable = FALSE;
	D3D11_RENDER_TARGET_BLEND_DESC & rt = desc.RenderTarget[0];
	rt.BlendEnable = rs.blendEnable ? TRUE : FALSE;
	rt.SrcBlend = To_D3D11_Blend_Color(rs.srcBlend);
	rt.DestBlend = To_D3D11_Blend_Color(rs.dstBlend);
	rt.BlendOp = To_D3D11_Blend_Op(rs.blendOp);
	rt.SrcBlendAlpha = To_D3D11_Blend_Alpha(rs.srcBlend);
	rt.DestBlendAlpha = To_D3D11_Blend_Alpha(rs.dstBlend);
	rt.BlendOpAlpha = To_D3D11_Blend_Op(rs.blendOp);
	rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

	ID3D11BlendState * state = nullptr;
	if (FAILED(device->CreateBlendState(&desc, &state))) {
		return nullptr;
	}
	m_blend[key] = state;
	return state;
}

ID3D11DepthStencilState * D3D11StateCache::Get_Depth_State(ID3D11Device * device, const RenderStateVector & rs)
{
	if (device == nullptr) {
		return nullptr;
	}
	const unsigned int key = rs.Depth_Key();
	std::unordered_map<unsigned int, ID3D11DepthStencilState *>::iterator it = m_depth.find(key);
	if (it != m_depth.end()) {
		return it->second;
	}

	D3D11_DEPTH_STENCIL_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.DepthEnable = rs.depthEnable ? TRUE : FALSE;
	desc.DepthWriteMask = rs.depthWrite ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
	desc.DepthFunc = To_D3D11_Cmp(rs.depthFunc);
	desc.StencilEnable = FALSE;

	ID3D11DepthStencilState * state = nullptr;
	if (FAILED(device->CreateDepthStencilState(&desc, &state))) {
		return nullptr;
	}
	m_depth[key] = state;
	return state;
}

ID3D11RasterizerState * D3D11StateCache::Get_Rasterizer_State(ID3D11Device * device, const RenderStateVector & rs)
{
	if (device == nullptr) {
		return nullptr;
	}
	const unsigned int key = rs.Raster_Key();
	std::unordered_map<unsigned int, ID3D11RasterizerState *>::iterator it = m_raster.find(key);
	if (it != m_raster.end()) {
		return it->second;
	}

	D3D11_RASTERIZER_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.FillMode = To_D3D11_Fill(rs.fillMode);
	desc.CullMode = To_D3D11_Cull(rs.cullMode);
	desc.FrontCounterClockwise = FALSE;
	desc.DepthClipEnable = TRUE;

	ID3D11RasterizerState * state = nullptr;
	if (FAILED(device->CreateRasterizerState(&desc, &state))) {
		return nullptr;
	}
	m_raster[key] = state;
	return state;
}

void D3D11StateCache::Release_All()
{
	for (std::unordered_map<unsigned int, ID3D11BlendState *>::iterator it = m_blend.begin(); it != m_blend.end(); ++it) {
		if (it->second != nullptr) {
			it->second->Release();
		}
	}
	m_blend.clear();
	for (std::unordered_map<unsigned int, ID3D11DepthStencilState *>::iterator it = m_depth.begin(); it != m_depth.end(); ++it) {
		if (it->second != nullptr) {
			it->second->Release();
		}
	}
	m_depth.clear();
	for (std::unordered_map<unsigned int, ID3D11RasterizerState *>::iterator it = m_raster.begin(); it != m_raster.end(); ++it) {
		if (it->second != nullptr) {
			it->second->Release();
		}
	}
	m_raster.clear();
}
