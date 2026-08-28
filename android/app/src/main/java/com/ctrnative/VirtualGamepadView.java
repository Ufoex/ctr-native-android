package com.ctrnative;

import android.content.Context;
import android.content.Intent;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.os.Vibrator;
import android.view.MotionEvent;
import android.view.View;

public class VirtualGamepadView extends View {
    private Paint strokePaint;
    private Paint fillPaint;
    private Paint textPaint;
    private Paint progressPaint;

    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private static final long LONG_PRESS_DURATION_MS = 200;
    private long togglePressStartTime = 0;
    private boolean isToggleLongPressTriggered = false;
    private int togglePointerId = -1;
    private final RectF toggleProgressRect = new RectF();

    private final Runnable longPressRunnable = new Runnable() {
        @Override
        public void run() {
            if (toggleButton != null && toggleButton.pressed) {
                isToggleLongPressTriggered = true;
                toggleButton.pressed = false;
                togglePointerId = -1;
                invalidate();

                try {
                    Vibrator v = (Vibrator) getContext().getSystemService(Context.VIBRATOR_SERVICE);
                    if (v != null && v.hasVibrator()) {
                        v.vibrate(60);
                    }
                } catch (Exception ignored) {}

                // Open settings activity
                Intent intent = new Intent(getContext(), SettingsActivity.class);
                getContext().startActivity(intent);
            }
        }
    };

    public VirtualGamepadView(Context context) {
        super(context);

        int colorWhite = 0xAAFFFFFF; // Semi-transparent white

        strokePaint = new Paint();
        strokePaint.setColor(colorWhite);
        strokePaint.setStyle(Paint.Style.STROKE);
        strokePaint.setStrokeWidth(6f);
        strokePaint.setAntiAlias(true);

        fillPaint = new Paint();
        fillPaint.setColor(colorWhite);
        fillPaint.setStyle(Paint.Style.FILL_AND_STROKE);
        fillPaint.setAntiAlias(true);

        textPaint = new Paint();
        textPaint.setColor(colorWhite);
        textPaint.setTextSize(40f);
        textPaint.setTextAlign(Paint.Align.CENTER);
        textPaint.setFakeBoldText(true);
        textPaint.setAntiAlias(true);

        progressPaint = new Paint();
        progressPaint.setColor(0xFFFFD700); // Gold progress ring
        progressPaint.setStyle(Paint.Style.STROKE);
        progressPaint.setStrokeWidth(8f);
        progressPaint.setAntiAlias(true);
        progressPaint.setStrokeCap(Paint.Cap.ROUND);
    }

    class ButtonDef {
        String label;
        float x, y, rx, ry; 
        int type; // 0 = circle, 1 = rounded rect
        int buttonMask; // PSX button mask
        boolean pressed = false;
        ButtonDef(String label, float x, float y, float r, int buttonMask) {
            this.label = label; this.x = x; this.y = y; this.rx = r; this.ry = r; this.type = 0; this.buttonMask = buttonMask;
        }
        ButtonDef(String label, float x, float y, float rx, float ry, int type, int buttonMask) {
            this.label = label; this.x = x; this.y = y; this.rx = rx; this.ry = ry; this.type = type; this.buttonMask = buttonMask;
        }
    }

    ButtonDef[] buttons = null;
    ButtonDef dpadUp, dpadDown, dpadLeft, dpadRight;
    float defaultDpadX, defaultDpadY, dpadR;
    float currentDpadX, currentDpadY;
    float analogFingerX = 0, analogFingerY = 0;
    boolean analogPressed = false;
    int analogPointerId = -1;

    ButtonDef toggleButton;
    boolean controlsVisible = true;

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        float minDim = Math.min(w, h);
        float bw = minDim / 14f; // Button radius based on height
        if (bw > 90) bw = 90;

        textPaint.setTextSize(bw * 0.7f); // Scale text to fit button
        strokePaint.setStrokeWidth(bw * 0.08f);

        dpadR = bw * 1.6f; // Reduced size
        defaultDpadX = bw * 4.5f; // Moved forward (to the right)
        defaultDpadY = h - bw * 3.0f;
        currentDpadX = defaultDpadX;
        currentDpadY = defaultDpadY;
        float spacing = bw * 1.5f;

        dpadUp = new ButtonDef("^", defaultDpadX, defaultDpadY - spacing, bw, 0x10);
        dpadDown = new ButtonDef("v", defaultDpadX, defaultDpadY + spacing, bw, 0x40);
        dpadLeft = new ButtonDef("<", defaultDpadX - spacing, defaultDpadY, bw, 0x80);
        dpadRight = new ButtonDef(">", defaultDpadX + spacing, defaultDpadY, bw, 0x20);

        // Diamond layout with equal spacing
        float centerX = w - bw * 3.5f;
        float centerY = h - bw * 3.5f;
        float vSpacing = bw * 2.0f; // vertical spacing (Triangle to X)
        float hSpacing = bw * 2.0f; // horizontal spacing (Square to Circle) - same as vertical

        buttons = new ButtonDef[] {
            // Crash CTR button layout - based on PSX controller
            // PSX button masks: Square=0x8000, Circle=0x2000, Triangle=0x1000, Cross=0x4000
            new ButtonDef("△", centerX, centerY - vSpacing, bw, 0x1000), // Triangle (top)
            new ButtonDef("□", centerX - hSpacing, centerY, bw, 0x8000), // Square (left)
            new ButtonDef("〇", centerX + hSpacing, centerY, bw, 0x2000), // Circle (right)
            new ButtonDef("X", centerX, centerY + vSpacing, bw, 0x4000), // Cross (bottom)
            new ButtonDef("START", w / 2f + bw*2.5f, h - bw*1.5f, bw*1.4f, bw*0.55f, 1, 0x8), // Start
            new ButtonDef("SELECT", w / 2f - bw*2.5f, h - bw*1.5f, bw*1.4f, bw*0.55f, 1, 0x1), // Select
            new ButtonDef("L1", bw * 4f, bw * 1.2f, bw*2f, bw*0.7f, 1, 0x400), // L1
            new ButtonDef("R1", w - bw * 4f, bw * 1.2f, bw*2f, bw*0.7f, 1, 0x800), // R1
            dpadUp, dpadDown, dpadLeft, dpadRight
        };

        toggleButton = new ButtonDef("☰", w / 2f, bw * 1.2f, bw * 0.5f, 0);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (buttons == null || toggleButton == null) return;

        // Draw toggle button
        if (toggleButton.pressed) {
            canvas.drawCircle(toggleButton.x, toggleButton.y, toggleButton.rx, fillPaint);

            // Draw progress arc around the button
            long elapsed = SystemClock.uptimeMillis() - togglePressStartTime;
            float progress = Math.min(1.0f, (float) elapsed / (float) LONG_PRESS_DURATION_MS);
            float sweepAngle = progress * 360f;

            float ringPadding = toggleButton.rx * 0.4f;
            toggleProgressRect.set(
                    toggleButton.x - toggleButton.rx - ringPadding,
                    toggleButton.y - toggleButton.rx - ringPadding,
                    toggleButton.x + toggleButton.rx + ringPadding,
                    toggleButton.y + toggleButton.rx + ringPadding
            );

            progressPaint.setStrokeWidth(toggleButton.rx * 0.25f);
            canvas.drawArc(toggleProgressRect, -90f, sweepAngle, false, progressPaint);

            if (progress < 1.0f) {
                postInvalidateDelayed(16);
            }
        }
        canvas.drawCircle(toggleButton.x, toggleButton.y, toggleButton.rx, strokePaint);
        float oldSizeToggle = textPaint.getTextSize();
        textPaint.setTextSize(oldSizeToggle * 0.6f);
        float toggleTextY = toggleButton.y - ((textPaint.descent() + textPaint.ascent()) / 2);
        canvas.drawText(toggleButton.label, toggleButton.x, toggleTextY, textPaint);
        textPaint.setTextSize(oldSizeToggle);

        if (!controlsVisible) return;

        // Draw Analog Stick (replacing D-Pad)
        canvas.drawCircle(currentDpadX, currentDpadY, dpadR, strokePaint);

        float analogX = currentDpadX + analogFingerX;
        float analogY = currentDpadY + analogFingerY;
        float distSq = analogFingerX * analogFingerX + analogFingerY * analogFingerY;
        float maxDist = dpadR * 0.5f; // knob can move up to half radius
        if (distSq > maxDist * maxDist) {
            float dist = (float) Math.sqrt(distSq);
            float scale = maxDist / dist;
            analogX = currentDpadX + analogFingerX * scale;
            analogY = currentDpadY + analogFingerY * scale;
        }

        Paint knobPaint = new Paint(fillPaint);
        knobPaint.setStyle(Paint.Style.FILL);
        knobPaint.setColor(0xAAFFFFFF);
        canvas.drawCircle(analogX, analogY, dpadR * 0.5f, knobPaint);

        for (ButtonDef b : buttons) {
            if (b == dpadUp || b == dpadDown || b == dpadLeft || b == dpadRight) continue;

            Paint currentPaint = b.pressed ? fillPaint : strokePaint;
            int currentTextColor = b.pressed ? 0xFF000000 : 0xAAFFFFFF;
            textPaint.setColor(currentTextColor);

            if (b.type == 0) {
                canvas.drawCircle(b.x, b.y, b.rx, currentPaint);
            } else if (b.type == 1) {
                android.graphics.RectF rect = new android.graphics.RectF(b.x - b.rx, b.y - b.ry, b.x + b.rx, b.y + b.ry);
                canvas.drawRoundRect(rect, b.ry, b.ry, currentPaint);
            }

            if (b.type == 1) {
                float oldSize = textPaint.getTextSize();
                textPaint.setTextSize(oldSize * 0.6f);
                float textY = b.y - ((textPaint.descent() + textPaint.ascent()) / 2);
                canvas.drawText(b.label, b.x, textY, textPaint);
                textPaint.setTextSize(oldSize);
            } else {
                float textY = b.y - ((textPaint.descent() + textPaint.ascent()) / 2);
                canvas.drawText(b.label, b.x, textY, textPaint);
            }
        }
    }

    private boolean isTouchOverButton(float x, float y) {
        if (toggleButton != null) {
            float tx = x - toggleButton.x;
            float ty = y - toggleButton.y;
            if (tx * tx + ty * ty <= (toggleButton.rx * 2.0f) * (toggleButton.rx * 2.0f)) {
                return true;
            }
        }
        if (buttons != null) {
            for (int j = 0; j < 8; j++) {
                ButtonDef b = buttons[j];
                float bx = x - b.x;
                float by = y - b.y;
                if (b.type == 0) {
                    if (bx * bx + by * by <= (b.rx * 1.8f) * (b.rx * 1.8f)) {
                        return true;
                    }
                } else if (b.type == 1) {
                    if (Math.abs(bx) <= b.rx * 1.5f && Math.abs(by) <= b.ry * 2.0f) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (buttons == null || toggleButton == null) return true;

        boolean[] nextState = new boolean[buttons.length];

        boolean analogActive = false;
        float currentFingerX = 0;
        float currentFingerY = 0;

        int pointerCount = event.getPointerCount();
        int action = event.getActionMasked();
        int actionIndex = event.getActionIndex();
        int actionId = event.getPointerId(actionIndex);

        // Handle touch down on toggle button
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            float tx = event.getX(actionIndex);
            float ty = event.getY(actionIndex);
            float dtx = tx - toggleButton.x;
            float dty = ty - toggleButton.y;
            if (dtx * dtx + dty * dty <= (toggleButton.rx * 2.0f) * (toggleButton.rx * 2.0f)) {
                if (togglePointerId == -1) {
                    togglePointerId = actionId;
                    toggleButton.pressed = true;
                    isToggleLongPressTriggered = false;
                    togglePressStartTime = SystemClock.uptimeMillis();
                    mHandler.removeCallbacks(longPressRunnable);
                    mHandler.postDelayed(longPressRunnable, LONG_PRESS_DURATION_MS);
                    invalidate();
                }
            }
        }

        // Handle touch up / cancel on toggle button
        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP || action == MotionEvent.ACTION_CANCEL) {
            if (actionId == togglePointerId) {
                mHandler.removeCallbacks(longPressRunnable);
                if (!isToggleLongPressTriggered && toggleButton.pressed) {
                    controlsVisible = !controlsVisible;
                }
                toggleButton.pressed = false;
                togglePointerId = -1;
                isToggleLongPressTriggered = false;
                invalidate();
            }
        }

        // Handle movement on toggle button (cancel if dragged away)
        if (action == MotionEvent.ACTION_MOVE && togglePointerId != -1) {
            int pIdx = event.findPointerIndex(togglePointerId);
            if (pIdx != -1) {
                float px = event.getX(pIdx);
                float py = event.getY(pIdx);
                float dpx = px - toggleButton.x;
                float dpy = py - toggleButton.y;
                if (dpx * dpx + dpy * dpy > (toggleButton.rx * 2.5f) * (toggleButton.rx * 2.5f)) {
                    mHandler.removeCallbacks(longPressRunnable);
                    toggleButton.pressed = false;
                    togglePointerId = -1;
                    isToggleLongPressTriggered = false;
                    invalidate();
                }
            }
        }

        // Handle touch down for floating analog
        if (controlsVisible && (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN)) {
            if (actionId != togglePointerId) {
                float tx = event.getX(actionIndex);
                float ty = event.getY(actionIndex);
                // Left half of screen, below the L button, and not over any button
                if (tx < getWidth() / 2f && ty > getHeight() * 0.35f && !isTouchOverButton(tx, ty)) {
                    if (analogPointerId == -1) {
                        analogPointerId = actionId;
                        currentDpadX = tx;
                        currentDpadY = ty;
                        invalidate();
                    }
                }
            }
        }

        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP || action == MotionEvent.ACTION_CANCEL) {
            if (actionId == analogPointerId) {
                analogPointerId = -1;
                currentDpadX = defaultDpadX;
                currentDpadY = defaultDpadY;
                invalidate();
            }
        }

        for (int i = 0; i < pointerCount; i++) {
            if (action == MotionEvent.ACTION_POINTER_UP ||
                action == MotionEvent.ACTION_UP ||
                action == MotionEvent.ACTION_CANCEL) {
                if (actionIndex == i) continue;
            }

            int id = event.getPointerId(i);
            if (id == togglePointerId) continue;

            float x = event.getX(i);
            float y = event.getY(i);

            if (controlsVisible) {
                // Check Analog Stick
                if (id == analogPointerId) {
                    float dx = x - currentDpadX;
                    float dy = y - currentDpadY;
                    analogActive = true;
                    currentFingerX = dx;
                    currentFingerY = dy;
                    if (dy < -dpadR*0.3f) nextState[8] = true; // UP
                    if (dy > dpadR*0.3f) nextState[9] = true; // DOWN
                    if (dx < -dpadR*0.3f) nextState[10] = true; // LEFT
                    if (dx > dpadR*0.3f) nextState[11] = true; // RIGHT
                }

                // Check other buttons
                for (int j=0; j<8; j++) {
                    ButtonDef b = buttons[j];
                    float bx = x - b.x;
                    float by = y - b.y;
                    if (b.type == 0) {
                        if (bx*bx + by*by <= (b.rx*1.8f)*(b.rx*1.8f)) {
                            nextState[j] = true;
                        }
                    } else if (b.type == 1) {
                        if (Math.abs(bx) <= b.rx * 1.5f && Math.abs(by) <= b.ry * 2.0f) {
                            nextState[j] = true;
                        }
                    }
                }
            }
        }

        boolean changed = false;

        if (analogActive) {
            if (analogFingerX != currentFingerX || analogFingerY != currentFingerY || !analogPressed) changed = true;
            analogFingerX = currentFingerX;
            analogFingerY = currentFingerY;
            analogPressed = true;
        } else {
            if (analogPressed) changed = true;
            analogFingerX = 0;
            analogFingerY = 0;
            analogPressed = false;
        }

        for (int i=0; i<buttons.length; i++) {
            if (buttons[i].pressed != nextState[i]) {
                buttons[i].pressed = nextState[i];
                changed = true;
            }
        }

        // Calculate button mask and send to native
        int buttonMask = 0xffff;
        for (int i=0; i<buttons.length; i++) {
            if (buttons[i].pressed) {
                buttonMask &= ~buttons[i].buttonMask;
            }
        }
        CTRNativeActivity.nativeApplyTouchButtons(0, buttonMask);

        if (changed) invalidate();

        return true;
    }
}
