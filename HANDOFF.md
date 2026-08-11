# Handoff: Hor+ ultrawide work — visual glitch investigation in progress

Context dump for continuing this work on another machine. Written 2026-08-10.

## Where things stand

Branch `master`, 3 commits ahead of `origin/master` (this push includes them):
- `8c31e4a76` feat(renderer): Hor+ ultrawide support
- `303852ab6` fix(android): unbreak the Android and desktop builds
- `ea1b18805` fix(renderer): correct Hor+ math and fix the buffer overflow it exposed

Hor+ ultrawide is implemented and the crash it exposed is fixed and verified stable
(149s desktop drive + 100s+ Android demo mode + multiple relaunches, all crash-free).
**But the user tested the latest Android build and found real, unresolved visual bugs.**
Do not consider Hor+ "done" — do not open a PR upstream (`CTR-tools/ctr-native`) until
these are fixed and re-verified on device.

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
- `game/PushBuffer.c`: `PUSHBUFFER_WIDE_W()` (now parameterless) drives `pb->rect.w` for
  1-player and 2-player split; **4-player split (`total==3/4`) is deliberately left unwidened**
  (out of scope, still `0xfd`).

## The crash that Hor+ exposed (fixed, verified)

Widening the FOV meant more geometry visible on screen per frame than retail's narrower FOV
ever produced. This exposed a pre-existing, latent buffer-overflow bug:

- `game/226/226_00_DrawLevelOvr1P.c` has a manual bump allocator (clip-record buffer,
  `DrawLevelOvr1P_GetClipRecordCursor`/`SetClipRecordCursor`, backed by
  `data.PtrClipBuffer[0]`) that is **not** bounds-checked by default — each writer must
  explicitly call `DrawLevelOvr1P_HasClipRecordSpace(size)` first. 5 of 7
  `Write*RenderedClippedRecordAtOtEntry` functions were missing that check. Found via GDB
  hardware watchpoint on a Linux desktop debug build (`watch *(void**)expr`), confirmed the
  overflow was corrupting `sdata->PLYROBJECTLIST`. Fixed by adding the missing check
  (matching the pattern already present in the other 2 functions).
- Related/same-class fix in `game/MAIN/MainInit.c`: `gGT->ptrRenderBucketInstance` was
  pointed at `&rdata.s_STATIC_GNORMALZ[0] + 148`, i.e. 148 bytes into a **16-byte** static
  scratch array — a "PC memory headroom" shortcut that silently overflows once more render
  instances are queued per frame than retail's narrow FOV ever produced. Changed to a proper
  `MEMPACK_AllocMem` (the checkpoint system already relocates this pointer as a normal
  MEMPACK allocation, so this was clearly supposed to be a real allocation).

## OPEN BUG — visual glitches on real device (unresolved, this is the actual next task)

User tested the latest APK (confirmed not-stale via `dumpsys package` timestamp) on their
Odin 2 Portal (Android, real ultrawide device) and reported, verbatim:

> "las texturas tienen glichec, no muestra bien la bandera de carga, errores en el mapa,
> se pierden texturas y las cosas estan a la izquiera, el hud no centrado"
> "tambien no esta a la resolucion de la pantalla"

Translation: texture glitches, loading flag doesn't display right, map errors, lost
textures, things positioned to the left, HUD not centered, not filling screen resolution.

A screenshot from the fixed build during a race is saved at:
`/tmp/claude-1000/-home-leonardo/ff9b43ad-90dc-4d08-a36e-bbfa3f2cc33f/scratchpad/user_glitch_check.png`
(that scratchpad is session-local and **will not exist on another machine** — re-capture
a fresh screenshot there if needed, the important thing is what it showed, described below).

**What that screenshot actually shows (my analysis, not yet confirmed by code investigation):**

1. **HUD (TIME, LAP counter, driver-portrait icons, item count) is bunched into roughly the
   left 512px of the now-wider screen**, not spanning/repositioning for the wide viewport.
   This part is a **known, already-flagged, unfixed gap** — 2D HUD draw calls use hardcoded
   absolute pixel coordinates assumed for a 512-wide screen. Not a new regression, just
   unfinished "Stage 2" work. Low mystery here, just needs HUD-repositioning work.

2. **A large vertical rectangle of garbage/striped texture data** (looks like blue/white/
   green vertical stripes — like sampling the wrong region of a texture atlas or VRAM)
   overlaid on 3D world geometry (a canyon wall) during the race. This looks like a genuine
   NEW regression from the Hor+ change, not a pre-existing bug.

3. **The in-race minimap/radar HUD element appears as a large vertical stretched rectangle**
   full of dots (racer position markers) and diagonal lines, instead of a small corner box.
   Also looks like a genuine NEW regression, likely related to #2 (same underlying mechanism).

**Investigation hypothesis (NOT YET VERIFIED — this was the very next step when I had to
stop):** The render target texture in `platform/native_gpu.c`
(`NativeRenderer_BindMainRenderTarget`) is now sized from `activeDispEnv.disp.w`, which
changes at runtime (starts at 512 for the splash, then widens). Suspect either:
   (a) the minimap is implemented as its own small sub-viewport/second PushBuffer-style
       render and something in it inherited `PUSHBUFFER_WIDE_W()` or another
       width-dependent value it shouldn't have, causing it to stretch to full width instead
       of staying a small fixed box; or
   (b) the minimap and/or "loading flag" graphic are implemented as a direct VRAM-region
       blit (a sprite draw specifying a fixed source rect in VRAM pixel coordinates) and
       some OTHER piece of code — a texture atlas packer, a VRAM upload helper, a "second
       buffer" offset computed as e.g. `1024 - disp.w` — still assumes `disp.w` is always
       512, so when it's now dynamically wider, the packing/addressing math desyncs and the
       blit samples the wrong VRAM region, producing the striped garbage.

**Concrete next step:** grep `game/` and `platform/` for "minimap"/"radar"/"map" (CTR's
original PSX code may name this module something else — check numbered overlay files near
where the HUD is drawn, e.g. near `UI_RenderFrame.c` or similar), and for any VRAM-region
blit helper with hardcoded or `disp.w`-derived pixel offsets in `platform/native_gpu.c` /
`platform/native_libgpu.c`. Confirm which of (a)/(b) is the actual mechanism before touching
any code — this needs the same evidence-first approach used for the crash fix (the user was
explicit about this: "no, investiga mas, no me importa que tarde" — keep investigating with
real evidence, don't guess-and-patch).

I was about to launch an Explore agent to do exactly this grep/read pass when I had to stop
(session ending, user switching machines) — that agent call was interrupted before it ran.

## Testing setup notes (for the other machine)

- Android device: Odin 2 Portal, connected via `adb` (was WiFi-connected in this session
  under airplane mode + wifi, may need `adb connect <ip>` again elsewhere, or plug in via
  USB — check `adb devices`).
- App id: `com.ctrnative`.
- Desktop Linux build (X11) was used for real GDB debugging (hardware watchpoints) — Android
  lldb-server remote debugging was attempted and abandoned (protocol handshake failures,
  "Remote connection closed" after `qSupported`). Prefer desktop repro + GDB for anything
  crash/memory-related; use the Android device only for final visual/device verification.
- `xdotool` + `import` (ImageMagick) were used to script input and screenshot the desktop
  X11 window during testing.
- A baseline (pre-Hor+) comparison worktree was set up at `/tmp/ctr-nowide-check` (git
  worktree at commit `303852ab6`) specifically to check whether the glitches above are
  pre-existing or new — **that comparison was incomplete when I had to stop**: I had reached
  a menu/cutscene screen on the baseline build but not yet the same in-race location as the
  glitch screenshot. `/tmp` is not shared across machines — recreate with:
  `git worktree add /tmp/ctr-nowide-check 303852ab6` if useful, or just reason from the code.
- Unity build gotcha: `main.c` includes platform/game `.c` files directly (not separate
  translation units) — if you add a new platform file, remember to `#include` it in
  `main.c`, guarded by `#if defined(__ANDROID__)` etc. as appropriate.

## Standing goal (deferred until the above is fixed)

Once ultrawide is genuinely solid (visually clean + stable), consider a PR to upstream
`CTR-tools/ctr-native` with just the core non-Android files: `platform/native_renderer.c/h`,
`game/MAIN/MainMain.c`, `game/PushBuffer.c`, `game/226/226_00_DrawLevelOvr1P.c`,
`game/MAIN/MainInit.c`. Explicitly deferred by the user pending this investigation.
