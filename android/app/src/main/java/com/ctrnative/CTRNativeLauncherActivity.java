package com.ctrnative;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.util.ArrayList;
import java.util.List;

/**
 * The screen the app opens on: pick the disc, change the settings, then play.
 *
 * It stays rather than getting out of the way once a disc exists. Settings used
 * to live only in ctrds.cfg or behind Select during a race, neither of which is
 * reachable when you just want to change the resolution before starting.
 */
public final class CTRNativeLauncherActivity extends Activity {
    private static final int REQUEST_DISC_IMAGE = 1001;
    private static final int COPY_BUFFER_SIZE = 1024 * 1024;
    private static final int RAW_SECTOR_SIZE = 2352;
    private static final int PVD_LBA = 16;
    private static final int FORM1_DATA_OFFSET = 24;

    private static final int COLOR_BACKGROUND = Color.rgb(18, 18, 18);
    private static final int COLOR_PANEL = Color.rgb(30, 30, 34);
    private static final int COLOR_MUTED = Color.rgb(150, 155, 165);
    private static final int COLOR_ACCENT = Color.rgb(255, 150, 60);
    private static final int COLOR_WARN = Color.rgb(255, 160, 122);

    private Button importButton;
    private ProgressBar importProgress;
    private TextView statusText;
    private TextView discText;
    private Button playButton;
    private boolean importInProgress;

    private CTRDSSettings settings;
    private final List<Button> optionButtons = new ArrayList<>();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Before anything else that could throw.
        CTRCrashLog.install(getApplicationContext());

        settings = new CTRDSSettings(getConfigFile());
        buildLauncherView();
    }

    @Override
    protected void onResume() {
        super.onResume();

        // The game rewrites this file whenever a setting is changed from the
        // in-game menu, so coming back from a session means re-reading it rather
        // than showing what the launcher last knew.
        if (settings != null) {
            settings = new CTRDSSettings(getConfigFile());
            refreshOptionButtons();
            refreshDiscStatus();
        }
    }

    private File getStorageRoot() {
        File root = getExternalFilesDir(null);
        return (root != null) ? root : getFilesDir();
    }

    private File getAssetDirectory() {
        return new File(getStorageRoot(), "assets");
    }

    private File getImportedBinImage() {
        return new File(getAssetDirectory(), "ctr-u.bin");
    }

    private File getImportedChdImage() {
        return new File(getAssetDirectory(), "ctr-u.chd");
    }

    /** Whichever of the two kinds of disc image is actually present. */
    private File getImportedDiscImage() {
        File chd = getImportedChdImage();
        return chd.isFile() ? chd : getImportedBinImage();
    }

    /**
     * A CHD is a compressed disc image and cannot be checked the way a raw BIN
     * is -- its sectors are inside compressed hunks. The header magic is enough
     * to tell what it is; whether it holds the right game is a question for the
     * native side, which reads its filesystem.
     */
    private boolean isChdImage(File image) {
        if (!image.isFile() || (image.length() < 16)) {
            return false;
        }

        byte[] magic = new byte[8];
        try (RandomAccessFile file = new RandomAccessFile(image, "r")) {
            file.readFully(magic);
        } catch (IOException exception) {
            return false;
        }

        return (magic[0] == 'M') && (magic[1] == 'C') && (magic[2] == 'o') && (magic[3] == 'm')
                && (magic[4] == 'p') && (magic[5] == 'r') && (magic[6] == 'H') && (magic[7] == 'D');
    }

    private boolean isUsableDiscImage(File image) {
        return isChdImage(image) || isRetailDiscImage(image);
    }

    private File getConfigFile() {
        return new File(getAssetDirectory(), "ctrds.cfg");
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private TextView makeText(String text, float size, int color) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextColor(color);
        view.setTextSize(size);
        view.setGravity(Gravity.CENTER);
        view.setMaxWidth(dp(720));
        return view;
    }

    private TextView makeSectionLabel(String text) {
        TextView label = new TextView(this);
        label.setText(text);
        label.setTextColor(COLOR_ACCENT);
        label.setTextSize(12.0f);
        label.setLetterSpacing(0.18f);
        label.setGravity(Gravity.START);
        return label;
    }

    private LinearLayout makePanel() {
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setBackgroundColor(COLOR_PANEL);
        panel.setPadding(dp(16), dp(14), dp(16), dp(14));
        return panel;
    }

    private LinearLayout.LayoutParams stacked(int bottomMarginDp) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        params.bottomMargin = dp(bottomMarginDp);
        return params;
    }

    private void buildLauncherView() {
        ScrollView scroller = new ScrollView(this);
        scroller.setBackgroundColor(COLOR_BACKGROUND);
        scroller.setFillViewport(true);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(24), dp(28), dp(24), dp(28));

        TextView title = makeText("CTR-DS", 32.0f, Color.WHITE);
        title.setGravity(Gravity.START);
        root.addView(title, stacked(2));

        TextView subtitle = makeText(getString(R.string.launcher_subtitle), 14.0f, COLOR_MUTED);
        subtitle.setGravity(Gravity.START);
        root.addView(subtitle, stacked(22));

        root.addView(makeSectionLabel(getString(R.string.launcher_disc_section)), stacked(8));
        root.addView(buildDiscPanel(), stacked(22));

        root.addView(makeSectionLabel(getString(R.string.launcher_settings_section)), stacked(8));
        root.addView(buildSettingsPanel(), stacked(24));

        playButton = new Button(this);
        playButton.setText(R.string.launcher_play);
        playButton.setTextSize(18.0f);
        playButton.setOnClickListener(view -> startGame());
        root.addView(playButton, stacked(0));

        scroller.addView(root, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        setContentView(scroller);

        refreshDiscStatus();
    }

    private LinearLayout buildDiscPanel() {
        LinearLayout panel = makePanel();

        discText = makeText("", 15.0f, COLOR_MUTED);
        discText.setGravity(Gravity.START);
        panel.addView(discText, stacked(10));

        statusText = makeText(getString(R.string.setup_instructions), 13.0f, COLOR_MUTED);
        statusText.setGravity(Gravity.START);
        panel.addView(statusText, stacked(10));

        importProgress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        importProgress.setMax(100);
        importProgress.setVisibility(View.GONE);
        LinearLayout.LayoutParams progressParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(10));
        progressParams.bottomMargin = dp(12);
        panel.addView(importProgress, progressParams);

        importButton = new Button(this);
        importButton.setOnClickListener(view -> selectDiscImage());
        panel.addView(importButton);

        return panel;
    }

    private LinearLayout buildSettingsPanel() {
        LinearLayout panel = makePanel();
        optionButtons.clear();

        for (int i = 0; i < CTRDSSettings.OPTIONS.length; i++) {
            final CTRDSSettings.Option option = CTRDSSettings.OPTIONS[i];

            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);

            TextView label = new TextView(this);
            label.setText(option.label);
            label.setTextColor(Color.WHITE);
            label.setTextSize(15.0f);
            LinearLayout.LayoutParams labelParams = new LinearLayout.LayoutParams(
                    0, LinearLayout.LayoutParams.WRAP_CONTENT, 1.0f);
            row.addView(label, labelParams);

            final Button value = new Button(this);
            value.setMinWidth(dp(132));
            value.setText(settings.nameOf(option));

            // Tap advances, long-press goes back -- with five resolutions and
            // five frame caps, being able to step backwards saves a lot of
            // cycling past the one you wanted.
            value.setOnClickListener(view -> {
                settings.cycle(option, 1);
                value.setText(settings.nameOf(option));
                settings.save();
            });
            value.setOnLongClickListener(view -> {
                settings.cycle(option, -1);
                value.setText(settings.nameOf(option));
                settings.save();
                return true;
            });

            row.addView(value);
            optionButtons.add(value);

            LinearLayout.LayoutParams rowParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT);
            rowParams.bottomMargin = dp(i == (CTRDSSettings.OPTIONS.length - 1) ? 0 : 4);
            panel.addView(row, rowParams);
        }

        return panel;
    }

    private void refreshOptionButtons() {
        for (int i = 0; (i < optionButtons.size()) && (i < CTRDSSettings.OPTIONS.length); i++) {
            optionButtons.get(i).setText(settings.nameOf(CTRDSSettings.OPTIONS[i]));
        }
    }

    private void refreshDiscStatus() {
        if ((discText == null) || (importButton == null) || (playButton == null)) {
            return;
        }

        File disc = getImportedDiscImage();
        boolean present = disc.isFile();
        boolean valid = present && isUsableDiscImage(disc);

        if (valid) {
            int label = isChdImage(disc) ? R.string.launcher_disc_ready_chd : R.string.launcher_disc_ready;
            discText.setText(getString(label, disc.length() / (1024L * 1024L)));
            discText.setTextColor(Color.WHITE);
            statusText.setText(R.string.launcher_disc_replace_hint);
            statusText.setTextColor(COLOR_MUTED);
            importButton.setText(R.string.setup_replace_disc);
        } else {
            discText.setText(getString(present ? R.string.setup_invalid_disc : R.string.setup_no_disc));
            discText.setTextColor(present ? COLOR_WARN : COLOR_MUTED);
            statusText.setText(R.string.setup_instructions);
            statusText.setTextColor(COLOR_MUTED);
            importButton.setText(present ? R.string.setup_replace_disc : R.string.setup_select_disc);
        }

        playButton.setEnabled(valid);
        playButton.setAlpha(valid ? 1.0f : 0.4f);
    }

    private void startGame() {
        if (importInProgress) {
            return;
        }

        settings.save();
        launchGame();
    }

    private void selectDiscImage() {
        if (importInProgress) {
            return;
        }

        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        startActivityForResult(intent, REQUEST_DISC_IMAGE);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        if ((requestCode != REQUEST_DISC_IMAGE) || (resultCode != RESULT_OK) || (data == null) || (data.getData() == null)) {
            return;
        }

        importDiscImage(data.getData());
    }

    private long querySourceSize(Uri uri) {
        try (Cursor cursor = getContentResolver().query(uri, new String[] {OpenableColumns.SIZE}, null, null, null)) {
            if ((cursor != null) && cursor.moveToFirst() && !cursor.isNull(0)) {
                return cursor.getLong(0);
            }
        } catch (Exception | Error exception) {
            return -1;
        }

        return -1;
    }

    private void importDiscImage(Uri uri) {
        importInProgress = true;
        importButton.setEnabled(false);
        playButton.setEnabled(false);

        long sourceSize = querySourceSize(uri);
        importProgress.setIndeterminate(sourceSize <= 0);
        importProgress.setProgress(0);
        importProgress.setVisibility(View.VISIBLE);
        statusText.setText(R.string.setup_importing_disc);
        statusText.setTextColor(COLOR_MUTED);

        Thread worker = new Thread(() -> copyDiscImage(uri, sourceSize), "CTR disc import");
        worker.start();
    }

    private void copyDiscImage(Uri uri, long sourceSize) {
        File assetDirectory = getAssetDirectory();
        File temporary = new File(assetDirectory, "ctr-u.importing");

        try {
            if (!assetDirectory.isDirectory() && !assetDirectory.mkdirs()) {
                throw new IOException("Could not create the app asset directory");
            }

            if (temporary.exists() && !temporary.delete()) {
                throw new IOException("Could not replace an incomplete import");
            }

            ContentResolver resolver = getContentResolver();
            try (InputStream input = resolver.openInputStream(uri);
                 FileOutputStream output = new FileOutputStream(temporary)) {
                if (input == null) {
                    throw new IOException("The selected file could not be opened");
                }

                byte[] buffer = new byte[COPY_BUFFER_SIZE];
                long copied = 0;
                int lastProgress = -1;
                int read;

                while ((read = input.read(buffer)) != -1) {
                    if (read == 0) {
                        continue;
                    }

                    output.write(buffer, 0, read);
                    copied += read;

                    if (sourceSize > 0) {
                        int progress = (int)Math.min(100, copied * 100 / sourceSize);
                        if (progress != lastProgress) {
                            lastProgress = progress;
                            int displayProgress = progress;
                            runOnUiThread(() -> updateImportProgress(displayProgress));
                        }
                    }
                }

                output.getFD().sync();
            }

            // What it is decides what it gets called, and the native side looks
            // for both names.
            boolean chd = isChdImage(temporary);

            if (!chd && !isRetailDiscImage(temporary)) {
                throw new IOException("The selected file is not a raw NTSC-U BIN or a CHD image");
            }

            File destination = chd ? getImportedChdImage() : getImportedBinImage();
            File other = chd ? getImportedBinImage() : getImportedChdImage();

            // Only one disc image may be left behind, or which one loads becomes
            // a matter of which name the native side happens to look for first.
            if (other.exists() && !other.delete()) {
                throw new IOException("Could not remove the previous disc image");
            }

            if (destination.exists() && !destination.delete()) {
                throw new IOException("Could not replace the previous disc image");
            }

            if (!temporary.renameTo(destination)) {
                throw new IOException("Could not finish the disc image import");
            }

            runOnUiThread(this::finishImport);
        } catch (Exception exception) {
            temporary.delete();
            String detail = exception.getMessage();
            if ((detail == null) || detail.isEmpty()) {
                detail = exception.getClass().getSimpleName();
            }

            String error = detail;
            runOnUiThread(() -> showImportError(error));
        }
    }

    /**
     * Imports land back on the launcher rather than starting the game. Picking a
     * disc is usually the first thing you do, and the settings underneath are
     * the second -- jumping straight into the game skips past them.
     */
    private void finishImport() {
        importInProgress = false;
        importProgress.setVisibility(View.GONE);
        importButton.setEnabled(true);
        refreshDiscStatus();
    }

    private void updateImportProgress(int progress) {
        importProgress.setProgress(progress);
        statusText.setText(getString(R.string.setup_import_progress, progress));
    }

    private void showImportError(String error) {
        importInProgress = false;
        importProgress.setVisibility(View.GONE);
        importButton.setEnabled(true);
        refreshDiscStatus();

        // After refreshDiscStatus, so the reason the import failed is what stays
        // on screen rather than the generic instructions.
        importButton.setText(R.string.setup_select_another_disc);
        statusText.setText(error);
        statusText.setTextColor(COLOR_WARN);
    }

    private boolean isRetailDiscImage(File image) {
        if (!image.isFile() || (image.length() < (long)(PVD_LBA + 1) * RAW_SECTOR_SIZE) || ((image.length() % RAW_SECTOR_SIZE) != 0)) {
            return false;
        }

        byte[] sector = new byte[RAW_SECTOR_SIZE];
        try (RandomAccessFile file = new RandomAccessFile(image, "r")) {
            file.seek((long)PVD_LBA * RAW_SECTOR_SIZE);
            file.readFully(sector);
        } catch (IOException exception) {
            return false;
        }

        if ((sector[0] != 0) || (sector[11] != 0) || (sector[15] != 2)) {
            return false;
        }

        for (int i = 1; i < 11; i++) {
            if ((sector[i] & 0xff) != 0xff) {
                return false;
            }
        }

        return (sector[FORM1_DATA_OFFSET] == 1)
                && (sector[FORM1_DATA_OFFSET + 1] == 'C')
                && (sector[FORM1_DATA_OFFSET + 2] == 'D')
                && (sector[FORM1_DATA_OFFSET + 3] == '0')
                && (sector[FORM1_DATA_OFFSET + 4] == '0')
                && (sector[FORM1_DATA_OFFSET + 5] == '1')
                && (sector[FORM1_DATA_OFFSET + 6] == 1);
    }

    private void launchGame() {
        // Deliberately not finishing: coming back from the game lands on the
        // launcher again, which is where the settings are.
        startActivity(new Intent(this, CTRNativeActivity.class));
    }
}
