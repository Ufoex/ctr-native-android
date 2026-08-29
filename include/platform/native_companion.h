#ifndef NATIVE_COMPANION_H
#define NATIVE_COMPANION_H

// CTR-DS companion display.
//
// SDL's Android backend allows exactly one window, so the bottom screen cannot
// be an SDL window. Java opens a Presentation on the secondary display and
// hands its Surface down here; this wraps that Surface in an EGLSurface built
// against SDL's own EGLContext and config, so both surfaces share every GL
// object -- crucially the VRAM texture the panel is blitted from.
//
// Off Android these are no-ops and the desktop build previews the panel by
// stacking both rectangles in the one window instead.

// Asks Java to open the Presentation. Safe to call repeatedly; does nothing
// until the renderer has a GL context.
void NativeCompanion_Init(void);

// True once the Presentation's surface exists and an EGLSurface was made for it.
int NativeCompanion_IsReady(void);

// Blits a VRAM rectangle to the companion display and swaps it, leaving the
// main window's surface current again.
void NativeCompanion_Present(int vramX, int vramY, int vramW, int vramH);

void NativeCompanion_Shutdown(void);

#endif
