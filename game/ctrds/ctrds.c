#include <common.h>
#include <platform/native_renderer.h>

// CTR-DS companion HUD: layout data and the few helpers the UI code calls.
//
// Coordinates are absolute framebuffer coordinates. The game view occupies
// rows 0..CTRDS_GAME_HEIGHT; the companion panel is everything below it, so a
// panel-relative Y of 10 is written here as 216 + 10 = 226.

// Panel-relative origin. CTRDS_INLINE renders the companion over the normal
// 512x216 frame (origin 0) so the layout can be verified on a desktop build.
// CTRDS_SECOND_SCREEN will place it at its own origin once the companion has a
// render target of its own -- see the VRAM note in ctrds.h.
#define CTRDS_PANEL_TOP 0

// Retail anchors that the companion values are re-derived from.
enum CtrdsRetailAnchors
{
	// UI_RenderFrame_Racing draws the live map with its bottom-right corner here.
	CTRDS_RETAIL_MAP_X = 500,
	CTRDS_RETAIL_MAP_Y = 195,

	// UI_MAP_ICON_Y_OFFSET, applied by UI_Map_GetIconPos to every dot.
	CTRDS_MAP_ICON_Y_OFFSET = 0x10,
};

struct CtrdsLayout g_ctrds = {
    .mode = CTRDS_SECOND_SCREEN,
    .tallFramebuffer = 0,
    .fbHeight = CTRDS_FB_HEIGHT,
    .fbPitch = CTRDS_FB_PITCH,

    .screenW = 512,
    .screenH = CTRDS_PANEL_HEIGHT,

    // --- top strip: time, item, fruit, lap ---
    .clockX = 20,
    .clockY = CTRDS_PANEL_TOP + 12,

    // --- speedometer backdrop; needle position lives in the slot table ---
    // Retail draws the backdrop at (480, 190) and the needle at (414, 145), so
    // the backdrop keeps its +66/+45 offset from the needle.
    .speedBgX = 372 + 66,
    .speedBgY = CTRDS_PANEL_TOP + 372 + 45,

    // --- left column: the full eight-driver order, not retail's top four ---
    .rankIconX = 22,
    .rankIconBaseY = CTRDS_PANEL_TOP + 96,
    .rankSlotH = 40,
    .rankVisible = 8,
    .rankTextX = 66,
    .rankTextStartY = CTRDS_PANEL_TOP + 100,
    .rankIconScale = CTRDS_FP_ONE + (CTRDS_FP_ONE / 2),

    // --- live map: big, anchored to the bottom-right corner of the panel ---
    .mapX = 412,
    .mapY = CTRDS_PANEL_TOP + 352,
    .mapScale = CTRDS_FP_ONE * 3,
    .mapIconScale = CTRDS_FP_ONE * 2,

    .mapRetailX = CTRDS_RETAIL_MAP_X,
    .mapRetailY = CTRDS_RETAIL_MAP_Y,
};

// Companion replacement for data.hud_1P_P1. Slots the companion does not use
// (battle, relic, adventure rewards) keep their retail values.
#define CTRDS_HUD_BLOCK                                                                                          \
    /* 0x00 WEAPON           */ {200, CTRDS_PANEL_TOP + 14, 0, 4096},                                                    \
    /* 0x01 LAP_COUNT        */ {466, CTRDS_PANEL_TOP + 16, 0, 0},                                                       \
    /* 0x02 BIG1             */ {436, CTRDS_PANEL_TOP + 196, 256, 5120},                                                  \
    /* 0x03 FRUIT_MODEL      */ {330, CTRDS_PANEL_TOP + 24, 512, 4096},                                                  \
    /* 0x04 WUMPA_COUNT      */ {350, CTRDS_PANEL_TOP + 16, 0, 0},                                                       \
    /* 0x05 RANK             */ {476, CTRDS_PANEL_TOP + 178, 0, 0},                                                       \
    /* 0x06 JUMP_METER       */ {449, CTRDS_PANEL_TOP + 433, 0, 0},                                                       \
    /* 0x07 (unused)         */ {475, 164, 0, 0},                                                                        \
    /* 0x08 SLIDE_METER      */ {448, CTRDS_PANEL_TOP + 433, 0, 0},                                                       \
    /* 0x09 SPEEDOMETER      */ {372, CTRDS_PANEL_TOP + 372, 0, 4096},                                                    \
    /* 0x0a (unused)         */ {20, 57, 0, 4096},                                                                       \
    /* 0x0b BATTLE_WEAPON_BG */ {209, -5, 0, 4096},                                                                      \
    /* 0x0c RACING_WEAPON_BG */ {254, CTRDS_PANEL_TOP + 18, 0, 2457},                                                    \
    /* 0x0d BATTLE_SCORE     */ {454, 8, 0, 0},                                                                          \
    /* 0x0e RELIC            */ {50, 24, 256, 1536},                                                                     \
    /* 0x0f KEY              */ {256, 24, 512, 3072},                                                                    \
    /* 0x10 TROPHY           */ {406, 24, 512, 6144},                                                                    \
    /* 0x11 CRYSTAL          */ {389, 30, 512, 2048},                                                                    \
    /* 0x12 TOKEN_OR_CTR     */ {145, 30, 512, 2048},                                                                    \
    /* 0x13 TIMEBOX          */ {200, 30, 256, 768},

struct UiElement2D g_ctrdsHud1P[UI_HUD_SLOT_COUNT * CTRDS_HUD_BLOCKS] = {
    CTRDS_HUD_BLOCK CTRDS_HUD_BLOCK CTRDS_HUD_BLOCK CTRDS_HUD_BLOCK CTRDS_HUD_BLOCK CTRDS_HUD_BLOCK CTRDS_HUD_BLOCK CTRDS_HUD_BLOCK
};

// Live-map scale is only applied while the companion map is being built, so
// the track-select and adventure-hub maps keep drawing at 1:1.
internal s16 s_ctrdsMapScale = CTRDS_FP_ONE;

s16 Ctrds_MapScale(void)
{
	return s_ctrdsMapScale;
}

s16 Ctrds_MapIconScale(void)
{
	return Ctrds_Enabled() ? g_ctrds.mapIconScale : CTRDS_FP_ONE;
}

void Ctrds_BeginMapScale(void)
{
	if (Ctrds_Enabled())
	{
		s_ctrdsMapScale = g_ctrds.mapScale;
	}
}

void Ctrds_EndMapScale(void)
{
	s_ctrdsMapScale = CTRDS_FP_ONE;
}

s16 Ctrds_ApplyScale(s16 value, s16 scale)
{
	return (s16)(((int)value * (int)scale) >> 12);
}

void Ctrds_ScaleMap(struct UIMap *dst, const struct UIMap *src)
{
	const int scale = (int)g_ctrds.mapScale;

	*dst = *src;

	if (!Ctrds_Enabled())
	{
		return;
	}

	// UI_Map_GetIconPos places a dot at
	//   x = iconStartX + (worldX * iconSizeX) / worldRangeX
	//   y = iconStartY + (worldZ * iconSizeY * 2) / worldRangeY - ICON_Y_OFFSET
	//
	// Scaling iconSize by s scales the offset term by s, so re-anchoring the
	// start point about the retail map corner reproduces
	//   p' = newAnchor + s * (p - retailAnchor)
	// exactly, and every driver, ghost and tracking dot lands on the enlarged
	// background without its own draw code changing.
	dst->iconSizeX = (s16)(((int)src->iconSizeX * scale) >> 12);
	dst->iconSizeY = (s16)(((int)src->iconSizeY * scale) >> 12);

	dst->iconStartX = (s16)((int)g_ctrds.mapX + ((((int)src->iconStartX - (int)g_ctrds.mapRetailX) * scale) >> 12));

	dst->iconStartY =
	    (s16)((int)g_ctrds.mapY + (((((int)src->iconStartY - CTRDS_MAP_ICON_Y_OFFSET) - (int)g_ctrds.mapRetailY) * scale) >> 12) + CTRDS_MAP_ICON_Y_OFFSET);
}

void Ctrds_InitFromEnv(void)
{
	const char *mode = getenv("CTRDS");
	const char *tall = getenv("CTRDS_TALL_FB");

	if (mode != NULL)
	{
		g_ctrds.mode = mode[0] - '0';
		if ((g_ctrds.mode < CTRDS_DISABLED) || (g_ctrds.mode > CTRDS_SECOND_SCREEN))
		{
			g_ctrds.mode = CTRDS_DISABLED;
		}
	}

	if (tall != NULL)
	{
		g_ctrds.tallFramebuffer = (tall[0] == '1');
	}

	{
		const char *h = getenv("CTRDS_FB_H");
		const char *pitch = getenv("CTRDS_FB_P");
		if (h != NULL)
		{
			g_ctrds.fbHeight = (s16)atoi(h);
		}
		if (pitch != NULL)
		{
			g_ctrds.fbPitch = (s16)atoi(pitch);
		}
	}

	Platform_Log("[CTR-DS] mode=%d tallFB=%d fb=%dx%d pitch=%d panel=%dx%d\n", g_ctrds.mode, g_ctrds.tallFramebuffer, 512, g_ctrds.fbHeight,
	        g_ctrds.fbPitch, g_ctrds.screenW, g_ctrds.screenH);
}

void Ctrds_DrawCompanionPass(struct GameTracker *gGT)
{
	// The UI region is the head of the shared ordering table: ClearOTagR builds
	// one reversed chain, pushBuffer_UI.ptrOT sits at entry 1, and the camera
	// tables start at entry 6. Entries 5..0 are therefore the UI, drawn last in
	// the normal walk. Drawing that sub-chain on its own gives the panel exactly
	// the HUD and map, with no 3D.
	uint32_t *uiChainHead = &gGT->pushBuffer_UI.ptrOT[4];
	uint32_t *otBase = gGT->pushBuffer_UI.ptrOT - 1;

	// DrawOTag would otherwise open the frame itself and bind the game view.
	Platform_BeginScene();

	NativeRenderer_BeginCompanionTarget(CTRDS_PANEL_W, CTRDS_PANEL_H);
	DrawOTag(uiChainHead);
	NativeRenderer_EndCompanionTarget(CTRDS_VRAM_PANEL_X, CTRDS_VRAM_PANEL_Y);

	// Empty the UI buckets so the main pass renders a HUD-free game view. The
	// prims stay in frame memory, they are simply no longer linked.
	ClearOTagR(otBase, 6);
}
