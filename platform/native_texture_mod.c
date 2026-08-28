/*
 * Native Texture Mod System for CTR Native Port
 * Implementation of Texture Dumping and HD Texture Replacement
 */

#include <SDL3/SDL.h>

#include <macros.h>
#include "platform/native_texture_mod.h"
#include "platform/native_log.h"
#include "platform/native_renderer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_JPEG
#include "../externals/SDL/src/video/stb_image.h"

#include "platform/native_glad.h"

#define NATIVE_TEXMOD_LOG(fmt, ...) Platform_Log("[CTR TexMod] " fmt, ##__VA_ARGS__)

#define MAX_CUSTOM_TEXTURES 4096

#if defined(__ANDROID__)
#define DUMP_DIR            "/sdcard/crash/textura/dump"
#define MOD_DIR             "/sdcard/crash/textura/load"
#else
#define DUMP_DIR            "crash/textura/dump"
#define MOD_DIR             "crash/textura/load"
#endif

extern struct NativeVramState s_vram;

global_variable b32 s_dumpEnabled = false;
global_variable b32 s_replacementEnabled = true;

global_variable CustomTextureEntry s_customTextures[MAX_CUSTOM_TEXTURES];
global_variable s32 s_customTextureCount = 0;

// Set of dumped hashes in current session to prevent redundant I/O
#define MAX_DUMPED_HASHES 8192
global_variable u32 s_dumpedHashes[MAX_DUMPED_HASHES];
global_variable s32 s_dumpedHashCount = 0;

static void EnsureDirExists(const char *path)
{
	char tmp[512];
	char *p = NULL;
	size_t len;

	snprintf(tmp, sizeof(tmp), "%s", path);
	len = strlen(tmp);
	if (len == 0)
		return;

	if (tmp[len - 1] == '/')
		tmp[len - 1] = 0;

	for (p = tmp + 1; *p; p++)
	{
		if (*p == '/')
		{
			*p = 0;
			SDL_CreateDirectory(tmp);
			*p = '/';
		}
	}
	SDL_CreateDirectory(tmp);
}

static b32 IsHashAlreadyDumped(u32 hash)
{
	for (s32 i = 0; i < s_dumpedHashCount; i++)
	{
		if (s_dumpedHashes[i] == hash)
			return true;
	}
	return false;
}

static void RecordDumpedHash(u32 hash)
{
	if (s_dumpedHashCount < MAX_DUMPED_HASHES)
	{
		s_dumpedHashes[s_dumpedHashCount++] = hash;
	}
}

static b32 WriteBMPFile(const char *filename, const u32 *pixels, s32 w, s32 h)
{
	FILE *f = fopen(filename, "wb");
	if (!f)
	{
		EnsureDirExists(DUMP_DIR);
		f = fopen(filename, "wb");
		if (!f)
			return false;
	}

	u32 filesize = 54 + 4 * w * h;
	u8 bmpfileheader[14] = {'B', 'M', 0, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0};
	u8 bmpinfoheader[40] = {40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

	bmpfileheader[2] = (u8)(filesize);
	bmpfileheader[3] = (u8)(filesize >> 8);
	bmpfileheader[4] = (u8)(filesize >> 16);
	bmpfileheader[5] = (u8)(filesize >> 24);

	bmpinfoheader[4] = (u8)(w);
	bmpinfoheader[5] = (u8)(w >> 8);
	bmpinfoheader[6] = (u8)(w >> 16);
	bmpinfoheader[7] = (u8)(w >> 24);
	bmpinfoheader[8] = (u8)(h);
	bmpinfoheader[9] = (u8)(h >> 8);
	bmpinfoheader[10] = (u8)(h >> 16);
	bmpinfoheader[11] = (u8)(h >> 24);

	fwrite(bmpfileheader, 1, 14, f);
	fwrite(bmpinfoheader, 1, 40, f);

	for (s32 y = h - 1; y >= 0; y--)
	{
		for (s32 x = 0; x < w; x++)
		{
			u32 px = pixels[y * w + x];
			u8 a = (px >> 24) & 0xFF;
			u8 r = (px >> 16) & 0xFF;
			u8 g = (px >> 8) & 0xFF;
			u8 b = (px) & 0xFF;
			u8 bgra[4] = {b, g, r, a};
			fwrite(bgra, 1, 4, f);
		}
	}

	fclose(f);
	return true;
}

b32 NativeTextureMod_DecodeVRAMRegion(s16 tpage, s16 clut, u8 uMin, u8 vMin, u8 uMax, u8 vMax, u32 *outRgba, s32 *outW, s32 *outH)
{
	if (uMax < uMin || vMax < vMin)
		return false;

	s32 w = (uMax - uMin) + 1;
	s32 h = (vMax - vMin) + 1;
	if (w <= 0 || h <= 0 || w > 256 || h > 256)
		return false;

	*outW = w;
	*outH = h;

	s32 mode = (tpage >> 7) & 0x3;
	s32 pageX = (tpage & 0x0F) * 64;
	s32 pageY = (tpage & 0x10) ? 256 : 0;

	s32 clutX = (clut & 0x3F) * 16;
	s32 clutY = (clut >> 6);

	for (s32 y = 0; y < h; y++)
	{
		s32 v = vMin + y;
		s32 vramY = (pageY + v) & (VRAM_HEIGHT - 1);

		for (s32 x = 0; x < w; x++)
		{
			s32 u = uMin + x;
			u32 rgba = 0;

			if (mode == 0) // 4-bit CLUT
			{
				s32 vramX = (pageX + (u / 4)) & (VRAM_WIDTH - 1);
				u16 word = s_vram.cpuPixels[vramY * VRAM_WIDTH + vramX];
				s32 shift = (u % 4) * 4;
				s32 idx = (word >> shift) & 0x0F;

				s32 cX = (clutX + idx) & (VRAM_WIDTH - 1);
				s32 cY = clutY & (VRAM_HEIGHT - 1);
				u16 cWord = s_vram.cpuPixels[cY * VRAM_WIDTH + cX];

				u8 r = (cWord & 0x1F) << 3;
				u8 g = ((cWord >> 5) & 0x1F) << 3;
				u8 b = ((cWord >> 10) & 0x1F) << 3;
				u8 a = (cWord == 0) ? 0 : 255;
				rgba = (a << 24) | (r << 16) | (g << 8) | b;
			}
			else if (mode == 1) // 8-bit CLUT
			{
				s32 vramX = (pageX + (u / 2)) & (VRAM_WIDTH - 1);
				u16 word = s_vram.cpuPixels[vramY * VRAM_WIDTH + vramX];
				s32 idx = (u % 2 == 0) ? (word & 0xFF) : (word >> 8);

				s32 cX = (clutX + idx) & (VRAM_WIDTH - 1);
				s32 cY = clutY & (VRAM_HEIGHT - 1);
				u16 cWord = s_vram.cpuPixels[cY * VRAM_WIDTH + cX];

				u8 r = (cWord & 0x1F) << 3;
				u8 g = ((cWord >> 5) & 0x1F) << 3;
				u8 b = ((cWord >> 10) & 0x1F) << 3;
				u8 a = (cWord == 0) ? 0 : 255;
				rgba = (a << 24) | (r << 16) | (g << 8) | b;
			}
			else // 16-bit RGB555
			{
				s32 vramX = (pageX + u) & (VRAM_WIDTH - 1);
				u16 cWord = s_vram.cpuPixels[vramY * VRAM_WIDTH + vramX];

				u8 r = (cWord & 0x1F) << 3;
				u8 g = ((cWord >> 5) & 0x1F) << 3;
				u8 b = ((cWord >> 10) & 0x1F) << 3;
				u8 a = (cWord == 0) ? 0 : 255;
				rgba = (a << 24) | (r << 16) | (g << 8) | b;
			}

			outRgba[y * w + x] = rgba;
		}
	}

	return true;
}

u32 NativeTextureMod_CalculateHash(s16 tpage, s16 clut, u8 uMin, u8 vMin, u8 uMax, u8 vMax)
{
	s32 w, h;
	u32 pixels[256 * 256];

	if (!NativeTextureMod_DecodeVRAMRegion(tpage, clut, uMin, vMin, uMax, vMax, pixels, &w, &h))
	{
		return 0;
	}

	// FNV-1a Hash
	u32 hash = 2166136261u;
	u32 count = w * h;

	for (u32 i = 0; i < count; i++)
	{
		u32 p = pixels[i];
		hash ^= (p & 0xFF);
		hash *= 16777619u;
		hash ^= ((p >> 8) & 0xFF);
		hash *= 16777619u;
		hash ^= ((p >> 16) & 0xFF);
		hash *= 16777619u;
		hash ^= ((p >> 24) & 0xFF);
		hash *= 16777619u;
	}

	return hash;
}

void NativeTextureMod_DumpTexture(s16 tpage, s16 clut, u8 uMin, u8 vMin, u8 uMax, u8 vMax)
{
	if (!s_dumpEnabled)
		return;

	u32 hash = NativeTextureMod_CalculateHash(tpage, clut, uMin, vMin, uMax, vMax);
	if (hash == 0 || IsHashAlreadyDumped(hash))
		return;

	s32 w, h;
	u32 pixels[256 * 256];
	if (!NativeTextureMod_DecodeVRAMRegion(tpage, clut, uMin, vMin, uMax, vMax, pixels, &w, &h))
		return;

	char filepath[512];
	snprintf(filepath, sizeof(filepath), "%s/0x%08X.bmp", DUMP_DIR, hash);

	if (WriteBMPFile(filepath, pixels, w, h))
	{
		NATIVE_TEXMOD_LOG("Dumped texture 0x%08X (%dx%d) -> %s", hash, w, h, filepath);
		RecordDumpedHash(hash);
	}
	else
	{
		NATIVE_TEXMOD_LOG("FAILED to dump texture 0x%08X -> %s", hash, filepath);
	}
}

static GLuint CreateGLTextureFromRGBA(const u8 *pixels, s32 w, s32 h)
{
	GLuint texId = 0;
	glGenTextures(1, &texId);
	glBindTexture(GL_TEXTURE_2D, texId);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	return texId;
}

static void LoadTextureFile(const char *filename)
{
	// Extract hash from filename e.g. "0x1A2B3C4D.png" or "1A2B3C4D.bmp"
	const char *basename = strrchr(filename, '/');
	if (!basename)
		basename = strrchr(filename, '\\');
	basename = basename ? basename + 1 : filename;

	u32 hash = 0;
	if (sscanf(basename, "0x%x", &hash) != 1 && sscanf(basename, "%x", &hash) != 1)
	{
		return;
	}

	size_t fileLen = 0;
	void *fileData = SDL_LoadFile(filename, &fileLen);
	if (!fileData || fileLen == 0)
	{
		return;
	}

	int w = 0, h = 0, comp = 0;
	stbi_uc *pixels = stbi_load_from_memory((const stbi_uc *)fileData, (int)fileLen, &w, &h, &comp, 4);
	SDL_free(fileData);

	if (!pixels)
	{
		return;
	}

	GLuint texId = CreateGLTextureFromRGBA(pixels, w, h);
	stbi_image_free(pixels);

	if (texId != 0 && s_customTextureCount < MAX_CUSTOM_TEXTURES)
	{
		s_customTextures[s_customTextureCount].hash = hash;
		s_customTextures[s_customTextureCount].textureId = texId;
		s_customTextures[s_customTextureCount].width = w;
		s_customTextures[s_customTextureCount].height = h;
		s_customTextureCount++;

		NATIVE_TEXMOD_LOG("Loaded replacement texture 0x%08X (%dx%d) [GL ID %u]", hash, w, h, texId);
	}
}

void NativeTextureMod_ReloadTextures(void)
{
	for (s32 i = 0; i < s_customTextureCount; i++)
	{
		if (s_customTextures[i].textureId != 0)
		{
			glDeleteTextures(1, &s_customTextures[i].textureId);
		}
	}
	s_customTextureCount = 0;

	EnsureDirExists(MOD_DIR);

	SDL_PathInfo info;
	if (!SDL_GetPathInfo(MOD_DIR, &info))
	{
		return;
	}

	int count = 0;
	char **files = SDL_GlobDirectory(MOD_DIR, "*.*", SDL_GLOB_CASEINSENSITIVE, &count);
	if (files)
	{
		for (int i = 0; i < count; i++)
		{
			char fullpath[512];
			snprintf(fullpath, sizeof(fullpath), "%s/%s", MOD_DIR, files[i]);
			LoadTextureFile(fullpath);
		}
		SDL_free(files);
	}
}

void NativeTextureMod_Init(void)
{
	EnsureDirExists(DUMP_DIR);
	EnsureDirExists(MOD_DIR);

	NativeTextureMod_ReloadTextures();
	NATIVE_TEXMOD_LOG("Initialized texture mod system (Loaded %d custom textures from %s)", s_customTextureCount, MOD_DIR);
}

void NativeTextureMod_Shutdown(void)
{
	for (s32 i = 0; i < s_customTextureCount; i++)
	{
		if (s_customTextures[i].textureId != 0)
		{
			glDeleteTextures(1, &s_customTextures[i].textureId);
		}
	}
	s_customTextureCount = 0;
}

void NativeTextureMod_SetDumpEnabled(b32 enabled)
{
	s_dumpEnabled = enabled;
	if (enabled)
	{
		EnsureDirExists(DUMP_DIR);
	}
	NATIVE_TEXMOD_LOG("Texture Dump %s -> %s", enabled ? "ENABLED" : "DISABLED", DUMP_DIR);
}

b32 NativeTextureMod_IsDumpEnabled(void)
{
	return s_dumpEnabled;
}

void NativeTextureMod_SetReplacementEnabled(b32 enabled)
{
	s_replacementEnabled = enabled;
}

b32 NativeTextureMod_IsReplacementEnabled(void)
{
	return s_replacementEnabled;
}

CustomTextureEntry *NativeTextureMod_GetReplacement(s16 tpage, s16 clut, u8 uMin, u8 vMin, u8 uMax, u8 vMax)
{
	if (!s_replacementEnabled || s_customTextureCount == 0)
		return NULL;

	u32 hash = NativeTextureMod_CalculateHash(tpage, clut, uMin, vMin, uMax, vMax);
	if (hash == 0)
		return NULL;

	for (s32 i = 0; i < s_customTextureCount; i++)
	{
		if (s_customTextures[i].hash == hash)
		{
			return &s_customTextures[i];
		}
	}

	return NULL;
}
