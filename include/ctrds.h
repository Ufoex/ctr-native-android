#ifndef CTR_NATIVE_CTRDS_H
#define CTR_NATIVE_CTRDS_H

// CTR-DS: dual-screen companion HUD.
//
// Relocates the glanceable race HUD (clock, item, fruit, lap, rank list,
// position, speedometer, meters and an enlarged live map) into a separate
// companion viewport sized for the AYN Thor's bottom panel.
//
// Every coordinate the companion uses lives in g_ctrds / g_ctrdsHud1P so the
// layout can be retuned without touching draw code. With CTRDS_DISABLED the
// values are the retail ones and the game renders exactly as it always did.

// 12-bit fixed point, matching UI_MAP_ICON_SCALE / UI_RANK_ICON_SCALE.
#define CTRDS_FP_ONE 0x1000

// Framebuffer geometry.
//
// Retail draws a 512x216 buffer. With the companion enabled each buffer is
// tall enough to hold that game view plus the companion panel directly below
// it, so a single draw environment covers both screens and the frame is
// presented as two rectangles instead of one.
//
// 512x446 is 1.148:1 -- the AYN Thor's bottom panel (1240x1080) exactly.
#define CTRDS_GAME_HEIGHT  216
#define CTRDS_PANEL_HEIGHT CTRDS_PANEL_H
#define CTRDS_FB_HEIGHT    (CTRDS_GAME_HEIGHT + CTRDS_PANEL_HEIGHT)

// Distance between the two buffers in VRAM. Must be >= CTRDS_FB_HEIGHT.
#define CTRDS_FB_PITCH 1024

// Companion panel, rendered into its own GL target.
//
// It cannot live in a VRAM band: PSX draw-env packets encode VRAM Y in 9 bits
// (0..511), so a second framebuffer above y=511 silently draws at y & 0x1ff --
// measured as a black screen at pitch 1024 and a correct one at pitch 296. Two
// double-buffered 512x446 regions do not fit under y=512 either. So the panel
// gets a native render target, and is packed into VRAM afterwards purely so
// presenting it is an ordinary blit; that pack is a native GL call and is not
// subject to the packet limit.
#define CTRDS_PANEL_W 512

// PS1 art is authored for a 512x216 buffer shown at 4:3, so one display pixel
// is 0.5625 as wide as it is tall and every sprite is stretched 1.78x
// vertically. The panel reproduces that by being 251 rows tall and filling the
// 1240x1080 screen: (1240/512) / (1080/251) = 0.5625. At the old 446 the ratio
// was 1.0 -- no vertical stretch at all -- which is why every icon looked
// squashed and the rank portraits needed a hand-applied 1.5x to look right.
#define CTRDS_PANEL_H 251

// The panel is divided into fixed regions so that nothing can overlap: every
// element below is placed inside exactly one of them, and the map is scaled to
// fit its own region rather than carrying a hand-tuned constant.
//
//   +-----------------------------------------------+ 0
//   | time        weapon      fruit           lap    |
//   +--------+--------------------+-----------------+ 44
//   | rank   |        map         |   speedometer   |
//   | list   |                    +-----------------+ 148
//   |        |                    |   position      |
//   +--------+--------------------+-----------------+ 251
//   0       104                  336               512
#define CTRDS_TOP_H    44
#define CTRDS_BODY_Y   CTRDS_TOP_H
#define CTRDS_BODY_H   (CTRDS_PANEL_H - CTRDS_BODY_Y)

#define CTRDS_RANK_X   0
#define CTRDS_RANK_W   104

#define CTRDS_MAP_RX   CTRDS_RANK_W
#define CTRDS_MAP_RW   232

#define CTRDS_RIGHT_X  (CTRDS_MAP_RX + CTRDS_MAP_RW)
#define CTRDS_RIGHT_W  (CTRDS_PANEL_W - CTRDS_RIGHT_X)

#define CTRDS_SPEEDO_Y CTRDS_BODY_Y
#define CTRDS_SPEEDO_H 104
#define CTRDS_NUM_Y    (CTRDS_SPEEDO_Y + CTRDS_SPEEDO_H)
#define CTRDS_NUM_H    (CTRDS_PANEL_H - CTRDS_NUM_Y)

// The panel's shape on screen, independent of how many source rows it holds:
// 512 x 446 is 1.148:1, the AYN Thor's bottom panel (1240x1080) exactly. The
// desktop preview lays the panel out at this ratio while sampling the real
// CTRDS_PANEL_H rows, so the preview shows the same proportions as the device
// instead of square pixels.
#define CTRDS_PANEL_LAYOUT_H 446

// Breathing room around the map inside its region. The panel stretches Y by
// 1.78x, so equal margins on screen need a smaller number vertically: 10 x 2.42
// and 6 x 4.30 are both about 24 physical pixels.
#define CTRDS_MAP_MARGIN_X 10
#define CTRDS_MAP_MARGIN_Y 3

// Transparent rows along the bottom of the map art (see Ctrds_FitMapToRegion).
#define CTRDS_MAP_ART_PAD_Y 9

// Where the finished panel is parked in VRAM. Needs VRAM_HEIGHT > 1470.
#define CTRDS_VRAM_PANEL_X 0
#define CTRDS_VRAM_PANEL_Y 1024

enum CtrdsMode
{
	CTRDS_DISABLED = 0,

	// Companion elements drawn into the normal framebuffer at companion
	// coordinates. Used to tune the layout on a desktop build.
	CTRDS_INLINE = 1,

	// Companion elements drawn into their own viewport for a second display.
	CTRDS_SECOND_SCREEN = 2,
};

struct CtrdsLayout
{
	int mode;

	// When set, each framebuffer is grown to hold the game view plus the
	// companion panel. Cleared, the companion coordinates still apply but
	// anything below the game view is clipped -- useful for isolating layout
	// problems from framebuffer problems.
	int tallFramebuffer;

	// Framebuffer geometry actually used when tallFramebuffer is set.
	// Runtime so it can be tuned without a rebuild (CTRDS_FB_H / CTRDS_FB_P).
	s16 fbHeight;
	s16 fbPitch;

	// Companion viewport, in the game's 512-wide HUD space.
	// 512x446 is 1.148:1, the Thor bottom panel (1240x1080) exactly.
	s16 screenW;
	s16 screenH;

	// Race clock. Retail hardcodes (0x14, 8) in UI_RenderFrame_Racing.
	s16 clockX;
	s16 clockY;

	// Speedometer backdrop. Retail hardcodes (480, 190) in UI_DrawSpeedBG;
	// the needle itself comes from the HUD slot table.
	s16 speedBgX;
	s16 speedBgY;

	// Ranked-driver column. Retail values are an enum in UI_Rank.c, and
	// retail only ever shows the top 4 of 8.
	s16 rankIconX;
	s16 rankIconBaseY;
	s16 rankSlotH;
	s16 rankVisible;
	s16 rankTextX;
	s16 rankTextStartY;
	s16 rankIconScale; // retail is FP(1); the panel has room for larger portraits

	// Live map. Retail hardcodes the (500, 195) anchor in
	// UI_RenderFrame_Racing and draws the bitmap 1:1 with its texture.
	// The anchor is the map's bottom-right corner.
	s16 mapX;
	s16 mapY;
	s16 mapScale;     // background + dot spread
	s16 mapIconScale; // dot glyphs only; keep low so they stay crisp

	// Retail anchor, needed to re-derive dot positions after a rescale.
	s16 mapRetailX;
	s16 mapRetailY;

	// Widen the 3D frustum to 16:9 instead of stretching the 4:3 image.
	int widescreen;

	// VSYNCs the frame loop waits before flipping. Retail waits 2, which is
	// 30fps on a 60Hz panel; 1 flips every VSYNC so the rate follows the
	// display -- 60Hz gives 60fps, the Thor's 120Hz mode gives 120. Physics are
	// delta-timed off gGT->elapsedTimeMS and stay correct at any rate; timers
	// counted in frames do not, and are scaled separately.
	int vsyncsPerFlip;

	// Multiplies the emulated PS1 VBlank rate. The platform paces VBlanks from
	// NTSC video timing (~59.817Hz) in software, independent of the panel, so
	// even flipping every VBlank caps at ~60fps. 2 emits them twice as fast for
	// ~120fps. Everything that matters downstream is delta-timed.
	int vblankMultiplier;

	// When set, vblankMultiplier tracks the panel's actual refresh rate instead
	// of a fixed value. The panel rate is not ours to choose: this device's
	// system service resets its own 60Hz cap back under us, so the multiplier
	// has to follow the panel at runtime rather than be decided at startup.
	int vblankAuto;

	// Post-processing on the presented image. FXAA runs at source resolution,
	// where it costs almost nothing; the CRT effect rides along in the upscale
	// pass. Both are set from ctrds.cfg beside the assets, since Android gives
	// an app no way to read an environment variable.
	int fxaa;

	// CRT-Royale-style beam simulation on the presented image.
	int crt;

	// Swaps the face buttons: cross<->circle and square<->triangle, i.e. A/B and
	// X/Y on an Xbox-labelled pad like the Thor's.
	int swapFaceButtons;

	// On-screen controls: 0 automatic (shown only with no pad), 1 always, 2 never.
	int touchControls;

	// Internal render resolution multiplier, 1..4. Geometry is drawn at this
	// multiple of the PS1 display size; textures stay native.
	int internalScale;

};

extern struct CtrdsLayout g_ctrds;

// Companion replacement for data.hud_1P_P1. Same 20 slots, same meaning.
//
// Eight identical blocks, not one: UI_INSTANCE walks this table with
// `hudStruct += UI_HUD_SLOT_COUNT` once per driver, so a single block would
// read off the end for drivers 1..7. Retail gets away with it because its 1P,
// 2P and 4P tables are contiguous in the data segment.
#define CTRDS_HUD_BLOCKS 8
extern struct UiElement2D g_ctrdsHud1P[UI_HUD_SLOT_COUNT * CTRDS_HUD_BLOCKS];

static inline int Ctrds_Enabled(void)
{
	return g_ctrds.mode != CTRDS_DISABLED;
}

static inline int Ctrds_TallFramebuffer(void)
{
	return (g_ctrds.mode != CTRDS_DISABLED) && (g_ctrds.tallFramebuffer != 0);
}

// 9/16 x 4/3 = 0.75. The view-projection's Y axis is already scaled so the
// 512x216 buffer reads as 4:3; narrowing X by this widens it to 16:9. Same
// factor the CTR-ModSDK 16BY9 mod uses.
#define CTRDS_WIDE_NUM 750
#define CTRDS_WIDE_DEN 1000

// True while a race is actually running. Menus, the adventure hub and the
// pre-race screens are not races.
int Ctrds_InRace(void);

// True on the main menu, where the panel offers the online switch.
int Ctrds_OnMainMenu(void);

// 0 auto, 1 always, 2 never.
int Ctrds_TouchControlsMode(void);

// Handles panel-only controls. Called once per frame from the game thread.
void Ctrds_PollPanelInput(void);

// Writes the current settings back to ctrds.cfg so panel changes persist.
void Ctrds_SaveConfig(void);

// A tap on the bottom screen, normalised 0..1 across the panel.
void Ctrds_PanelTap(float nx, float ny);

// Draws the settings list; returns the y it finished at.
int Ctrds_DrawSettingsList(uint32_t *head, int centreX, int topY);

// Main-screen settings, for devices with no second screen.
void Ctrds_DrawSettingsOnMainScreen(struct GameTracker *gGT);

// VBlanks to wait between flips. Racing runs as fast as the panel allows, but
// menus must not: menu animation is counted in frames rather than delta-timed,
// so an extra frame is an extra animation step and the whole front end plays
// fast. Holding retail's every-other-VBlank cadence there keeps menus at the
// 30fps they were authored for, and there is nothing to gain from a higher rate
// on screens that are mostly static anyway.
int Ctrds_VsyncsPerFlip(void);

static inline int Ctrds_VBlankMultiplier(void)
{
	return (g_ctrds.vblankMultiplier > 0) ? g_ctrds.vblankMultiplier : 1;
}

// Feeds the measured panel refresh rate in. Safe to call every frame.
void Ctrds_UpdateAutoVBlank(float panelHz);

// Derives the map scale and anchor from the grid above. Call once at startup.
void Ctrds_InitLayout(void);

// Reads ctrds.cfg from the asset directory, if present. Must run after the
// asset directory is known.
void Ctrds_LoadConfig(void);

// Called when the platform reports that no second display exists. The panel has
// nowhere to go, so the HUD goes back on the main screen and the game looks like
// the unmodified port rather than a letterboxed split.
void Ctrds_DisableSecondScreen(void);

// OnlineCTR connection settings, read from ctrds.cfg.
struct CtrdsOnlineConfig
{
	int enabled;
	char host[128];
	int port;
	char name[16];

	// Room to join automatically once the room list arrives, or -1 to stay in
	// the lobby and wait to be told.
	int room;
};

const struct CtrdsOnlineConfig *Ctrds_OnlineConfig(void);

static inline int Ctrds_Fxaa(void)
{
	return Ctrds_Enabled() && (g_ctrds.fxaa != 0);
}

static inline int Ctrds_Crt(void)
{
	return Ctrds_Enabled() && (g_ctrds.crt != 0);
}

static inline int Ctrds_SwapFaceButtons(void)
{
	return g_ctrds.swapFaceButtons != 0;
}

// True on frames where the frame-counted half of the game is allowed to step.
// Level objects -- barrels, carts, platforms, mines -- advance by a fixed amount
// per frame instead of by elapsed time, so above 30fps they run fast. Drivers,
// camera and HUD are delta-timed and keep running every frame.
int Ctrds_BucketRunsThisFrame(int bucket);

// Scales the live map to its region and centres it there, from the actual
// dimensions of the track's own map textures. Call before the map is drawn.
struct Icon;
void Ctrds_FitMapToRegion(const struct Icon *mapTop, const struct Icon *mapBottom);


static inline int Ctrds_Widescreen(void)
{
	return g_ctrds.widescreen != 0;
}

static inline int Ctrds_SecondScreen(void)
{
	return g_ctrds.mode == CTRDS_SECOND_SCREEN;
}

// Reads CTRDS (0 off, 1 inline, 2 second screen) and CTRDS_TALL_FB (0/1).
void Ctrds_InitFromEnv(void);

// Renders the UI ordering table into the companion panel, then empties those OT
// buckets so the main pass draws a HUD-free game view. Call once per frame,
// immediately before the frame's DrawOTag.
void Ctrds_DrawCompanionPass(struct GameTracker *gGT);

// Registers the companion's own ordering table with the native GPU link
// tokeniser. Must run alongside the frame's other range registrations.
void Ctrds_RegisterGpuRanges(void);

// Scale currently applied to live-map geometry. CTRDS_FP_ONE outside the
// companion map draw, so the track-select and adventure maps stay 1:1.
// Converts a duration written in 30fps frames into frames at the rate actually
// being rendered. Physics and race timing are delta-timed off elapsedTimeMS and
// need no help, but anything counted in frames -- weapon cooldowns, banner
// threads, spawn rates -- would elapse proportionally faster without this.
// Derived from the measured frame time, so it is correct at 60 and at 120
// rather than assuming a fixed doubling the way CTR-ModSDK's FPS_DOUBLE does.
int Ctrds_ScaleFrames(int frames30);

// True on the subset of frames that corresponds to retail's 30fps cadence.
// Effects that spawn once per frame -- exhaust, tyre smoke, sparks -- would
// otherwise emit proportionally more at a higher rate and look like a different
// game. CTR-ModSDK gates the same way with an explicit timer&1 / timer&2.
int Ctrds_Is30HzTick(void);

s16 Ctrds_MapScale(void);

// Scale for the dots drawn on the live map. Separate from the background scale
// so the icons can stay crisp while the bitmap stretches.
s16 Ctrds_MapIconScale(void);
void Ctrds_BeginMapScale(void);
void Ctrds_EndMapScale(void);

// Applies a scaled dimension: (value * scale) >> 12, rounded toward zero.
s16 Ctrds_ApplyScale(s16 value, s16 scale);

// Builds a scaled copy of a level's UIMap so driver, ghost and tracking dots
// land on the enlarged background without touching their own draw code.
void Ctrds_ScaleMap(struct UIMap *dst, const struct UIMap *src);

#endif
