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
#define CTRDS_PANEL_HEIGHT 446
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
#define CTRDS_PANEL_H 446

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

static inline int Ctrds_VsyncsPerFlip(void)
{
	return (g_ctrds.vsyncsPerFlip > 0) ? g_ctrds.vsyncsPerFlip : 2;
}

static inline int Ctrds_VBlankMultiplier(void)
{
	return (g_ctrds.vblankMultiplier > 0) ? g_ctrds.vblankMultiplier : 1;
}

// Feeds the measured panel refresh rate in. Safe to call every frame.
void Ctrds_UpdateAutoVBlank(float panelHz);

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
