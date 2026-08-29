package com.ctrnative;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.view.MotionEvent;
import android.view.View;

/**
 * On-screen controls, shown only when no physical gamepad is connected.
 *
 * Drawn here rather than in the renderer: this way it sits at panel resolution
 * instead of the emulated 512x216, hit-testing is in the same coordinates as the
 * drawing, and none of it touches the PSX graphics pipeline.
 *
 * The button mask handed to native is active-low in PSX bit order, matching what
 * the pad itself produces, so the native side needs no translation.
 */
public class CTRTouchOverlay extends View {

    // PSX button bits. Same order as RAW_BTN_* in namespace_Gamepad.h.
    private static final int BTN_SELECT   = 0x0001;
    private static final int BTN_START    = 0x0008;
    private static final int BTN_UP       = 0x0010;
    private static final int BTN_RIGHT    = 0x0020;
    private static final int BTN_DOWN     = 0x0040;
    private static final int BTN_LEFT     = 0x0080;
    private static final int BTN_L1       = 0x0400;
    private static final int BTN_R1       = 0x0800;
    private static final int BTN_TRIANGLE = 0x1000;
    private static final int BTN_CIRCLE   = 0x2000;
    private static final int BTN_CROSS    = 0x4000;
    private static final int BTN_SQUARE   = 0x8000;

    private static final int ALL_RELEASED = 0xFFFF;

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint ring = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint label = new Paint(Paint.ANTI_ALIAS_FLAG);

    /** A round button: centre, radius, the bit it presses and its caption. */
    private static final class Btn {
        float cx, cy, r;
        final int bit;
        final String caption;
        int pointerId = -1;

        Btn(int bit, String caption) {
            this.bit = bit;
            this.caption = caption;
        }

        boolean contains(float x, float y) {
            final float dx = x - cx;
            final float dy = y - cy;
            // Generous hit area: fingers are imprecise and the art is small.
            final float hit = r * 1.35f;
            return (dx * dx + dy * dy) <= (hit * hit);
        }
    }

    private final Btn[] buttons = {
        new Btn(BTN_CROSS,    "X"),      // accelerate
        new Btn(BTN_SQUARE,   "□"), // brake
        new Btn(BTN_CIRCLE,   "O"),      // fire
        new Btn(BTN_TRIANGLE, "△"), // look back
        new Btn(BTN_L1,       "L1"),     // hop
        new Btn(BTN_R1,       "R1"),     // hop
        new Btn(BTN_START,    "START"),
        new Btn(BTN_SELECT,   "SELECT"),
    };

    // Analog stick.
    private float stickCx, stickCy, stickR, knobR;
    private int stickPointerId = -1;
    private float knobX, knobY;

    private int lastMask = ALL_RELEASED;
    private int lastX = 0;
    private int lastY = 0;

    public CTRTouchOverlay(Context context) {
        super(context);
        setFocusable(false);
        setClickable(false);

        fill.setStyle(Paint.Style.FILL);
        ring.setStyle(Paint.Style.STROKE);
        ring.setColor(Color.argb(150, 255, 255, 255));
        label.setColor(Color.argb(190, 255, 255, 255));
        label.setTextAlign(Paint.Align.CENTER);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        layoutControls(w, h);
    }

    private void layoutControls(int w, int h) {
        // Scale everything off the short edge so the controls stay the same
        // physical size whatever the aspect ratio is.
        final float unit = Math.min(w, h);
        final float r = unit * 0.085f;
        final float margin = unit * 0.07f;

        ring.setStrokeWidth(Math.max(2f, unit * 0.005f));
        label.setTextSize(r * 0.72f);

        stickR = unit * 0.20f;
        knobR = stickR * 0.42f;
        stickCx = margin + stickR;
        stickCy = h - margin - stickR;
        knobX = stickCx;
        knobY = stickCy;

        // Face buttons in a diamond, bottom right.
        final float faceCx = w - margin - r * 2.1f;
        final float faceCy = h - margin - r * 2.1f;
        final float spread = r * 1.85f;

        place(BTN_CROSS,    faceCx,          faceCy + spread, r);
        place(BTN_SQUARE,   faceCx - spread, faceCy,          r);
        place(BTN_CIRCLE,   faceCx + spread, faceCy,          r);
        place(BTN_TRIANGLE, faceCx,          faceCy - spread, r);

        // Shoulders above the face cluster.
        place(BTN_L1, margin + r, margin + r, r * 0.9f);
        place(BTN_R1, w - margin - r, margin + r, r * 0.9f);

        // Start/Select centred along the top, out of the way.
        place(BTN_START,  w * 0.5f + r * 1.6f, margin + r * 0.8f, r * 0.72f);
        place(BTN_SELECT, w * 0.5f - r * 1.6f, margin + r * 0.8f, r * 0.72f);
    }

    private void place(int bit, float cx, float cy, float r) {
        for (Btn b : buttons) {
            if (b.bit == bit) {
                b.cx = cx;
                b.cy = cy;
                b.r = r;
                return;
            }
        }
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);

        // Stick well, then the knob at its current offset.
        fill.setColor(Color.argb(60, 0, 0, 0));
        canvas.drawCircle(stickCx, stickCy, stickR, fill);
        canvas.drawCircle(stickCx, stickCy, stickR, ring);

        fill.setColor(Color.argb(stickPointerId >= 0 ? 170 : 110, 255, 255, 255));
        canvas.drawCircle(knobX, knobY, knobR, fill);

        for (Btn b : buttons) {
            final boolean down = b.pointerId >= 0;

            fill.setColor(down ? Color.argb(170, 255, 210, 120) : Color.argb(60, 0, 0, 0));
            canvas.drawCircle(b.cx, b.cy, b.r, fill);
            canvas.drawCircle(b.cx, b.cy, b.r, ring);

            // Vertically centre the caption on the button.
            final Paint.FontMetrics fm = label.getFontMetrics();
            final float baseline = b.cy - (fm.ascent + fm.descent) * 0.5f;
            canvas.drawText(b.caption, b.cx, baseline, label);
        }
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        final int action = event.getActionMasked();

        switch (action) {
        case MotionEvent.ACTION_DOWN:
        case MotionEvent.ACTION_POINTER_DOWN: {
            final int index = event.getActionIndex();
            claim(event.getPointerId(index), event.getX(index), event.getY(index));
            break;
        }

        case MotionEvent.ACTION_MOVE: {
            for (int i = 0; i < event.getPointerCount(); i++) {
                final int id = event.getPointerId(i);
                if (id == stickPointerId) {
                    moveStick(event.getX(i), event.getY(i));
                }
            }
            break;
        }

        case MotionEvent.ACTION_UP:
        case MotionEvent.ACTION_POINTER_UP:
        case MotionEvent.ACTION_CANCEL: {
            final int id = (action == MotionEvent.ACTION_CANCEL)
                    ? -1 : event.getPointerId(event.getActionIndex());
            release(id);
            break;
        }

        default:
            break;
        }

        push();
        invalidate();
        return true;
    }

    private void claim(int pointerId, float x, float y) {
        for (Btn b : buttons) {
            if (b.pointerId < 0 && b.contains(x, y)) {
                b.pointerId = pointerId;
                return;
            }
        }

        // Anything in the lower-left quadrant that missed a button grabs the
        // stick, so the thumb does not have to find the well exactly.
        if (stickPointerId < 0 && x < getWidth() * 0.5f && y > getHeight() * 0.35f) {
            stickPointerId = pointerId;
            moveStick(x, y);
        }
    }

    private void release(int pointerId) {
        for (Btn b : buttons) {
            if (pointerId < 0 || b.pointerId == pointerId) {
                b.pointerId = -1;
            }
        }

        if (pointerId < 0 || stickPointerId == pointerId) {
            stickPointerId = -1;
            knobX = stickCx;
            knobY = stickCy;
        }
    }

    private void moveStick(float x, float y) {
        float dx = x - stickCx;
        float dy = y - stickCy;

        final float len = (float) Math.sqrt(dx * dx + dy * dy);
        if (len > stickR) {
            dx = dx / len * stickR;
            dy = dy / len * stickR;
        }

        knobX = stickCx + dx;
        knobY = stickCy + dy;
    }

    /** Current state to native, only when it actually changed. */
    private void push() {
        int mask = ALL_RELEASED;

        for (Btn b : buttons) {
            if (b.pointerId >= 0) {
                mask &= ~b.bit;
            }
        }

        int ax = 0;
        int ay = 0;

        if (stickPointerId >= 0 && stickR > 0f) {
            final float nx = (knobX - stickCx) / stickR;
            final float ny = (knobY - stickCy) / stickR;

            ax = (int) (nx * 32767f);
            ay = (int) (ny * 32767f);

            // Also drive the d-pad: menus read it, and it is the path both
            // steering modes already go through.
            final float dead = 0.45f;
            if (nx < -dead) mask &= ~BTN_LEFT;
            if (nx >  dead) mask &= ~BTN_RIGHT;
            if (ny < -dead) mask &= ~BTN_UP;
            if (ny >  dead) mask &= ~BTN_DOWN;
        }

        if (mask != lastMask || ax != lastX || ay != lastY) {
            lastMask = mask;
            lastX = ax;
            lastY = ay;

            try {
                CTRNativeActivity.nativeTouchInput(mask, ax, ay);
            } catch (UnsatisfiedLinkError e) {
                // Native not loaded yet; the next event will carry the state.
            }
        }
    }
}
