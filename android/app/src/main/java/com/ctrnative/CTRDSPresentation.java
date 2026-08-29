package com.ctrnative;

import android.app.Presentation;
import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.os.Bundle;
import android.util.Log;
import android.view.Display;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.ViewGroup;
import android.view.WindowManager;

/**
 * The dual-screen companion panel.
 *
 * SDL owns the only SDL_Window and its Android backend refuses a second one
 * ("Android only supports one window"), so the bottom screen cannot be an SDL
 * window. It is a Presentation on the secondary display instead: this holds a
 * plain SurfaceView, and the Surface behind it is handed to native code, which
 * wraps it in an EGLSurface sharing SDL's EGLContext. The companion render
 * target is then blitted to that surface each frame.
 */
public class CTRDSPresentation extends Presentation implements SurfaceHolder.Callback {

    private static final String TAG = "CTR-DS";

    private SurfaceView surfaceView;

    public CTRDSPresentation(Context outerContext, Display display) {
        super(outerContext, display);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // The panel is driven entirely by GL; keep Android from drawing over it
        // and keep the screen alive while the game is running.
        if (getWindow() != null) {
            // FLAG_NOT_FOCUSABLE matters: without it this window takes key focus
            // on the secondary display and the game stops receiving input
            // entirely -- the panel is output only.
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
                    | WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                    | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE);
            getWindow().setBackgroundDrawable(new ColorDrawable(Color.BLACK));
        }

        surfaceView = new SurfaceView(getContext());
        surfaceView.setLayoutParams(new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        // Do NOT give the view a background. A SurfaceView punches a hole through
        // the window to show its own surface, and an opaque view background paints
        // straight over that hole -- the panel stays black no matter what GL draws
        // into it. Put the black on the window instead, and lift the surface above
        // the window so nothing composites on top of it.
        surfaceView.setZOrderOnTop(true);

        setContentView(surfaceView);
        surfaceView.getHolder().addCallback(this);
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        // Size is not final until surfaceChanged; wait for that.
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        Log.i(TAG, "companion surface " + width + "x" + height);
        CTRNativeActivity.nativeCompanionSurfaceChanged(holder.getSurface(), width, height);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        Log.i(TAG, "companion surface destroyed");
        CTRNativeActivity.nativeCompanionSurfaceDestroyed();
    }
}
