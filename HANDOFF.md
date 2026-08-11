# Handoff: Hor+ ultrawide work — fixed and verified on both Linux desktop and Android device

Context dump for continuing this work. Rewritten 2026-08-10 (second pass, different
machine — `ufoex`, CachyOS, real 3440x1440 ultrawide monitor + a dedicated 4K OLED second
monitor available, both used for testing this session; Android toolchain (JDK 17, SDK,
NDK r27, CMake 3.22.1) installed fresh this session at `~/Android/Sdk`, and the user's
Odin 2 Portal (Android 13, Adreno 740, arm64 device that also supports armeabi-v7a) used for
on-device verification over USB).

## Where things stand

Branch `master`, on top of `origin/master`'s `7eab5a6af` (docs: add handoff notes).
Working tree currently has THREE validated, uncommitted fixes (not yet committed):
- `game/RaceFlag.c`: re-center the loading/countdown flag's GTE offset for Hor+.
- `game/230/MM_Title.c`: re-center the main-menu text box for Hor+.
- `platform/native_gpu.c`: clamp the framebuffer-feedback self-texturing overlap check to
  PS1's real 512-wide bound — **this was the actual root cause of the "canyon-wall texture
  glitch" / minimap-corruption bug from the original user report.**

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

## Fixes made this session (uncommitted, validated by visual A/B testing)

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
