package com.ctrnative;

import android.app.Activity;
import android.content.SharedPreferences;
import android.content.pm.ActivityInfo;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.TextView;
import android.graphics.Color;
import android.view.Gravity;
import android.widget.Toast;

public class SettingsActivity extends Activity {
    private static final String PREFS_NAME = "CTRNativePrefs";
    private static final String KEY_FILTER_MODE = "filterMode";
    private static final String KEY_TEXTURE_DUMP = "textureDump";
    private static final String KEY_TEXTURE_LOAD = "textureLoad";

    private void enableImmersiveMode() {
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        View decorView = getWindow().getDecorView();
        int flags = View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                  | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                  | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                  | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                  | View.SYSTEM_UI_FLAG_FULLSCREEN
                  | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY;
        decorView.setSystemUiVisibility(flags);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            getWindow().getAttributes().layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            enableImmersiveMode();
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        enableImmersiveMode();

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setBackgroundColor(Color.argb(230, 18, 18, 18));
        root.setPadding(dp(16), dp(16), dp(16), dp(16));

        TextView title = new TextView(this);
        title.setText(R.string.settings_title);
        title.setTextColor(Color.WHITE);
        title.setTextSize(24f);
        title.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams titleParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        titleParams.bottomMargin = dp(16);
        root.addView(title, titleParams);

        // Filter Mode Spinner
        TextView filterLabel = new TextView(this);
        filterLabel.setText(R.string.settings_filter_mode);
        filterLabel.setTextColor(Color.WHITE);
        filterLabel.setTextSize(16f);
        filterLabel.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams filterLabelParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        filterLabelParams.bottomMargin = dp(8);
        root.addView(filterLabel, filterLabelParams);

        Spinner filterSpinner = new Spinner(this);
        ArrayAdapter<CharSequence> adapter = ArrayAdapter.createFromResource(this,
                R.array.filter_mode_entries, android.R.layout.simple_spinner_item);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        filterSpinner.setAdapter(adapter);
        filterSpinner.setSelection(getFilterModePref());
        LinearLayout.LayoutParams spinnerParams = new LinearLayout.LayoutParams(
                dp(280),
                LinearLayout.LayoutParams.WRAP_CONTENT);
        spinnerParams.bottomMargin = dp(12);
        root.addView(filterSpinner, spinnerParams);

        filterSpinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                setFilterModePref(position);
                applyFilterMode(position);
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {}
        });

        // CheckBox: Texture Dump
        CheckBox dumpCheckBox = new CheckBox(this);
        dumpCheckBox.setText(R.string.settings_texture_dump);
        dumpCheckBox.setTextColor(Color.WHITE);
        dumpCheckBox.setTextSize(15f);
        dumpCheckBox.setChecked(getTextureDumpPref());
        dumpCheckBox.setOnCheckedChangeListener((buttonView, isChecked) -> {
            setTextureDumpPref(isChecked);
            applyTextureDump(isChecked ? 1 : 0);
        });
        LinearLayout.LayoutParams dumpParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        dumpParams.bottomMargin = dp(8);
        root.addView(dumpCheckBox, dumpParams);

        // CheckBox: Texture Load
        CheckBox loadCheckBox = new CheckBox(this);
        loadCheckBox.setText(R.string.settings_texture_load);
        loadCheckBox.setTextColor(Color.WHITE);
        loadCheckBox.setTextSize(15f);
        loadCheckBox.setChecked(getTextureLoadPref());
        loadCheckBox.setOnCheckedChangeListener((buttonView, isChecked) -> {
            setTextureLoadPref(isChecked);
            applyTextureLoad(isChecked ? 1 : 0);
        });
        LinearLayout.LayoutParams loadParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        loadParams.bottomMargin = dp(12);
        root.addView(loadCheckBox, loadParams);

        // Button: Reload Textures
        TextView reloadButton = new TextView(this);
        reloadButton.setText(R.string.settings_reload_textures);
        reloadButton.setTextColor(Color.CYAN);
        reloadButton.setTextSize(15f);
        reloadButton.setGravity(Gravity.CENTER);
        reloadButton.setPadding(dp(12), dp(8), dp(12), dp(8));
        reloadButton.setOnClickListener(v -> {
            reloadTextures();
            Toast.makeText(SettingsActivity.this, "Texturas recarregadas de crash/textura/load!", Toast.LENGTH_SHORT).show();
        });
        LinearLayout.LayoutParams reloadParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        reloadParams.bottomMargin = dp(16);
        root.addView(reloadButton, reloadParams);

        // Close button
        TextView closeButton = new TextView(this);
        closeButton.setText(R.string.settings_close);
        closeButton.setTextColor(Color.LTGRAY);
        closeButton.setTextSize(13f);
        closeButton.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams closeParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        closeParams.topMargin = dp(8);
        root.addView(closeButton, closeParams);

        closeButton.setOnClickListener(v -> finish());
        setContentView(root);

        // Apply current settings on load
        applyFilterMode(getFilterModePref());
        applyTextureDump(getTextureDumpPref() ? 1 : 0);
        applyTextureLoad(getTextureLoadPref() ? 1 : 0);
    }

    private int getFilterModePref() {
        SharedPreferences prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        return prefs.getInt(KEY_FILTER_MODE, 0);
    }

    private void setFilterModePref(int mode) {
        SharedPreferences.Editor editor = getSharedPreferences(PREFS_NAME, MODE_PRIVATE).edit();
        editor.putInt(KEY_FILTER_MODE, mode);
        editor.apply();
    }

    private boolean getTextureDumpPref() {
        SharedPreferences prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        return prefs.getBoolean(KEY_TEXTURE_DUMP, false);
    }

    private void setTextureDumpPref(boolean enabled) {
        SharedPreferences.Editor editor = getSharedPreferences(PREFS_NAME, MODE_PRIVATE).edit();
        editor.putBoolean(KEY_TEXTURE_DUMP, enabled);
        editor.apply();
    }

    private boolean getTextureLoadPref() {
        SharedPreferences prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        return prefs.getBoolean(KEY_TEXTURE_LOAD, true);
    }

    private void setTextureLoadPref(boolean enabled) {
        SharedPreferences.Editor editor = getSharedPreferences(PREFS_NAME, MODE_PRIVATE).edit();
        editor.putBoolean(KEY_TEXTURE_LOAD, enabled);
        editor.apply();
    }

    private native void applyFilterMode(int mode);
    private native void applyTextureDump(int enabled);
    private native void applyTextureLoad(int enabled);
    private native void reloadTextures();

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    @Override
    protected void onResume() {
        super.onResume();
        enableImmersiveMode();
    }
}
