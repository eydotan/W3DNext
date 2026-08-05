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

// Fixed-function-emulation vertex shader (RENDERER_PORT.md steps 5/6/7).
//
// Applies the world*view*projection transform, computes fixed-function per-vertex
// lighting (step 7), and forwards the resulting DIFFUSE color, up to two
// texture-coordinate sets, and a view-space fog coordinate so the combiner
// ubershader (FFPixel.hlsl) can run the multitexture cascade and fog blend. This
// is a VERBATIM mirror of the runtime-compiled source in
// Shaders/D3D11FFShaders.h - keep the two in sync.
//
// MATRIX CONVENTION: WWMath's Matrix4x4 is column-vector / OpenGL-style
// (translation in the last column) and is uploaded row-by-row into the constant
// buffer. We therefore declare the matrices row_major and multiply matrix * vector
// (mul(M, v)) so the GPU performs the same column-vector transform WWMath does.
//
// LIGHTING (step 7): when LightingEnable != 0 the per-vertex color is
//   MatEmissive + MatAmbient*GlobalAmbient
//     + sum_i saturate(dot(N, -LightDir_i)) * MatDiffuse * LightDiffuse_i
// with MatDiffuse / MatAmbient resolving to the material constant (D3DMCS_MATERIAL)
// or the vertex color (D3DMCS_COLOR1) per DiffuseSource / AmbientSource. When
// lighting is disabled the vertex diffuse passes straight through (pre-step-7
// behavior), so the existing flat/combiner asserts are unaffected.
//
// TEXCOORD leniency: a vertex whose FVF has no (or only one) texcoord set - or no
// normal - still compiles against this VS; the input layout aliases any missing
// semantic onto offset 0 (harmless, unused when lighting is off / stage inactive).

cbuffer cbPerObject : register(b0)
{
	row_major float4x4 WorldViewProj;
};

cbuffer cbLighting : register(b1)
{
	row_major float4x4 World;   // normal transform (world space)
	row_major float4x4 View;    // for the view-space fog coordinate
	float4 GlobalAmbient;       // scene ambient (D3DRS_AMBIENT), .rgb
	float4 MatDiffuse;          // material diffuse (.rgb), .a = opacity
	float4 MatAmbient;          // material ambient (.rgb)
	float4 MatEmissive;         // material emissive (.rgb)
	float4 LightDir[4];         // per-light world-space travel direction (.xyz)
	float4 LightDiffuse[4];     // per-light diffuse color (.rgb)
	uint LightingEnable;        // 0 => pass vertex diffuse through
	uint NumLights;             // active directional lights
	uint DiffuseSource;         // 0=D3DMCS_MATERIAL, 1=D3DMCS_COLOR1 (vertex)
	uint AmbientSource;         // 0=D3DMCS_MATERIAL, 1=D3DMCS_COLOR1 (vertex)
};

// GPU skinning (step 8): fixed-function indexed vertex blending. Emulates
// D3DFVF_XYZB4 | LASTBETA_UBYTE4 - 4 bone weights (last implicit = 1 - sum of the
// 3 explicit) + a packed UBYTE4 of bone-palette indices. Skinned position =
// sum_i weight_i * mul(Bones[index_i], localPos); normal likewise with the bone
// rotation part. Gated by SkinningEnable so every non-skinned draw is unchanged.
#define MAX_BONES 64
cbuffer cbSkinning : register(b2)
{
	row_major float4x4 Bones[MAX_BONES]; // bone-palette matrices (WWMath convention)
	uint SkinningEnable;                 // 0 => pass the raw position/normal through
	uint3 _skinpad;
	// W3DNext tree sway (W3DTreeBuffer / Trees.nvv): during the tree draw
	// the vertex "normal" packs (swayType, colorScale, treeBaseZ) and position
	// skews by heightAboveBase * Sway[type]. Sway[0] is the zero no-sway entry
	// (DX8's c8); Sway[1..10] mirror the per-type wave constants (c9..c18).
	float4 Sway[11];
	uint SwayEnable;
	uint3 _swaypad;
};

// Fixed-function texcoord generation (the D3D8 D3DTSS_TCI_* / texture-matrix
// slice, used by the terrain shroud/cloud passes). Per stage: TexGen[s].x = 1
// when D3DTSS_TCI_CAMERASPACEPOSITION is active (generate coords from the
// view-space vertex position), TexGen[s].y = 1 when the stage's D3DTS_TEXTUREn
// matrix is enabled (D3DTTFF_COUNT2). TexMat is the raw D3D texture matrix in
// D3D's ROW-VECTOR convention (uv = pos * M) - unlike the WWMath matrices above
// it is NOT transposed, so the multiply is mul(vector, matrix).
cbuffer cbTexGen : register(b3)
{
	row_major float4x4 TexMat[4];
	uint4 TexGen[4]; // x=camera-space texgen, y=matrix enable, z/w unused
};

struct VSInput
{
	float3 pos     : POSITION;
	float3 weights : BLENDWEIGHT;   // 3 explicit bone weights (w3 = 1 - sum)
	uint4  indices : BLENDINDICES;  // packed UBYTE4 bone-palette indices
	float3 normal  : NORMAL;
	float4 color   : COLOR0;
	float2 uv0     : TEXCOORD0;
	float2 uv1     : TEXCOORD1;
};

struct VSOutput
{
	float4 pos   : SV_Position;
	float4 color : COLOR0;
	float2 uv0   : TEXCOORD0;
	float2 uv1   : TEXCOORD1;
	float  fog   : TEXCOORD2; // view-space depth for the pixel-shader fog blend
	float4 gen01 : TEXCOORD3; // generated texcoords for stages 0 (.xy) / 1 (.zw)
	float4 gen23 : TEXCOORD4; // generated texcoords for stages 2 (.xy) / 3 (.zw)
};

VSOutput main(VSInput input)
{
	VSOutput output;

	// --- GPU skinning (step 8) ---------------------------------------------
	// Blend the local position/normal through the bone palette before the WVP.
	// When skinning is off, localPos/localNormal are the raw inputs (identical to
	// the pre-step-8 behavior, so every existing assert is unchanged).
	float4 localPos    = float4(input.pos, 1.0);
	float3 localNormal = input.normal;
	if (SkinningEnable != 0u) {
		float4 w;
		w.x = input.weights.x;
		w.y = input.weights.y;
		w.z = input.weights.z;
		w.w = 1.0 - (input.weights.x + input.weights.y + input.weights.z);
		float3 blendedPos =
			w.x * mul(Bones[input.indices.x], localPos).xyz +
			w.y * mul(Bones[input.indices.y], localPos).xyz +
			w.z * mul(Bones[input.indices.z], localPos).xyz +
			w.w * mul(Bones[input.indices.w], localPos).xyz;
		float3 blendedNormal =
			w.x * mul((float3x3)Bones[input.indices.x], input.normal) +
			w.y * mul((float3x3)Bones[input.indices.y], input.normal) +
			w.z * mul((float3x3)Bones[input.indices.z], input.normal) +
			w.w * mul((float3x3)Bones[input.indices.w], input.normal);
		localPos    = float4(blendedPos, 1.0);
		localNormal = blendedNormal;
	}

	// W3DNext tree sway (Trees.nvv: r1 = (v0.z - baseZ) * c[8+type] + v0).
	// Only ever enabled around W3DTreeBuffer's draw, where the normal is not a
	// normal - so the lighting path below never sees this draw lit.
	if (SwayEnable != 0u) {
		float h = localPos.z - input.normal.z;
		localPos.xyz += h * Sway[(uint)input.normal.x].xyz;
	}

	output.pos = mul(WorldViewProj, localPos);

	float4 diffuse = input.color;
	if (LightingEnable != 0u) {
		float3 N = normalize(mul((float3x3)World, localNormal));
		float3 mDiff = (DiffuseSource == 1u) ? input.color.rgb : MatDiffuse.rgb;
		float3 mAmb  = (AmbientSource == 1u) ? input.color.rgb : MatAmbient.rgb;
		float3 col = MatEmissive.rgb + mAmb * GlobalAmbient.rgb;
		[unroll] for (uint i = 0u; i < 4u; ++i) {
			if (i < NumLights) {
				float3 L = normalize(-LightDir[i].xyz);
				float ndl = saturate(dot(N, L));
				col += ndl * mDiff * LightDiffuse[i].rgb;
			}
		}
		float a = (DiffuseSource == 1u) ? input.color.a : MatDiffuse.a;
		diffuse = float4(col, a);
	}

	output.color = diffuse;
	output.uv0 = input.uv0;
	output.uv1 = input.uv1;

	float4 viewPos = mul(View, mul(World, localPos));
	output.fog = viewPos.z;

	// Fixed-function texgen: for each stage flagged TCI_CAMERASPACEPOSITION,
	// generate coords from the view-space position - input (x,y,z,1), the stage's
	// texture matrix applied in D3D's row-vector convention, first two components
	// taken (D3DTTFF_COUNT2). Stages without texgen leave zeros (the PS keeps
	// using the explicit uv0/uv1 sets for them).
	float4 camPos = float4(viewPos.xyz, 1.0);
	float2 g0 = float2(0.0, 0.0);
	float2 g1 = float2(0.0, 0.0);
	float2 g2 = float2(0.0, 0.0);
	float2 g3 = float2(0.0, 0.0);
	if (TexGen[0].x != 0u) g0 = (TexGen[0].y != 0u) ? mul(camPos, TexMat[0]).xy : camPos.xy;
	if (TexGen[1].x != 0u) g1 = (TexGen[1].y != 0u) ? mul(camPos, TexMat[1]).xy : camPos.xy;
	if (TexGen[2].x != 0u) g2 = (TexGen[2].y != 0u) ? mul(camPos, TexMat[2]).xy : camPos.xy;
	if (TexGen[3].x != 0u) g3 = (TexGen[3].y != 0u) ? mul(camPos, TexMat[3]).xy : camPos.xy;
	output.gen01 = float4(g0, g1);
	output.gen23 = float4(g2, g3);
	return output;
}
