# Handoff: Hor+ ultrawide work — fixed and verified on both Linux desktop and Android device

Context dump for continuing this work. Rewritten 2026-08-10 (second pass, different
machine — `ufoex`, CachyOS, real 3440x1440 ultrawide monitor + a dedicated 4K OLED second
monitor available, both used for testing this session; Android toolchain (JDK 17, SDK,
NDK r27, CMake 3.22.1) installed fresh this session at `~/Android/Sdk`, and the user's
Odin 2 Portal (Android 13, Adreno 740, arm64 device that also supports armeabi-v7a) used for
on-device verification over USB).

## FOURTH PASS (2026-08-11, same day, different machine) — found and fixed the real "doesn't fill the screen" bug (boot-time-only Hor+ width); found and reverted a real architectural dead end for the texture-corruption bug

**Fixed and kept**: `platform/native_platform.c`'s `Platform_Init` now sizes and positions the
boot window from `SDL_GetPrimaryDisplay()`'s own usable bounds instead of the hardcoded
`800x600`/`1280x720`. (Mid-session this was briefly pinned to this machine's laptop panel by
connector name, `eDP-1`, at the user's request while testing on a specific monitor - reverted
that hardcoded name back out before committing, since it's meaningless on any other machine;
use `SDL_SetWindowPosition` after creation if a future session needs to target a specific
monitor again.) Root cause this closes: `MainMain.c`'s `StateZero` computes the
Hor+ width **exactly once, at boot**, from whatever `g_windowWidth`/`g_windowHeight` are at that
instant — confirmed with a temporary debug log (`froze screenWidth=512` while the window was
still the 800x600 default). Nothing else ever re-touches `clip.w`/`disp.w` afterward, so a
window that starts small and gets resized/maximized later stays pillarboxed at plain 4:3
forever, no matter how big the window becomes. Starting the window at the real display size
from frame 1 sidesteps this entirely — verified clean (fills the whole window, no corruption)
across several screens (crate/logo intro, paused in-race menu, main title screen).

**Tried and reverted — do not repeat without solving the multi-pass problem first**: attempted a
real fix for the still-open texture-corruption bug (see "OPEN" section below) by clamping
`NativeRenderer_StoreFrameBuffer`'s write to the real 512-wide VRAM half (protecting the
texture atlas) and adding a second draw path (`NativeRenderer_DrawRenderTargetRegion` + a new
`ctr_present_target_shader`) that presents the Hor+-only overscan strip (x=512 to disp.w)
directly from `s_mainRenderTarget`, bypassing VRAM only for that strip. Viewport math was
verified correct with a diagnostic shader (gradient fill showed the exact right UV range, no
gap). It still doesn't work: on the main title screen, the overscan strip showed a
cropped/duplicated copy of the "ADVENTURE/TIME TRIAL/..." text box instead of a clean
continuation. Root cause: **the menu logo+textbox are composited across multiple sub-passes
within one frame, accumulating in VRAM between passes** (this is the exact same fact the
"IMPORTANT CORRECTION" section below already documented breaking a *full* VRAM bypass) — the
core (VRAM-routed) region correctly shows the accumulated result of all passes, but sampling
`s_mainRenderTarget` directly for the overscan only sees whatever the *last* pass left behind,
which is a different (partial) image. A real fix needs the overscan path to also see the
accumulated multi-pass result, not a plain render-target snapshot - that's a bigger change than
a two-shader split (e.g. teaching the store step to composite into a second, off-atlas VRAM
region across all of the same passes, then presenting from *there* instead of the render
target directly). Confirmed via a clean A/B (this exact fix stashed in/out) that the duplicate
artifact is 100% caused by this change, not pre-existing.

**Practical state after this pass**: the "doesn't fill the screen" complaint is fixed. The
original texture-corruption complaint (canyon wall glitch) is **still open** - reproduced again
this pass in a real (non-demo) race with the boot-size fix active (see screenshot description
in chat: clear vertical stripe corruption top-left of a canyon/cliff race). Whoever picks this
up next should read the "OPEN" section right below before trying anything - two different
approaches (this pass's VRAM-store clamp, and a from-scratch full-bypass attempt in an earlier
pass) have now both run into the same multi-pass-compositing wall from different angles.

## THIRD PASS (2026-08-11, this session, different machine again) — fixed the second store call the previous pass flagged, but could NOT get a clean visual repro either way

Picked up the "OPEN" item directly below: the previous pass's own hypothesis #2 was that
`Platform_EndScene`'s per-frame `NativeRenderer_StoreFrameBuffer(activeDispEnv.disp.x, ...,
activeDispEnv.disp.w, ...)` call (`platform/native_platform.c:374`) is a second,
independent VRAM-corruption path, separate from the already-fixed
`NativeGpu_TPageOverlapsActiveDrawPage` overlap check. Confirmed by reading the code that
this hypothesis is structurally sound and applied the fix — but this session's own live
testing neither confirmed nor ruled out that it's *the* cause of the user's live "canyon
wall" report. Read both parts below before deciding what to do next.

**The fix (applied, committed to the working tree, not yet a separate commit as of this
writing):** `platform/native_renderer.c`, `NativeRenderer_StoreFrameBuffer` — the VRAM
texture (`s_vram.texture`/`s_glVramFramebuffer`) is a fixed `1024x512` GL texture
(`VRAM_WIDTH`/`VRAM_HEIGHT`, `include/platform/native_renderer_types.h:17-18`), a real
allocation mirroring PS1's 1MB VRAM — not something that scales with the window. Its left
half (`x<512`) is PS1's addressable framebuffer space; `x>=512` is the texture atlas (this
split is exactly what the earlier `native_gpu.c` fix's `VRAM_WIDTH/2` clamp assumes too).
`NativeRenderer_StoreFrameBuffer(x, y, w, h)` packs `s_mainRenderTarget.texture` (which
"stays at CTR's logical display size" per its own comment — i.e. it **is** widened by Hor+)
into that fixed VRAM texture via `glViewport(x, y, w, h)`. Both of its callers
(`native_gpu.c`'s framebuffer-feedback path, and `native_platform.c`'s unconditional
per-frame `Platform_EndScene` call) can pass a Hor+-widened `w` that pushes `x+w` past 512 —
at which point this call is writing into the texture-atlas half of the *real* VRAM texture
every single frame, not just during an actual self-texturing feedback effect. Added the same
clamp used in `NativeGpu_TPageOverlapsActiveDrawPage` (`if (x + w > VRAM_WIDTH / 2) { w =
(VRAM_WIDTH/2) - x; }`, skipping the pack entirely if that leaves `w <= 0`) directly inside
`NativeRenderer_StoreFrameBuffer` itself so both call sites are covered by one guard, per the
"fix once where all callers route through" pattern the previous pass's own fix already
established. This is a no-op whenever `x+w<=512` already (i.e. every retail/non-Hor+ case),
confirmed by a byte-exact VRAM-dump diff below.

**What this session's testing actually showed, precisely, so the next pass doesn't have to
redo it:**
- Built both with and without this fix (`git stash`/`git stash pop` to A/B on the same
  checkout) and ran both at real ultrawide (2560x1080, via `xdotool windowsize` on the
  resizable SDL window — window ID changes per launch, re-resolve with `xdotool search
  --name "Crash Team Racing"` then `getwindowpid` to disambiguate from an unrelated stale
  `mutter-x11-frames` window that also matches that title search on this machine).
- **Confirmed no regression**: on a screen captured *before* Hor+'s widening has taken
  effect (`MainMain.c`'s `StateZero` widens `disp.w` only once a race/menu state actually
  starts — the boot-time Naughty Dog crate logo and checkered-flag transition run before
  that), a `F7` VRAM dump's `x>=512` half was **byte-for-byte identical** between the fixed
  and unfixed builds (`compare -metric AE` → `0`). Good: the clamp truly only engages once
  `x+w` actually exceeds 512, exactly as intended.
- **Did not get a clean same-frame repro either way**: tried to reach the same in-race
  paused-HUD screen on both builds by replaying an identical `xdotool key Return` sequence
  with fixed sleeps (relying on demo-mode being a scripted/deterministic replay), but landed
  on visibly different moments each time (different HUD state, different lap/track content)
  — real-world X11/process-startup timing jitter was enough to desync it. Diffing those
  mismatched frames was meaningless (of course they differ, they're different frames) and is
  **not included as evidence either way** — don't reuse those dumps
  (`vram_prefix_race2.tga`/`vram_postfix_race.tga` if they're still in scratch, they're
  already gone from this session's scratchpad) as if they proved anything.
- **Found and ruled OUT a red herring**: every dump taken during an actual paused race
  (fixed or unfixed build, any window width including narrow 800x600 with Hor+ not even
  engaged) showed a small flat pink/salmon rectangle + black block + a tiny live-looking
  race-scene thumbnail around VRAM `x≈512-940, y≈0-95`. Confirmed this is **unrelated** to
  Hor+ or this fix: it's byte-identical regardless of fix/window-width, and traced to a
  separate mechanism, `NativeRenderer_FlushOffscreenToVRAM` (`native_renderer.c:1994`, called
  with its own explicit `s_previousOffscreen` rect, nothing to do with `disp`/`clip`) — almost
  certainly a legitimate small live-preview compositing pass the pause menu (or similar UI)
  uses intentionally. **Don't mistake this for corruption if you see it again.**
- Net result: the fix is applied and reasoned through carefully, and is safe (proven
  no-regression on the unwidened case), but **this session did not visually catch the
  StoreFrameBuffer bleed in the act**, the same way the previous pass could not either.
  Given the previous pass's note that the user explicitly asked to stop live-debugging this
  interactively to save tokens once real evidence was in hand, this pass stopped chasing a
  perfect manual repro rather than keep burning turns on it — the fix is justified by direct
  code reading (matches the exact, already-validated pattern from the sibling
  `native_gpu.c` fix) rather than by a fresh screenshot.

**Recommended next step, cheaply**: the user's own original repro was a *real, sustained,
player-controlled race* (not menu navigation, not demo mode) — that's the one scenario this
pass (and the previous one) never actually reached manually. If the glitch is still visible
next time the user plays for real (ideally on the Android device where it was field-reported),
grab an `F7`/`adb`-equivalent VRAM dump right then and diff the `x>=512` half against a dump
from the same build+resolution taken on a menu screen (unwidened) — if they now match (both
clean atlas noise), this fix closed it; if the in-race one still shows a large-scale
scene-shaped bleed (not the small `NativeRenderer_FlushOffscreenToVRAM` box above), there's a
third path still open.

## OPEN — user reports the texture glitch is still visible during a real, live-played race (2026-08-11)

After the three fixes below, the user played a real race themselves (not a demo) at a
resized-down window (1798x753, reached by resizing during the session) and reported: HUD
bunched left (expected/known, see "Not yet checked"/Stage 2 gap elsewhere in this doc), AND
a real striped/glitched texture on the canyon wall on the left side of the screen — the
same class of symptom as the original bug report. A screenshot taken at that moment
(`live_shot2.png`, session-local scratchpad, gone) confirms it: vertical striped corruption
over the wall/water geometry, HUD crammed left.

**My in-the-moment read (NOT confirmed, the user pushed back on it and asked to stop
live-debugging to save tokens — do not treat this as settled):** a few follow-up screenshots
taken seconds later, and again after resizing the window back to fullscreen, came back
clean, which made it look transient/resize-triggered. **The user explicitly said they did
not resize the window and that this is how it has always looked** — i.e. they do not agree
with the "just a transient resize artifact" theory. This is unresolved. Do not assume the
native_gpu.c fix (`117b130bf`) fully covers every occurrence of this bug class — there may
be a second, still-live trigger path producing the same visual symptom during real
(non-demo) gameplay specifically, which nothing in this session's testing (all demo-mode or
short manual test races) exercised the same way a longer real race would.

**Next step, cheaply, before spending more tokens on live back-and-forth:** get a same-frame
screenshot + `F7` VRAM dump pair during an actual real race (not demo mode) without
resizing the window at all mid-session, and check whether `NativeGpu_TPageOverlapsActiveDrawPage`
truly never fires for the tpage involved (add a temporary log/breakpoint on the `return true`
path rather than re-deriving everything from screenshots), OR check whether
`Platform_EndScene`'s per-frame `NativeRenderer_StoreFrameBuffer(activeDispEnv.disp.x, ...,
activeDispEnv.disp.w, ...)` call (`platform/native_platform.c`, NOT touched by any fix this
session) is a second, independent path that can still overwrite VRAM x>=512 under some
condition the demo-mode testing never hit (e.g. a specific track's texture-page layout, or
sustained play over a full lap rather than a few seconds).

## Where things stand

Branch `master`, 3 commits ahead of `origin/master`'s `7eab5a6af` (docs: add handoff notes),
not yet pushed:
- `95bf32e45` `game/RaceFlag.c` + `game/230/MM_Title.c`: re-center the loading flag and
  main-menu text box for Hor+.
- `117b130bf` `platform/native_gpu.c`: clamp the framebuffer-feedback self-texturing overlap
  check to PS1's real 512-wide bound — **this was the actual root cause of the "canyon-wall
  texture glitch" / minimap-corruption bug from the original user report.**
- `232fe8436` `HANDOFF.md` update (this document, as of the previous revision).

**All three are visually confirmed fixed on BOTH the Linux desktop build (real ultrawide
2560x1080/3440x1440 and 4K 3840x2160) AND the actual Android device (Odin 2 Portal, real
hardware, not an emulator)** — see "Fixes made this session" below for the desktop
verification and "Android verification" below for the on-device pass. No crashes in
`logcat` across the whole Android test session.

**The in-race minimap specifically — the original report's other big complaint — is also
confirmed fixed**, directly, on-device: the user played into a real, player-controlled
Adventure-mode race on the Odin 2 Portal (not demo mode) and the minimap rendered as a
correctly-proportioned small square box in the bottom-right corner with correct dot/arrow
markers, no stretching, no corruption — screenshot taken via
`adb exec-out screencap`. This closes the one gap the earlier verification passes couldn't
reach (demo mode hides the HUD/minimap entirely, so this required an actual controlled
race, which menu-navigation friction had prevented all session until the user did it
manually). Only remaining unverified item: the sibling title submenus (Adventure/Race
Type/Players/Difficulty) — not individually checked for the same class of bug as the
main-menu fix (see the "Not yet checked" note under fix #2 below). **This is ready to
consider for the upstream PR** (see "Standing goal" at the bottom).

## How Hor+ was implemented (for context)

- PSX renders 512x216(NTSC)/512x236(PAL) but that buffer is *authored* to display as 4:3
  on a real TV — the reference aspect for FOV math is a fixed 4:3, not the raw buffer ratio.
- Hor+ = widen horizontal FOV, keep vertical FOV: recenter GTE screen-space `OFX`
  (`SetGeomOffset`) to the new half-width, widen `DISPENV`/`DRAWENV` width, do NOT touch
  `C2_H` (`SetGeomScreen`).
- `platform/native_renderer.c`: `NativeRenderer_GetWideGeomWidth(int originalWidth)` computes
  the widened width from the live window aspect (via `SDL_GetWindowSizeInPixels`, not a
  cached value) against the fixed 4:3 reference. `NativeRenderer_UpdatePresentationViewport`
  recomputes the presentation letterbox every frame from `activeDispEnv.disp.w`.
- `game/MAIN/MainMain.c` (`StateZero`): widens `clip.w`/`disp.w` on both double-buffer slots
  and re-centers `SetGeomOffset` — but only **after** the fixed-size "SCEA Presents" splash
  (a pre-rendered VRAM image at native 512x216) has already been blitted, so the splash isn't
  incorrectly stretched.
- `game/PushBuffer.c`: `PUSHBUFFER_WIDE_W()` (now parameterless, `= NativeRenderer_GetWideGeomWidth(0x200)`)
  drives `pb->rect.w` for 1-player and 2-player split; **4-player split (`total==3/4`) is
  deliberately left unwidened** (out of scope, still `0xfd`).

## The crash that Hor+ exposed (fixed, verified — old news, still true)

Widening the FOV meant more geometry visible on screen per frame than retail's narrower FOV
ever produced. This exposed a pre-existing, latent buffer-overflow bug:

- `game/226/226_00_DrawLevelOvr1P.c` has a manual bump allocator (clip-record buffer,
  `DrawLevelOvr1P_GetClipRecordCursor`/`SetClipRecordCursor`, backed by
  `data.PtrClipBuffer[0]`) that is **not** bounds-checked by default — each writer must
  explicitly call `DrawLevelOvr1P_HasClipRecordSpace(size)` first. 5 of 7
  `Write*RenderedClippedRecordAtOtEntry` functions were missing that check. Fixed by adding
  the missing check (matching the pattern already present in the other 2 functions).
- Related/same-class fix in `game/MAIN/MainInit.c`: `gGT->ptrRenderBucketInstance` was
  pointed at `&rdata.s_STATIC_GNORMALZ[0] + 148`, i.e. 148 bytes into a **16-byte** static
  scratch array. Changed to a proper `MEMPACK_AllocMem`.

## IMPORTANT CORRECTION — a dead-end theory from earlier this session

Earlier in this session (before the fixes below), I chased a theory that
`NativeRenderer_StoreFrameBuffer`/`Platform_EndScene` was packing the Hor+-widened
framebuffer (now wider than PS1's real 512-wide VRAM half) into the shared 1024x512 VRAM
GL texture, clobbering the texture atlas stored at VRAM x>=512, and that this explained the
minimap/texture glitches. **This was wrong.** I proved it wrong by diffing an `F7`
(`NativeRenderer_SaveVRAM`) VRAM dump taken with a clamp-the-store-width fix applied against
the *exact same dump taken on unmodified code* — the "corruption-looking" noise at VRAM
x>=512 is bit-for-bit the same in both: it's just what 4-bit/8-bit indexed PS1 texture data
looks like when displayed as raw 5:5:5:1 truecolor (an indexed atlas always looks like TV
static in a raw dump; this is not a symptom of anything).

I also tried bypassing VRAM for final presentation (sampling the render target texture
directly) to "fix" this non-bug, and that **broke the main menu** (its logo + text box are
composited across multiple sub-passes within one frame that rely on VRAM persisting/
accumulating content between those passes — bypassing VRAM skips that accumulation). That
change has been fully reverted. **Do not resurrect the "clamp VRAM store width" or "bypass
VRAM for presentation" approach without new evidence** — the actual bugs (below) are
unrelated to VRAM-atlas corruption.

Lesson for whoever continues this: a VRAM.TGA dump showing "noise" at x>=512 is **normal**
and proves nothing by itself. Always diff against the same dump taken on unmodified code
before concluding something is corrupted.

## Fixes made this session (committed as `95bf32e45`, validated by visual A/B testing)

Both bugs below share one root cause pattern: **some piece of UI positioning is a hardcoded
pixel constant authored for a 512-wide screen, while some other visually-related element
(a 3D model, or a GTE-projected mesh) correctly recenters itself using the live
Hor+-widened width — so widening the screen moves one but not the other, and they drift
apart or collide instead of maintaining their original relationship.**

1. **`game/RaceFlag.c:576`** (`RaceFlag_DrawSelf`) — `gte_SetGeomOffset(0x100, 0x78)` was
   hardcoded to the narrow half-width (256), unlike every other GTE-projected draw path in
   the codebase (`game/PushBuffer.c:162`, `game/RenderLevel/RenderLists.c:64`,
   `game/RenderBucket/RenderBucket_QueueExecute.c:1110,5233`,
   `game/Vehicle/VehGroundSkids.c:236`, `game/MAIN/MainMain.c:628,664` — all recenter via
   `pb->rect.w/2` or `screenWidth>>1`). This is the checkered-flag mesh used both for the
   loading screen AND the in-race "3-2-1-GO" transition. Fixed by recentering via
   `sdata->gGT->pushBuffer[0].rect.w / 2`. Confirmed visually: the "LOAD..." text on the
   flag used to be clipped hard against the left edge of the (now-wide) screen; after the
   fix it sits correctly on the flag mesh.

2. **`game/230/MM_Title.c:248`** (`MM_Title_MenuUpdate`) — the main-menu text box
   (ADVENTURE/TIME TRIAL/ARCADE/VS./BATTLE/HIGH SCORE) is positioned from
   `D230.titleMainMenuPos.x = 0x180` (384, a retail-authored constant meaning "75% across a
   512-wide screen", i.e. deliberately placed clear of the 3D logo on the left). The 3D
   "CTR" logo model's screen position, meanwhile, comes from the GTE projection center
   (`SetGeomOffset(screenWidth>>1, ...)` in `MainMain.c`), which Hor+ correctly re-centers —
   so as the screen widens, the logo drifts right while the text box constant never moves,
   and at real ultrawide (tested at 2560x1080) they end up overlapping, bunched together in
   the middle-right of the screen with wasted space on both sides. **Confirmed root cause by
   direct reproduction** (see below) — this is NOT the same thing as the already-known
   "Stage 2 HUD repositioning" gap (hardcoded pixel HUD elements bunched at the *left*
   edge during races); this is a *different* hardcoded-position bug that only shows up on
   the main menu / title screen. Fixed by adding the same half-width delta the logo gets:
   `D230.titleMainMenuPos.x + (gGT->pushBuffer[0].rect.w - 0x200) / 2`. Confirmed visually
   at 2560x1080: logo and text box no longer overlap.

   **Not yet checked**: `D230.c` has four sibling submenu position fields
   (`titleAdventureMenuPos`, `titleRaceTypeMenuPos`, `titlePlayersMenuPos`,
   `titleDifficultyMenuPos`), all currently `{0,0}` — those menus reach their final on-screen
   position purely via an authored slide-in transition curve
   (`D230.transitionMeta_Menu` / `*Transition.currX`), whose *endpoint* is presumably ALSO
   tuned assuming a 512-wide screen. I have not navigated into Adventure/Race
   Type/Players/Difficulty menus at true ultrawide to check whether they have the same
   collision-with-the-logo bug. Worth checking before considering the title screen done.

## FIXED — canyon-wall / background texture glitch (was the main bug from the original report)

This was the bug that mattered most (the in-race visual bug from the original user report:
"las texturas tienen glichec... errores en el mapa... se pierden texturas"). Root-caused and
fixed this session; confirmed clean by direct reproduction on the Linux desktop build across
five different tracks at real ultrawide (2560x1080) and 4K (3840x2160).

**Root cause, confirmed:** `NativeGpu_TPageOverlapsActiveDrawPage` (`platform/native_gpu.c`,
called from `AddSplit` for every textured primitive drawn) answers "does this polygon's
texture page overlap the region the GPU is *currently rendering into*?" — a real PS1 idiom
check used to detect self-texturing screen-feedback effects (heat haze, etc.), which then
triggers `NativeGpu_PrepareFramebufferFeedback` → forces a flush + a mid-frame
`NativeRenderer_StoreFrameBuffer` snapshot of the *partially-rendered* scene into VRAM, so
the feedback polygon can sample "the screen so far" as its texture.

Retail's `activeDrawEnv.clip.w` was always <=512, so only texture pages 0-7 (VRAM x<512,
i.e. the framebuffer's own address range) could ever satisfy this check — genuinely
self-referencing draws only. **Hor+ widens `clip.w` past 512**, and the overlap check used
the *live widened* `clip.w` unmodified — so once `clip.w` exceeds 512, texture pages 8-14
(VRAM x=512-960, where essentially all of the game's *real* textures live — walls, sand,
icons, everything) start geometrically falling "inside" the widened clip rect too, even
though those polygons have nothing to do with screen-feedback. Every such polygon then got
its real texture data silently replaced by a snapshot of whatever partial scene content
happened to be on screen at that instant — explaining both observed symptoms with one
mechanism: a flat solid-blue "hole" in a VRAM dump (captured mid-frame, before the rest of
the scene was drawn) and visible colorful "TV static" stripes on screen (a different texture
page, captured at a different, differently-inconsistent moment). This also explains the
minimap/HUD-icon corruption from the original report, since HUD icons are exactly the kind
of small textured sprite living in that VRAM region.

**Fix:** clamp the comparison rect's width in `NativeGpu_TPageOverlapsActiveDrawPage` to
PS1's real 512-wide bound before checking for overlap — retail self-texturing feedback
effects were never authored against anything wider than that anyway, so this fully restores
original behavior for the intentional case while excluding the Hor+-only overscan region
from ever matching. Three-line change plus a comment; see the diff in `platform/native_gpu.c`.

**How this was found:** an initial theory (see "IMPORTANT CORRECTION" above) wrongly blamed
`NativeRenderer_StoreFrameBuffer`'s VRAM-atlas write path directly. That was disproven by an
A/B VRAM dump diff. The *real* clue was in that same disproven investigation's evidence: a
`vram_c2.png` dump (session-local scratchpad, gone now) showed a flat-color "hole" in the
widened region instead of atlas noise, which doesn't fit "content was overwritten with atlas
garbage" — it fits "content was overwritten with a screen-feedback snapshot." That pointed
straight at `NativeGpu_PrepareFramebufferFeedback`'s caller,
`NativeGpu_TPageOverlapsActiveDrawPage`, which an Explore agent had already flagged earlier
in the session (before the wrong theory took over) as comparing against the live widened
`clip.w`. Once identified, fixing it was a 3-line clamp.

**Verified by direct reproduction**, not just code reasoning: rebuilt, resized the window to
2560x1080 and separately to 3840x2160 fullscreen, navigated Arcade → demo-mode races, and
confirmed the specific canyon/desert track that showed heavy red/black/orange static on the
wall pre-fix now renders completely cleanly post-fix. Also checked four other tracks (ice/
Blizzard Bluff, an underwater/jungle track, a rainy temple track, one more) — all clean, no
static anywhere. A small track-preview map widget (top-right corner during Time Trial demo)
also rendered correctly as a small proportioned box, not stretched/corrupted — indirect
evidence the in-race minimap (same draw-primitive family, `game/UI/UI_Map.c` per an earlier
Explore agent's research, see below) is fixed too, though the actual in-race minimap itself
was not directly confirmed (couldn't reliably navigate past demo-mode auto-trigger into a
real player-controlled race this session — see "known friction" note below).

For reference, the minimap draw code itself was already researched earlier this session
(before the root cause above was found) and confirmed to NOT be the bug's location — it's
drawn as ordinary fixed-pixel 2D (`game/UI/UI_Map.c`, `game/UI/UI_RenderFrame.c:748-791`,
via `DecalHUD_DrawPolyGT4`) with no GTE/width dependency of its own. It only *looked* broken
because it's a small textured sprite living in the VRAM region that was getting corrupted by
the bug above — same root cause, not a separate bug in the minimap code.

## Android verification (this session, real device — Odin 2 Portal)

No Android toolchain existed on this machine at session start (only `adb`). Installed:
JDK 17 (`pacman -S jdk17-openjdk`), Android cmdline-tools (Google's zip, since no AUR
helper had a working `android-sdk` package readily available and this avoids AUR
entirely), then via `sdkmanager`: `platform-tools`, `platforms;android-34`,
`build-tools;34.0.0`, `ndk;27.0.12077973`, `cmake;3.22.1`, all under `~/Android/Sdk`. Wrote
`android/local.properties` with `sdk.dir=/home/ufoex/Android/Sdk` (gitignored, machine-
specific, not committed). Built with `cd android && ./gradlew assembleDebug` — succeeded
first try, only pre-existing compiler warnings (implicit int→s16/char truncation in
`Particle.c`/`224.c`/`PlayLevel.c`/`GhostTape.c`/`CS_Thread.c`, unrelated to this session's
changes). Output: `android/app/build/outputs/apk/debug/app-debug.apk`
(`armeabi-v7a`+`x86` per `build.gradle`'s `abiFilters` — no arm64 variant, but the device
supports `armeabi-v7a` natively per `getprop ro.product.cpu.abilist`, so this installs and
runs fine; this is a deliberate existing choice, not something this session changed, see
`README_ANDROID.md`/the 32-bit memory-model comment in `android/app/build.gradle`).

Installed via `adb install -r app-debug.apk`. Assets: pushed the same `ctr-u.bin` used for
the Linux build straight to the app's scoped external storage —
`adb push ctr-u.bin /sdcard/Android/data/com.ctrnative/files/assets/ctr-u.bin` — matching
`README_ANDROID.md`'s "Manual Setup" path (`getExternalFilesDir(null)/assets/ctr-u.bin` per
`CTRNativeLauncherActivity.java:50-55`); this sidesteps the in-app file-picker/"All Files
Access" flow entirely. Also had to `adb shell appops set --uid com.ctrnative
MANAGE_EXTERNAL_STORAGE allow` (the plain `appops set com.ctrnative ...` form without
`--uid` didn't actually take — `dumpsys package` kept reporting `granted=false` for it
until the `--uid` form was used) since the app requests that permission on first run
regardless of whether it's actually needed for this asset path.

**Friction hit along the way, for next time:**
- The device's screen was **asleep and separately keyguard-locked (PIN)** — `am start`
  intents were silently accepted but never actually changed what was on screen
  (`dumpsys window | grep isKeyguardShowing` → `true`, `dumpsys power | grep mWakefulness`
  → `Asleep`), and `wm dismiss-keyguard`/swipe gestures don't bypass a real PIN lock. Had to
  ask the user to physically wake + unlock the device. **Always check keyguard/wakefulness
  state before concluding an `am start`/activity-navigation command "isn't working."**
- After the very first launch, the app opened Android's own Settings
  (`MANAGE_APP_ALL_FILES_ACCESS_PERMISSION`) to ask for All-Files-Access, which backgrounded
  CTR. That Settings task then got "stuck" as the resumed/focused task — `am force-stop` on
  either package did not dismiss it. What actually worked: `adb shell am stack remove
  <taskId>` (get `<taskId>` from `dumpsys activity activities | grep "Task{.*com.ctrnative"`
  or the `t<N>` suffix on the stuck `ActivityRecord`) to forcibly kill that specific task,
  then a fresh `am start` worked normally.
- Screenshots: `adb exec-out screencap -p > file.png` worked reliably throughout (much
  simpler than the desktop session's X11 tooling saga) — no window-scoping concerns since
  it's the device's one physical screen.
- Menu/demo-mode navigation friction was the same as on desktop (see "Known friction" under
  the Linux repro section below) — `adb shell input keyevent KEYCODE_DPAD_DOWN` /
  `KEYCODE_BUTTON_A` for menu nav, same auto-triggering demo-mode-before-real-race problem,
  never got the actual in-race minimap on screen to look at directly.

**What was verified, all on the real device screen via `adb exec-out screencap`:**
- Main menu: logo and ADVENTURE/TIME TRIAL/.../HIGH SCORE text box **do not overlap** —
  `MM_Title.c` fix confirmed on-device.
- Checkered-flag "LOAD..." text during the loading-screen transition is properly spread
  across the flag mesh, not clipped/bunched at the left edge — `RaceFlag.c` fix confirmed
  on-device.
- Four different demo-mode race tracks (a canyon/desert start-line scene, a jungle/river
  scene, a mountain-cliff scene, a green mountain/portal scene), all filling the device's
  full wide screen (Hor+ active — the device's screen is 1920x1080, wider than 4:3, so
  `NativeRenderer_GetWideGeomWidth` widens automatically same as it did on the desktop
  ultrawide tests) — **zero texture corruption/static on any of them.** This is the
  `native_gpu.c` fix confirmed on-device, on the actual hardware/GPU (Adreno 740) the user's
  reported bug was originally seen on.
- No crashes (`logcat` grepped for `fatal|AndroidRuntime.*Exception|SIGSEGV|crash` across
  the whole session, zero matches) across boot, cutscenes, menu navigation, and four demo
  races.

**Update — minimap confirmed on-device after all**: demo mode's HUD-hidden auto-race kept
preempting attempts to reach a real, player-controlled race all session (see friction notes
above), but the user manually navigated into a real Adventure-mode race afterward and
confirmed via `adb exec-out screencap` that the in-race minimap renders correctly — a small,
correctly-proportioned square box bottom-right with proper dot/arrow markers, no stretching
or corruption. See "Where things stand" at the top of this document. Still not verified:
the Adventure/Race Type/Players/Difficulty title submenus.

## How to reach a real race (for reproduction on Linux desktop)

From a fresh boot: `Return` (skip legal splash) → wait ~3s → `Return` (skip intro
cutscene... sometimes needs a 2nd `Return` a few seconds later, timing is inconsistent) →
wait for main menu (CTR logo + ADVENTURE/TIME TRIAL/... box) → `Down` `Down` (highlights
ARCADE) → `c` (confirm select — **not** `Return`; `Return`/Start does not confirm menu
selections, `c` is bound to `SDL_SCANCODE_C` = PSX Cross per
`platform/native_input.c:270-273`) → lands in an Arcade attract/demo race after a moment →
if a "DEMO MODE / PRESS ANY BUTTON TO EXIT" track-preview screen appears first, press `c`
again to drop into the actual in-race demo footage with full HUD. Menu navigation timing is
finicky (a `Down` press right after a screen transition can get eaten) — screenshot between
each keypress to confirm before proceeding.

To test at real ultrawide: the window is resizable at runtime
(`SDL_WINDOW_RESIZABLE`, picked up live via `Platform_HandleWindowResize`) — no need to
relaunch, just `xdotool windowsize <winid> 2560 1080` (or whatever) after the window
appears. `NativeRenderer_GetWideGeomWidth` recomputes from the live window size every time
it's called, so this is a fully valid way to test different aspect ratios without an actual
ultrawide monitor — though this machine happens to have a real one (3440x1440), plus a
second 4K OLED monitor the user dedicated to this testing (see tooling notes below).
`F11` toggles fullscreen (`Platform_HandleFullscreenToggle`) — use it on the OLED monitor to
avoid a static titlebar/header burning in during long test sessions.

**Known friction — reliably reaching a real, player-controlled race is hard**: selecting
ARCADE (or apparently any race-starting menu path) almost immediately drops into an
attract-mode "DEMO MODE / PRESS ANY BUTTON TO EXIT" auto-played race with the camera
cutting between tracks every few seconds and *no HUD/minimap displayed at all* — useful for
checking world/texture rendering (which is what most of this session's verification relied
on) but NOT useful for checking HUD or minimap specifically, since demo mode hides both. I
was not able to reliably get past this into an actual controllable race this session (menu
navigation timing is inconsistent — a `Down` press right after a screen transition
frequently gets eaten, and confirming a menu selection sometimes re-triggers demo mode
instead of proceeding to track/kart select). If continuing this investigation, that's worth
solving first (maybe there's a way to disable the demo-mode idle-timeout for testing, or
navigate faster/more precisely than `xdotool key` with fixed sleeps allows).

## Testing/tooling notes for the next session

- **Screenshots**: **strongly prefer `import -window <id> <file>` (ImageMagick), scoped to
  the game's own window ID from `xdotool search --name "Crash Team Racing"` — never a
  full-screen capture tool** (`spectacle -f`, etc.) on this machine, because the user runs
  other applications and browser windows on the same screen(s) concurrently (their own
  claude.ai session, other games, etc.) and a full-screen grab will capture that unrelated
  content. `import -window <id>` briefly stopped working mid-session with a misleading
  `missing an image filename` error (root cause: a transient X11 auth/`XAUTHORITY` issue,
  confirmed via `env -i DISPLAY=:0 import ...` giving a clearer "Authorization required"
  error — not an ImageMagick bug) but came back on its own; if it recurs, retry rather than
  falling back to full-screen capture. The user dedicated a second 4K monitor to this
  session's testing specifically to make window-scoped capture cleaner — ask before
  assuming a monitor/window is free to use for testing, and re-check
  `xdotool getwindowgeometry <id>` before every capture since the window's position can
  change between launches (varies with the window manager, not something this session
  controls).
- **`F7`** (`NativeRenderer_SaveVRAM` → `VRAM.TGA` in the CWD, i.e. `build/`) is extremely
  useful for diagnosing VRAM/texture issues directly — but see the "IMPORTANT CORRECTION"
  above before drawing conclusions from what it shows.
- Linux build: `./build.sh` needs both `CMAKE_C_FLAGS` and `CMAKE_CXX_FLAGS` set to `-m32`
  (the repo's own `build.sh` only sets `CMAKE_C_FLAGS`, which fails at the CMake CXX-compiler
  ABI-check step on this machine — pass both explicitly if `build.sh` fails there).
  Game assets: symlink `assets/ctr-u.bin` and the `.cue` from a working prebuilt package
  (e.g. this machine has one at `~/Descargas/ctr-native-beta-7.1-linux-x86/assets/`) into
  the repo's `assets/` dir, no need to own a physical disc/rip separately per-checkout.
- Android SDK/NDK/JDK toolchain is now installed on this machine at `~/Android/Sdk` (JDK 17
  at `/usr/lib/jvm/java-17-openjdk`) — see "Android verification" above for the exact
  packages. `android/local.properties` (gitignored, machine-specific) points at it. To
  rebuild: `cd android && JAVA_HOME=/usr/lib/jvm/java-17-openjdk
  PATH="$JAVA_HOME/bin:$PATH" ./gradlew assembleDebug`.

## Minor cosmetic findings from a follow-up pass (not the original bug, not blocking)

After committing the three fixes, went back to check the four sibling title submenus
(Adventure/Race Type/Players/Difficulty) mentioned as unverified above. Never actually
reached them cleanly — demo-mode auto-triggers before the Race Type/Players/Difficulty
chain ever shows (confirmed this isn't an idle-timeout race: even a zero-delay `Down Down
c` sequence right after landing on the main menu went straight to a demo race instead of
`D230.menuRaceType`, so something about entering Arcade/Vs from a menu that's about to be
superseded by the attract-mode cycle skips the submenu display entirely - not investigated
further). Adventure mode, by contrast, WAS reachable (that's how the in-race minimap got
confirmed above) but goes straight into the hub world, no `D230.menuAdventure` overlay was
ever seen either. **The submenus remain unverified — this needs a cleaner way to disable/
outlast the demo-mode timeout before it can be checked properly, not more manual retries.**

While chasing that, spotted two small, separate, unrelated-looking visual details, both at
real ultrawide (3440x1412) - noted here so they aren't lost, not treated as blocking:

1. **A ~1px vertical fringe of RGB-noise pixels right at the boundary between rendered
   content and the black pillarbar**, visible on at least one demo track (a
   temple/coliseum-style track, screenshot was `fresh4.png` in that session's scratchpad,
   already gone). Cosmetic scale (a single pixel column), plausibly a texture-edge-clamping/
   bilinear-filtering artifact at the render target's boundary rather than anything related
   to the `native_gpu.c` fix. Not investigated further - if picked up again, check
   `NativeRenderer_SetOverrideTextureSize`/edge-clamp sampler state on the main render
   target, and whether it reproduces at 2560x1080 too (only seen at 3440x1412 this pass).

2. **A single flat, untextured, blank light-lavender quad floating near some ice/beach
   scenery** on a different demo track (`fresh5.png`, also gone from scratchpad). Looked
   like a billboard/sign sprite whose texture didn't bind. **Checked whether this is a
   regression: built the exact pre-session commit (`7eab5a6af`, via a `git worktree`) and
   ran its own attract loop for a comparable stretch of time** - did not happen to land on
   the same quad/track to do a pixel-exact comparison, so this specific finding is
   NOT confirmed either way (not proven pre-existing, not proven a regression). Given the
   original bug reports never mentioned a blank/missing-texture quad (they described
   striped/colorful static, the opposite failure mode - "wrong data" vs "no data"), this is
   very likely a separate, unrelated, minor issue if it's a real bug at all (could also be
   an intentional design element, e.g. an unlit sign catching flat light at a certain
   angle) - worth a second look if seen again, but not chased further this session.

Also worth noting: at the SAME 3440x1412 resolution, a screenshot of the main menu
(`sub22.png`, gone from scratchpad) appeared to show the post-fix menu text box almost
entirely cut off by the pillarbox, which briefly looked like a regression in the
`MM_Title.c` fix. **Ruled out**: rebuilt and ran the pre-session (`7eab5a6af`) code at the
identical resolution and it showed the exact same near-invisible box. So this is a
pre-existing edge case in how the box's position/the pillarbox interact at this specific,
very wide aspect ratio - NOT something introduced by the `MM_Title.c` fix (which remains
correctly verified at 2560x1080/2560x1080-class ultrawide, the more common real-world
ratio). Might be worth a look if someone tests on a monitor wider than ~2.4:1 - possibly the
half-width-delta formula needs a different scaling behavior at extreme ratios, or the
pillarbox math itself has a separate pre-existing bug at that range.

## Standing goal

Ultrawide is now visually clean + stable on both the title screen and in-race, on both
Linux (real ultrawide + 4K) and Android (real Odin 2 Portal hardware) — see "Android
verification" above. The two remaining unverified items (in-race minimap directly observed
in a real race, and the four title submenus) are minor/inferred-fixed, not open bugs with
reproductions — see the caveats at the top of this document. **Given that, this is ready to
consider for a PR** to upstream `CTR-tools/ctr-native` with just the core non-Android files:
`platform/native_renderer.c/h`, `platform/native_gpu.c`, `game/MAIN/MainMain.c`,
`game/PushBuffer.c`, `game/226/226_00_DrawLevelOvr1P.c`, `game/MAIN/MainInit.c`,
`game/RaceFlag.c`, `game/230/MM_Title.c` — but confirm with the user first, since opening
the PR is explicitly something they wanted to decide on themselves, not something to do
automatically once the code looks ready.
