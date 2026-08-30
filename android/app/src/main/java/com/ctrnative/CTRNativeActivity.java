package com.ctrnative;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.hardware.display.DisplayManager;
import android.util.Log;
import android.view.Display;
import android.view.View;
import android.view.Surface;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;

public class CTRNativeActivity extends SDLActivity {
    private static final int PICK_DIRECTORY_REQUEST = 1001;
    private static final String PREFS_NAME = "CTRNativePrefs";
    private static final String KEY_ASSET_PATH = "assetPath";

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL3",
            "ctr_native"
        };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // The game half needs this as much as the launcher does: a native crash
        // after PLAY looks identical to a launcher crash from the outside.
        CTRCrashLog.install(getApplicationContext());

        requestHighestRefreshRate(getWindow(), getWindowManager().getDefaultDisplay());
        enterImmersiveMode();

        startTouchControls();

        // Open the panel now rather than waiting for the first EndScene: the
        // boot splash presents VRAM directly and never reaches that path, so the
        // bottom screen would otherwise stay dark until the Naughty Dog logo.
        // The Presentation's own background is black, so the panel lights up
        // immediately and the idle art follows once assets are loaded.
        startCompanionDisplay();

        checkStoragePermission();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            // The bars come back on their own after a swipe or a focus change,
            // and each time they do the window shrinks and the game letterboxes.
            enterImmersiveMode();
        }
    }

    /**
     * Theme.NoTitleBar.Fullscreen no longer hides the status and gesture-nav
     * bars on modern Android: they stay, the window comes back short of the
     * panel height (1920x970 rather than 1920x1080), and the game is letterboxed
     * inside it. Hide them explicitly so the window is the whole display.
     */
    private void enterImmersiveMode() {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                getWindow().setDecorFitsSystemWindows(false);

                WindowInsetsController controller = getWindow().getInsetsController();
                if (controller != null) {
                    controller.hide(WindowInsets.Type.systemBars());
                    controller.setSystemBarsBehavior(
                            WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                }
            } else {
                getWindow().getDecorView().setSystemUiVisibility(
                        android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                                | android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                                | android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                                | android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                                | android.view.View.SYSTEM_UI_FLAG_FULLSCREEN
                                | android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
            }

            // Draw into the display cutout area too, rather than letterboxing
            // away from it.
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                WindowManager.LayoutParams lp = getWindow().getAttributes();
                lp.layoutInDisplayCutoutMode =
                        WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
                getWindow().setAttributes(lp);
            }
        } catch (Exception e) {
            Log.e(CTRDS_TAG, "immersive mode failed: " + e.getMessage());
        }
    }

    private CTRTouchOverlay touchOverlay;
    private Handler touchHandler;

    /**
     * Adds the on-screen controls and keeps their visibility in step with
     * whether a physical pad is attached. Polled rather than event-driven
     * because SDL owns the controller callbacks and the answer only needs to be
     * right within a second or two.
     */
    private void startTouchControls() {
        try {
            touchOverlay = new CTRTouchOverlay(this);
            touchOverlay.setVisibility(View.GONE);

            addContentView(touchOverlay, new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    Gravity.FILL));

            touchHandler = new Handler(Looper.getMainLooper());
            touchHandler.postDelayed(new Runnable() {
                @Override
                public void run() {
                    updateTouchControls();
                    applyPreferredFrameRate();
                    touchHandler.postDelayed(this, 1500);
                }
            }, 2500);
        } catch (Exception e) {
            Log.e(CTRDS_TAG, "touch controls unavailable: " + e.getMessage());
        }
    }

    private void updateTouchControls() {
        if (touchOverlay == null) {
            return;
        }

        int pads;
        try {
            pads = nativeGetGamepadCount();
        } catch (UnsatisfiedLinkError e) {
            Log.e(CTRDS_TAG, "nativeGetGamepadCount not bound: " + e.getMessage());
            return;
        }

        int mode = 0;
        try {
            mode = nativeTouchControlsMode();
        } catch (UnsatisfiedLinkError e) {
            // leave on automatic
        }

        final boolean wanted = (mode == 1) || ((mode != 2) && (pads == 0));
        final boolean shown = touchOverlay.getVisibility() == View.VISIBLE;

        if (wanted != shown) {
            touchOverlay.setVisibility(wanted ? View.VISIBLE : View.GONE);

            try {
                nativeTouchSetActive(wanted);
            } catch (UnsatisfiedLinkError e) {
                // native not up yet
            }

            Log.i(CTRDS_TAG, "on-screen controls " + (wanted ? "shown" : "hidden")
                    + " (gamepads=" + pads + ", mode=" + mode + ")");
        }
    }

    private int lastRequestedFrameRate = 0;

    /**
     * Asks the display to run at the configured cap.
     *
     * preferredDisplayModeId, which requestHighestRefreshRate sets, is a hint
     * that modern Android and OEM refresh-rate managers routinely override --
     * this device asks for 165Hz at startup and gets 90. Surface.setFrameRate is
     * the API they actually honour for games, and it names a rate rather than a
     * mode, so the panel can pick whichever mode serves it.
     *
     * Repeated from the same poll as the touch overlay because the surface does
     * not exist yet in onCreate, and because the cap can change between runs.
     */
    private void applyPreferredFrameRate() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            return;
        }

        int fps;
        try {
            fps = nativeTargetFps();
        } catch (UnsatisfiedLinkError e) {
            return;
        }

        if ((fps <= 0) || (fps == lastRequestedFrameRate)) {
            return;
        }

        try {
            if (mSurface == null) {
                return;
            }

            Surface surface = mSurface.getHolder().getSurface();
            if ((surface == null) || !surface.isValid()) {
                return;
            }

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                surface.setFrameRate((float) fps,
                        Surface.FRAME_RATE_COMPATIBILITY_DEFAULT,
                        Surface.CHANGE_FRAME_RATE_ALWAYS);
            } else {
                surface.setFrameRate((float) fps, Surface.FRAME_RATE_COMPATIBILITY_DEFAULT);
            }

            lastRequestedFrameRate = fps;

            float actual = getWindowManager().getDefaultDisplay().getRefreshRate();
            Log.i(CTRDS_TAG, "asked the surface for " + fps + "Hz; display reports " + actual + "Hz");
        } catch (Exception e) {
            Log.e(CTRDS_TAG, "setFrameRate failed: " + e.getMessage());
        }
    }

    private void checkStoragePermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                try {
                    Intent intent = new Intent(android.provider.Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                    intent.addCategory("android.intent.category.DEFAULT");
                    intent.setData(Uri.parse(String.format("package:%s", getApplicationContext().getPackageName())));
                    startActivity(intent);
                } catch (Exception e) {
                    Intent intent = new Intent();
                    intent.setAction(android.provider.Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
                    startActivity(intent);
                }
            }
        }
    }

    private static boolean pickerActive = false;

    public static void pickFile() {
        Activity activity = (Activity) SDLActivity.getContext();
        pickerActive = true;
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        // Filter for .bin and .BIG extensions is hard to do via intent, 
        // we'll check the result instead.
        activity.startActivityForResult(intent, PICK_DIRECTORY_REQUEST);
    }

    public static boolean isPickerActive() {
        return pickerActive;
    }

    public static String getStoredAssetPath() {
        Activity activity = (Activity) SDLActivity.getContext();
        SharedPreferences prefs = activity.getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        String path = prefs.getString(KEY_ASSET_PATH, null);
        Log.d("CTRNative", "getStoredAssetPath: " + path);
        return path;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == PICK_DIRECTORY_REQUEST) {
            pickerActive = false;
            if (resultCode == RESULT_OK && data != null) {
                Uri uri = data.getData();
                if (uri == null) return;
                try {
                    getContentResolver().takePersistableUriPermission(uri,
                            Intent.FLAG_GRANT_READ_URI_PERMISSION);

                    String displayName = getFileName(uri);
                    if (displayName != null && (displayName.toLowerCase().endsWith(".bin") || displayName.toLowerCase().endsWith(".big"))) {
                        copyFileToInternal(uri, displayName);

                        File assetsDir = new File(getFilesDir(), "assets");
                        SharedPreferences.Editor editor = getSharedPreferences(PREFS_NAME, MODE_PRIVATE).edit();
                        editor.putString(KEY_ASSET_PATH, assetsDir.getAbsolutePath());
                        editor.apply();
                        Log.d("CTRNative", "Stored internal assets dir: " + assetsDir.getAbsolutePath());
                        runOnUiThread(() -> android.widget.Toast.makeText(this, "Assets copied to internal storage. Please restart the app.", android.widget.Toast.LENGTH_LONG).show());
                    } else {
                        String path = getPathFromUri(uri);
                        if (path != null) {
                            File file = new File(path);
                            String parentDir = file.getParent();
                            if (parentDir != null) {
                                SharedPreferences.Editor editor = getSharedPreferences(PREFS_NAME, MODE_PRIVATE).edit();
                                editor.putString(KEY_ASSET_PATH, parentDir);
                                editor.apply();
                                Log.d("CTRNative", "Stored parent dir: " + parentDir);
                                runOnUiThread(() -> android.widget.Toast.makeText(this, "Assets location saved. Please restart the app.", android.widget.Toast.LENGTH_LONG).show());
                            }
                        } else {
                            Log.e("CTRNative", "Failed to resolve path from URI: " + uri.toString());
                        }
                    }
                } catch (Exception e) {
                    Log.e("CTRNative", "Error processing selected file: " + e.getMessage());
                }
            }
        }
    }

    private String getFileName(Uri uri) {
        String result = null;
        if (uri.getScheme().equals("content")) {
            try (android.database.Cursor cursor = getContentResolver().query(uri, null, null, null, null)) {
                if (cursor != null && cursor.moveToFirst()) {
                    int index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                    if (index != -1) {
                        result = cursor.getString(index);
                    }
                }
            }
        }
        if (result == null) {
            result = uri.getPath();
            int cut = result.lastIndexOf('/');
            if (cut != -1) {
                result = result.substring(cut + 1);
            }
        }
        return result;
    }

    private void copyFileToInternal(Uri uri, String fileName) {
        File assetsDir = new File(getFilesDir(), "assets");
        if (!assetsDir.exists()) {
            assetsDir.mkdirs();
        }
        File outFile = new File(assetsDir, fileName);
        try (InputStream in = getContentResolver().openInputStream(uri);
             FileOutputStream out = new FileOutputStream(outFile)) {
            byte[] buffer = new byte[16384];
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }
            Log.d("CTRNative", "Copied file to: " + outFile.getAbsolutePath());
        } catch (Exception e) {
            Log.e("CTRNative", "Failed to copy file: " + e.getMessage());
        }
    }

    private String getPathFromUri(Uri uri) {
        if (uri == null) return null;
        
        String path = null;
        String host = uri.getHost();
        String docId = null;
        
        try {
            if ("com.android.externalstorage.documents".equals(host)) {
                docId = DocumentsContract.getDocumentId(uri);
                final String[] split = docId.split(":");
                final String type = split[0];
                if ("primary".equalsIgnoreCase(type)) {
                    path = Environment.getExternalStorageDirectory() + "/" + (split.length > 1 ? split[1] : "");
                } else {
                    // Secondary SD cards
                    path = "/storage/" + type + "/" + (split.length > 1 ? split[1] : "");
                }
            } else if ("com.android.providers.downloads.documents".equals(host)) {
                docId = DocumentsContract.getDocumentId(uri);
                if (docId.startsWith("raw:")) {
                    path = docId.substring(4);
                } else {
                    path = Environment.getExternalStorageDirectory() + "/Download/" + docId;
                }
            }
        } catch (Exception e) {
            Log.e("CTRNative", "Error in getPathFromUri: " + e.getMessage());
        }
        
        if (path != null) {
            path = path.replace("//", "/");
            if (path.endsWith("/")) {
                path = path.substring(0, path.length() - 1);
            }
        }

        return path;
    }

    // ---------------------------------------------------------------- CTR-DS
    //
    // Dual-screen companion panel. Native code calls startCompanionDisplay()
    // once the renderer is up; this opens a Presentation on the secondary
    // display and hands its Surface back down so native can make an EGLSurface
    // out of it. Everything here has to happen on the UI thread, while the
    // caller is on the game thread.

    private static final String CTRDS_TAG = "CTR-DS";

    /**
     * Android hands an app the display's default mode, which on this device is
     * 60Hz even though both panels also advertise 120Hz. Ask for the highest
     * refresh rate available at the mode's current resolution; without this the
     * frame loop can never exceed 60fps no matter how it is paced.
     */
    static void requestHighestRefreshRate(android.view.Window window, Display display) {
        if (window == null || display == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return;
        }

        try {
            Display.Mode current = display.getMode();
            Display.Mode best = current;

            for (Display.Mode mode : display.getSupportedModes()) {
                if (mode.getPhysicalWidth() == current.getPhysicalWidth()
                        && mode.getPhysicalHeight() == current.getPhysicalHeight()
                        && mode.getRefreshRate() > best.getRefreshRate()) {
                    best = mode;
                }
            }

            WindowManager.LayoutParams lp = window.getAttributes();
            lp.preferredDisplayModeId = best.getModeId();
            window.setAttributes(lp);

            Log.i(CTRDS_TAG, "display " + display.getDisplayId() + " requested "
                    + best.getRefreshRate() + "Hz (mode " + best.getModeId() + ")");
        } catch (Exception e) {
            Log.e(CTRDS_TAG, "refresh rate request failed: " + e.getMessage());
        }
    }
    private static CTRDSPresentation ctrdsPresentation;

    public static native void nativeCompanionSurfaceChanged(Surface surface, int width, int height);

    public static native void nativeCompanionSurfaceDestroyed();

    /** Tells native this device has no second screen, so the HUD stays on the main one. */
    public static native void nativeCompanionUnavailable();

    /** On-screen controls -> pad. Mask is active-low in PSX bit order. */
    public static native void nativeTouchInput(int buttonMask, int stickX, int stickY);

    public static native void nativeTouchSetActive(boolean active);

    /** The configured frame cap, used to ask the display for a matching rate. */
    public static native int nativeTargetFps();

    public static native int nativeGetGamepadCount();

    /** 0 auto, 1 always, 2 never -- from touch_controls in ctrds.cfg. */
    public static native int nativeTouchControlsMode();

    /** A tap on the bottom screen, normalised 0..1. Drives the settings list. */
    public static native void nativePanelTap(float nx, float ny);

    /** Called from native. Safe to call more than once. */
    public static void startCompanionDisplay() {
        final Activity activity = (Activity) SDLActivity.getContext();
        if (activity == null) {
            Log.e(CTRDS_TAG, "no activity; cannot open the companion display");
            return;
        }

        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (ctrdsPresentation != null && ctrdsPresentation.isShowing()) {
                    return;
                }

                DisplayManager dm = (DisplayManager) activity.getSystemService(DISPLAY_SERVICE);
                if (dm == null) {
                    Log.e(CTRDS_TAG, "no DisplayManager");
                    return;
                }

                // DISPLAY_CATEGORY_PRESENTATION is the sanctioned way to find a
                // secondary panel; on the AYN Thor this is display 4, "Screen-2",
                // which reports FLAG_PRESENTATION.
                Display[] displays = dm.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION);
                if (displays == null || displays.length == 0) {
                    Log.w(CTRDS_TAG, "no presentation display; running single-screen");
                    try {
                        nativeCompanionUnavailable();
                    } catch (UnsatisfiedLinkError e) {
                        Log.e(CTRDS_TAG, "nativeCompanionUnavailable missing: " + e.getMessage());
                    }
                    return;
                }

                Display target = displays[0];
                Log.i(CTRDS_TAG, "companion display id=" + target.getDisplayId() + " name=" + target.getName());

                try {
                    ctrdsPresentation = new CTRDSPresentation(activity, target);
                    ctrdsPresentation.show();
                } catch (Exception e) {
                    Log.e(CTRDS_TAG, "failed to show companion presentation: " + e.getMessage());
                    ctrdsPresentation = null;
                }
            }
        });
    }

    /** Called from native on shutdown. */
    public static void stopCompanionDisplay() {
        final Activity activity = (Activity) SDLActivity.getContext();
        if (activity == null) {
            return;
        }

        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (ctrdsPresentation != null) {
                    try {
                        ctrdsPresentation.dismiss();
                    } catch (Exception e) {
                        Log.e(CTRDS_TAG, "dismiss failed: " + e.getMessage());
                    }
                    ctrdsPresentation = null;
                }
            }
        });
    }
}
