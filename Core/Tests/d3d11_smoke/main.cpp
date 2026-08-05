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

// w3d_d3d11_smoke - machine oracle for the D3D11Backend skeleton
// (RENDERER_PORT.md step 3).
//
// Creates a plain hidden Win32 window, instantiates D3D11Backend directly
// (not via g_renderBackend), initializes it on that HWND, clears to magenta
// (R=255, G=0, B=255), presents a few frames, then reads the backbuffer back
// through a staging texture and asserts the center pixel equals the clear
// color exactly.
//
// Prints "SMOKE PASS" and exits 0 on success; prints got-vs-expected (or the
// failing HRESULT) and exits 1 otherwise. Console-subsystem app so the output
// is capturable by scripts.

#include "Backend/D3D11Backend.h"

#include "vector3.h"
#include "matrix4.h"

#include <windows.h>
#include <d3d11.h>
#include <cstdio>
#include <cmath>

namespace
{

const int kWidth = 640;
const int kHeight = 480;

// Clear color fed to the backend (floats 0..1): magenta, opaque.
const Vector3 kClearColor(1.0f, 0.0f, 1.0f);
const float kClearAlpha = 1.0f;

// Expected readback bytes for the clear (magenta) pixels. The swapchain is
// DXGI_FORMAT_B8G8R8A8_UNORM, so memory order is B, G, R, A.
const unsigned char kExpectedR = 255;
const unsigned char kExpectedG = 0;
const unsigned char kExpectedB = 255;
const unsigned char kExpectedA = 255;

// Expected readback bytes for a pixel covered by the flat-shaded green triangle
// (diffuse 0xFF00FF00 -> R=0, G=255, B=0, A=255).
const unsigned char kFillR = 0;
const unsigned char kFillG = 255;
const unsigned char kFillB = 0;
const unsigned char kFillA = 255;

// XYZ | DIFFUSE vertex (FVF 0x042). Position in pixel space, diffuse as a
// D3DCOLOR (0xAARRGGBB).
struct SmokeVertex
{
	float x, y, z;
	unsigned int diffuse;
};
const unsigned int kFVF_XYZ_DIFFUSE = 0x002 /*XYZ*/ | 0x040 /*DIFFUSE*/;
const unsigned int kGreen = 0xFF00FF00u; // A=FF, R=00, G=FF, B=00

// XYZ | DIFFUSE | TEX1 vertex (FVF 0x142) for the textured combiner draws.
struct TexVertex
{
	float x, y, z;
	unsigned int diffuse; // 0xAARRGGBB
	float u, v;
};
const unsigned int kFVF_XYZ_DIFFUSE_TEX1 =
	0x002 /*XYZ*/ | 0x040 /*DIFFUSE*/ | 0x100 /*1 tex set*/;

// Quad A diffuse: opaque red. White texel * red = red.
const unsigned int kRed = 0xFFFF0000u;      // A=FF, R=FF, G=00, B=00
// Quad B diffuse: opaque (R=255,G=128,B=64). gray(128) texel * this = (128,64,32).
const unsigned int kDiffuseB = 0xFFFF8040u; // A=FF, R=FF, G=80, B=40

// Expected readback for quad A (white texel * red diffuse). A texture-only bug
// would leave it white (255,255,255); a correct modulate gives pure red.
const unsigned char kQAExpR = 255, kQAExpG = 0, kQAExpB = 0, kQAExpA = 255;

// Expected readback for quad B - the genuine two-factor product:
//   R: (128/255)*(255/255) = 0.50196 -> 128
//   G: (128/255)*(128/255) = 0.25197 -> 64
//   B: (128/255)*( 64/255) = 0.12598 -> 32
// Differs from BOTH the texture (128,128,128) and the diffuse (255,128,64), so a
// diffuse-only OR a texture-only bug both fail this single assert.
#ifdef SMOKE_NEG_CONTROL
// NEGATIVE CONTROL: claim no modulation happened (diffuse passes straight
// through). The true modulated pixel is (128,64,32), so this MUST go red.
const unsigned char kQBExpR = 255, kQBExpG = 128, kQBExpB = 64, kQBExpA = 255;
#else
const unsigned char kQBExpR = 128, kQBExpG = 64, kQBExpB = 32, kQBExpA = 255;
#endif

// --- FF lighting draw (RENDERER_PORT.md step 7) -----------------------------
// XYZ | NORMAL | DIFFUSE vertex (FVF 0x052) for the lit draws.
struct LitVertex
{
	float x, y, z;
	float nx, ny, nz;
	unsigned int diffuse; // 0xAARRGGBB
};
const unsigned int kFVF_XYZ_NORMAL_DIFFUSE =
	0x002 /*XYZ*/ | 0x010 /*NORMAL*/ | 0x040 /*DIFFUSE*/;

// Known lighting inputs. Directional light TRAVEL direction chosen so that, with a
// (0,0,1) surface normal, N . (-normalize(dir)) == 0.5 exactly - a clean,
// non-trivial N.L that a "lighting ignored / full-bright" bug cannot fake.
//   -dir = (0.8660254, 0, 0.5), |.| = 1, dot((0,0,1), .) = 0.5
const float kLightDir[3]  = { -0.8660254038f, 0.0f, -0.5f };
const float kLightDiff[3] = { 1.0f, 1.0f, 1.0f };   // white light
const float kSceneAmbient[3] = { 0.3f, 0.3f, 0.3f };// D3DRS_AMBIENT
const float kMatAmbient[3]   = { 0.5f, 0.5f, 0.5f };// material ambient (MATERIAL source)
// Lit-quad vertex diffuse = red; DIFFUSE material source is COLOR1 (the vertex
// color), so red doubles as the material diffuse in the N.L term.
const unsigned int kLitDiffuse = 0xFFFF0000u; // A=FF R=FF G=00 B=00 -> (1,0,0)
const float kLitNormal[3] = { 0.0f, 0.0f, 1.0f };

// Fog: linear, red fog color, start 0 / end 1. The fog quad is drawn at view-space
// depth z=0.5, so the fog factor f = (end - d)/(end - start) = 0.5 and the pixel is
// lerp(fogColor, quadColor, 0.5).
const float kFogColor[3] = { 1.0f, 0.0f, 0.0f }; // red
const float kFogStart = 0.0f, kFogEnd = 1.0f, kFogDepth = 0.5f;
const unsigned int kFogQuadDiffuse = 0xFF0000FFu; // blue -> (0,0,1)

// --- GPU skinning draw (RENDERER_PORT.md step 8) ----------------------------
// FVF: XYZB4 | LASTBETA_UBYTE4 | NORMAL | DIFFUSE (fixed-function indexed vertex
// blending). Byte layout mirrors DX8's FVFInfoClass exactly:
//   pos(3f=12) + 3 float weights(12) + UBYTE4 bone index(4) + normal(3f=12) + diffuse(4)
// = 44 bytes, with the packed index sitting between the weights and the normal.
struct SkinVertex
{
	float x, y, z;            // POSITION
	float w0, w1, w2;         // BLENDWEIGHT - 3 explicit weights (w3 = 1 - their sum)
	unsigned char idx[4];     // BLENDINDICES - UBYTE4: idx[0]->w0 .. idx[3]->w3
	float nx, ny, nz;         // NORMAL
	unsigned int diffuse;     // COLOR0 (0xAARRGGBB)
};
const unsigned int kFVF_SKIN =
	0x00c /*XYZB4*/ | 0x1000 /*LASTBETA_UBYTE4*/ | 0x010 /*NORMAL*/ | 0x040 /*DIFFUSE*/;
// Skinned fill color: yellow (255,255,0) - distinct from every other draw's color
// (green triangle, red/gray quads, lit ~166,38,38, blue-fog) so a sampled yellow
// pixel can ONLY be the skinned geometry.
const unsigned int kYellow = 0xFFFFFF00u; // A=FF R=FF G=FF B=00
const unsigned char kSkinR = 255, kSkinG = 255, kSkinB = 0, kSkinA = 255;
// Bone 1 is a pure +400px x-translation; bone 0 is identity. A vertex 100%-weighted
// to bone1 moves the full +400px; a 50/50 blend moves half (+200px) - the midpoint
// that proves a genuine weighted blend. The skinned quads live in the empty bottom
// strip (y ~ 445..475), clear of every other draw and sample point.
const float kBoneTx = 400.0f;
const float kBoneTy = 0.0f;

// --- Blend / depth state-object cache (RENDERER_PORT.md step 9) --------------
// Alpha-blend draw: opaque RED base, then a BLUE overlay at alpha 128/255 (~0.5)
// with SRCALPHA/INVSRCALPHA. The overlap pixel is the genuine blend product
//   src*a + dst*(1-a) = blue*0.502 + red*0.498 = (~127, 0, ~128),
// which differs from BOTH the src (0,0,255) and the dst (255,0,0), so a
// blend-didn't-happen (overwrite) OR a draw-dropped bug both fail the assert.
const unsigned int kAlphaBaseColor = 0xFFFF0000u; // opaque red base (A=FF)
const unsigned int kAlphaOverColor = 0x800000FFu; // blue, alpha=128/255 (~0.5)
// Depth-test occluders: NEAR green at z=0.2 (drawn first, writes depth), then a
// FAR blue at z=0.8 behind it. With Z-test LESSEQUAL the far quad is rejected, so
// the near green must survive at the overlap.
const unsigned int kDepthNearColor = 0xFF00FF00u; // green (near, survives)
const unsigned int kDepthFarColor  = 0xFF0000FFu; // blue  (far, rejected)
const float kDepthNearZ = 0.2f;
const float kDepthFarZ  = 0.8f;

// Float channel -> UNORM8 byte, matching the GPU's round-to-nearest conversion.
unsigned char ExpectU8(float c)
{
	if (c < 0.0f) c = 0.0f;
	if (c > 1.0f) c = 1.0f;
	int v = static_cast<int>(c * 255.0f + 0.5f);
	if (v > 255) v = 255;
	return static_cast<unsigned char>(v);
}

int Fail_HR(const char * what, HRESULT hr)
{
	std::printf("SMOKE FAIL: %s failed, HRESULT=0x%08lX\n", what, static_cast<unsigned long>(hr));
	return 1;
}

// Fetch one pixel (B8G8R8A8 memory order) from a mapped staging texture.
void Read_Pixel(
	const D3D11_MAPPED_SUBRESOURCE & mapped, int x, int y,
	unsigned char & r, unsigned char & g, unsigned char & b, unsigned char & a)
{
	const unsigned char * row = static_cast<const unsigned char *>(mapped.pData) + y * mapped.RowPitch;
	const unsigned char * px = row + x * 4; // B, G, R, A
	b = px[0];
	g = px[1];
	r = px[2];
	a = px[3];
}

} // namespace

int main()
{
	// --- Plain Win32 window (never shown) -----------------------------------
	WNDCLASSEXA wc;
	ZeroMemory(&wc, sizeof(wc));
	wc.cbSize = sizeof(wc);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = DefWindowProcA;
	wc.hInstance = GetModuleHandleA(nullptr);
	wc.lpszClassName = "W3DD3D11SmokeWindow";
	if (RegisterClassExA(&wc) == 0) {
		std::printf("SMOKE FAIL: RegisterClassExA failed, GetLastError=%lu\n", GetLastError());
		return 1;
	}

	HWND hwnd = CreateWindowExA(
		0, wc.lpszClassName, "w3d_d3d11_smoke", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, kWidth, kHeight,
		nullptr, nullptr, wc.hInstance, nullptr);
	if (hwnd == nullptr) {
		std::printf("SMOKE FAIL: CreateWindowExA failed, GetLastError=%lu\n", GetLastError());
		return 1;
	}

	// --- Backend init --------------------------------------------------------
	D3D11Backend backend;
	backend.Initialize(hwnd, kWidth, kHeight);
	if (!backend.Is_Initialized()) {
		return Fail_HR("D3D11Backend::Initialize", static_cast<HRESULT>(backend.Get_Init_Result()));
	}

	// --- Clear + present a few frames ---------------------------------------
	for (int frame = 0; frame < 3; ++frame) {
		backend.Begin_Scene();
		backend.Clear(true, true, kClearColor, kClearAlpha, 1.0f, 0);
		backend.End_Scene(true); // Present
	}

	// Final frame: clear again and read back BEFORE presenting - with
	// DXGI_SWAP_EFFECT_DISCARD the backbuffer contents are undefined after
	// Present, so the readback must happen while the clear is still live.
	backend.Begin_Scene();
	backend.Clear(true, true, kClearColor, kClearAlpha, 1.0f, 0);

	// --- Transform + geometry: draw one flat-shaded green triangle ----------
	// (RENDERER_PORT.md steps 4/5). An orthographic projection maps pixel space
	// (x right, y DOWN, z in [0,1]) straight to clip space, so the triangle's
	// vertices are given in pixels and land at a known screen location. World
	// and view are identity, so WorldViewProj == this projection; if the VS
	// ignored the constant buffer the vertices (pixel coords ~100..400) would
	// fall far outside NDC and nothing would rasterize - making the projection
	// genuinely load-bearing for the inside-pixel assert below.
	if (!backend.Is_Pipeline_Ready()) {
		std::printf("SMOKE FAIL: D3D11 pipeline (shaders/CB) not ready\n");
		return 1;
	}

	Matrix4x4 proj(true); // identity
	proj[0][0] = 2.0f / static_cast<float>(kWidth);
	proj[0][3] = -1.0f;
	proj[1][1] = -2.0f / static_cast<float>(kHeight); // flip Y (pixel y is down)
	proj[1][3] = 1.0f;
	proj[2][2] = 1.0f; // z passes through (vertices use z=0)
	proj[2][3] = 0.0f;

	backend.Set_World_Identity();
	backend.Set_View_Identity();
	backend.Set_Transform(RB_TRANSFORM_PROJECTION, proj);

	// Triangle in pixel space, deliberately in the lower-left quadrant so it
	// does NOT cover the center (320,240) - the pre-existing clear assertion on
	// the center pixel therefore still holds after the draw.
	const SmokeVertex verts[3] = {
		{ 100.0f, 400.0f, 0.0f, kGreen },
		{ 300.0f, 400.0f, 0.0f, kGreen },
		{ 200.0f, 200.0f, 0.0f, kGreen },
	};
	const unsigned short indices[3] = { 0, 1, 2 };

	if (!backend.Upload_Vertices(verts, sizeof(verts), kFVF_XYZ_DIFFUSE)) {
		std::printf("SMOKE FAIL: Upload_Vertices failed\n");
		return 1;
	}
	if (!backend.Upload_Indices16(indices, 3)) {
		std::printf("SMOKE FAIL: Upload_Indices16 failed\n");
		return 1;
	}
	backend.Draw_Triangles(0 /*start_index*/, 1 /*polygon_count*/, 0 /*min_vertex*/, 3 /*vertex_count*/);

	// --- Textured combiner draws (RENDERER_PORT.md step 6) ------------------
	// Combiner config for both quads: stage 0 color = TEXTURE * DIFFUSE
	// (MODULATE), alpha = SELECTARG1(DIFFUSE) - the DIFFUSE*TEXTURE base case the
	// game's ShaderClass drives by default. Same combiner for both quads; only the
	// texture and the diffuse color change.
	backend.Set_Texture_Stage_Count(1);
	backend.Set_Texture_Stage_ColorOp(0, RB_TEXOP_MODULATE, RB_TEXARG_TEXTURE, RB_TEXARG_DIFFUSE);
	backend.Set_Texture_Stage_AlphaOp(0, RB_TEXOP_SELECTARG1, RB_TEXARG_DIFFUSE, RB_TEXARG_DIFFUSE);
	backend.Set_Texture_Stage_TexCoordIndex(0, 0);

	const unsigned short quad_indices[6] = { 0, 1, 2, 0, 2, 3 };

	// Quad A: 2x2 all-white texture, red diffuse -> expect pure red. Upper-left
	// region x[40..240] y[40..180], clear of every other sample point.
	{
		unsigned char white_px[2 * 2 * 4];
		for (int i = 0; i < 2 * 2; ++i) {
			white_px[i * 4 + 0] = 255; // R
			white_px[i * 4 + 1] = 255; // G
			white_px[i * 4 + 2] = 255; // B
			white_px[i * 4 + 3] = 255; // A
		}
		if (!backend.Upload_Texture_RGBA(0, 2, 2, white_px)) {
			std::printf("SMOKE FAIL: Upload_Texture_RGBA(white) failed\n");
			return 1;
		}
		const TexVertex qa[4] = {
			{  40.0f,  40.0f, 0.0f, kRed, 0.0f, 0.0f },
			{ 240.0f,  40.0f, 0.0f, kRed, 1.0f, 0.0f },
			{ 240.0f, 180.0f, 0.0f, kRed, 1.0f, 1.0f },
			{  40.0f, 180.0f, 0.0f, kRed, 0.0f, 1.0f },
		};
		if (!backend.Upload_Vertices(qa, sizeof(qa), kFVF_XYZ_DIFFUSE_TEX1) ||
			!backend.Upload_Indices16(quad_indices, 6)) {
			std::printf("SMOKE FAIL: quad A upload failed\n");
			return 1;
		}
		backend.Draw_Triangles(0 /*start_index*/, 2 /*polygon_count*/, 0, 4);
	}

	// Quad B: 2x2 all-gray(128) texture, diffuse (255,128,64) -> expect the
	// genuine product (128,64,32). Lower-right region x[400..600] y[300..440].
	{
		unsigned char gray_px[2 * 2 * 4];
		for (int i = 0; i < 2 * 2; ++i) {
			gray_px[i * 4 + 0] = 128; // R
			gray_px[i * 4 + 1] = 128; // G
			gray_px[i * 4 + 2] = 128; // B
			gray_px[i * 4 + 3] = 255; // A
		}
		if (!backend.Upload_Texture_RGBA(0, 2, 2, gray_px)) {
			std::printf("SMOKE FAIL: Upload_Texture_RGBA(gray) failed\n");
			return 1;
		}
		const TexVertex qb[4] = {
			{ 400.0f, 300.0f, 0.0f, kDiffuseB, 0.0f, 0.0f },
			{ 600.0f, 300.0f, 0.0f, kDiffuseB, 1.0f, 0.0f },
			{ 600.0f, 440.0f, 0.0f, kDiffuseB, 1.0f, 1.0f },
			{ 400.0f, 440.0f, 0.0f, kDiffuseB, 0.0f, 1.0f },
		};
		if (!backend.Upload_Vertices(qb, sizeof(qb), kFVF_XYZ_DIFFUSE_TEX1) ||
			!backend.Upload_Indices16(quad_indices, 6)) {
			std::printf("SMOKE FAIL: quad B upload failed\n");
			return 1;
		}
		backend.Draw_Triangles(0 /*start_index*/, 2 /*polygon_count*/, 0, 4);
	}

	// Quads GS/GN: the 2D grayscale override (disabled command-bar buttons).
	// Same combiner as quads A/B. A pure-red texture drawn with white diffuse
	// through Set_Grayscale_Override(true) must come out as its DX8-trick
	// luminance, gray(75) = 255*0.295; the second quad, drawn after the
	// override is cleared, must come out pure red again - proving the override
	// both applies and un-applies (the sticky-state negative control).
	{
		unsigned char red_px[2 * 2 * 4];
		for (int i = 0; i < 2 * 2; ++i) {
			red_px[i * 4 + 0] = 255; // R
			red_px[i * 4 + 1] = 0;   // G
			red_px[i * 4 + 2] = 0;   // B
			red_px[i * 4 + 3] = 255; // A
		}
		if (!backend.Upload_Texture_RGBA(0, 2, 2, red_px)) {
			std::printf("SMOKE FAIL: Upload_Texture_RGBA(gs red) failed\n");
			return 1;
		}
		const unsigned int kGSWhite = 0xFFFFFFFFu;
		const TexVertex qgs[4] = {
			{ 245.0f,  95.0f, 0.0f, kGSWhite, 0.0f, 0.0f },
			{ 275.0f,  95.0f, 0.0f, kGSWhite, 1.0f, 0.0f },
			{ 275.0f, 135.0f, 0.0f, kGSWhite, 1.0f, 1.0f },
			{ 245.0f, 135.0f, 0.0f, kGSWhite, 0.0f, 1.0f },
		};
		if (!backend.Upload_Vertices(qgs, sizeof(qgs), kFVF_XYZ_DIFFUSE_TEX1) ||
			!backend.Upload_Indices16(quad_indices, 6)) {
			std::printf("SMOKE FAIL: quad GS upload failed\n");
			return 1;
		}
		backend.Set_Grayscale_Override(true);
		backend.Draw_Triangles(0 /*start_index*/, 2 /*polygon_count*/, 0, 4);
		backend.Set_Grayscale_Override(false);

		const TexVertex qgn[4] = {
			{ 245.0f, 145.0f, 0.0f, kGSWhite, 0.0f, 0.0f },
			{ 275.0f, 145.0f, 0.0f, kGSWhite, 1.0f, 0.0f },
			{ 275.0f, 185.0f, 0.0f, kGSWhite, 1.0f, 1.0f },
			{ 245.0f, 185.0f, 0.0f, kGSWhite, 0.0f, 1.0f },
		};
		if (!backend.Upload_Vertices(qgn, sizeof(qgn), kFVF_XYZ_DIFFUSE_TEX1) ||
			!backend.Upload_Indices16(quad_indices, 6)) {
			std::printf("SMOKE FAIL: quad GN upload failed\n");
			return 1;
		}
		backend.Draw_Triangles(0 /*start_index*/, 2 /*polygon_count*/, 0, 4);
	}

	// Quads HX/HY: HALF-PIXEL rasterization-convention pins. The backend nudges
	// every draw +0.5px right/down (D3D8 viewport convention, see
	// D3D11Backend::Update_Constant_Buffer). Each quad maps an 8x8 two-tone
	// texture 1:1 texel-per-pixel with a LINEAR sampler; the sampled interior
	// pixel sits exactly ON the color boundary WITH the nudge (-> 50/50 blend
	// ~(128,0,128)) and half a texel PAST it without any nudge or with the
	// wrong sign (-> pure blue (0,0,255) both ways). So this assert goes red if
	// the half-pixel offset is removed OR its sign flips - the inherent
	// negative control. HX pins the X axis (vertical red|blue split), HY pins
	// the Y axis (horizontal split). Region x[440..460] y[8..16], clear of
	// every other draw and sample point.
	{
		unsigned char split_px[8 * 8 * 4];
		// HX: columns 0-3 red, 4-7 blue.
		for (int y = 0; y < 8; ++y) {
			for (int x = 0; x < 8; ++x) {
				unsigned char * p = &split_px[(y * 8 + x) * 4];
				p[0] = (x < 4) ? 255 : 0; // R
				p[1] = 0;                 // G
				p[2] = (x < 4) ? 0 : 255; // B
				p[3] = 255;               // A
			}
		}
		if (!backend.Upload_Texture_RGBA(0, 8, 8, split_px, false /*wrap*/, true /*linear*/)) {
			std::printf("SMOKE FAIL: Upload_Texture_RGBA(HX split) failed\n");
			return 1;
		}
		const unsigned int kWhite = 0xFFFFFFFFu;
		const TexVertex qhx[4] = {
			{ 440.0f,  8.0f, 0.0f, kWhite, 0.0f, 0.0f },
			{ 448.0f,  8.0f, 0.0f, kWhite, 1.0f, 0.0f },
			{ 448.0f, 16.0f, 0.0f, kWhite, 1.0f, 1.0f },
			{ 440.0f, 16.0f, 0.0f, kWhite, 0.0f, 1.0f },
		};
		if (!backend.Upload_Vertices(qhx, sizeof(qhx), kFVF_XYZ_DIFFUSE_TEX1) ||
			!backend.Upload_Indices16(quad_indices, 6)) {
			std::printf("SMOKE FAIL: quad HX upload failed\n");
			return 1;
		}
		backend.Draw_Triangles(0, 2, 0, 4);

		// HY: rows 0-3 red, 4-7 blue.
		for (int y = 0; y < 8; ++y) {
			for (int x = 0; x < 8; ++x) {
				unsigned char * p = &split_px[(y * 8 + x) * 4];
				p[0] = (y < 4) ? 255 : 0; // R
				p[1] = 0;                 // G
				p[2] = (y < 4) ? 0 : 255; // B
				p[3] = 255;               // A
			}
		}
		if (!backend.Upload_Texture_RGBA(0, 8, 8, split_px, false /*wrap*/, true /*linear*/)) {
			std::printf("SMOKE FAIL: Upload_Texture_RGBA(HY split) failed\n");
			return 1;
		}
		const TexVertex qhy[4] = {
			{ 452.0f,  8.0f, 0.0f, kWhite, 0.0f, 0.0f },
			{ 460.0f,  8.0f, 0.0f, kWhite, 1.0f, 0.0f },
			{ 460.0f, 16.0f, 0.0f, kWhite, 1.0f, 1.0f },
			{ 452.0f, 16.0f, 0.0f, kWhite, 0.0f, 1.0f },
		};
		if (!backend.Upload_Vertices(qhy, sizeof(qhy), kFVF_XYZ_DIFFUSE_TEX1) ||
			!backend.Upload_Indices16(quad_indices, 6)) {
			std::printf("SMOKE FAIL: quad HY upload failed\n");
			return 1;
		}
		backend.Draw_Triangles(0, 2, 0, 4);
	}

	// Quad C: native BC1 (DXT1) upload. One hand-crafted 4x4 DXT1 block:
	// color0 = 0xF800 (R5=31 -> pure red), color1 = 0x0000, color0 > color1 so the
	// block is 4-color opaque mode, and all 16 2-bit indices are 0 -> every texel
	// decodes to color0 = (255,0,0,255). White diffuse so MODULATE passes the
	// texture through - the pixel measures the BC decode alone. A wrong-pitch /
	// wrong-format / no-upload bug leaves the stage empty (sampling black) or the
	// previous gray texture (128,128,128); both are far outside tolerance.
	// Region x[560..620] y[50..150], clear of every other draw and sample point.
	{
		const unsigned char dxt1_red_block[8] = {
			0x00, 0xF8, // color0 = 0xF800 little-endian (pure red in 565)
			0x00, 0x00, // color1 = 0x0000 (black; < color0 -> opaque 4-color mode)
			0x00, 0x00, 0x00, 0x00, // 16 x 2-bit indices, all 0 -> color0
		};
		if (!backend.Upload_Texture_BC(0, 4, 4, D3D11Backend::RB_BC1, dxt1_red_block, 0)) {
			std::printf("SMOKE FAIL: Upload_Texture_BC(DXT1 red) failed\n");
			return 1;
		}
		const unsigned int kWhite = 0xFFFFFFFFu;
		const TexVertex qc[4] = {
			{ 560.0f,  50.0f, 0.0f, kWhite, 0.0f, 0.0f },
			{ 620.0f,  50.0f, 0.0f, kWhite, 1.0f, 0.0f },
			{ 620.0f, 150.0f, 0.0f, kWhite, 1.0f, 1.0f },
			{ 560.0f, 150.0f, 0.0f, kWhite, 0.0f, 1.0f },
		};
		if (!backend.Upload_Vertices(qc, sizeof(qc), kFVF_XYZ_DIFFUSE_TEX1) ||
			!backend.Upload_Indices16(quad_indices, 6)) {
			std::printf("SMOKE FAIL: quad C (BC1) upload failed\n");
			return 1;
		}
		backend.Draw_Triangles(0 /*start_index*/, 2 /*polygon_count*/, 0, 4);
	}

	// Quad T: fixed-function TEXGEN (camera-space position + texture matrix).
	// 2x2 texture: texel(0,0) BLUE, the other three RED. The vertices carry
	// deliberately-wrong explicit UVs (0.9,0.9) -> texel(1,1) RED under the
	// point/clamp sampler. Texgen is enabled on stage 0 with a matrix mapping the
	// quad's camera-space (== pixel, view/world identity) footprint x[20..90]
	// y[300..420] onto uv [0..1]: u=(x-20)/70, v=(y-300)/120, in D3D's ROW-VECTOR
	// convention (translation in the 4th row). The sample point (40,330) maps to
	// uv(0.29,0.25) -> texel(0,0) BLUE. Inherent negative control: if texgen is
	// ignored (explicit UVs win) OR the matrix is ignored (raw camera-space xy
	// clamps to 1,1), the pixel reads RED - either failure mode is distinct.
	{
		unsigned char tg_px[2 * 2 * 4];
		for (int i = 0; i < 2 * 2; ++i) {
			tg_px[i * 4 + 0] = 255; // R
			tg_px[i * 4 + 1] = 0;   // G
			tg_px[i * 4 + 2] = 0;   // B
			tg_px[i * 4 + 3] = 255; // A
		}
		tg_px[0] = 0;   // texel(0,0): R=0
		tg_px[2] = 255; //             B=255 -> BLUE
		if (!backend.Upload_Texture_RGBA(0, 2, 2, tg_px)) {
			std::printf("SMOKE FAIL: Upload_Texture_RGBA(texgen) failed\n");
			return 1;
		}
		const float texgen_mat[16] = {
			1.0f / 70.0f,   0.0f,           0.0f, 0.0f,
			0.0f,           1.0f / 120.0f,  0.0f, 0.0f,
			0.0f,           0.0f,           1.0f, 0.0f,
			-20.0f / 70.0f, -300.0f / 120.0f, 0.0f, 1.0f,
		};
		backend.Set_Texture_Stage_Texgen_CameraSpace(0, true);
		backend.Set_Texture_Transform_Enable(0, true);
		backend.Set_Texture_Transform_Matrix(0, texgen_mat);
		const unsigned int kWhite = 0xFFFFFFFFu;
		const TexVertex qt[4] = {
			{ 20.0f, 300.0f, 0.0f, kWhite, 0.9f, 0.9f },
			{ 90.0f, 300.0f, 0.0f, kWhite, 0.9f, 0.9f },
			{ 90.0f, 420.0f, 0.0f, kWhite, 0.9f, 0.9f },
			{ 20.0f, 420.0f, 0.0f, kWhite, 0.9f, 0.9f },
		};
		if (!backend.Upload_Vertices(qt, sizeof(qt), kFVF_XYZ_DIFFUSE_TEX1) ||
			!backend.Upload_Indices16(quad_indices, 6)) {
			std::printf("SMOKE FAIL: quad T (texgen) upload failed\n");
			return 1;
		}
		backend.Draw_Triangles(0 /*start_index*/, 2 /*polygon_count*/, 0, 4);
		// Restore the texgen-off default so every later draw is unaffected.
		backend.Set_Texture_Stage_Texgen_CameraSpace(0, false);
		backend.Set_Texture_Transform_Enable(0, false);
	}

	// --- FF lighting draws (RENDERER_PORT.md step 7) ------------------------
	// No texture stage: the combiner passes the VS-computed (lit or unlit) diffuse
	// straight through, so these asserts measure the lighting equation alone.
	backend.Set_Texture_Stage_Count(0);

	// Lit quad: lighting ENABLED, one directional light, known ambient + material.
	// Expected pixel = the FF lighting equation evaluated on the CPU below. Top-
	// center region x[280..380] y[60..160], clear of every other sample point.
	backend.Set_Ambient(Vector3(kSceneAmbient[0], kSceneAmbient[1], kSceneAmbient[2]));
	backend.Set_Material_Params(
		Vector3(0.0f, 0.0f, 0.0f),                                  // diffuse (unused: COLOR1 source)
		Vector3(kMatAmbient[0], kMatAmbient[1], kMatAmbient[2]),    // ambient
		Vector3(0.0f, 0.0f, 0.0f),                                  // emissive
		1.0f,                                                       // opacity
		D3D11Backend::RB_MATSRC_VERTEX,                             // diffuse from COLOR1
		D3D11Backend::RB_MATSRC_MATERIAL);                          // ambient from material
	backend.Set_Light_Directional(0,
		Vector3(kLightDir[0], kLightDir[1], kLightDir[2]),
		Vector3(kLightDiff[0], kLightDiff[1], kLightDiff[2]));
	backend.Set_Lighting_Enable(true);
	{
		const LitVertex lit[4] = {
			{ 280.0f,  60.0f, 0.0f, kLitNormal[0], kLitNormal[1], kLitNormal[2], kLitDiffuse },
			{ 380.0f,  60.0f, 0.0f, kLitNormal[0], kLitNormal[1], kLitNormal[2], kLitDiffuse },
			{ 380.0f, 160.0f, 0.0f, kLitNormal[0], kLitNormal[1], kLitNormal[2], kLitDiffuse },
			{ 280.0f, 160.0f, 0.0f, kLitNormal[0], kLitNormal[1], kLitNormal[2], kLitDiffuse },
		};
		if (!backend.Upload_Vertices(lit, sizeof(lit), kFVF_XYZ_NORMAL_DIFFUSE) ||
			!backend.Upload_Indices16(quad_indices, 6)) {
			std::printf("SMOKE FAIL: lit quad upload failed\n");
			return 1;
		}
		backend.Draw_Triangles(0 /*start_index*/, 2 /*polygon_count*/, 0, 4);
	}

	// Lighting-DISABLED quad, same red diffuse: proves the enable gate passes the
	// vertex diffuse through unchanged (guards against always-on lighting). Top-
	// right region x[420..560] y[180..260].
	backend.Set_Lighting_Enable(false);
	{
		const LitVertex unlit[4] = {
			{ 420.0f, 180.0f, 0.0f, kLitNormal[0], kLitNormal[1], kLitNormal[2], kLitDiffuse },
			{ 560.0f, 180.0f, 0.0f, kLitNormal[0], kLitNormal[1], kLitNormal[2], kLitDiffuse },
			{ 560.0f, 260.0f, 0.0f, kLitNormal[0], kLitNormal[1], kLitNormal[2], kLitDiffuse },
			{ 420.0f, 260.0f, 0.0f, kLitNormal[0], kLitNormal[1], kLitNormal[2], kLitDiffuse },
		};
		if (!backend.Upload_Vertices(unlit, sizeof(unlit), kFVF_XYZ_NORMAL_DIFFUSE) ||
			!backend.Upload_Indices16(quad_indices, 6)) {
			std::printf("SMOKE FAIL: unlit quad upload failed\n");
			return 1;
		}
		backend.Draw_Triangles(0 /*start_index*/, 2 /*polygon_count*/, 0, 4);
	}

	// Fog quad: lighting off, blue diffuse, linear fog toward red, drawn at depth
	// z=0.5 so f=0.5 -> the pixel is pulled halfway to the fog color. Mid region
	// x[290..380] y[300..400].
	backend.Set_Fog(true, Vector3(kFogColor[0], kFogColor[1], kFogColor[2]), kFogStart, kFogEnd);
	{
		const LitVertex fogq[4] = {
			{ 290.0f, 300.0f, kFogDepth, 0.0f, 0.0f, 1.0f, kFogQuadDiffuse },
			{ 380.0f, 300.0f, kFogDepth, 0.0f, 0.0f, 1.0f, kFogQuadDiffuse },
			{ 380.0f, 400.0f, kFogDepth, 0.0f, 0.0f, 1.0f, kFogQuadDiffuse },
			{ 290.0f, 400.0f, kFogDepth, 0.0f, 0.0f, 1.0f, kFogQuadDiffuse },
		};
		if (!backend.Upload_Vertices(fogq, sizeof(fogq), kFVF_XYZ_NORMAL_DIFFUSE) ||
			!backend.Upload_Indices16(quad_indices, 6)) {
			std::printf("SMOKE FAIL: fog quad upload failed\n");
			return 1;
		}
		backend.Draw_Triangles(0 /*start_index*/, 2 /*polygon_count*/, 0, 4);
	}
	backend.Set_Fog(false, Vector3(0.0f, 0.0f, 0.0f), 0.0f, 1.0f);

	// --- GPU skinning draws (RENDERER_PORT.md step 8) -----------------------
	// Lighting + fog OFF, no texture stage: the combiner passes the vertex diffuse
	// (yellow) straight through, so these asserts measure the bone transform alone.
	// Bone0 = identity; bone1 = translate (+450,-300)px. The skinned geometry's
	// MODEL positions sit in the empty lower-left; the bones fling them into the
	// empty upper-right / mid regions. A 100%-bone1 vertex lands at the full offset;
	// a 50/50-blended vertex lands at the midpoint - proving weighted blend, not a
	// bone-index pick. WWMath Matrix4x4 puts translation in the last column, read
	// row_major and mul(bone, pos) in the VS (same convention as the WVP buffer).
	Matrix4x4 bone0(true); // identity
	Matrix4x4 bone1(true);
	bone1[0][3] = kBoneTx; // +400 px in x (column-vector convention: last column)
	bone1[1][3] = kBoneTy; // 0 px in y
	const Matrix4x4 bones[2] = { bone0, bone1 };
	backend.Set_Bone_Matrices(2, bones);
	backend.Set_Skinning_Enable(true);

	// Skinned quad FULL: every vertex 100% weighted to bone1 (explicit weights 0 ->
	// implicit w3 = 1, paired with index.w == 1). Model x[20..80] y[445..475] ->
	// transformed +400px -> x[420..480] y[445..475].
	{
		const SkinVertex full[4] = {
			{ 20.0f, 445.0f, 0.0f, 0.0f, 0.0f, 0.0f, {0,0,0,1}, 0.0f, 0.0f, 1.0f, kYellow },
			{ 80.0f, 445.0f, 0.0f, 0.0f, 0.0f, 0.0f, {0,0,0,1}, 0.0f, 0.0f, 1.0f, kYellow },
			{ 80.0f, 475.0f, 0.0f, 0.0f, 0.0f, 0.0f, {0,0,0,1}, 0.0f, 0.0f, 1.0f, kYellow },
			{ 20.0f, 475.0f, 0.0f, 0.0f, 0.0f, 0.0f, {0,0,0,1}, 0.0f, 0.0f, 1.0f, kYellow },
		};
		if (!backend.Upload_Vertices(full, sizeof(full), kFVF_SKIN) ||
			!backend.Upload_Indices16(quad_indices, 6)) {
			std::printf("SMOKE FAIL: skinned FULL quad upload failed\n");
			return 1;
		}
		backend.Draw_Triangles(0 /*start_index*/, 2 /*polygon_count*/, 0, 4);
	}

	// Skinned quad BLEND: every vertex 50% bone0 (identity) + 50% bone1 (explicit
	// w0=0.5 paired with index.x==0; implicit w3=0.5 paired with index.w==1). Model
	// x[120..180] y[445..475] -> transformed by +200px -> x[320..380] y[445..475].
	// bone0-only would leave it at the model origin (x[120..180]); bone1-only would
	// fling it to x[520..580]; only a true 50/50 blend lands at the +200 midpoint.
	{
		const SkinVertex blend[4] = {
			{ 120.0f, 445.0f, 0.0f, 0.5f, 0.0f, 0.0f, {0,0,0,1}, 0.0f, 0.0f, 1.0f, kYellow },
			{ 180.0f, 445.0f, 0.0f, 0.5f, 0.0f, 0.0f, {0,0,0,1}, 0.0f, 0.0f, 1.0f, kYellow },
			{ 180.0f, 475.0f, 0.0f, 0.5f, 0.0f, 0.0f, {0,0,0,1}, 0.0f, 0.0f, 1.0f, kYellow },
			{ 120.0f, 475.0f, 0.0f, 0.5f, 0.0f, 0.0f, {0,0,0,1}, 0.0f, 0.0f, 1.0f, kYellow },
		};
		if (!backend.Upload_Vertices(blend, sizeof(blend), kFVF_SKIN) ||
			!backend.Upload_Indices16(quad_indices, 6)) {
			std::printf("SMOKE FAIL: skinned BLEND quad upload failed\n");
			return 1;
		}
		backend.Draw_Triangles(0 /*start_index*/, 2 /*polygon_count*/, 0, 4);
	}
	backend.Set_Skinning_Enable(false);

	// --- Blend / depth / rasterizer state-object cache (RENDERER_PORT.md step 9)
	// Lighting + fog OFF, no texture stage: the combiner passes the vertex diffuse
	// straight through, so these draws measure the blend/depth STATE alone. Every
	// draw uses cull NONE (winding-independent) so only the state under test can
	// change a pixel. LitVertex (XYZ|NORMAL|DIFFUSE) is reused with lighting off -
	// the normal is ignored, the diffuse passes through.
	backend.Set_Texture_Stage_Count(0);

	const void * blendPtrOpaque = nullptr; // ID3D11BlendState for the opaque vector
	const void * blendPtrAlpha  = nullptr; // ...for the alpha-blend vector (1st request)
	const void * blendPtrAlpha2 = nullptr; // ...for the alpha-blend vector (2nd request)

	// (1a) Opaque state (blend off ONE/ZERO, depth test+write on LESSEQUAL, cull
	// none, solid) -> draw a WIDE opaque RED base. Region x[20..140] y[210..270];
	// wider than the overlay so a base-only pixel survives for the non-overlap check.
	backend.Set_Blend_Enable(false);
	backend.Set_Blend_Func(RB_BLEND_ONE, RB_BLEND_ZERO);
	backend.Set_Blend_Op(RB_BLENDOP_ADD);
	backend.Set_Depth_Test_Enable(true);
	backend.Set_Depth_Write_Enable(true);
	backend.Set_Depth_Func(RB_CMP_LESSEQUAL);
	backend.Set_Cull_Mode(RB_CULL_NONE);
	backend.Set_Fill_Mode(RB_FILL_SOLID);
	backend.Apply_Render_State_Changes();
	blendPtrOpaque = backend.Get_Bound_Blend_State();
	{
		const LitVertex base[4] = {
			{  20.0f, 210.0f, 0.0f, 0.0f, 0.0f, 1.0f, kAlphaBaseColor },
			{ 140.0f, 210.0f, 0.0f, 0.0f, 0.0f, 1.0f, kAlphaBaseColor },
			{ 140.0f, 270.0f, 0.0f, 0.0f, 0.0f, 1.0f, kAlphaBaseColor },
			{  20.0f, 270.0f, 0.0f, 0.0f, 0.0f, 1.0f, kAlphaBaseColor },
		};
		if (!backend.Upload_Vertices(base, sizeof(base), kFVF_XYZ_NORMAL_DIFFUSE) ||
			!backend.Upload_Indices16(quad_indices, 6)) {
			std::printf("SMOKE FAIL: alpha base upload failed\n");
			return 1;
		}
		backend.Draw_Triangles(0, 2, 0, 4);
	}

	// (1b) Alpha-blend state (blend ON SRCALPHA/INVSRCALPHA, Z-write OFF - the
	// game's alpha case; Z-test stays LESSEQUAL so 0<=0 passes over the base) ->
	// draw a NARROW blue overlay at alpha ~0.5. Region x[20..80] y[210..270].
	backend.Set_Blend_Enable(true);
	backend.Set_Blend_Func(RB_BLEND_SRCALPHA, RB_BLEND_INVSRCALPHA);
	backend.Set_Depth_Write_Enable(false);
	backend.Apply_Render_State_Changes();
	blendPtrAlpha = backend.Get_Bound_Blend_State();
	{
		const LitVertex over[4] = {
			{ 20.0f, 210.0f, 0.0f, 0.0f, 0.0f, 1.0f, kAlphaOverColor },
			{ 80.0f, 210.0f, 0.0f, 0.0f, 0.0f, 1.0f, kAlphaOverColor },
			{ 80.0f, 270.0f, 0.0f, 0.0f, 0.0f, 1.0f, kAlphaOverColor },
			{ 20.0f, 270.0f, 0.0f, 0.0f, 0.0f, 1.0f, kAlphaOverColor },
		};
		if (!backend.Upload_Vertices(over, sizeof(over), kFVF_XYZ_NORMAL_DIFFUSE) ||
			!backend.Upload_Indices16(quad_indices, 6)) {
			std::printf("SMOKE FAIL: alpha overlay upload failed\n");
			return 1;
		}
		backend.Draw_Triangles(0, 2, 0, 4);
	}

	// (2) DEPTH test. Restore the opaque state (blend off, Z-write on) and draw a
	// NEAR green occluder at z=0.2 first, then a FAR blue quad at z=0.8 over the
	// same rect. LESSEQUAL rejects the far quad -> green survives. Region
	// x[300..410] y[8..48] (top band, clear of every other draw AND of the
	// pre-existing outside(500,100) / center sample points).
	backend.Set_Blend_Enable(false);
	backend.Set_Blend_Func(RB_BLEND_ONE, RB_BLEND_ZERO);
	backend.Set_Depth_Write_Enable(true);
	backend.Apply_Render_State_Changes();
	{
		const LitVertex nearq[4] = {
			{ 300.0f,  8.0f, kDepthNearZ, 0.0f, 0.0f, 1.0f, kDepthNearColor },
			{ 410.0f,  8.0f, kDepthNearZ, 0.0f, 0.0f, 1.0f, kDepthNearColor },
			{ 410.0f, 48.0f, kDepthNearZ, 0.0f, 0.0f, 1.0f, kDepthNearColor },
			{ 300.0f, 48.0f, kDepthNearZ, 0.0f, 0.0f, 1.0f, kDepthNearColor },
		};
		if (!backend.Upload_Vertices(nearq, sizeof(nearq), kFVF_XYZ_NORMAL_DIFFUSE) ||
			!backend.Upload_Indices16(quad_indices, 6)) {
			std::printf("SMOKE FAIL: depth near upload failed\n");
			return 1;
		}
		backend.Draw_Triangles(0, 2, 0, 4);
	}
	{
		const LitVertex farq[4] = {
			{ 300.0f,  8.0f, kDepthFarZ, 0.0f, 0.0f, 1.0f, kDepthFarColor },
			{ 410.0f,  8.0f, kDepthFarZ, 0.0f, 0.0f, 1.0f, kDepthFarColor },
			{ 410.0f, 48.0f, kDepthFarZ, 0.0f, 0.0f, 1.0f, kDepthFarColor },
			{ 300.0f, 48.0f, kDepthFarZ, 0.0f, 0.0f, 1.0f, kDepthFarColor },
		};
		if (!backend.Upload_Vertices(farq, sizeof(farq), kFVF_XYZ_NORMAL_DIFFUSE) ||
			!backend.Upload_Indices16(quad_indices, 6)) {
			std::printf("SMOKE FAIL: depth far upload failed\n");
			return 1;
		}
		backend.Draw_Triangles(0, 2, 0, 4);
	}

	// (3) CACHE IDENTITY. Re-request the SAME alpha-blend vector; the cache must
	// hand back the SAME ID3D11BlendState pointer it returned in (1b), proving no
	// per-request CreateBlendState. (No draw needed - just the re-bind.)
	backend.Set_Blend_Enable(true);
	backend.Set_Blend_Func(RB_BLEND_SRCALPHA, RB_BLEND_INVSRCALPHA);
	backend.Set_Depth_Write_Enable(false);
	backend.Apply_Render_State_Changes();
	blendPtrAlpha2 = backend.Get_Bound_Blend_State();
	// Snapshot the distinct-object counts NOW, before Shutdown releases the cache.
	// Two distinct blend vectors (opaque, alpha) were requested (the 2nd alpha
	// request was a cache hit), so exactly 2 blend objects should be cached.
	const unsigned int blendObjsCached = backend.Get_Blend_State_Count();
	const unsigned int depthObjsCached = backend.Get_Depth_State_Count();
	const unsigned int rasterObjsCached = backend.Get_Rasterizer_State_Count();

	// Texture-cache IDENTITY probe (asserted as check R after the readback; run
	// here because it needs the live device). The first revision of the cache
	// keyed on the raw IDirect3DTexture8*, so a freed texture's upload was served
	// to whatever the engine reallocated at that address - a striped-garbage
	// world. Keys are now never-reused TextureBaseClass ids versioned by the
	// texture's D3D generation: every miss case below IS that aliasing bug.
	unsigned char idpx[4 * 4 * 4];
	for (int i = 0; i < 4 * 4 * 4; ++i) idpx[i] = 200;
	const unsigned int kTexKeyA = 91001u, kTexKeyB = 91002u;
	const bool texId_upload = backend.Upload_Texture_RGBA(0, 4, 4, idpx, false, false);
	backend.Store_Cached_Texture(0, kTexKeyA, /*version*/1ull);
	const bool texId_same = backend.Bind_Cached_Texture(0, kTexKeyA, 1ull);
	const bool texId_newGen = backend.Bind_Cached_Texture(0, kTexKeyA, 2ull);
	const bool texId_otherId = backend.Bind_Cached_Texture(0, kTexKeyB, 1ull);
	backend.Evict_Cached_Texture(kTexKeyA);
	const bool texId_evicted = backend.Bind_Cached_Texture(0, kTexKeyA, 1ull);

	// (4) SCREEN-FILTER path: Capture_Backbuffer + Draw_Screen_Filter_Quad with
	// the monochrome post-op, over the bottom-right corner OF QUAD B
	// (x[540..620] y[400..460]; quad B spans x[400..600] y[300..440], the rest
	// is clear magenta). The quad samples the capture at its own screen
	// location (u = x/W, v = y/H - exactly how the game filters build their
	// quads), so the pixel under it is the monochromed quad-B color:
	// gray = dot((128,64,32)/255, lum(0.3,0.59,0.11)) = 0.3126 -> 80 in UNORM
	// (alpha follows the monochrome.nvp lrp and grays to the same value).
	// Inherent negative controls: a no-op filter path leaves quad B's
	// (128,64,32,255) here != gray; a broken RHW screen mapping shifts the quad
	// onto the filterKeep sample below and goes RED there.
	{
		struct FilterRHWVertex
		{
			float x, y, z, rhw;
			unsigned int diffuse;
			float u, v;
		};
		const float fx0 = 540.0f, fy0 = 400.0f, fx1 = 620.0f, fy1 = 460.0f;
		const FilterRHWVertex fverts[4] = {
			// Strip order mirrors the game filters: BR, TR, BL, TL, with the
			// same -0.5 screen offsets and u=x/W v=y/H mapping.
			{ fx1 - 0.5f, fy1 - 0.5f, 0.0f, 1.0f, 0xffffffffu, fx1 / (float)kWidth, fy1 / (float)kHeight },
			{ fx1 - 0.5f, fy0 - 0.5f, 0.0f, 1.0f, 0xffffffffu, fx1 / (float)kWidth, fy0 / (float)kHeight },
			{ fx0 - 0.5f, fy1 - 0.5f, 0.0f, 1.0f, 0xffffffffu, fx0 / (float)kWidth, fy1 / (float)kHeight },
			{ fx0 - 0.5f, fy0 - 0.5f, 0.0f, 1.0f, 0xffffffffu, fx0 / (float)kWidth, fy0 / (float)kHeight },
		};
		backend.Capture_Backbuffer();
		RenderBackendFilterQuad fq;
		memset(&fq, 0, sizeof(fq));
		fq.verts = fverts;
		fq.vertex_count = 4;
		fq.stride_bytes = sizeof(FilterRHWVertex);
		fq.uv_sets = 1;
		fq.blend = RenderBackendFilterQuad::BLEND_NONE;
		fq.use_captured_scene = true;
		fq.monochrome_enable = true;
		fq.mono_lum[0] = 0.3f; fq.mono_lum[1] = 0.59f; fq.mono_lum[2] = 0.11f; fq.mono_lum[3] = 1.0f;
		fq.mono_tint[0] = fq.mono_tint[1] = fq.mono_tint[2] = fq.mono_tint[3] = 1.0f;
		fq.mono_fade[0] = fq.mono_fade[1] = fq.mono_fade[2] = fq.mono_fade[3] = 1.0f;
		backend.Draw_Screen_Filter_Quad(fq);
	}

	ID3D11Device * device = backend.Get_Device();
	ID3D11DeviceContext * context = backend.Get_Context();
	IDXGISwapChain * swap_chain = backend.Get_Swap_Chain();

	ID3D11Texture2D * back_buffer = nullptr;
	HRESULT hr = swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&back_buffer));
	if (FAILED(hr)) {
		return Fail_HR("IDXGISwapChain::GetBuffer", hr);
	}

	D3D11_TEXTURE2D_DESC desc;
	back_buffer->GetDesc(&desc);
	desc.Usage = D3D11_USAGE_STAGING;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	desc.MiscFlags = 0;

	ID3D11Texture2D * staging = nullptr;
	hr = device->CreateTexture2D(&desc, nullptr, &staging);
	if (FAILED(hr)) {
		back_buffer->Release();
		return Fail_HR("CreateTexture2D(staging)", hr);
	}

	context->CopyResource(staging, back_buffer);
	back_buffer->Release();

	D3D11_MAPPED_SUBRESOURCE mapped;
	hr = context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
	if (FAILED(hr)) {
		staging->Release();
		return Fail_HR("ID3D11DeviceContext::Map", hr);
	}

	// --- Three-point readback ----------------------------------------------
	// 1) center  (320,240): NOT covered by the triangle -> still clear magenta
	//    (this is the pre-existing step-3 assertion, unchanged).
	// 2) inside  (200,333): interior of the triangle    -> fill green.
	// 3) outside (500,100): far from the triangle        -> clear magenta.
	const int cx = kWidth / 2;   // 320
	const int cy = kHeight / 2;  // 240
	const int inside_x = 200, inside_y = 333;
	const int outside_x = 500, outside_y = 100;
	// Textured-quad interior sample points (see the draw regions above).
	const int qa_x = 140, qa_y = 110;   // quad A (white*red)
	const int qb_x = 500, qb_y = 370;   // quad B (gray*colored)
	const int qc_x = 590, qc_y = 100;   // quad C (BC1 red block, white diffuse)
	const int qt_x = 40, qt_y = 330;    // quad T (texgen: camera-space + matrix)
	const int hx_x = 444, hx_y = 12;    // quad HX (half-pixel pin, X axis)
	const int hy_x = 456, hy_y = 12;    // quad HY (half-pixel pin, Y axis)
	// FF lighting/fog sample points (step 7).
	const int lit_x = 330, lit_y = 110;    // lit quad (lighting on)
	const int unlit_x = 490, unlit_y = 220;// lighting-disabled quad (passthrough)
	const int fog_x = 340, fog_y = 350;    // fog quad (blue -> red-fogged)
	// GPU skinning sample points (step 8), all in the empty bottom strip (y=460).
	const int skin_full_x = 450, skin_full_y = 460;   // 100%-bone1 quad (moved +400)
	const int skin_orig_x = 50,  skin_orig_y = 460;   // its un-skinned origin (must be empty)
	const int skin_b1only_x = 550, skin_b1only_y = 460;// where a full-bone1 BLEND would land (must be empty)
#ifdef SMOKE_NEG_CONTROL_SKIN
	// NEGATIVE CONTROL: sample the blend quad's UN-skinned origin instead of its
	// 50/50 midpoint, but still expect the yellow fill. Skinning moved the geometry
	// to the midpoint, so the origin is clear magenta -> this assert MUST go red.
	const int skin_blend_x = 150, skin_blend_y = 460; // un-skinned origin (geometry NOT here)
#else
	const int skin_blend_x = 350, skin_blend_y = 460; // 50/50 blend midpoint (moved +200)
#endif

	unsigned char c_r, c_g, c_b, c_a;
	unsigned char in_r, in_g, in_b, in_a;
	unsigned char out_r, out_g, out_b, out_a;
	unsigned char qa_r, qa_g, qa_b, qa_a;
	unsigned char qb_r, qb_g, qb_b, qb_a;
	unsigned char qc_r, qc_g, qc_b, qc_a;
	unsigned char qt_r, qt_g, qt_b, qt_a;
	unsigned char hx_r, hx_g, hx_b, hx_a;
	unsigned char hy_r, hy_g, hy_b, hy_a;
	unsigned char lit_r, lit_g, lit_b, lit_a;
	unsigned char un_r, un_g, un_b, un_a;
	unsigned char fog_r, fog_g, fog_b, fog_a;
	unsigned char sf_r, sf_g, sf_b, sf_a;      // skinned FULL quad (transformed)
	unsigned char so_r, so_g, so_b, so_a;      // skinned FULL quad un-skinned origin
	unsigned char sbl_r, sbl_g, sbl_b, sbl_a;  // skinned BLEND quad midpoint
	unsigned char sb1_r, sb1_g, sb1_b, sb1_a;  // blend's full-bone1 phantom location
	Read_Pixel(mapped, cx, cy, c_r, c_g, c_b, c_a);
	Read_Pixel(mapped, inside_x, inside_y, in_r, in_g, in_b, in_a);
	Read_Pixel(mapped, outside_x, outside_y, out_r, out_g, out_b, out_a);
	Read_Pixel(mapped, qa_x, qa_y, qa_r, qa_g, qa_b, qa_a);
	Read_Pixel(mapped, qb_x, qb_y, qb_r, qb_g, qb_b, qb_a);
	Read_Pixel(mapped, qc_x, qc_y, qc_r, qc_g, qc_b, qc_a);
	Read_Pixel(mapped, qt_x, qt_y, qt_r, qt_g, qt_b, qt_a);
	Read_Pixel(mapped, hx_x, hx_y, hx_r, hx_g, hx_b, hx_a);
	Read_Pixel(mapped, hy_x, hy_y, hy_r, hy_g, hy_b, hy_a);
	Read_Pixel(mapped, lit_x, lit_y, lit_r, lit_g, lit_b, lit_a);
	Read_Pixel(mapped, unlit_x, unlit_y, un_r, un_g, un_b, un_a);
	Read_Pixel(mapped, fog_x, fog_y, fog_r, fog_g, fog_b, fog_a);
	Read_Pixel(mapped, skin_full_x, skin_full_y, sf_r, sf_g, sf_b, sf_a);
	Read_Pixel(mapped, skin_orig_x, skin_orig_y, so_r, so_g, so_b, so_a);
	Read_Pixel(mapped, skin_blend_x, skin_blend_y, sbl_r, sbl_g, sbl_b, sbl_a);
	Read_Pixel(mapped, skin_b1only_x, skin_b1only_y, sb1_r, sb1_g, sb1_b, sb1_a);

	// Step-9 blend/depth sample points.
	const int alpha_over_x = 50,  alpha_over_y = 240; // overlap: red base + blue@0.5
	const int alpha_base_x = 110, alpha_base_y = 240; // base-only (overlay narrower)
	const int depth_x = 355, depth_y = 28;            // near green survives far blue
	unsigned char ao_r, ao_g, ao_b, ao_a;  // alpha overlap
	unsigned char ab_r, ab_g, ab_b, ab_a;  // alpha base-only
	unsigned char dp_r, dp_g, dp_b, dp_a;  // depth near-survives
	Read_Pixel(mapped, alpha_over_x, alpha_over_y, ao_r, ao_g, ao_b, ao_a);
	Read_Pixel(mapped, alpha_base_x, alpha_base_y, ab_r, ab_g, ab_b, ab_a);
	Read_Pixel(mapped, depth_x, depth_y, dp_r, dp_g, dp_b, dp_a);

	// Screen-filter sample points: inside the mono quad / outside it (same row).
	const int filter_in_x = 580, filter_in_y = 430;
	const int filter_out_x = 500, filter_out_y = 430;
	unsigned char fi_r, fi_g, fi_b, fi_a;
	unsigned char fo_r, fo_g, fo_b, fo_a;
	Read_Pixel(mapped, filter_in_x, filter_in_y, fi_r, fi_g, fi_b, fi_a);
	Read_Pixel(mapped, filter_out_x, filter_out_y, fo_r, fo_g, fo_b, fo_a);

	// 2D grayscale-override sample points: override-on quad / override-off quad.
	const int gs_x = 260, gs_y = 115;
	const int gn_x = 260, gn_y = 165;
	unsigned char gs_r, gs_g, gs_b, gs_a;
	unsigned char gn_r, gn_g, gn_b, gn_a;
	Read_Pixel(mapped, gs_x, gs_y, gs_r, gs_g, gs_b, gs_a);
	Read_Pixel(mapped, gn_x, gn_y, gn_r, gn_g, gn_b, gn_a);

	context->Unmap(staging, 0);
	staging->Release();

	// Present the final frame too, after the readback.
	backend.End_Scene(true);
	backend.Shutdown();
	DestroyWindow(hwnd);

	// --- Assertions ---------------------------------------------------------
	int failed = 0;

	// Pre-existing check: center pixel is the clear color.
	if (c_r != kExpectedR || c_g != kExpectedG || c_b != kExpectedB || c_a != kExpectedA) {
		std::printf(
			"SMOKE FAIL: center(%d,%d) got RGBA(%u,%u,%u,%u), expected clear RGBA(%u,%u,%u,%u)\n",
			cx, cy, c_r, c_g, c_b, c_a, kExpectedR, kExpectedG, kExpectedB, kExpectedA);
		failed = 1;
	} else {
		std::printf("center(%d,%d) RGBA(%u,%u,%u,%u) == clear magenta - OK\n",
			cx, cy, c_r, c_g, c_b, c_a);
	}

	// New check A: a pixel INSIDE the triangle is the fill color (green).
	if (in_r != kFillR || in_g != kFillG || in_b != kFillB || in_a != kFillA) {
		std::printf(
			"SMOKE FAIL: inside(%d,%d) got RGBA(%u,%u,%u,%u), expected fill RGBA(%u,%u,%u,%u)\n",
			inside_x, inside_y, in_r, in_g, in_b, in_a, kFillR, kFillG, kFillB, kFillA);
		failed = 1;
	} else {
		std::printf("inside(%d,%d) RGBA(%u,%u,%u,%u) == fill green - OK\n",
			inside_x, inside_y, in_r, in_g, in_b, in_a);
	}

	// New check B: a pixel OUTSIDE the triangle is still the clear color.
	if (out_r != kExpectedR || out_g != kExpectedG || out_b != kExpectedB || out_a != kExpectedA) {
		std::printf(
			"SMOKE FAIL: outside(%d,%d) got RGBA(%u,%u,%u,%u), expected clear RGBA(%u,%u,%u,%u)\n",
			outside_x, outside_y, out_r, out_g, out_b, out_a, kExpectedR, kExpectedG, kExpectedB, kExpectedA);
		failed = 1;
	} else {
		std::printf("outside(%d,%d) RGBA(%u,%u,%u,%u) == clear magenta - OK\n",
			outside_x, outside_y, out_r, out_g, out_b, out_a);
	}

	// New check C: quad A interior = white texel * red diffuse = pure red.
	if (qa_r != kQAExpR || qa_g != kQAExpG || qa_b != kQAExpB || qa_a != kQAExpA) {
		std::printf(
			"SMOKE FAIL: quadA(%d,%d) got RGBA(%u,%u,%u,%u), expected TEXTURE*DIFFUSE RGBA(%u,%u,%u,%u)\n",
			qa_x, qa_y, qa_r, qa_g, qa_b, qa_a, kQAExpR, kQAExpG, kQAExpB, kQAExpA);
		failed = 1;
	} else {
		std::printf("quadA(%d,%d) RGBA(%u,%u,%u,%u) == white*red modulate - OK\n",
			qa_x, qa_y, qa_r, qa_g, qa_b, qa_a);
	}

	// New check D: quad B interior = gray(128) texel * diffuse(255,128,64) =
	// (128,64,32) - a genuine product of two non-trivial factors.
	if (qb_r != kQBExpR || qb_g != kQBExpG || qb_b != kQBExpB || qb_a != kQBExpA) {
		std::printf(
			"SMOKE FAIL: quadB(%d,%d) got RGBA(%u,%u,%u,%u), expected TEXTURE*DIFFUSE RGBA(%u,%u,%u,%u)\n",
			qb_x, qb_y, qb_r, qb_g, qb_b, qb_a, kQBExpR, kQBExpG, kQBExpB, kQBExpA);
		failed = 1;
	} else {
		std::printf("quadB(%d,%d) RGBA(%u,%u,%u,%u) == gray*color modulate - OK\n",
			qb_x, qb_y, qb_r, qb_g, qb_b, qb_a);
	}

	// New check D2: quad C interior = BC1-decoded red block * white diffuse.
	// Tolerance +/-8 per channel: the block's endpoint color0 = 0xF800 expands
	// 5-bit R=31 -> 255 (and G/B -> 0) essentially exactly, but the D3D11 spec
	// gives BC decoders a small latitude in the 565->888 expansion / filtering
	// path, so +/-8 absorbs any conformant decoder's rounding while still being
	// ~32x tighter than the failure modes (black 0, gray 128, white 255).
	{
#ifdef SMOKE_NEG_CONTROL_BC
		// NEGATIVE CONTROL: claim the BC1 block decodes to GREEN. The true decoded
		// texel is pure red, so this MUST go red - proving the assert measures the
		// actual BC decode, not merely "some pixel was drawn here".
		const unsigned char qc_exp_r = 0, qc_exp_g = 255, qc_exp_b = 0;
#else
		const unsigned char qc_exp_r = 255, qc_exp_g = 0, qc_exp_b = 0;
#endif
		const int kBCTol = 8;
		if (std::abs((int)qc_r - (int)qc_exp_r) > kBCTol ||
			std::abs((int)qc_g - (int)qc_exp_g) > kBCTol ||
			std::abs((int)qc_b - (int)qc_exp_b) > kBCTol ||
			qc_a != 255) {
			std::printf(
				"SMOKE FAIL: quadC-BC1(%d,%d) got RGBA(%u,%u,%u,%u), expected BC1-decoded RGBA(%u,%u,%u,255) +/-%d\n",
				qc_x, qc_y, qc_r, qc_g, qc_b, qc_a, qc_exp_r, qc_exp_g, qc_exp_b, kBCTol);
			failed = 1;
		} else {
			std::printf("quadC-BC1(%d,%d) RGBA(%u,%u,%u,%u) == native BC1 red block +/-%d - OK\n",
				qc_x, qc_y, qc_r, qc_g, qc_b, qc_a, kBCTol);
		}
	}

	// Texgen check: quad T's sample point must be the BLUE texel(0,0) that only
	// the camera-space texgen + texture matrix can reach - the explicit vertex
	// UVs (0.9,0.9) and the unmatrixed camera-space coords both clamp to the RED
	// texel(1,1), so either failure mode reads red here (inherent negative
	// control, see the draw).
	if (qt_r != 0 || qt_g != 0 || qt_b != 255 || qt_a != 255) {
		std::printf(
			"SMOKE FAIL: quadT-texgen(%d,%d) got RGBA(%u,%u,%u,%u), expected texgen blue RGBA(0,0,255,255)\n",
			qt_x, qt_y, qt_r, qt_g, qt_b, qt_a);
		failed = 1;
	} else {
		std::printf("quadT-texgen(%d,%d) RGBA(%u,%u,%u,%u) == camera-space texgen blue - OK\n",
			qt_x, qt_y, qt_r, qt_g, qt_b, qt_a);
	}

	// Half-pixel pins: each sample point sits exactly on its quad's texel color
	// boundary ONLY when the D3D8 half-pixel nudge is active, reading the 50/50
	// red/blue blend (127.5,0,127.5) +/-3 for GPU filtering rounding. A removed
	// offset OR a sign-flipped offset both land half a texel into the blue half
	// and read pure (0,0,255) - far outside tolerance (inherent negative
	// control). HX pins X, HY pins Y.
	{
		const int kHPTol = 3;
		if (std::abs((int)hx_r - 128) > kHPTol + 1 || hx_g > kHPTol ||
			std::abs((int)hx_b - 128) > kHPTol + 1 || hx_a != 255) {
			std::printf(
				"SMOKE FAIL: halfpix-X(%d,%d) got RGBA(%u,%u,%u,%u), expected boundary blend RGBA(~128,0,~128,255)\n",
				hx_x, hx_y, hx_r, hx_g, hx_b, hx_a);
			failed = 1;
		} else {
			std::printf("halfpix-X(%d,%d) RGBA(%u,%u,%u,%u) == D3D8 half-pixel boundary blend - OK\n",
				hx_x, hx_y, hx_r, hx_g, hx_b, hx_a);
		}
		if (std::abs((int)hy_r - 128) > kHPTol + 1 || hy_g > kHPTol ||
			std::abs((int)hy_b - 128) > kHPTol + 1 || hy_a != 255) {
			std::printf(
				"SMOKE FAIL: halfpix-Y(%d,%d) got RGBA(%u,%u,%u,%u), expected boundary blend RGBA(~128,0,~128,255)\n",
				hy_x, hy_y, hy_r, hy_g, hy_b, hy_a);
			failed = 1;
		} else {
			std::printf("halfpix-Y(%d,%d) RGBA(%u,%u,%u,%u) == D3D8 half-pixel boundary blend - OK\n",
				hy_x, hy_y, hy_r, hy_g, hy_b, hy_a);
		}
	}

	// New check E: the LIT quad. Evaluate the fixed-function lighting equation on
	// the CPU with the SAME math the VS runs, then assert the rendered pixel
	// matches within +/-1 per channel (float->UNORM8 rounding tolerance).
	//   N = (0,0,1); L = normalize(-lightDir) = (0.8660254,0,0.5); N.L = 0.5
	//   diffuse source COLOR1 -> material diffuse = vertex red (1,0,0)
	//   ambient source MATERIAL -> ambient term = matAmbient * sceneAmbient
	//   litColor = emissive(0) + matAmbient*sceneAmbient + N.L * vtxDiffuse * lightDiffuse
	{
		float lx = -kLightDir[0], ly = -kLightDir[1], lz = -kLightDir[2];
		float llen = std::sqrt(lx * lx + ly * ly + lz * lz);
		lx /= llen; ly /= llen; lz /= llen;
		float nx = kLitNormal[0], ny = kLitNormal[1], nz = kLitNormal[2];
		float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
		nx /= nlen; ny /= nlen; nz /= nlen;
		float ndl = nx * lx + ny * ly + nz * lz;
		if (ndl < 0.0f) ndl = 0.0f;
		const float vr = 1.0f, vg = 0.0f, vb = 0.0f; // kLitDiffuse red as material diffuse
		float er = kMatAmbient[0] * kSceneAmbient[0] + ndl * vr * kLightDiff[0];
		float eg = kMatAmbient[1] * kSceneAmbient[1] + ndl * vg * kLightDiff[1];
		float eb = kMatAmbient[2] * kSceneAmbient[2] + ndl * vb * kLightDiff[2];
#ifdef SMOKE_NEG_CONTROL_LIT
		// NEGATIVE CONTROL: claim lighting had NO effect (full-bright vertex diffuse
		// = red 255,0,0). The true lit pixel is ~(166,38,38), so this MUST go red.
		// This plant isolates the lighting assert: build with -DSMOKE_NEG_CONTROL_LIT
		// and only this check flips, proving it tests the N.L.material product.
		const unsigned char lit_exp_r = 255, lit_exp_g = 0, lit_exp_b = 0;
#else
		const unsigned char lit_exp_r = ExpectU8(er);
		const unsigned char lit_exp_g = ExpectU8(eg);
		const unsigned char lit_exp_b = ExpectU8(eb);
#endif
		const int kTol = 1;
		if (std::abs((int)lit_r - (int)lit_exp_r) > kTol ||
			std::abs((int)lit_g - (int)lit_exp_g) > kTol ||
			std::abs((int)lit_b - (int)lit_exp_b) > kTol ||
			lit_a != 255) {
			std::printf(
				"SMOKE FAIL: lit(%d,%d) got RGBA(%u,%u,%u,%u), expected FF-lit RGBA(%u,%u,%u,255) +/-%d (N.L=%.3f)\n",
				lit_x, lit_y, lit_r, lit_g, lit_b, lit_a,
				lit_exp_r, lit_exp_g, lit_exp_b, kTol, ndl);
			failed = 1;
		} else {
			std::printf("lit(%d,%d) RGBA(%u,%u,%u,%u) == FF-lit expected RGBA(%u,%u,%u,255) - OK\n",
				lit_x, lit_y, lit_r, lit_g, lit_b, lit_a, lit_exp_r, lit_exp_g, lit_exp_b);
		}
	}

	// New check F: the SAME red geometry with lighting DISABLED passes the vertex
	// diffuse straight through (255,0,0). This is exact (no lighting math), and it
	// differs from the lit pixel above - proving the enable gate actually gates.
	if (un_r != 255 || un_g != 0 || un_b != 0 || un_a != 255) {
		std::printf(
			"SMOKE FAIL: unlit(%d,%d) got RGBA(%u,%u,%u,%u), expected passthrough RGBA(255,0,0,255)\n",
			unlit_x, unlit_y, un_r, un_g, un_b, un_a);
		failed = 1;
	} else {
		std::printf("unlit(%d,%d) RGBA(%u,%u,%u,%u) == diffuse passthrough (lighting off) - OK\n",
			unlit_x, unlit_y, un_r, un_g, un_b, un_a);
	}

	// New check G: the FOG quad. Blue diffuse, linear fog toward red, depth 0.5 ->
	// f = (end-d)/(end-start) = 0.5, so pixel = lerp(fogColor, quadColor, 0.5).
	{
		float f = (kFogEnd - kFogDepth) / (kFogEnd - kFogStart); // 0.5
		if (f < 0.0f) f = 0.0f; if (f > 1.0f) f = 1.0f;
		const float qr = 0.0f, qg = 0.0f, qb = 1.0f; // kFogQuadDiffuse blue
		float fr = kFogColor[0] * (1.0f - f) + qr * f;
		float fg = kFogColor[1] * (1.0f - f) + qg * f;
		float fb = kFogColor[2] * (1.0f - f) + qb * f;
		const unsigned char fog_exp_r = ExpectU8(fr);
		const unsigned char fog_exp_g = ExpectU8(fg);
		const unsigned char fog_exp_b = ExpectU8(fb);
		const int kTol = 1;
		if (std::abs((int)fog_r - (int)fog_exp_r) > kTol ||
			std::abs((int)fog_g - (int)fog_exp_g) > kTol ||
			std::abs((int)fog_b - (int)fog_exp_b) > kTol ||
			fog_a != 255) {
			std::printf(
				"SMOKE FAIL: fog(%d,%d) got RGBA(%u,%u,%u,%u), expected fogged RGBA(%u,%u,%u,255) +/-%d (f=%.3f)\n",
				fog_x, fog_y, fog_r, fog_g, fog_b, fog_a, fog_exp_r, fog_exp_g, fog_exp_b, kTol, f);
			failed = 1;
		} else {
			std::printf("fog(%d,%d) RGBA(%u,%u,%u,%u) == linear-fogged expected RGBA(%u,%u,%u,255) - OK\n",
				fog_x, fog_y, fog_r, fog_g, fog_b, fog_a, fog_exp_r, fog_exp_g, fog_exp_b);
		}
	}

	// New check H (step 8): the FULL skinned quad is yellow at the BONE-TRANSFORMED
	// location (+450,-300 from its model origin). Yellow here means the bone matrix
	// actually moved the geometry; a skinning-ignored bug would leave this empty.
	if (sf_r != kSkinR || sf_g != kSkinG || sf_b != kSkinB || sf_a != kSkinA) {
		std::printf(
			"SMOKE FAIL: skinFull(%d,%d) got RGBA(%u,%u,%u,%u), expected bone1-moved fill RGBA(%u,%u,%u,%u)\n",
			skin_full_x, skin_full_y, sf_r, sf_g, sf_b, sf_a, kSkinR, kSkinG, kSkinB, kSkinA);
		failed = 1;
	} else {
		std::printf("skinFull(%d,%d) RGBA(%u,%u,%u,%u) == yellow at bone-transformed pos - OK\n",
			skin_full_x, skin_full_y, sf_r, sf_g, sf_b, sf_a);
	}

	// New check I (step 8): the FULL skinned quad's UN-skinned origin is CLEAR - the
	// geometry left its pre-bone location. (This is the always-on counterpart to the
	// SMOKE_NEG_CONTROL_SKIN plant: if skinning were a no-op the fill would be here.)
	if (so_r != kExpectedR || so_g != kExpectedG || so_b != kExpectedB || so_a != kExpectedA) {
		std::printf(
			"SMOKE FAIL: skinOrigin(%d,%d) got RGBA(%u,%u,%u,%u), expected clear magenta RGBA(%u,%u,%u,%u) - geometry did NOT move\n",
			skin_orig_x, skin_orig_y, so_r, so_g, so_b, so_a, kExpectedR, kExpectedG, kExpectedB, kExpectedA);
		failed = 1;
	} else {
		std::printf("skinOrigin(%d,%d) RGBA(%u,%u,%u,%u) == clear (geometry moved away) - OK\n",
			skin_orig_x, skin_orig_y, so_r, so_g, so_b, so_a);
	}

	// New check J (step 8): the BLEND quad is yellow at the 50/50 MIDPOINT (+225,-150,
	// i.e. exactly half of bone1's +450,-300). This is the strongest skinning assert:
	// a bone0-only pick would leave it at the origin, a bone1-only pick would fling it
	// to +450,-300 - only a genuine weighted blend of the two lands at the midpoint.
	// The SMOKE_NEG_CONTROL_SKIN build samples the un-skinned origin instead, which is
	// clear, so this exact assert flips red under the negative control.
	if (sbl_r != kSkinR || sbl_g != kSkinG || sbl_b != kSkinB || sbl_a != kSkinA) {
		std::printf(
			"SMOKE FAIL: skinBlend(%d,%d) got RGBA(%u,%u,%u,%u), expected 50/50-blended fill RGBA(%u,%u,%u,%u)\n",
			skin_blend_x, skin_blend_y, sbl_r, sbl_g, sbl_b, sbl_a, kSkinR, kSkinG, kSkinB, kSkinA);
		failed = 1;
	} else {
		std::printf("skinBlend(%d,%d) RGBA(%u,%u,%u,%u) == yellow at 50/50 blend midpoint - OK\n",
			skin_blend_x, skin_blend_y, sbl_r, sbl_g, sbl_b, sbl_a);
	}

	// New check K (step 8): the location a full-bone1 (weight-ignoring) interpretation
	// of the BLEND quad WOULD land is CLEAR - proving the blend weighted the bones
	// rather than snapping to a single one.
	if (sb1_r != kExpectedR || sb1_g != kExpectedG || sb1_b != kExpectedB || sb1_a != kExpectedA) {
		std::printf(
			"SMOKE FAIL: skinBlendPhantom(%d,%d) got RGBA(%u,%u,%u,%u), expected clear magenta RGBA(%u,%u,%u,%u) - blend snapped to full bone1\n",
			skin_b1only_x, skin_b1only_y, sb1_r, sb1_g, sb1_b, sb1_a, kExpectedR, kExpectedG, kExpectedB, kExpectedA);
		failed = 1;
	} else {
		std::printf("skinBlendPhantom(%d,%d) RGBA(%u,%u,%u,%u) == clear (blend is weighted, not full bone1) - OK\n",
			skin_b1only_x, skin_b1only_y, sb1_r, sb1_g, sb1_b, sb1_a);
	}

	// New check L (step 9): the ALPHA-BLENDED overlap pixel = src*a + dst*(1-a),
	// a = 128/255. src = blue (0,0,1), dst = opaque red base (1,0,0) ->
	// (~127, 0, ~128). Computed on the CPU with the SAME blend math the OM stage
	// runs; asserted within +/-1 per channel (float->UNORM8 rounding tolerance).
	{
		const float a = 128.0f / 255.0f;
		const float er = 0.0f * a + 1.0f * (1.0f - a); // red base survives (1-a)
		const float eg = 0.0f;
		const float eb = 1.0f * a + 0.0f * (1.0f - a); // blue overlay contributes a
#ifdef SMOKE_NEG_CONTROL_BLEND
		// NEGATIVE CONTROL: claim the overlay OVERWROTE the base (no blend) - expect
		// the pure SRC blue (0,0,255). The true blended pixel is ~(127,0,128), so
		// this MUST go red, proving the assert tests the src*a+dst*(1-a) product and
		// not merely "some blue landed here".
		const unsigned char ab_exp_r = 0, ab_exp_g = 0, ab_exp_b = 255;
#else
		const unsigned char ab_exp_r = ExpectU8(er);
		const unsigned char ab_exp_g = ExpectU8(eg);
		const unsigned char ab_exp_b = ExpectU8(eb);
#endif
		// The alpha CHANNEL blends with the same factors (D3DRS_ALPHABLENDENABLE is
		// not separate-alpha in DX8): resultA = srcA*srcA + dstA*(1-srcA), dstA=1
		// (opaque base) -> ~0.750 (191). Asserted too, so a wrong alpha-blend factor
		// is caught rather than ignored.
		const float ea = a * a + 1.0f * (1.0f - a);
		const unsigned char ab_exp_a = ExpectU8(ea);
		const int kTol = 1;
		if (std::abs((int)ao_r - (int)ab_exp_r) > kTol ||
			std::abs((int)ao_g - (int)ab_exp_g) > kTol ||
			std::abs((int)ao_b - (int)ab_exp_b) > kTol ||
			std::abs((int)ao_a - (int)ab_exp_a) > kTol) {
			std::printf(
				"SMOKE FAIL: alphaBlend(%d,%d) got RGBA(%u,%u,%u,%u), expected src*a+dst*(1-a) RGBA(%u,%u,%u,%u) +/-%d (a=%.4f)\n",
				alpha_over_x, alpha_over_y, ao_r, ao_g, ao_b, ao_a, ab_exp_r, ab_exp_g, ab_exp_b, ab_exp_a, kTol, a);
			failed = 1;
		} else {
			std::printf("alphaBlend(%d,%d) RGBA(%u,%u,%u,%u) == src*a+dst*(1-a) expected RGBA(%u,%u,%u,%u) - OK\n",
				alpha_over_x, alpha_over_y, ao_r, ao_g, ao_b, ao_a, ab_exp_r, ab_exp_g, ab_exp_b, ab_exp_a);
		}
	}

	// New check M (step 9): a NON-overlapped base pixel is still the opaque base
	// (255,0,0) - the narrow overlay didn't cover it, so no blend happened here.
	if (ab_r != 255 || ab_g != 0 || ab_b != 0 || ab_a != 255) {
		std::printf(
			"SMOKE FAIL: alphaBase(%d,%d) got RGBA(%u,%u,%u,%u), expected opaque base RGBA(255,0,0,255)\n",
			alpha_base_x, alpha_base_y, ab_r, ab_g, ab_b, ab_a);
		failed = 1;
	} else {
		std::printf("alphaBase(%d,%d) RGBA(%u,%u,%u,%u) == opaque red base (no overlay) - OK\n",
			alpha_base_x, alpha_base_y, ab_r, ab_g, ab_b, ab_a);
	}

	// New check N (step 9): the DEPTH-TEST overlap is the NEAR green (0,255,0). The
	// far blue quad (z=0.8) was drawn after the near green (z=0.2) but Z-test
	// LESSEQUAL rejected it, so the near color survives. A depth-test-broken bug
	// would show the far blue instead.
	if (dp_r != 0 || dp_g != 255 || dp_b != 0 || dp_a != 255) {
		std::printf(
			"SMOKE FAIL: depthTest(%d,%d) got RGBA(%u,%u,%u,%u), expected near-green RGBA(0,255,0,255) - far quad not depth-rejected\n",
			depth_x, depth_y, dp_r, dp_g, dp_b, dp_a);
		failed = 1;
	} else {
		std::printf("depthTest(%d,%d) RGBA(%u,%u,%u,%u) == near green survived (far quad Z-rejected) - OK\n",
			depth_x, depth_y, dp_r, dp_g, dp_b, dp_a);
	}

	// New check O (step 9): CACHE IDENTITY. Requesting the same alpha-blend vector
	// twice returns the SAME ID3D11BlendState pointer (one cached object, no
	// per-request create); the distinct opaque vector returns a DIFFERENT object.
	if (blendPtrAlpha == nullptr || blendPtrOpaque == nullptr ||
		blendPtrAlpha2 != blendPtrAlpha || blendPtrAlpha == blendPtrOpaque) {
		std::printf(
			"SMOKE FAIL: state cache identity: opaque=%p alpha=%p alpha2=%p (want alpha==alpha2, alpha!=opaque, none null)\n",
			blendPtrOpaque, blendPtrAlpha, blendPtrAlpha2);
		failed = 1;
	} else if (blendObjsCached != 2) {
		std::printf(
			"SMOKE FAIL: state cache growth: expected 2 distinct blend objects (opaque + alpha), got %u\n",
			blendObjsCached);
		failed = 1;
	} else {
		std::printf("stateCache identity: alpha==alpha2 (%p reused), alpha!=opaque (%p) - OK; cached blend=%u depth=%u raster=%u\n",
			blendPtrAlpha, blendPtrOpaque, blendObjsCached, depthObjsCached, rasterObjsCached);
	}

	// New check P (screen filters): the monochrome filter quad grayed quad B's
	// (128,64,32) to 80 gray (dot((.502,.251,.126),(0.3,0.59,0.11)) = 0.3126;
	// alpha follows the monochrome.nvp lrp, so it grays too). A no-op
	// capture/filter path leaves 128,64,32,255 here and goes RED.
	{
		const int expected = 80, tol = 2;
		const bool gray_ok =
			fi_r >= expected - tol && fi_r <= expected + tol &&
			fi_g >= expected - tol && fi_g <= expected + tol &&
			fi_b >= expected - tol && fi_b <= expected + tol &&
			fi_a >= expected - tol && fi_a <= expected + tol;
		if (!gray_ok) {
			std::printf(
				"SMOKE FAIL: filterGray(%d,%d) got RGBA(%u,%u,%u,%u), expected mono gray RGBA(~%d x4)\n",
				filter_in_x, filter_in_y, fi_r, fi_g, fi_b, fi_a, expected);
			failed = 1;
		} else {
			std::printf("filterGray(%d,%d) RGBA(%u,%u,%u,%u) == monochromed capture - OK\n",
				filter_in_x, filter_in_y, fi_r, fi_g, fi_b, fi_a);
		}
	}

	// New check Q (screen filters): one row-pixel OUTSIDE the filter quad keeps
	// quad B's ORIGINAL color - pins the legacy RHW screen-pixel mapping (an
	// offset/scale bug spills the gray quad here and goes RED).
	if (fo_r != kQBExpR || fo_g != kQBExpG || fo_b != kQBExpB || fo_a != kQBExpA) {
		std::printf(
			"SMOKE FAIL: filterKeep(%d,%d) got RGBA(%u,%u,%u,%u), expected untouched quad B RGBA(%u,%u,%u,%u) - filter quad spilled\n",
			filter_out_x, filter_out_y, fo_r, fo_g, fo_b, fo_a, kQBExpR, kQBExpG, kQBExpB, kQBExpA);
		failed = 1;
	} else {
		std::printf("filterKeep(%d,%d) RGBA(%u,%u,%u,%u) == untouched quad B (no spill) - OK\n",
			filter_out_x, filter_out_y, fo_r, fo_g, fo_b, fo_a);
	}

	// New check R (texture cache): the IDENTITY contract the uploaded-texture
	// cache rests on, tested directly rather than hoped for in-game. The first
	// revision keyed on the raw IDirect3DTexture8* and served a freed texture's
	// upload to whatever the engine reallocated at that address (striped-garbage
	// world). Keys are now TextureBaseClass ids - never reused - versioned by the
	// texture's D3D generation. All three miss cases below are the aliasing bug:
	// if any of them HITS, a stale upload is being served for changed bytes.
	if (!texId_upload || !texId_same || texId_newGen || texId_otherId || texId_evicted) {
		std::printf(
			"SMOKE FAIL: texCacheIdentity upload=%d same=%d(want 1) newGen=%d(want 0) otherId=%d(want 0) evicted=%d(want 0)\n",
			(int)texId_upload, (int)texId_same, (int)texId_newGen, (int)texId_otherId, (int)texId_evicted);
		failed = 1;
	} else {
		std::printf("texCacheIdentity: same-key/ver hits; bumped-generation, foreign-id and evicted all MISS - OK\n");
	}

	// New check S (2D grayscale override): the red quad drawn under
	// Set_Grayscale_Override(true) reads back as its DX8-DOT3-trick luminance -
	// gray(75) = round(255 * 0.295) for pure red - alpha untouched. Tolerance
	// +/-2 for float->UNORM rounding. This is the disabled-command-bar-button
	// path: if the override never reaches cbCombiner the quad stays pure red.
	{
		const int kGSExp = 75;
		const bool gs_gray_ok =
			std::abs((int)gs_r - kGSExp) <= 2 && std::abs((int)gs_g - kGSExp) <= 2 &&
			std::abs((int)gs_b - kGSExp) <= 2 && gs_a == 255;
		if (!gs_gray_ok) {
			std::printf(
				"SMOKE FAIL: gsOn(%d,%d) got RGBA(%u,%u,%u,%u), expected luminance gray(~%d,a=255)\n",
				gs_x, gs_y, gs_r, gs_g, gs_b, gs_a, kGSExp);
			failed = 1;
		} else {
			std::printf("gsOn(%d,%d) RGBA(%u,%u,%u,%u) == DX8-trick luminance of red - OK\n",
				gs_x, gs_y, gs_r, gs_g, gs_b, gs_a);
		}
	}

	// New check T (2D grayscale override clears): the same red draw AFTER
	// Set_Grayscale_Override(false) is pure red again. A sticky override (the
	// bracket in Render2DClass failing to clear) would gray every later UI
	// draw and goes RED here - the negative control for check S.
	if (gn_r != 255 || gn_g != 0 || gn_b != 0 || gn_a != 255) {
		std::printf(
			"SMOKE FAIL: gsOff(%d,%d) got RGBA(%u,%u,%u,%u), expected pure red (override stuck?)\n",
			gn_x, gn_y, gn_r, gn_g, gn_b, gn_a);
		failed = 1;
	} else {
		std::printf("gsOff(%d,%d) RGBA(%u,%u,%u,%u) == pure red, override cleared - OK\n",
			gn_x, gn_y, gn_r, gn_g, gn_b, gn_a);
	}

	if (failed) {
		return 1;
	}

	std::printf("SMOKE PASS\n");
	return 0;
}
