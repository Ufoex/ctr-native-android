/*
 * Native Texture Mod System for CTR Native Port
 * Supports texture dumping and HD texture replacement via Hash lookup
 */

#ifndef NATIVE_TEXTURE_MOD_H
#define NATIVE_TEXTURE_MOD_H

#include <macros.h>
#include <platform/native_renderer_types.h>
#include <psx/libgpu.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	u32 hash;
	TextureID textureId;
	s32 width;
	s32 height;
} CustomTextureEntry;

// Public API
void NativeTextureMod_Init(void);
void NativeTextureMod_Shutdown(void);

// Toggle Dump Mode (e.g. F9 key or config)
void NativeTextureMod_SetDumpEnabled(b32 enabled);
b32 NativeTextureMod_IsDumpEnabled(void);

// Toggle Custom Textures Replacement
void NativeTextureMod_SetReplacementEnabled(b32 enabled);
b32 NativeTextureMod_IsReplacementEnabled(void);

// Reload replacement textures from disk
void NativeTextureMod_ReloadTextures(void);

// Calculate texture hash for a given primitive region in VRAM
u32 NativeTextureMod_CalculateHash(s16 tpage, s16 clut, u8 uMin, u8 vMin, u8 uMax, u8 vMax);

// Lookup replacement texture for a given primitive
CustomTextureEntry *NativeTextureMod_GetReplacement(s16 tpage, s16 clut, u8 uMin, u8 vMin, u8 uMax, u8 vMax);

// Dump texture region to disk if dumping is enabled
void NativeTextureMod_DumpTexture(s16 tpage, s16 clut, u8 uMin, u8 vMin, u8 uMax, u8 vMax);

// Decode VRAM texture region to RGBA8888 buffer
b32 NativeTextureMod_DecodeVRAMRegion(s16 tpage, s16 clut, u8 uMin, u8 vMin, u8 uMax, u8 vMax, u32 *outRgba, s32 *outW, s32 *outH);

#ifdef __cplusplus
}
#endif

#endif // NATIVE_TEXTURE_MOD_H
