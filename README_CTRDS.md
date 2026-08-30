# CTR-DS

A fork of [CTR Native](https://github.com/Simon358/ctr-native-android) that adds a
second screen, a 64-bit build, and a set of graphics and frame-rate options.

CTR Native is itself a native port of Crash Team Racing (PS1, 1999) built on the
[CTR-ModSDK](https://github.com/CTR-tools/CTR-ModSDK) decompilation. This fork
does not change what the game is; it changes where it can run and what it can be
told to do.

## What this fork adds

**A companion screen.** On a dual-screen handheld the HUD, the live map and a
settings panel move to the bottom display, leaving the top one entirely to the
game. The map draws driver portraits rather than coloured dots, because there is
room for them.

**A 64-bit build.** The original targets 32-bit only. `CTR_FORCE_32BIT=OFF`
builds LP64, which is what lets it run on a device with no 32-bit runtime at all
— a modern phone, for instance. Pointers no longer fit the 32-bit fields the PSX
structures use, so they are resolved through a region registry instead.

**Frame caps above 30.** The game was written for a 30fps frame and a 60Hz
VBlank, and a great deal of it counts one or the other. Anything that does now
reads a clock that advances at the retail rate whatever the cap is, so a 120fps
race does not play at four times speed.

**Internal resolution up to 8x.** Geometry is drawn into a target of
`240 × scale` lines and presented to the panel. Past roughly 3x on a handheld
this is supersampling rather than detail.

**Widescreen**, an HD replacement path for the 2D HUD art, CHD disc images,
on-screen touch controls, and a launcher for picking a disc and settings without
editing a file.

## Devices this has run on

| device | notes |
|---|---|
| AYN Thor | dual-screen handheld; the companion display is the reason this fork exists |
| OnePlus 15 | 64-bit only, so it needs the LP64 build; single screen |
| Linux desktop | both 32- and 64-bit, used for most debugging |

## Building

The upstream [README.md](README.md) covers prerequisites and the standard build.
Two things are specific to this fork.

### 64-bit desktop

```sh
cmake -S . -B build64 -DCTR_FORCE_32BIT=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build64 -j$(nproc)
```

Put `ctr-u.bin` (or `ctr-u.chd`) in `build64/assets/` and run `build64/ctr_native`
from that directory.

### Android

```sh
cd android && ./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

The debug variant compiles the native side with `-O2` (see `app/build.gradle`).
Without that the Android plugin passes no `-O` flag at all, and a recompiled PS1
game runs roughly three to four times slower — enough to miss a 120fps cap that
it otherwise holds comfortably.

## Settings

`ctrds.cfg` lives beside the disc image — on Android at
`/sdcard/Android/data/com.ctrnative/files/assets/`. The launcher writes it, the
in-game panel writes it, and lines it does not recognise are preserved.

| key | values | meaning |
|---|---|---|
| `target_fps` | 30, 60, 90, 120, … | frame cap; VBlanks are emitted at this rate |
| `internal_scale` | 1–8 | render target is `240 × scale` lines |
| `aspect_mode` | 0, 1 | 0 is retail 4:3, 1 follows the display |
| `fxaa`, `crt` | 0, 1 | post-processing |
| `touch_controls` | 0, 1, 2 | off, on, automatic |
| `swap_face_buttons` | 0, 1 | swap cross and circle |
| `online*` | — | built but there is no public server to reach |

Not offered in the launcher, but read if present:

| key | meaning |
|---|---|
| `hd_art` | use replacement HUD art from `assets/hd/` |
| `hd_dump`, `hd_log` | author the replacements: dump icons out of VRAM, report indices |
| `skip_av` | disc has no intro video or XA audio (also auto-detected) |
| `prim_reject` | oversize primitive cull; `2` reports what it drops |
| `perf` | write per-frame bucket timings next to the config |
| `widescreen` | derived from the aspect; override only if you know why |

## Diagnostics

The frame line, on by default, is the fastest way to see what the game is doing:

```
FPS: 120.0 of 120 reachable (cap 120, 1 vblanks per flip, race, 6x) audio 60.0/s disc 0/0
```

`reachable` is the cap the current cadence allows — menus deliberately flip every
`cap/30` VBlanks, so 30 is the correct answer there and a lower number is not a
fault. `audio` should read 60/s at any cap; it is the rate the sound is consumed
at, and it made a doubled-speed bug visible that the ear had already caught.

A watchdog aborts the process if no VBlank is emitted for twelve seconds while
the window is in front. That is deliberate: a frozen process tells you nothing,
while an abort produces a tombstone with a native backtrace for every thread.
`CTRDS_WATCHDOG=0` disables it.

Setting `perf=1` writes `perf/frame_times.csv` with a column per timing bucket.

## Known issues

- Small flat lavender quads appear on some level geometry, and a rectangle in
  the lower-left of the main screen. Present on both 32- and 64-bit builds, at
  both 4:3 and widescreen, with the HD art path and the oversize-primitive cull
  both disabled — so none of those is the cause. Not yet found.
- The dual-screen path presents at the frame cap in menus while the game logic
  advances at 30, so the same frame is presented repeatedly. Wasted GPU rather
  than a speed fault.
- Online is implemented but has no server to connect to.

## Licence

Same as upstream; see [LICENSE](LICENSE).
