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
import android.view.Surface;
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
        requestHighestRefreshRate(getWindow(), getWindowManager().getDefaultDisplay());
        checkStoragePermission();
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
                    Log.w(CTRDS_TAG, "no presentation display; companion stays off");
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
