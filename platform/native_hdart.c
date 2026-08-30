// Replacement art for the 2D HUD.
//
// The HUD is drawn as PSX textured quads queued into an ordering table that
// samples emulated VRAM, so there is no point in that list where a GL texture
// can be swapped in. Instead, a draw whose icon has a replacement is recorded
// here and skipped in the PSX list, then all recorded draws are rendered as GL
// quads into the same target once the list has been walked. The HUD is drawn
// last anyway, so nothing is lost by compositing it afterwards.
//
// A replacement is a PNG in <assets>/hd/ named for the icon's index in
// GameTracker::ptrIcons, e.g. hd/icon_42.png. Because these are real textures
// with real alpha, replaced art also composites cleanly -- no additive
// saturation the way the VRAM path needs for artwork with a black backing.

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "common.h"
#include "ctrds.h"

#define CTRDS_HD_ICON_COUNT 0x88
#define CTRDS_HD_MAX_QUADS  256

// 0 means "not looked at yet"; the sentinel means "looked, nothing there".
#define CTRDS_HD_NONE ((GLuint)0xFFFFFFFFu)

struct CtrdsHdQuad
{
	GLuint texture;
	float x0, y0, x1, y1;
	float r, g, b;
};

global_variable GLuint s_hdTexture[CTRDS_HD_ICON_COUNT];
global_variable struct CtrdsHdQuad s_hdQuads[CTRDS_HD_MAX_QUADS];
global_variable int s_hdQuadCount = 0;

global_variable GLuint s_hdShader = 0;
global_variable GLint s_hdProjLoc = -1;
global_variable GLint s_hdTintLoc = -1;
global_variable GLuint s_hdVao = 0;
global_variable GLuint s_hdVbo = 0;
global_variable int s_hdReady = 0;

global_variable const char *s_hdVertexSrc = "#version 330 core\n"
                                            "layout(location=0) in vec2 a_pos;\n"
                                            "layout(location=1) in vec2 a_uv;\n"
                                            "uniform mat4 u_proj;\n"
                                            "out vec2 v_uv;\n"
                                            "void main() {\n"
                                            "  v_uv = a_uv;\n"
                                            "  gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);\n"
                                            "}\n";

global_variable const char *s_hdFragmentSrc = "#version 330 core\n"
                                              "in vec2 v_uv;\n"
                                              "uniform sampler2D u_tex;\n"
                                              "uniform vec3 u_tint;\n"
                                              "out vec4 fragColor;\n"
                                              "void main() {\n"
                                              "  vec4 c = texture(u_tex, v_uv);\n"
                                              "  if (c.a < 0.01) { discard; }\n"
                                              "  fragColor = vec4(c.rgb * u_tint, c.a);\n"
                                              "}\n";

internal GLuint Ctrds_HdCompile(GLenum type, const char *src)
{
	GLuint shader = glCreateShader(type);
	GLint ok = 0;

	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);

	if (!ok)
	{
		char log[512];

		glGetShaderInfoLog(shader, sizeof(log), NULL, log);
		Platform_Log("[CTR-DS/hd] shader: %s\n", log);
		glDeleteShader(shader);
		return 0;
	}

	return shader;
}

internal void Ctrds_HdEnsureResources(void)
{
	GLuint vs;
	GLuint fs;

	if (s_hdReady)
	{
		return;
	}
	s_hdReady = 1;

	vs = Ctrds_HdCompile(GL_VERTEX_SHADER, s_hdVertexSrc);
	fs = Ctrds_HdCompile(GL_FRAGMENT_SHADER, s_hdFragmentSrc);

	if ((vs == 0) || (fs == 0))
	{
		return;
	}

	s_hdShader = glCreateProgram();
	glAttachShader(s_hdShader, vs);
	glAttachShader(s_hdShader, fs);
	glLinkProgram(s_hdShader);
	glDeleteShader(vs);
	glDeleteShader(fs);

	s_hdProjLoc = glGetUniformLocation(s_hdShader, "u_proj");
	s_hdTintLoc = glGetUniformLocation(s_hdShader, "u_tint");

	glGenVertexArrays(1, &s_hdVao);
	glGenBuffers(1, &s_hdVbo);

	glBindVertexArray(s_hdVao);
	glBindBuffer(GL_ARRAY_BUFFER, s_hdVbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void *)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void *)(sizeof(float) * 2));
	glBindVertexArray(0);
}

// Tries once per icon and remembers the answer, so a missing file costs one
// failed open for the whole run rather than one per frame.
internal GLuint Ctrds_HdTexture(int index)
{
	char path[1024];
	SDL_Surface *surface;
	SDL_Surface *rgba;
	GLuint texture = 0;

	const char *dir = NativeAssets_GetAssetDir();

	if ((index < 0) || (index >= CTRDS_HD_ICON_COUNT))
	{
		return CTRDS_HD_NONE;
	}

	if (s_hdTexture[index] != 0)
	{
		return s_hdTexture[index];
	}

	if ((dir == NULL) || (dir[0] == '\0'))
	{
		return CTRDS_HD_NONE;
	}

	snprintf(path, sizeof(path), "%s/hd/icon_%d.png", dir, index);

	surface = SDL_LoadPNG(path);
	if (surface == NULL)
	{
		s_hdTexture[index] = CTRDS_HD_NONE;
		return CTRDS_HD_NONE;
	}

	rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(surface);

	if (rgba == NULL)
	{
		s_hdTexture[index] = CTRDS_HD_NONE;
		return CTRDS_HD_NONE;
	}

	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	Platform_Log("[CTR-DS/hd] icon %d replaced (%dx%d)\n", index, rgba->w, rgba->h);
	SDL_DestroySurface(rgba);

	s_hdTexture[index] = texture;
	return texture;
}

int Ctrds_HdIconIndex(const struct Icon *icon)
{
	struct GameTracker *gGT = sdata->gGT;
	int i;

	if ((icon == NULL) || (gGT == NULL))
	{
		return -1;
	}

	for (i = 0; i < CTRDS_HD_ICON_COUNT; i++)
	{
		if (gGT->ptrIcons[i] == icon)
		{
			return i;
		}
	}

	return -1;
}

int Ctrds_HdQueue(const struct Icon *icon, int x, int y, int w, int h, unsigned color)
{
	GLuint texture;
	struct CtrdsHdQuad *q;

	const int index = Ctrds_HdIconIndex(icon);

	if (index < 0)
	{
		return 0;
	}

	// Authoring aid: with hd_log=1 in ctrds.cfg, every icon index the HUD draws
	// is reported once, which is how you find out that (say) icon 42 is the
	// portrait you want to replace. Without it there is no way to know.
	if (g_ctrds.hdLog)
	{
		local_persist unsigned char reported[CTRDS_HD_ICON_COUNT];

		if (!reported[index])
		{
			reported[index] = 1;
			Platform_Log("[CTR-DS/hd] icon index %d drawn (%dx%d)\n", index, w, h);
		}
	}

	texture = Ctrds_HdTexture(index);
	if ((texture == 0) || (texture == CTRDS_HD_NONE))
	{
		return 0;
	}

	if (s_hdQuadCount >= CTRDS_HD_MAX_QUADS)
	{
		return 0;
	}

	q = &s_hdQuads[s_hdQuadCount++];
	q->texture = texture;
	q->x0 = (float)x;
	q->y0 = (float)y;
	q->x1 = (float)(x + w);
	q->y1 = (float)(y + h);

	// The PSX vertex colour is a modulate around 0x80, so 0x80 means unchanged.
	q->r = (float)((color >> 0) & 0xFF) / 128.0f;
	q->g = (float)((color >> 8) & 0xFF) / 128.0f;
	q->b = (float)((color >> 16) & 0xFF) / 128.0f;

	return 1;
}

void Ctrds_HdFlush(int width, int height)
{
	int i;
	float proj[16];

	if ((s_hdQuadCount == 0) || (width <= 0) || (height <= 0))
	{
		s_hdQuadCount = 0;
		return;
	}

	Ctrds_HdEnsureResources();

	if (s_hdShader == 0)
	{
		s_hdQuadCount = 0;
		return;
	}

	// Ortho in PSX units with Y downwards, matching the coordinates the HUD is
	// laid out in, so a queued rectangle lands exactly where its quad would.
	memset(proj, 0, sizeof(proj));
	proj[0] = 2.0f / (float)width;
	proj[5] = -2.0f / (float)height;
	proj[10] = -1.0f;
	proj[12] = -1.0f;
	proj[13] = 1.0f;
	proj[15] = 1.0f;

	glUseProgram(s_hdShader);
	glUniformMatrix4fv(s_hdProjLoc, 1, GL_FALSE, proj);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glBindVertexArray(s_hdVao);
	glBindBuffer(GL_ARRAY_BUFFER, s_hdVbo);
	glActiveTexture(GL_TEXTURE0);

	for (i = 0; i < s_hdQuadCount; i++)
	{
		const struct CtrdsHdQuad *q = &s_hdQuads[i];

		const float verts[24] = {
		    q->x0, q->y0, 0.0f, 0.0f, q->x1, q->y0, 1.0f, 0.0f, q->x1, q->y1, 1.0f, 1.0f,
		    q->x0, q->y0, 0.0f, 0.0f, q->x1, q->y1, 1.0f, 1.0f, q->x0, q->y1, 0.0f, 1.0f,
		};

		glBindTexture(GL_TEXTURE_2D, q->texture);
		glUniform3f(s_hdTintLoc, q->r, q->g, q->b);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
		glDrawArrays(GL_TRIANGLES, 0, 6);
	}

	glBindVertexArray(0);
	glUseProgram(0);

	s_hdQuadCount = 0;

	// The renderer caches what it last bound; it has no idea this pass ran.
	NativeRenderer_InvalidateStateCache();
}
