#include <common.h>
#include <platform/native_renderer.h>
#include <platform/native_gpu_links.h>

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

    .screenW = CTRDS_PANEL_W,
    .screenH = CTRDS_PANEL_H,

    // --- top strip: time, item, fruit, lap ---
    .clockX = 14,
    .clockY = 6,

    // --- speedometer: upper half of the right column. Retail draws the
    // backdrop at (480,190) and the needle at (414,145), so the backdrop keeps
    // its +66/+45 offset from the needle. ---
    .speedBgX = 398 + 66,
    .speedBgY = 60 + 45,

    // --- left column: the full eight-driver order, not retail's top four.
    // No vertical fudge any more: the panel itself now stretches art 1.78x. ---
    .rankIconX = 16,
    .rankIconBaseY = CTRDS_BODY_Y + 6,
    .rankSlotH = 25,
    .rankVisible = 8,
    .rankTextX = 54,
    .rankTextStartY = CTRDS_BODY_Y + 8,
    // Eight portraits share the column, so trim them slightly to stop the
    // taller ones touching their neighbours.
    .rankIconScale = (CTRDS_FP_ONE * 7) / 8,

    // --- live map: centre column. Scale and anchor are computed from the
    // region by Ctrds_InitLayout, so changing the grid re-fits the map. ---
    .mapX = CTRDS_MAP_RX + CTRDS_MAP_RW,
    .mapY = CTRDS_BODY_Y + CTRDS_BODY_H,
    .mapScale = CTRDS_FP_ONE,
    .mapIconScale = CTRDS_FP_ONE * 2,

    .widescreen = 1,
    .vsyncsPerFlip = 1,
    .vblankMultiplier = 1,
    .vblankAuto = 1,
    .fxaa = 0,
    .crt = 1,
    .swapFaceButtons = 1,

    .mapRetailX = CTRDS_RETAIL_MAP_X,
    .mapRetailY = CTRDS_RETAIL_MAP_Y,
};

// Companion replacement for data.hud_1P_P1. Slots the companion does not use
// (battle, relic, adventure rewards) keep their retail values.
#define CTRDS_HUD_BLOCK                                                                                          \
    /* 0x00 WEAPON           */ {200, 10, 0, 4096},                                                              \
    /* 0x01 LAP_COUNT        */ {466, 10, 0, 0},                                                                 \
    /* 0x02 BIG1             */ {410, CTRDS_NUM_Y + 52, 256, 5530},                               \
    /* 0x03 FRUIT_MODEL      */ {330, 18, 512, 4096},                                                            \
    /* 0x04 WUMPA_COUNT      */ {350, 10, 0, 0},                                                                 \
    /* 0x05 RANK             */ {472, CTRDS_NUM_Y + 30, 0, 0},                                   \
    /* 0x06 JUMP_METER       */ {398 + 77, 60 + 61, 0, 0},                                 \
    /* 0x07 (unused)         */ {475, 164, 0, 0},                                                                \
    /* 0x08 SLIDE_METER      */ {398 + 76, 60 + 61, 0, 0},                                 \
    /* 0x09 SPEEDOMETER      */ {398, 60, 0, 4096},                                             \
    /* 0x0a (unused)         */ {20, 57, 0, 4096},                                                               \
    /* 0x0b BATTLE_WEAPON_BG */ {200 - 23, 10 - 10, 0, 4096},                                                              \
    /* 0x0c RACING_WEAPON_BG */ {330 - 30, 18 - 15, 0, 2457},                                                              \
    /* 0x0d BATTLE_SCORE     */ {454, 8, 0, 0},                                                                  \
    /* 0x0e RELIC            */ {50, 24, 256, 1536},                                                             \
    /* 0x0f KEY              */ {256, 24, 512, 3072},                                                            \
    /* 0x10 TROPHY           */ {406, 24, 512, 6144},                                                            \
    /* 0x11 CRYSTAL          */ {389, 30, 512, 2048},                                                            \
    /* 0x12 TOKEN_OR_CTR     */ {145, 30, 512, 2048},                                                            \
    /* 0x13 TIMEBOX          */ {200, 30, 256, 768},

struct UiElement2D g_ctrdsHud1P[UI_HUD_SLOT_COUNT * CTRDS_HUD_BLOCKS] = {
    CTRDS_HUD_BLOCK CTRDS_HUD_BLOCK CTRDS_HUD_BLOCK CTRDS_HUD_BLOCK CTRDS_HUD_BLOCK CTRDS_HUD_BLOCK CTRDS_HUD_BLOCK CTRDS_HUD_BLOCK
};

// Live-map scale is only applied while the companion map is being built, so
// the track-select and adventure-hub maps keep drawing at 1:1.
internal s16 s_ctrdsMapScale = CTRDS_FP_ONE;

// 0x20 is the elapsed-time value a 30fps frame produces. Never scale below 1x:
// a slow frame should not shorten cooldowns.
#define CTRDS_RETAIL_FRAME_MS 0x20

int Ctrds_ScaleFrames(int frames30)
{
	int ms;

	if (!Ctrds_Enabled())
	{
		return frames30;
	}

	ms = sdata->gGT->elapsedTimeMS;

	if (ms < 4)
	{
		ms = 4;
	}
	if (ms > CTRDS_RETAIL_FRAME_MS)
	{
		ms = CTRDS_RETAIL_FRAME_MS;
	}

	return (frames30 * CTRDS_RETAIL_FRAME_MS) / ms;
}

int Ctrds_BucketRunsThisFrame(int bucket)
{
	if (!Ctrds_Enabled())
	{
		return 1;
	}

	switch (bucket)
	{
	// Delta-timed off elapsedTimeMS, so these are already correct at any frame
	// rate and need every frame to stay smooth.
	case PLAYER:
	case ROBOT:
	case GHOST:
	case TRACKING:
	case CAMERA:
	case HUD:
	case PAUSE:
		return 1;

	// Everything else steps by a fixed amount per frame -- barrels, carts,
	// platforms, mines, warp pads, the start banner -- and would run at two or
	// four times speed if it ran on every frame at 60 or 120fps.
	default:
		return Ctrds_Is30HzTick();
	}
}

int Ctrds_Is30HzTick(void)
{
	int step;

	if (!Ctrds_Enabled())
	{
		return 1;
	}

	// 1 at 30fps, 2 at 60, 4 at 120.
	step = Ctrds_ScaleFrames(1);
	if (step < 2)
	{
		return 1;
	}

	return ((int)sdata->gGT->timer % step) == 0;
}

int Ctrds_InRace(void)
{
	const struct GameTracker *gGT = sdata->gGT;

	// END_OF_RACE covers the results and podium screens, which keep the race HUD
	// flag set but are menus: their HUD and menu belong on the main screen.
	return ((gGT->hudFlags & HUD_FLAG_RACE_HUD) != 0) && ((gGT->gameMode1 & ADVENTURE_ARENA) == 0)
	    && ((gGT->gameMode1 & END_OF_RACE) == 0);
}

int Ctrds_VsyncsPerFlip(void)
{
	if (!Ctrds_Enabled())
	{
		return 2;
	}

	if (Ctrds_InRace())
	{
		return (g_ctrds.vsyncsPerFlip > 0) ? g_ctrds.vsyncsPerFlip : 2;
	}

	// Retail flips every 2nd VBlank at the stock rate. Scale by the multiplier
	// so the cadence stays 30fps however fast VBlanks are being emitted.
	return 2 * Ctrds_VBlankMultiplier();
}

void Ctrds_FitMapToRegion(const struct Icon *mapTop, const struct Icon *mapBottom)
{
	int baseW;
	int baseH;
	int drawsTopHalf;
	int scaleX;
	int scaleY;
	int scale;
	int w;
	int h;

	if (!Ctrds_Enabled() || (mapTop == NULL) || (mapBottom == NULL))
	{
		return;
	}

	// UI_Map_DrawMap builds the map from these two textures, anchored by their
	// right edge and bottom edge, so this is its true drawn extent. A fixed
	// guess cannot work: every track's map is a different size, which is why
	// the map sat low in its region with a gap above it.
	// UI_Map_DrawMap only draws the top half under this condition; counting it
	// when it is not drawn overestimates the height and pushes the map down,
	// leaving a gap above it.
	{
		struct GameTracker *gGT = sdata->gGT;
		struct UIMapSpawnMetadata *mapMetadata = NULL;

		if (gGT->level1->ptrSpawnType1 != 0)
		{
			void **pointers = ST1_GETPOINTERS(gGT->level1->ptrSpawnType1);
			mapMetadata = pointers[ST1_MAP];
		}

		drawsTopHalf = (((mapMetadata != NULL) && (mapMetadata->topHalfMode == 0)) || ((gGT->gameMode1 & MAIN_MENU) != 0));
	}

	baseW = (int)((u16)mapBottom->texLayout.u1 - (u16)mapBottom->texLayout.u0);
	baseH = (int)((u16)mapBottom->texLayout.v2 - (u16)mapBottom->texLayout.v0);

	if (drawsTopHalf)
	{
		baseH += (int)((u16)mapTop->texLayout.v2 - (u16)mapTop->texLayout.v0);
	}

	if ((baseW <= 0) || (baseH <= 0))
	{
		return;
	}

	scaleX = ((CTRDS_MAP_RW - (2 * CTRDS_MAP_MARGIN_X)) * CTRDS_FP_ONE) / baseW;
	scaleY = ((CTRDS_BODY_H - (2 * CTRDS_MAP_MARGIN_Y)) * CTRDS_FP_ONE) / baseH;
	scale = (scaleX < scaleY) ? scaleX : scaleY;

	// Sit at 90% of the fit so the map has some air around it rather than
	// running right up to the neighbouring regions. Centring below uses the
	// scaled size, so the margins grow evenly on all four sides.
	scale = (scale * 9) / 10;

	w = (baseW * scale) / CTRDS_FP_ONE;
	h = (baseH * scale) / CTRDS_FP_ONE;

	g_ctrds.mapScale = (s16)scale;
	g_ctrds.mapX = (s16)(CTRDS_MAP_RX + ((CTRDS_MAP_RW + w) / 2));
	// The map art carries a few rows of transparent padding along its bottom
	// edge, so centring the texture leaves the visible track sitting high in the
	// region. Measured at ~6 units; there is no way to see it from texLayout,
	// which describes the rect and not what is opaque inside it.
	g_ctrds.mapY = (s16)(CTRDS_BODY_Y + ((CTRDS_BODY_H + h) / 2) + CTRDS_MAP_ART_PAD_Y);
}

// Tiny key=value reader. Only the handful of switches worth changing without a
// rebuild; anything absent keeps its default.
internal struct CtrdsOnlineConfig s_onlineConfig = {
    0,
    "127.0.0.1",
    64001,
    "CTRDS",
    -1,
};

const struct CtrdsOnlineConfig *Ctrds_OnlineConfig(void)
{
	return &s_onlineConfig;
}

void Ctrds_LoadConfig(void)
{
	char path[1024];
	char line[128];
	FILE *f;

	const char *dir = NativeAssets_GetAssetDir();

	if ((dir == NULL) || (dir[0] == '\0'))
	{
		return;
	}

	snprintf(path, sizeof(path), "%s/ctrds.cfg", dir);

	f = fopen(path, "r");
	if (f == NULL)
	{
		// Write the defaults out so there is something to edit on the device.
		// The game creates it rather than adb pushing one in: a shell-owned file
		// in the app's own files directory makes the next install fail to stage
		// its assets.
		f = fopen(path, "w");
		if (f != NULL)
		{
			fprintf(f, "# CTR-DS settings. Edit and restart the game.\n");
			fprintf(f, "# fxaa: smooth jagged edges (0/1)\n");
			fprintf(f, "fxaa=%d\n", g_ctrds.fxaa);
			fprintf(f, "# crt: CRT-Royale-style scanlines, phosphor mask and halation (0/1)\n");
			fprintf(f, "crt=%d\n", g_ctrds.crt);
			fprintf(f, "# swap_face_buttons: swap A/B and X/Y (0/1)\n");
			fprintf(f, "swap_face_buttons=%d\n", g_ctrds.swapFaceButtons);
			fprintf(f, "# widescreen: 16:9 field of view (0/1)\n");
			fprintf(f, "widescreen=%d\n", g_ctrds.widescreen);
			fprintf(f, "# vblank_mult: 1 = up to 60fps, 2 = up to 120fps.\n");
			fprintf(f, "# Leave commented out to follow the panel automatically.\n");
			fprintf(f, "#vblank_mult=2\n");
			fprintf(f, "\n# --- OnlineCTR ---\n");
			fprintf(f, "# online: connect to an OnlineCTR server on launch (0/1)\n");
			fprintf(f, "online=%d\n", s_onlineConfig.enabled);
			fprintf(f, "online_host=%s\n", s_onlineConfig.host);
			fprintf(f, "online_port=%d\n", s_onlineConfig.port);
			fprintf(f, "online_name=%s\n", s_onlineConfig.name);
			fprintf(f, "# online_room: room to join automatically, -1 to wait\n");
			fprintf(f, "online_room=%d\n", s_onlineConfig.room);
			fclose(f);
		}

		return;
	}

	while (fgets(line, sizeof(line), f) != NULL)
	{
		char *eq = strchr(line, '=');
		int value;

		if ((line[0] == '#') || (eq == NULL))
		{
			continue;
		}

		*eq = '\0';
		value = atoi(eq + 1);

		if (strncmp(line, "fxaa", 4) == 0)
		{
			g_ctrds.fxaa = value;
		}
		else if (strncmp(line, "online_host", 11) == 0)
		{
			// value is text, not a number
			char *v = eq + 1;
			size_t n = strlen(v);
			while ((n > 0) && ((v[n - 1] == '\n') || (v[n - 1] == '\r') || (v[n - 1] == ' ')))
			{
				v[--n] = '\0';
			}
			snprintf(s_onlineConfig.host, sizeof(s_onlineConfig.host), "%s", v);
		}
		else if (strncmp(line, "online_room", 11) == 0)
		{
			s_onlineConfig.room = value;
		}
		else if (strncmp(line, "online_port", 11) == 0)
		{
			s_onlineConfig.port = value;
		}
		else if (strncmp(line, "online_name", 11) == 0)
		{
			char *v = eq + 1;
			size_t n = strlen(v);
			while ((n > 0) && ((v[n - 1] == '\n') || (v[n - 1] == '\r') || (v[n - 1] == ' ')))
			{
				v[--n] = '\0';
			}
			snprintf(s_onlineConfig.name, sizeof(s_onlineConfig.name), "%s", v);
		}
		else if (strncmp(line, "online", 6) == 0)
		{
			s_onlineConfig.enabled = value;
		}
		else if (strncmp(line, "crt", 3) == 0)
		{
			g_ctrds.crt = value;
		}
		else if (strncmp(line, "swap_face_buttons", 17) == 0)
		{
			g_ctrds.swapFaceButtons = value;
		}
		else if (strncmp(line, "widescreen", 10) == 0)
		{
			g_ctrds.widescreen = value;
		}
		else if (strncmp(line, "vblank_mult", 11) == 0)
		{
			g_ctrds.vblankMultiplier = value;
			g_ctrds.vblankAuto = 0;
		}
	}

	fclose(f);

	Platform_Log("[CTR-DS] ctrds.cfg: fxaa=%d crt=%d swapFaceButtons=%d widescreen=%d\n", g_ctrds.fxaa, g_ctrds.crt, g_ctrds.swapFaceButtons,
	        g_ctrds.widescreen);
}

void Ctrds_InitLayout(void)
{
	// The map's real scale and anchor come from the track's own map textures in
	// Ctrds_FitMapToRegion, which runs before it is drawn. These are only the
	// values used before the first race has loaded a map.
	g_ctrds.mapScale = CTRDS_FP_ONE;
	g_ctrds.mapX = (s16)(CTRDS_MAP_RX + CTRDS_MAP_RW);
	g_ctrds.mapY = (s16)(CTRDS_BODY_Y + CTRDS_BODY_H);
}

void Ctrds_UpdateAutoVBlank(float panelHz)
{
	int want;

	if (!g_ctrds.vblankAuto)
	{
		return;
	}

	// Emit VBlanks fast enough to feed the panel. Below 100Hz there is nothing
	// to gain: extra VBlanks would burn CPU on frames the panel never shows.
	want = (panelHz >= 100.0f) ? 2 : 1;

	if (want != g_ctrds.vblankMultiplier)
	{
		g_ctrds.vblankMultiplier = want;
		Platform_Log("[CTR-DS] panel %.1fHz -> vblankMult=%d\n", (double)panelHz, want);
	}
}

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
	Ctrds_InitLayout();

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

	{
		const char *flip = getenv("CTRDS_VSYNCS");
		if (flip != NULL)
		{
			g_ctrds.vsyncsPerFlip = atoi(flip);
		}
	}

	{
		const char *mult = getenv("CTRDS_VBLANK_MULT");
		if (mult != NULL)
		{
			g_ctrds.vblankMultiplier = atoi(mult);
			g_ctrds.vblankAuto = 0;
		}
	}

	{
		const char *wide = getenv("CTRDS_WIDE");
		if (wide != NULL)
		{
			g_ctrds.widescreen = (wide[0] == '1');
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

	Platform_Log("[CTR-DS] mode=%d panel=%dx%d widescreen=%d vsyncsPerFlip=%d vblankMult=%d\n", g_ctrds.mode, g_ctrds.screenW, g_ctrds.screenH,
	        g_ctrds.widescreen, g_ctrds.vsyncsPerFlip, g_ctrds.vblankMultiplier);
}

// The panel needs an ordering table of its own for the idle screen, because
// outside a race the UI table belongs to the top screen and must be left alone.
#define CTRDS_IDLE_OT_LEN 16
global_variable uint32_t s_ctrdsIdleOT[CTRDS_IDLE_OT_LEN];

void Ctrds_RegisterGpuRanges(void)
{
	NativeGpuLinks_RegisterRangeChecked("ctrds idle OT", s_ctrdsIdleOT, sizeof(s_ctrdsIdleOT));
}

// Outside a race the panel shows the game's wordmark on black rather than
// mirroring menus, which stay on the top screen where they belong.
internal void Ctrds_DrawIdlePanel(void)
{
	uint32_t *head = &s_ctrdsIdleOT[CTRDS_IDLE_OT_LEN - 1];

	ClearOTagR(s_ctrdsIdleOT, CTRDS_IDLE_OT_LEN);

	DecalFont_DrawLineOT("CTR", g_ctrds.screenW / 2, (g_ctrds.screenH / 2) - 20, FONT_BIG, JUSTIFY_CENTER, head);
	DecalFont_DrawLineOT("CRASH TEAM RACING", g_ctrds.screenW / 2, (g_ctrds.screenH / 2) + 16, FONT_SMALL, JUSTIFY_CENTER, head);

	DrawOTag(head);
}

void Ctrds_DrawCompanionPass(struct GameTracker *gGT)
{
	// The UI region is the head of the shared ordering table: ClearOTagR builds
	// one reversed chain, pushBuffer_UI.ptrOT sits at entry 1, and the camera
	// tables start at entry 6. Entries 5..0 are therefore the UI, drawn last in
	// the normal walk. Drawing that sub-chain on its own gives the panel exactly
	// the HUD and map, with no 3D.
	// Entry 5 holds 3D world geometry, not UI -- starting the walk there draws
	// the player's kart onto the panel. 4 is the top of the UI range.
	uint32_t *uiChainHead = &gGT->pushBuffer_UI.ptrOT[4];
	uint32_t *otBase = gGT->pushBuffer_UI.ptrOT - 1;

	// Only a race moves its HUD to the panel. Menus, the adventure hub and the
	// pre-race screens keep their HUD on the top screen, so outside a race the
	// UI table is left linked and the panel shows the idle screen instead.
	const int inRace = Ctrds_InRace();

	// DrawOTag would otherwise open the frame itself and bind the game view.
	Platform_BeginScene();

	NativeRenderer_BeginCompanionTarget(CTRDS_PANEL_W, CTRDS_PANEL_H);

	if (inRace)
	{
		DrawOTag(uiChainHead);
	}
	else
	{
		Ctrds_DrawIdlePanel();
	}

	NativeRenderer_EndCompanionTarget(CTRDS_VRAM_PANEL_X, CTRDS_VRAM_PANEL_Y);

	if (inRace)
	{
		// Empty the UI buckets so the main pass renders a HUD-free game view.
		// The prims stay in frame memory, they are simply no longer linked.
		ClearOTagR(otBase, 6);
	}
}
