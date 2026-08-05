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

// Fixed-function texture-stage combiner ubershader + fog (RENDERER_PORT.md
// steps 6/7).
//
// One parameterized pixel shader that emulates the D3D8 multitexture combiner
// (D3DTSS_COLOROP/ALPHAOP with COLORARG1/2, TEXCOORDINDEX) instead of compiling a
// permutation per stage config, then applies fixed-function fog (D3DFOGENABLE /
// D3DFOGTABLEMODE). The active per-stage config is packed into cbCombiner by the
// backend; this shader loops the stages and runs the classic fixed-function
// cascade: each stage computes color/alpha from its args (current / texture /
// diffuse / tfactor) per its op, feeding the next stage's "current". Stage 0's
// "current" seeds from the vertex diffuse (which the VS may have lit). After the
// cascade, linear/exp/exp2 fog lerps the result toward the fog color using the
// interpolated view-space depth.
//
// The integer op/arg codes below MUST match the RenderBackendTexOp /
// RenderBackendTexArg enums in IRenderBackend.h (the backend maps RB_* -> these
// 1:1). VERBATIM mirror of the runtime-compiled source in D3D11FFShaders.h.

cbuffer cbCombiner : register(b0)
{
	uint gNumStages;      // active stage count (0 => pass diffuse through)
	uint3 _pad0;
	float4 gTFactor;      // constant texture-factor color (RB_TEXARG_TFACTOR)
	uint4 gStageColor[4]; // per stage: x=colorOp y=colorArg1 z=colorArg2 w=texcoordIndex
	uint4 gStageAlpha[4]; // per stage: x=alphaOp y=alphaArg1 z=alphaArg2 w=unused
	// Screen-filter monochrome post-op (the BW filter's monochrome.nvp shader):
	// gray = dot(rgb, gMonoLum), result = lerp(current, gray*gMonoTint, gMonoFade).
	float4 gMonoLum;
	float4 gMonoTint;
	float4 gMonoFade;
	uint gMonoEnable;
	uint3 _padm;
	// DX8 alpha test (D3DRS_ALPHATEST*): discard on the post-combiner alpha.
	// gAlphaTestLE = 1 -> pass alpha <= ref (ShaderClass INVSRCALPHA case),
	// else pass alpha >= ref.
	uint gAlphaTestEnable;
	uint gAlphaTestLE;
	float gAlphaTestRef;
	uint _padA;
};

cbuffer cbFog : register(b1)
{
	float4 FogColor;   // fog color (.rgb)
	uint FogEnable;    // 0 => no fog
	uint FogMode;      // FOG_LINEAR / FOG_EXP / FOG_EXP2
	float FogStart;    // linear: full-scene start distance
	float FogEnd;      // linear: fully-fogged end distance
	float FogDensity;  // exp/exp2 density
	float3 _fogpad;
};

// Same buffer object as the VS's cbTexGen (bound at PS b2). The PS only reads
// the per-stage TexGen[s].x flag to pick the VS-generated coords over the
// explicit uv sets; the matrices are consumed by the VS.
cbuffer cbTexGen : register(b2)
{
	row_major float4x4 TexMat[4];
	uint4 TexGen[4]; // x=camera-space texgen, y=matrix enable, z/w unused
};

Texture2D gTex0 : register(t0);
Texture2D gTex1 : register(t1);
Texture2D gTex2 : register(t2);
Texture2D gTex3 : register(t3);
SamplerState gSmp0 : register(s0);
SamplerState gSmp1 : register(s1);
SamplerState gSmp2 : register(s2);
SamplerState gSmp3 : register(s3);

struct PSInput
{
	float4 pos   : SV_Position;
	float4 color : COLOR0;
	float2 uv0   : TEXCOORD0;
	float2 uv1   : TEXCOORD1;
	float  fog   : TEXCOORD2;
	float4 gen01 : TEXCOORD3; // VS-generated texcoords for stages 0/1
	float4 gen23 : TEXCOORD4; // VS-generated texcoords for stages 2/3
};

// op codes - match RenderBackendTexOp
#define OP_DISABLE    0
#define OP_SELECTARG1 1
#define OP_SELECTARG2 2
#define OP_MODULATE   3
#define OP_MODULATE2X 4
#define OP_ADD        5

// arg codes - match RenderBackendTexArg
#define ARG_CURRENT 0
#define ARG_DIFFUSE 1
#define ARG_TEXTURE 2
#define ARG_TFACTOR 3

// fog modes - match D3DFOGMODE (NONE/EXP/EXP2/LINEAR order collapsed to our set)
#define FOG_NONE   0
#define FOG_LINEAR 1
#define FOG_EXP    2
#define FOG_EXP2   3

float4 SampleStage(uint stage, float2 uv)
{
	switch (stage) {
	case 0:  return gTex0.Sample(gSmp0, uv);
	case 1:  return gTex1.Sample(gSmp1, uv);
	case 2:  return gTex2.Sample(gSmp2, uv);
	default: return gTex3.Sample(gSmp3, uv);
	}
}

float3 ResolveColorArg(uint arg, float3 current, float3 texc, float3 diffuse)
{
	if (arg == ARG_TEXTURE) return texc;
	if (arg == ARG_DIFFUSE) return diffuse;
	if (arg == ARG_TFACTOR) return gTFactor.rgb;
	return current; // ARG_CURRENT
}

float ResolveAlphaArg(uint arg, float current, float texa, float diffuse)
{
	if (arg == ARG_TEXTURE) return texa;
	if (arg == ARG_DIFFUSE) return diffuse;
	if (arg == ARG_TFACTOR) return gTFactor.a;
	return current; // ARG_CURRENT
}

float3 ApplyColorOp(uint op, float3 a1, float3 a2, float3 current)
{
	if (op == OP_SELECTARG1) return a1;
	if (op == OP_SELECTARG2) return a2;
	if (op == OP_MODULATE)   return a1 * a2;
	if (op == OP_MODULATE2X) return saturate(a1 * a2 * 2.0);
	if (op == OP_ADD)        return saturate(a1 + a2);
	return current; // OP_DISABLE
}

float ApplyAlphaOp(uint op, float a1, float a2, float current)
{
	if (op == OP_SELECTARG1) return a1;
	if (op == OP_SELECTARG2) return a2;
	if (op == OP_MODULATE)   return a1 * a2;
	if (op == OP_MODULATE2X) return saturate(a1 * a2 * 2.0);
	if (op == OP_ADD)        return saturate(a1 + a2);
	return current; // OP_DISABLE
}

// Fixed-function fog blend factor (1 => no fog, 0 => full fog color).
float FogFactor(float d)
{
	if (FogMode == FOG_EXP)  return saturate(1.0 / exp(d * FogDensity));
	if (FogMode == FOG_EXP2) return saturate(1.0 / exp((d * FogDensity) * (d * FogDensity)));
	return saturate((FogEnd - d) / (FogEnd - FogStart)); // FOG_LINEAR
}

float4 main(PSInput input) : SV_Target
{
	float3 curColor = input.color.rgb; // stage-0 CURRENT == diffuse
	float  curAlpha = input.color.a;
	float2 uvs[2] = { input.uv0, input.uv1 };

	[unroll] for (uint s = 0; s < 4; ++s) {
		if (s < gNumStages) {
			uint tci = gStageColor[s].w & 1u; // texcoord set (0 or 1)
			float2 uv = uvs[tci];
			if (TexGen[s].x != 0u) {
				// Stage runs camera-space texgen: use the VS-generated coords.
				uv = (s == 0u) ? input.gen01.xy
				   : (s == 1u) ? input.gen01.zw
				   : (s == 2u) ? input.gen23.xy
				   :             input.gen23.zw;
			}
			float4 tex = SampleStage(s, uv);

			float3 c1 = ResolveColorArg(gStageColor[s].y, curColor, tex.rgb, input.color.rgb);
			float3 c2 = ResolveColorArg(gStageColor[s].z, curColor, tex.rgb, input.color.rgb);
			float3 newColor = ApplyColorOp(gStageColor[s].x, c1, c2, curColor);

			float a1 = ResolveAlphaArg(gStageAlpha[s].y, curAlpha, tex.a, input.color.a);
			float a2 = ResolveAlphaArg(gStageAlpha[s].z, curAlpha, tex.a, input.color.a);
			float newAlpha = ApplyAlphaOp(gStageAlpha[s].x, a1, a2, curAlpha);

			curColor = newColor;
			curAlpha = newAlpha;
		}
	}

	// Monochrome post-op (mirrors monochrome.nvp: dp3 / mul / lrp on all
	// channels including alpha).
	if (gMonoEnable != 0u) {
		float g = dot(curColor, gMonoLum.rgb);
		float4 tinted = g * gMonoTint;
		float4 cur4 = lerp(float4(curColor, curAlpha), tinted, gMonoFade);
		curColor = cur4.rgb;
		curAlpha = cur4.a;
	}

	// DX8 alpha test on the post-combiner alpha (fog does not touch alpha, so
	// ordering before the fog blend matches the fixed-function pipeline).
	if (gAlphaTestEnable != 0u) {
		if (gAlphaTestLE != 0u) {
			if (curAlpha > gAlphaTestRef) discard;
		} else {
			if (curAlpha < gAlphaTestRef) discard;
		}
	}

	if (FogEnable != 0u) {
		float f = FogFactor(input.fog);
		curColor = lerp(FogColor.rgb, curColor, f);
	}

	return float4(curColor, curAlpha);
}
