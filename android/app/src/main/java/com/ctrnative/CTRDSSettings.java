package com.ctrnative;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Reads and writes ctrds.cfg -- the same file the game itself reads at startup
 * and rewrites when a setting is changed from the in-game menu.
 *
 * Both sides own this file, so neither may assume it wrote the last version of
 * it. Lines that are not recognised here are carried through untouched: the
 * native side keeps its own keys and its explanatory comments in there, and a
 * launcher that helpfully tidied them away would be deleting settings it does
 * not know exist.
 */
final class CTRDSSettings {

    /** One row in the launcher, and one key in the file. */
    static final class Option {
        final String key;
        final String label;
        final int[] values;
        final String[] names;
        final int fallback;

        Option(String key, String label, int[] values, String[] names, int fallback) {
            this.key = key;
            this.label = label;
            this.values = values;
            this.names = names;
            this.fallback = fallback;
        }

        /** Index of the current value, or the fallback's index if it is not one we offer. */
        int indexOf(int value) {
            for (int i = 0; i < values.length; i++) {
                if (values[i] == value) {
                    return i;
                }
            }
            for (int i = 0; i < values.length; i++) {
                if (values[i] == fallback) {
                    return i;
                }
            }
            return 0;
        }
    }

    /** { min, min+step, ..., max }, generated instead of hand-typed since a few of
     * these runs are 20-40 values long. */
    private static int[] range(int min, int max, int step) {
        int[] values = new int[(max - min) / step + 1];
        for (int i = 0; i < values.length; i++) {
            values[i] = min + i * step;
        }
        return values;
    }

    private static String[] percentNames(int[] values) {
        String[] names = new String[values.length];
        for (int i = 0; i < values.length; i++) {
            names[i] = values[i] + "%";
        }
        return names;
    }

    private static final int[] SPEED_VALUES = range(10, 200, 10);
    private static final int[] TURN_VALUES = range(10, 400, 10);
    private static final int[] JUMP_VALUES = range(10, 300, 10);
    private static final int[] GRAVITY_VALUES = range(10, 300, 10);

    /**
     * Defaults here match the native side's. They are only reached when the file
     * has nothing to say about a key -- a first run, or a setting added since
     * the file was last written.
     */
    static final Option[] OPTIONS = new Option[] {
        new Option("internal_scale", "Resolution",
                new int[] { 1, 2, 3, 4, 5, 6, 7, 8 },
                // The PSX frame is 240 lines, so a step is 240 of them. Naming
                // the height says what the setting buys far better than a
                // multiplier does -- and it makes it obvious where the panel is
                // passed, after which it is supersampling rather than detail.
                new String[] { "240p (1x)", "480p (2x)", "720p (3x)", "960p (4x)",
                        "1200p (5x)", "1440p (6x)", "1680p (7x)", "1920p (8x)" }, 4),

        new Option("target_fps", "Frame cap",
                new int[] { 30, 60, 90, 120, 480 },
                new String[] { "30", "60", "90", "120", "Unlimited" }, 60),

        // 0 is the original 4:3; 1 follows whatever the display actually is,
        // which on a phone is rarely a preset worth naming.
        new Option("aspect_mode", "Aspect",
                new int[] { 0, 1 },
                new String[] { "4:3", "Widescreen" }, 1),

        new Option("fxaa", "FXAA", new int[] { 0, 1 }, new String[] { "Off", "On" }, 0),
        // Default 0, matching g_ctrds.crt in game/ctrds/ctrds.c.
        new Option("crt", "CRT filter", new int[] { 0, 1 }, new String[] { "Off", "On" }, 0),
        // Default 1 (on), matching g_ctrds.dither: PS1's own dither pattern,
        // which hides its 15-bit colour banding.
        new Option("dither", "Dither", new int[] { 0, 1 }, new String[] { "Off", "On" }, 1),

        new Option("hd_art", "Upscaled 2D art",
                new int[] { 0, 1 }, new String[] { "Off", "On" }, 1),

        new Option("swap_face_buttons", "Swap A/B and X/Y",
                new int[] { 0, 1 }, new String[] { "Off", "On" }, 1),

        new Option("touch_controls", "Touch controls",
                new int[] { 0, 1, 2 },
                new String[] { "Auto", "Always", "Never" }, 0),

        new Option("increase_draw_distance", "Draw distance",
                new int[] { 0, 1 }, new String[] { "Normal", "Far" }, 0),

        new Option("speed_stat_multiplier", "Speed", SPEED_VALUES, percentNames(SPEED_VALUES), 100),
        new Option("turn_stat_multiplier", "Turn", TURN_VALUES, percentNames(TURN_VALUES), 100),
        new Option("jump_stat_multiplier", "Jump", JUMP_VALUES, percentNames(JUMP_VALUES), 100),
        new Option("gravity_stat_multiplier", "Gravity", GRAVITY_VALUES, percentNames(GRAVITY_VALUES), 100),

        new Option("unlock_all_characters", "Unlock characters",
                new int[] { 0, 1 }, new String[] { "Off", "On" }, 0),
        new Option("unlock_all_gates", "Unlock gates",
                new int[] { 0, 1 }, new String[] { "Off", "On" }, 0),
        new Option("unlock_all_portals", "Unlock portals",
                new int[] { 0, 1 }, new String[] { "Off", "On" }, 0),

        new Option("skip_intro", "Skip intro", new int[] { 0, 1 }, new String[] { "Off", "On" }, 0),
        new Option("skip_hints", "Skip hints", new int[] { 0, 1 }, new String[] { "Off", "On" }, 0),

        // skip_av and prim_reject are deliberately not offered here.
        //
        // Neither is a preference. The missing intro and XA audio are a property
        // of the disc, not something to choose, and the native side already
        // detects it and writes its own marker; the oversize polygon cull is a
        // diagnostic for finding out whether the PSX size rule is what ate a
        // piece of geometry. Both stay settable by hand in ctrds.cfg, and any
        // line already there is preserved on save, so nothing changes for a
        // config that has one.
    };

    private final File file;
    private final List<String> lines = new ArrayList<>();
    private final Map<String, Integer> values = new LinkedHashMap<>();

    CTRDSSettings(File file) {
        this.file = file;
        load();
    }

    private void load() {
        lines.clear();
        values.clear();

        if (!file.isFile()) {
            return;
        }

        try (BufferedReader reader = new BufferedReader(new FileReader(file))) {
            String line;
            while ((line = reader.readLine()) != null) {
                lines.add(line);

                String key = keyOf(line);
                if (key == null) {
                    continue;
                }

                try {
                    values.put(key, Integer.parseInt(line.substring(line.indexOf('=') + 1).trim()));
                } catch (NumberFormatException ignored) {
                    // A value we cannot read is a value we must not rewrite.
                }
            }
        } catch (IOException ignored) {
            // An unreadable file is treated as an empty one: every option falls
            // back to its default and a save will write a complete file.
        }
    }

    /** The key of a `key = value` line, or null for comments and anything else. */
    private static String keyOf(String line) {
        String trimmed = line.trim();
        if (trimmed.isEmpty() || trimmed.startsWith("#")) {
            return null;
        }

        int equals = trimmed.indexOf('=');
        if (equals <= 0) {
            return null;
        }

        return trimmed.substring(0, equals).trim();
    }

    int get(Option option) {
        Integer stored = values.get(option.key);
        return (stored != null) ? stored : option.fallback;
    }

    void set(Option option, int value) {
        values.put(option.key, value);
    }

    /** Advances an option to its next value and returns what that now is. */
    int cycle(Option option, int direction) {
        int index = option.indexOf(get(option));
        int next = (index + direction + option.values.length) % option.values.length;

        set(option, option.values[next]);
        return option.values[next];
    }

    String nameOf(Option option) {
        return option.names[option.indexOf(get(option))];
    }

    boolean save() {
        List<String> out = new ArrayList<>();
        List<String> written = new ArrayList<>();

        // Existing lines keep their place and their comments; only the value
        // changes. Anything unrecognised passes through exactly as it was.
        for (String line : lines) {
            String key = keyOf(line);

            if ((key != null) && values.containsKey(key) && !written.contains(key)) {
                out.add(key + "=" + values.get(key));
                written.add(key);
            } else {
                out.add(line);
            }
        }

        for (Map.Entry<String, Integer> entry : values.entrySet()) {
            if (!written.contains(entry.getKey())) {
                out.add(entry.getKey() + "=" + entry.getValue());
            }
        }

        File parent = file.getParentFile();
        if ((parent != null) && !parent.isDirectory() && !parent.mkdirs()) {
            return false;
        }

        try (FileWriter writer = new FileWriter(file)) {
            for (String line : out) {
                writer.write(line);
                writer.write("\n");
            }
        } catch (IOException e) {
            return false;
        }

        // Keep the in-memory copy consistent with what is now on disk, so a
        // second save in the same session does not duplicate keys.
        lines.clear();
        lines.addAll(out);
        return true;
    }
}
