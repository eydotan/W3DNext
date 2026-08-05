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

// w3d_parity_diff - PNG frame-diff oracle for the DX8-vs-D3D11 A/B parity pass
// (RENDERER_PORT.md step 10). Decodes two PNG frames (or two directories of
// frames), computes per-pixel deltas, and emits a machine-readable verdict:
//
//     PARITY <PASS|FAIL> maxdelta=N mae=X.XX over=K/total
//
// with a nonzero exit on FAIL. It consumes exactly the format W3DDisplay::
// takeScreenShot writes today: 8-bit truecolor (color type 2), single zlib
// IDAT, filter-0 scanlines, named w3dnext_screenshot_NNN.png. The decoder is a
// touch broader (grayscale/RGBA/all five filter types) so real captured assets
// also load, but everything routes through the same RGB diff core.
//
// The D3D11 backend is NOT yet selectable in the running game, so a real
// DX8-vs-D3D11 frame comparison cannot run yet. This binary is therefore
// self-validated against SYNTHETIC image pairs with known deltas (--selftest):
// it round-trips real PNG files through the game's own encode/decode format and
// asserts the verdicts. When step 10 wires the D3D11 backend behind a
// backend-select flag, the same tool A/Bs the two backends' F9 captures with no
// change. See Core/Tests/parity/README.md and tools/parity/README.md.

#include <zlib.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

// ---------------------------------------------------------------------------
// Image model - always normalized to 8-bit RGB (3 bytes/pixel, top-down).
// ---------------------------------------------------------------------------
struct Image
{
	int width;
	int height;
	std::vector<uint8_t> rgb;	// width*height*3, R,G,B

	Image() : width(0), height(0) {}
	size_t pixels() const { return (size_t)width * (size_t)height; }
};

void putBE32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)(v);
}

uint32_t getBE32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

bool readFile(const std::string &path, std::vector<uint8_t> &out)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n < 0) { fclose(f); return false; }
	out.resize((size_t)n);
	size_t got = n > 0 ? fread(out.data(), 1, (size_t)n, f) : 0;
	fclose(f);
	return got == (size_t)n;
}

int paeth(int a, int b, int c)
{
	int p = a + b - c;
	int pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
	if (pa <= pb && pa <= pc) return a;
	if (pb <= pc) return b;
	return c;
}

// ---------------------------------------------------------------------------
// Minimal PNG decoder. Supports 8-bit color types 0 (gray), 2 (RGB), 4
// (gray+alpha), 6 (RGBA); all five scanline filters; non-interlaced. Inflate
// is delegated to the linked zlib (the game's own IDAT producer). Returns
// false with *err set on anything it can't handle, rather than guessing.
// ---------------------------------------------------------------------------
bool decodePNG(const std::vector<uint8_t> &file, Image &img, std::string &err)
{
	static const uint8_t SIG[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	if (file.size() < 8 || memcmp(file.data(), SIG, 8) != 0)
	{
		err = "not a PNG (bad signature)";
		return false;
	}

	size_t pos = 8;
	int width = 0, height = 0, bitDepth = 0, colorType = 0, interlace = 0;
	bool haveIHDR = false;
	std::vector<uint8_t> idat;

	while (pos + 8 <= file.size())
	{
		uint32_t len = getBE32(&file[pos]);
		const char *type = (const char *)&file[pos + 4];
		size_t dataPos = pos + 8;
		if (dataPos + len + 4 > file.size())
		{
			err = "truncated chunk";
			return false;
		}
		if (memcmp(type, "IHDR", 4) == 0)
		{
			if (len != 13) { err = "bad IHDR length"; return false; }
			width  = (int)getBE32(&file[dataPos + 0]);
			height = (int)getBE32(&file[dataPos + 4]);
			bitDepth  = file[dataPos + 8];
			colorType = file[dataPos + 9];
			interlace = file[dataPos + 12];
			haveIHDR = true;
		}
		else if (memcmp(type, "IDAT", 4) == 0)
		{
			idat.insert(idat.end(), &file[dataPos], &file[dataPos + len]);
		}
		else if (memcmp(type, "IEND", 4) == 0)
		{
			break;
		}
		pos = dataPos + len + 4;	// skip data + CRC
	}

	if (!haveIHDR) { err = "no IHDR"; return false; }
	if (bitDepth != 8) { err = "unsupported bit depth (only 8 supported)"; return false; }
	if (interlace != 0) { err = "interlaced PNG not supported"; return false; }
	if (width <= 0 || height <= 0) { err = "bad dimensions"; return false; }

	int channels;
	switch (colorType)
	{
		case 0: channels = 1; break;	// grayscale
		case 2: channels = 3; break;	// truecolor (the game's format)
		case 4: channels = 2; break;	// gray + alpha
		case 6: channels = 4; break;	// truecolor + alpha
		default: err = "unsupported color type (0/2/4/6 only; no palette)"; return false;
	}

	const size_t rowBytes = (size_t)width * channels;
	const size_t rawLen = (size_t)height * (rowBytes + 1);	// +1 filter byte/row
	std::vector<uint8_t> raw(rawLen);
	uLongf outLen = (uLongf)rawLen;
	int zerr = uncompress(raw.data(), &outLen, idat.data(), (uLong)idat.size());
	if (zerr != Z_OK || outLen != rawLen)
	{
		err = "IDAT inflate failed or size mismatch";
		return false;
	}

	// Un-filter into a channels-wide buffer, then collapse to RGB.
	std::vector<uint8_t> unfiltered((size_t)height * rowBytes);
	for (int y = 0; y < height; ++y)
	{
		const uint8_t *src = &raw[(size_t)y * (rowBytes + 1)];
		uint8_t filter = src[0];
		++src;
		uint8_t *cur = &unfiltered[(size_t)y * rowBytes];
		const uint8_t *prev = (y > 0) ? &unfiltered[(size_t)(y - 1) * rowBytes] : nullptr;
		for (size_t i = 0; i < rowBytes; ++i)
		{
			int a = (i >= (size_t)channels) ? cur[i - channels] : 0;	// left
			int b = prev ? prev[i] : 0;								// up
			int c = (prev && i >= (size_t)channels) ? prev[i - channels] : 0;	// up-left
			int x = src[i];
			int v;
			switch (filter)
			{
				case 0: v = x; break;
				case 1: v = x + a; break;
				case 2: v = x + b; break;
				case 3: v = x + ((a + b) >> 1); break;
				case 4: v = x + paeth(a, b, c); break;
				default: err = "unknown scanline filter"; return false;
			}
			cur[i] = (uint8_t)(v & 0xff);
		}
	}

	img.width = width;
	img.height = height;
	img.rgb.resize(img.pixels() * 3);
	for (size_t p = 0; p < img.pixels(); ++p)
	{
		const uint8_t *s = &unfiltered[p * channels];
		uint8_t r, g, bch;
		if (channels <= 2) { r = g = bch = s[0]; }	// gray / gray+alpha
		else { r = s[0]; g = s[1]; bch = s[2]; }		// RGB / RGBA (alpha dropped)
		img.rgb[p * 3 + 0] = r;
		img.rgb[p * 3 + 1] = g;
		img.rgb[p * 3 + 2] = bch;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Minimal PPM (binary P6, maxval 255) decoder - the format the in-engine
// framedumps write (W3DNEXT_DX8_FRAMEDUMP / W3DNEXT_D3D11_FRAMEDUMP), so backend
// backbuffer dumps are consumable directly without a conversion step.
// ---------------------------------------------------------------------------
bool decodePPM(const std::vector<uint8_t> &file, Image &img, std::string &err)
{
	if (file.size() < 2 || file[0] != 'P' || file[1] != '6')
	{
		err = "not a P6 PPM";
		return false;
	}
	// Header: "P6" <ws> width <ws> height <ws> maxval <single ws> raster.
	// '#' comments allowed between tokens.
	size_t pos = 2;
	long vals[3];
	for (int v = 0; v < 3; ++v)
	{
		while (pos < file.size() && (isspace(file[pos]) || file[pos] == '#'))
		{
			if (file[pos] == '#')
				while (pos < file.size() && file[pos] != '\n') ++pos;
			else
				++pos;
		}
		long n = 0;
		bool any = false;
		while (pos < file.size() && isdigit(file[pos]))
		{
			n = n * 10 + (file[pos] - '0');
			++pos;
			any = true;
		}
		if (!any) { err = "bad PPM header"; return false; }
		vals[v] = n;
	}
	if (pos >= file.size() || !isspace(file[pos])) { err = "bad PPM header terminator"; return false; }
	++pos;	// single whitespace before raster
	if (vals[0] <= 0 || vals[1] <= 0) { err = "bad PPM dimensions"; return false; }
	if (vals[2] != 255) { err = "unsupported PPM maxval (255 only)"; return false; }
	const size_t need = (size_t)vals[0] * vals[1] * 3;
	if (file.size() - pos < need) { err = "truncated PPM raster"; return false; }
	img.width = (int)vals[0];
	img.height = (int)vals[1];
	img.rgb.assign(file.begin() + pos, file.begin() + pos + need);
	return true;
}

// ---------------------------------------------------------------------------
// PNG encoder - byte-for-byte the game's format (color type 2, filter 0, one
// zlib IDAT). Used only by --selftest to emit real on-disk PNGs, so the
// self-test exercises decode of exactly what W3DDisplay writes.
// ---------------------------------------------------------------------------
bool encodePNG(const std::string &path, const Image &img)
{
	const uint32_t rowBytes = (uint32_t)img.width * 3;
	const uint32_t rawLen = (uint32_t)img.height * (rowBytes + 1);
	std::vector<uint8_t> raw(rawLen);
	for (int y = 0; y < img.height; ++y)
	{
		uint8_t *dst = &raw[(size_t)y * (rowBytes + 1)];
		*dst++ = 0;	// filter: none
		memcpy(dst, &img.rgb[(size_t)y * rowBytes], rowBytes);
	}

	// compressBound is zlib >= 1.2.0; the bundled fallback zlib (1.1.4, non-vcpkg
	// presets) lacks it. Same formula, valid for any zlib deflate output.
	uLong cap = rawLen + rawLen / 1000 + 64;
	std::vector<uint8_t> comp(cap);
	uLongf compLen = cap;
	if (compress(comp.data(), &compLen, raw.data(), rawLen) != Z_OK)
		return false;

	FILE *f = fopen(path.c_str(), "wb");
	if (!f)
		return false;

	auto writeChunk = [&](const char *type, const uint8_t *data, uint32_t len)
	{
		uint8_t head[8];
		putBE32(head, len);
		memcpy(head + 4, type, 4);
		fwrite(head, 1, 8, f);
		if (len)
			fwrite(data, 1, len, f);
		uLong crc = crc32(0, (const Bytef *)type, 4);
		if (len)
			crc = crc32(crc, data, len);
		uint8_t crcbuf[4];
		putBE32(crcbuf, (uint32_t)crc);
		fwrite(crcbuf, 1, 4, f);
	};

	static const uint8_t SIG[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	fwrite(SIG, 1, 8, f);
	uint8_t ihdr[13];
	putBE32(ihdr + 0, (uint32_t)img.width);
	putBE32(ihdr + 4, (uint32_t)img.height);
	ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
	writeChunk("IHDR", ihdr, 13);
	writeChunk("IDAT", comp.data(), compLen);
	writeChunk("IEND", nullptr, 0);
	fclose(f);
	return true;
}

// ---------------------------------------------------------------------------
// Diff core.
// ---------------------------------------------------------------------------
struct DiffResult
{
	bool dimsMatch;
	int maxDelta;			// max abs channel delta over all pixels
	double mae;				// mean abs error over all channel samples
	size_t over;			// pixels whose max channel delta exceeds tol
	size_t total;			// width*height
	std::vector<std::pair<int,int> > overList;	// (x,y) of exceeding pixels
};

DiffResult diffImages(const Image &a, const Image &b, int tol, size_t listCap)
{
	DiffResult r;
	r.dimsMatch = (a.width == b.width && a.height == b.height);
	r.maxDelta = 0;
	r.mae = 0.0;
	r.over = 0;
	r.total = a.pixels();
	if (!r.dimsMatch)
		return r;

	double sum = 0.0;
	for (size_t p = 0; p < a.pixels(); ++p)
	{
		int pixMax = 0;
		for (int c = 0; c < 3; ++c)
		{
			int d = abs((int)a.rgb[p * 3 + c] - (int)b.rgb[p * 3 + c]);
			sum += d;
			if (d > pixMax) pixMax = d;
		}
		if (pixMax > r.maxDelta) r.maxDelta = pixMax;
		if (pixMax > tol)
		{
			++r.over;
			if (r.overList.size() < listCap)
				r.overList.push_back(std::make_pair((int)(p % a.width), (int)(p / a.width)));
		}
	}
	r.mae = a.pixels() ? sum / (a.pixels() * 3.0) : 0.0;
	return r;
}

// Amplified delta visualization: per channel min(255, |dA|*amp).
bool writeDiffViz(const std::string &path, const Image &a, const Image &b, int amp)
{
	if (a.width != b.width || a.height != b.height)
		return false;
	Image out;
	out.width = a.width;
	out.height = a.height;
	out.rgb.resize(out.pixels() * 3);
	for (size_t i = 0; i < out.rgb.size(); ++i)
	{
		int d = abs((int)a.rgb[i] - (int)b.rgb[i]) * amp;
		out.rgb[i] = (uint8_t)(d > 255 ? 255 : d);
	}
	return encodePNG(path, out);
}

// ---------------------------------------------------------------------------
// Verdict printing. Returns process exit code (0 pass, 2 fail).
// ---------------------------------------------------------------------------
int reportVerdict(const std::string &label, const DiffResult &r, bool listOver)
{
	bool pass = r.dimsMatch && r.over == 0;
	if (!r.dimsMatch)
	{
		printf("PARITY FAIL maxdelta=- mae=- over=- (dimension mismatch) %s\n", label.c_str());
		return 2;
	}
	printf("PARITY %s maxdelta=%d mae=%.4f over=%zu/%zu %s\n",
		pass ? "PASS" : "FAIL", r.maxDelta, r.mae, r.over, r.total, label.c_str());
	if (listOver && !r.overList.empty())
	{
		printf("  exceeding pixels (first %zu):", r.overList.size());
		for (size_t i = 0; i < r.overList.size(); ++i)
			printf(" (%d,%d)", r.overList[i].first, r.overList[i].second);
		printf("\n");
	}
	return pass ? 0 : 2;
}

// ---------------------------------------------------------------------------
// Directory helpers (compare matching leaf filenames across two dirs).
// ---------------------------------------------------------------------------
bool isDir(const std::string &p)
{
#ifdef _WIN32
	DWORD a = GetFileAttributesA(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
	return false;
#endif
}

std::vector<std::string> listPNGs(const std::string &dir)
{
	std::vector<std::string> out;
#ifdef _WIN32
	WIN32_FIND_DATAA fd;
	std::string glob = dir + "\\*.png";
	HANDLE h = FindFirstFileA(glob.c_str(), &fd);
	if (h != INVALID_HANDLE_VALUE)
	{
		do { out.push_back(fd.cFileName); } while (FindNextFileA(h, &fd));
		FindClose(h);
	}
#endif
	std::sort(out.begin(), out.end());
	return out;
}

bool loadImage(const std::string &path, Image &img)
{
	std::vector<uint8_t> bytes;
	if (!readFile(path, bytes))
	{
		fprintf(stderr, "error: cannot read %s\n", path.c_str());
		return false;
	}
	std::string err;
	// Dispatch on magic bytes, not extension: PPM (framedumps) vs PNG (F9 shots).
	bool ok = (bytes.size() >= 2 && bytes[0] == 'P' && bytes[1] == '6')
		? decodePPM(bytes, img, err)
		: decodePNG(bytes, img, err);
	if (!ok)
	{
		fprintf(stderr, "error: decode %s: %s\n", path.c_str(), err.c_str());
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Exclusion masks (--mask x,y,w,h; repeatable). Masked rects are blacked out
// in BOTH images before diffing, so per-construction different regions (FPS/
// wall-clock overlays) stop polluting cross-backend numbers. Zeroing both
// sides keeps the pixel totals and mae denominator unchanged and honest:
// masked pixels contribute exactly 0 delta.
// ---------------------------------------------------------------------------
struct MaskRect { int x, y, w, h; };
std::vector<MaskRect> gMasks;

void applyMasks(Image &img)
{
	for (size_t m = 0; m < gMasks.size(); ++m)
	{
		const MaskRect &r = gMasks[m];
		for (int y = r.y; y < r.y + r.h; ++y)
		{
			if (y < 0 || y >= img.height) continue;
			for (int x = r.x; x < r.x + r.w; ++x)
			{
				if (x < 0 || x >= img.width) continue;
				size_t p = ((size_t)y * img.width + x) * 3;
				img.rgb[p] = img.rgb[p + 1] = img.rgb[p + 2] = 0;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Synthetic self-test - the tool's own green oracle. Builds image pairs with
// KNOWN deltas, round-trips them through real on-disk PNGs (the game's format),
// decodes, diffs, and asserts. This validates the DIFF TOOL only; the real
// DX8-vs-D3D11 game A/B is deferred to step 10 (see README).
// ---------------------------------------------------------------------------
Image solid(int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
	Image img;
	img.width = w; img.height = h;
	img.rgb.resize((size_t)w * h * 3);
	for (size_t p = 0; p < img.pixels(); ++p)
	{
		img.rgb[p * 3 + 0] = r;
		img.rgb[p * 3 + 1] = g;
		img.rgb[p * 3 + 2] = b;
	}
	return img;
}

Image gradient(int w, int h)
{
	Image img;
	img.width = w; img.height = h;
	img.rgb.resize((size_t)w * h * 3);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			size_t p = (size_t)y * w + x;
			img.rgb[p * 3 + 0] = (uint8_t)(x * 255 / (w > 1 ? w - 1 : 1));
			img.rgb[p * 3 + 1] = (uint8_t)(y * 255 / (h > 1 ? h - 1 : 1));
			img.rgb[p * 3 + 2] = (uint8_t)((x + y) & 0xff);
		}
	return img;
}

int gTestsRun = 0, gTestsFailed = 0;

// Grep-visible negative-control plant flag. When true, --selftest also runs the
// negative-control case that MUST go red. Left false in the committed tree so the
// default self-test is green; flip via --with-negcontrol (README documents it).
bool gNegControl = false;

// checkEq: assert an integer verdict field equals expected; log RED on miss.
void checkEq(const char *what, long got, long expected)
{
	++gTestsRun;
	bool ok = (got == expected);
	if (!ok) ++gTestsFailed;
	printf("  [%s] %-28s got=%ld expected=%ld\n", ok ? "OK" : "FAIL", what, got, expected);
}

int runSelfTest(const std::string &tmpDir)
{
	printf("=== w3d_parity_diff --selftest (round-trip through real PNG files) ===\n");
	const int W = 64, H = 48;
	std::string pA = tmpDir + "/parity_selftest_a.png";
	std::string pB = tmpDir + "/parity_selftest_b.png";

	// Case 1: identical images -> PASS, maxdelta=0, over=0.
	{
		Image a = gradient(W, H);
		if (!encodePNG(pA, a) || !encodePNG(pB, a))
			{ printf("  [FAIL] encode case1\n"); ++gTestsFailed; }
		Image da, db;
		if (loadImage(pA, da) && loadImage(pB, db))
		{
			DiffResult r = diffImages(da, db, 0, 16);
			printf("case1 identical: ");
			reportVerdict("(selftest)", r, false);
			checkEq("case1.dimsMatch", r.dimsMatch ? 1 : 0, 1);
			checkEq("case1.maxdelta", r.maxDelta, 0);
			checkEq("case1.over", (long)r.over, 0);
		}
	}

	// Case 2: differ by exactly 1 in one channel at one pixel -> maxdelta=1, over=1.
	{
		Image a = gradient(W, H);
		Image b = a;
		int px = 10, py = 5;
		size_t idx = ((size_t)py * W + px) * 3 + 1;	// green channel
		b.rgb[idx] = (uint8_t)((b.rgb[idx] + 1) & 0xff);	// +1 with wrap guard
		if (a.rgb[idx] == b.rgb[idx]) b.rgb[idx] = (uint8_t)(a.rgb[idx] - 1);	// ensure delta 1
		encodePNG(pA, a); encodePNG(pB, b);
		Image da, db;
		if (loadImage(pA, da) && loadImage(pB, db))
		{
			DiffResult r = diffImages(da, db, 0, 16);
			printf("case2 one-pixel-+1: ");
			reportVerdict("(selftest)", r, true);
			checkEq("case2.maxdelta", r.maxDelta, 1);
			checkEq("case2.over(tol0)", (long)r.over, 1);
			// The same pair under tol>=1 must PASS (epsilon tolerance model).
			DiffResult r2 = diffImages(da, db, 1, 16);
			checkEq("case2.over(tol1)", (long)r2.over, 0);
		}
	}

	// Case 3: fully inverted image (black vs white) -> maxdelta=255, over=all.
	{
		Image a = solid(W, H, 0, 0, 0);
		Image b = solid(W, H, 255, 255, 255);	// full 0<->255 inversion
		encodePNG(pA, a); encodePNG(pB, b);
		Image da, db;
		if (loadImage(pA, da) && loadImage(pB, db))
		{
			DiffResult r = diffImages(da, db, 0, 16);
			printf("case3 inverted: ");
			reportVerdict("(selftest)", r, false);
			checkEq("case3.maxdelta", r.maxDelta, 255);
			checkEq("case3.over", (long)r.over, (long)r.total);
		}
	}

	// Case 4: dimension mismatch -> FAIL (dimsMatch=0).
	{
		Image a = solid(8, 8, 0, 0, 0);
		Image b = solid(8, 9, 0, 0, 0);
		DiffResult r = diffImages(a, b, 0, 16);
		printf("case4 dim-mismatch: ");
		reportVerdict("(selftest)", r, false);
		checkEq("case4.dimsMatch", r.dimsMatch ? 1 : 0, 0);
	}

	// Case 5: PPM round-trip - write the gradient as a binary P6 (the format
	// the in-engine framedumps emit), load it back, and diff against the same
	// image loaded from PNG. Must be pixel-identical (both formats lossless).
	{
		Image a = gradient(W, H);
		std::string pP = tmpDir + "/parity_selftest_a.ppm";
		FILE *f = fopen(pP.c_str(), "wb");
		bool wrote = false;
		if (f != nullptr)
		{
			fprintf(f, "P6\n%d %d\n255\n", a.width, a.height);
			wrote = fwrite(a.rgb.data(), 1, a.rgb.size(), f) == a.rgb.size();
			fclose(f);
		}
		if (!wrote) { printf("  [FAIL] write case5 ppm\n"); ++gTestsFailed; }
		encodePNG(pA, a);
		Image dp, dn;
		if (loadImage(pP, dp) && loadImage(pA, dn))
		{
			DiffResult r = diffImages(dp, dn, 0, 16);
			printf("case5 ppm-roundtrip: ");
			reportVerdict("(selftest)", r, false);
			checkEq("case5.dimsMatch", r.dimsMatch ? 1 : 0, 1);
			checkEq("case5.maxdelta", r.maxDelta, 0);
			checkEq("case5.over", (long)r.over, 0);
		}
	}

	// Case 6: --mask semantics. Plant a KNOWN difference inside a rect; with
	// the mask covering it the diff must read clean, and WITHOUT the mask the
	// same pair must fail (the case's own falsifier - proves the mask excluded
	// the difference rather than the difference never existing).
	{
		Image a = gradient(W, H);
		Image b = a;
		for (int y = 4; y < 12; ++y)
			for (int x = 8; x < 24; ++x)
			{
				size_t p = ((size_t)y * W + x) * 3;
				b.rgb[p] = (uint8_t)(255 - b.rgb[p]);
			}
		Image am = a, bm = b;
		DiffResult rNoMask = diffImages(a, b, 0, 16);
		checkEq("case6.unmasked-over>0", rNoMask.over > 0 ? 1 : 0, 1);
		gMasks.push_back(MaskRect());
		gMasks.back().x = 8; gMasks.back().y = 4; gMasks.back().w = 16; gMasks.back().h = 8;
		applyMasks(am);
		applyMasks(bm);
		gMasks.pop_back();
		DiffResult rMask = diffImages(am, bm, 0, 16);
		printf("case6 mask-excludes-planted-diff: ");
		reportVerdict("(selftest)", rMask, false);
		checkEq("case6.masked-maxdelta", rMask.maxDelta, 0);
		checkEq("case6.masked-over", (long)rMask.over, 0);
	}

	// --- NEGATIVE CONTROL --------------------------------------------------
	// Feed two KNOWN-DIFFERENT images (case3's inverted pair) but ASSERT the
	// verdict is PASS/maxdelta=0. This MUST make the self-test go RED. It
	// proves the assertion machinery actually detects differences rather than
	// rubber-stamping. Guarded so normal runs stay green; flip on with
	// --with-negcontrol (or the grep-visible PARITY_NEGCONTROL plant below).
	if (gNegControl)
	{
		printf("case-NC negative-control (EXPECTED RED): ");
		Image a = solid(W, H, 0, 0, 0);
		Image b = solid(W, H, 255, 255, 255);
		DiffResult r = diffImages(a, b, 0, 16);
		reportVerdict("(selftest-negcontrol)", r, false);
		checkEq("NC.maxdelta(WRONG-expect-0)", r.maxDelta, 0);	// deliberately wrong -> RED
		checkEq("NC.over(WRONG-expect-0)", (long)r.over, 0);		// deliberately wrong -> RED
	}

	printf("=== selftest: %d checks, %d failed ===\n", gTestsRun, gTestsFailed);
	printf("SELFTEST %s\n", gTestsFailed == 0 ? "PASS" : "FAIL");
	return gTestsFailed == 0 ? 0 : 1;
}

void usage()
{
	printf(
		"w3d_parity_diff - PNG frame-diff parity oracle (RENDERER_PORT.md step 10)\n"
		"\n"
		"Usage:\n"
		"  w3d_parity_diff <refA.png> <newB.png> [options]   compare two frames\n"
		"  w3d_parity_diff <dirA> <dirB> [options]           compare matching *.png\n"
		"  Inputs may be game PNGs (F9 shots) or binary P6 PPMs (backend framedumps).\n"
		"  w3d_parity_diff --selftest [--tmp <dir>]          run synthetic oracle\n"
		"\n"
		"Options:\n"
		"  --tol N          per-pixel max-channel tolerance (default 0 = exact)\n"
		"  --mask x,y,w,h   black out a rect in BOTH images before diffing\n"
		"                   (repeatable; for FPS/wall-clock overlay exclusion)\n"
		"  --list N         list up to N pixels exceeding tolerance (default 16)\n"
		"  --diff-out P     write amplified delta visualization PNG\n"
		"  --amp N          delta amplification for --diff-out (default 8)\n"
		"  --with-negcontrol  (selftest) run the RED negative-control case\n"
		"\n"
		"Verdict:  PARITY <PASS|FAIL> maxdelta=N mae=X.XX over=K/total\n"
		"Exit:     0 = PASS, 2 = FAIL, 1 = selftest failure / usage error\n"
		"\n"
		"Tolerance model: same-backend determinism -> --tol 0 (exact). Cross-backend\n"
		"DX8-vs-D3D11 -> a small epsilon (e.g. --tol 2..4) absorbs rounding in the\n"
		"combiner/blend emulation. Pick per the A/B procedure in the README.\n");
}

} // namespace

int main(int argc, char **argv)
{
	std::vector<std::string> pos;
	int tol = 0, amp = 8;
	size_t listCap = 16;
	bool selftest = false;
	// Selftest scratch files default to the OS temp dir, not the CWD - running
	// --selftest from the repo root used to litter parity_selftest_*.{png,ppm}
	// into the tree (Spooky's step-8 probe had to clean them up). --tmp still
	// overrides.
	std::string diffOut, tmpDir = ".";
	{
		const char * t = getenv("TEMP");
		if (t == nullptr || t[0] == '\0') t = getenv("TMP");
		if (t != nullptr && t[0] != '\0') tmpDir = t;
	}

	for (int i = 1; i < argc; ++i)
	{
		std::string a = argv[i];
		if (a == "--selftest") selftest = true;
		else if (a == "--with-negcontrol") gNegControl = true;
		else if (a == "--tol" && i + 1 < argc) tol = atoi(argv[++i]);
		else if (a == "--list" && i + 1 < argc) listCap = (size_t)atoi(argv[++i]);
		else if (a == "--amp" && i + 1 < argc) amp = atoi(argv[++i]);
		else if (a == "--diff-out" && i + 1 < argc) diffOut = argv[++i];
		else if (a == "--tmp" && i + 1 < argc) tmpDir = argv[++i];
		else if (a == "--mask" && i + 1 < argc)
		{
			MaskRect m;
			if (sscanf(argv[++i], "%d,%d,%d,%d", &m.x, &m.y, &m.w, &m.h) != 4)
			{
				fprintf(stderr, "bad --mask (want x,y,w,h): %s\n", argv[i]);
				return 1;
			}
			gMasks.push_back(m);
		}
		else if (a == "-h" || a == "--help") { usage(); return 0; }
		else if (!a.empty() && a[0] == '-') { fprintf(stderr, "unknown option: %s\n", a.c_str()); usage(); return 1; }
		else pos.push_back(a);
	}

	if (selftest)
		return runSelfTest(tmpDir);

	if (pos.size() != 2)
	{
		usage();
		return 1;
	}

	// Directory mode: compare matching leaf filenames.
	if (isDir(pos[0]) && isDir(pos[1]))
	{
		std::vector<std::string> files = listPNGs(pos[0]);
		if (files.empty())
		{
			fprintf(stderr, "error: no *.png in %s\n", pos[0].c_str());
			return 1;
		}
		int worst = 0;
		for (size_t i = 0; i < files.size(); ++i)
		{
			Image a, b;
			std::string fa = pos[0] + "/" + files[i];
			std::string fb = pos[1] + "/" + files[i];
			if (!loadImage(fa, a) || !loadImage(fb, b)) { worst = 2; continue; }
			applyMasks(a);
			applyMasks(b);
			DiffResult r = diffImages(a, b, tol, listCap);
			int rc = reportVerdict(files[i], r, listCap > 0);
			if (rc > worst) worst = rc;
		}
		printf("PARITY-DIR %s (%zu frames)\n", worst == 0 ? "PASS" : "FAIL", files.size());
		return worst;
	}

	// Single-pair mode.
	Image a, b;
	if (!loadImage(pos[0], a) || !loadImage(pos[1], b))
		return 2;
	applyMasks(a);
	applyMasks(b);
	DiffResult r = diffImages(a, b, tol, listCap);
	if (!diffOut.empty())
		writeDiffViz(diffOut, a, b, amp);
	return reportVerdict(pos[0] + " vs " + pos[1], r, listCap > 0);
}
