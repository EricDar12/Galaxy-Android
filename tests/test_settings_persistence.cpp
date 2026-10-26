#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <string>

#include "Settings.h"
#include "SettingsCodec.h"

using mobile_ui::Settings;
using mobile_ui::applySettingLine;
using mobile_ui::gameSpeedIndexForMultiplier;
using mobile_ui::gameSpeedMultiplier;
using mobile_ui::serializeSettings;

// These tests drive the same parser and serializer Settings::load/save use,
// while keeping the Android file/logging wrapper out of the host build.

static size_t applyText(Settings& settings, const char* text) {
    size_t applied = 0;
    const char* cursor = text;
    while (*cursor) {
        const char* end = strchr(cursor, '\n');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        std::string line(cursor, length);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (applySettingLine(settings, line.c_str())) applied++;
        if (!end) break;
        cursor = end + 1;
    }
    return applied;
}

static size_t countLines(const std::string& text) {
    size_t lines = 0;
    for (char c : text) {
        if (c == '\n') lines++;
    }
    return lines;
}

static bool near(float a, float b) {
    return fabsf(a - b) < 0.001f;
}

static void testCleanProfileDefaults(void) {
    Settings settings;
    assert(settings.gameSpeed == 1);
    assert(gameSpeedMultiplier(settings.gameSpeed) == 1.0f);
    assert(settings.targetFps == 60);
    assert(settings.opacity == 0.65f);
    assert(settings.haptics == false);

    applyText(settings, "");
    assert(settings.gameSpeed == 1);
    assert(gameSpeedMultiplier(settings.gameSpeed) == 1.0f);
    printf("[PASS] Clean profile keeps the 1.0x default when no settings file exists\n");
}

static void testSaveLoadRoundTrip(void) {
    Settings original;
    original.gameSpeed = 4;
    original.targetFps = 90;
    original.preferredRefreshRate = 120;
    original.haptics = true;
    original.opacity = 0.5f;
    original.stickOffsetX = -12.5f;
    original.modFly = true;
    original.showControls = false;
    original.fullTouchMode = false;

    const std::string text = serializeSettings(original);
    Settings restored;
    const size_t applied = applyText(restored, text.c_str());

    assert(applied == countLines(text));
    assert(restored.gameSpeed == 4);
    assert(gameSpeedMultiplier(restored.gameSpeed) == 3.0f);
    assert(restored.targetFps == 90);
    assert(restored.preferredRefreshRate == 120);
    assert(restored.haptics == true);
    assert(near(restored.opacity, 0.5f));
    assert(near(restored.stickOffsetX, -12.5f));
    assert(restored.modFly == true);
    assert(restored.showControls == false);
    assert(restored.fullTouchMode == false);
    assert(text.find("haptics=") != std::string::npos);
    assert(text.find("target_fps=") != std::string::npos);
    assert(text.find("target_fps=") == text.rfind("target_fps="));
    printf("[PASS] Save/load round-trip restores every persisted field, including game speed\n");
}

static void testEverySupportedSpeed(void) {
    // Literal contract from README/Settings.h; never derived from the helper.
    const float expected[] = {1.0f, 1.5f, 2.0f, 3.0f, 4.0f};
    assert(mobile_ui::kGameSpeedOptionCount == 5);
    for (int i = 0; i < mobile_ui::kGameSpeedOptionCount; ++i) {
        assert(mobile_ui::kGameSpeedMultipliers[i] == expected[i]);
    }
    for (int index = 1; index <= 5; ++index) {
        char line[32];
        snprintf(line, sizeof(line), "game_speed=%d\n", index);
        Settings settings;
        assert(applyText(settings, line) == 1);
        assert(settings.gameSpeed == index);
        assert(gameSpeedMultiplier(settings.gameSpeed) == expected[index - 1]);

        // Persisted value -> startup multiplier, through the full serializer.
        Settings saved;
        saved.gameSpeed = index;
        const std::string text = serializeSettings(saved);
        Settings restored;
        assert(applyText(restored, text.c_str()) == countLines(text));
        assert(restored.gameSpeed == index);
        assert(gameSpeedMultiplier(restored.gameSpeed) == expected[index - 1]);
    }
    assert(gameSpeedMultiplier(0) == 1.0f);
    assert(gameSpeedMultiplier(-3) == 1.0f);
    assert(gameSpeedMultiplier(6) == 4.0f);
    printf("[PASS] All five persisted game-speed indices map to the menu multipliers\n");
}

static void testSliderSnapsToCanonicalSpeeds(void) {
    struct SliderCase {
        float position;
        int expectedIndex;
        float expectedMultiplier;
    };
    const SliderCase cases[] = {
        {1.0f, 1, 1.0f}, {1.2f, 1, 1.0f}, {1.5f, 2, 1.5f},
        {2.0f, 3, 2.0f}, {2.5f, 4, 3.0f}, {3.0f, 4, 3.0f},
        {3.5f, 5, 4.0f}, {4.0f, 5, 4.0f},
    };
    for (const SliderCase& c : cases) {
        const int index = gameSpeedIndexForMultiplier(c.position);
        assert(index == c.expectedIndex);
        assert(gameSpeedMultiplier(index) == c.expectedMultiplier);
    }
    printf("[PASS] Every slider position snaps to one of the five canonical speeds\n");
}

static void testInvalidAndOutOfRangeValues(void) {
    const char* malformed[] = {
        "game_speed=nan\n", "game_speed=inf\n", "game_speed=-inf\n",
        "game_speed=1e999\n", "game_speed=abc\n", "game_speed=\n",
        "game_speed\n",
    };
    for (const char* line : malformed) {
        Settings settings;
        settings.gameSpeed = 3;
        applyText(settings, line);
        assert(settings.gameSpeed == 3);
        assert(gameSpeedMultiplier(settings.gameSpeed) == 2.0f);
    }

    struct RangeCase {
        const char* line;
        int expectedIndex;
        float expectedMultiplier;
    };
    const RangeCase ranges[] = {
        {"game_speed=-4\n", 1, 1.0f},   {"game_speed=0\n", 1, 1.0f},
        {"game_speed=1\n", 1, 1.0f},    {"game_speed=2.4\n", 2, 1.5f},
        {"game_speed=2.9\n", 3, 2.0f},  {"game_speed=6\n", 5, 4.0f},
        {"game_speed=99\n", 5, 4.0f},   {"game_speed=1e30\n", 5, 4.0f},
        {"game_speed=-1e30\n", 1, 1.0f},
    };
    for (const RangeCase& c : ranges) {
        Settings settings;
        applyText(settings, c.line);
        assert(settings.gameSpeed == c.expectedIndex);
        assert(gameSpeedMultiplier(settings.gameSpeed) == c.expectedMultiplier);
    }

    Settings defaults;
    applyText(defaults, "target_fps=nan\nhaptics=nan\n");
    assert(defaults.targetFps == 60);
    assert(defaults.haptics == false);
    printf("[PASS] Malformed, non-finite and out-of-range values never create a bad multiplier\n");
}

static void testUiChangeSurvivesRestart(void) {
    Settings changed;
    const int menuSelection = 4;  // fifth slider segment
    changed.gameSpeed = menuSelection + 1;
    assert(gameSpeedMultiplier(changed.gameSpeed) == 4.0f);

    const std::string text = serializeSettings(changed);
    for (int restart = 0; restart < 3; ++restart) {
        Settings restored;
        assert(applyText(restored, text.c_str()) == countLines(text));
        assert(restored.gameSpeed == 5);
        assert(gameSpeedMultiplier(restored.gameSpeed) == 4.0f);
        assert(serializeSettings(restored) == text);
    }

    Settings repeated;
    applyText(repeated, text.c_str());
    applyText(repeated, text.c_str());
    assert(repeated.gameSpeed == 5);
    assert(gameSpeedMultiplier(repeated.gameSpeed) == 4.0f);
    printf("[PASS] A UI speed change survives repeated launches and reloads\n");
}

static void testStartupRefreshFollowsPersistedDisplay(void) {
    Settings settings;
    settings.targetFps = 60;
    settings.preferredRefreshRate = 90;
    assert(mobile_ui::startupDisplayRefreshRate(settings) == 90);

    settings.targetFps = 144;
    settings.preferredRefreshRate = 60;
    assert(mobile_ui::startupDisplayRefreshRate(settings) == 60);

    settings.batterySaver = true;
    assert(mobile_ui::startupDisplayRefreshRate(settings) == 30);
    printf("[PASS] Startup restores the persisted display mode, not the pacing target\n");
}

static void testMissingAndUnknownKeysAreIgnored(void) {
    Settings settings;
    settings.gameSpeed = 2;
    assert(!applySettingLine(settings, "unknown_key=7"));
    assert(!applySettingLine(settings, "not a setting"));
    assert(!applySettingLine(settings, "=5"));
    assert(settings.gameSpeed == 2);
    assert(gameSpeedMultiplier(settings.gameSpeed) == 1.5f);
    printf("[PASS] Missing and unknown keys leave the last valid value untouched\n");
}

static void testOverflowAndUnsupportedValues(void) {
    Settings huge;
    applyText(huge,
              "game_speed=1e30\nlanguage_choice=1e30\nrender_scale=1e30\n"
              "target_fps=1e30\ndisplay_refresh_rate=1e30\nright_stick_mode=1e30\n"
              "graphics_backend=1e30\nperf_hud=1e30\nscreen_orientation=1e30\n");
    assert(huge.gameSpeed == 5);
    assert(huge.languageChoice == 5);
    assert(huge.renderScale == 3);
    assert(huge.targetFps == 144);
    assert(huge.preferredRefreshRate == 144);
    assert(huge.rightStickMode == 1);
    assert(huge.graphicsBackend == 1);
    assert(huge.perfHudMode == 2);
    assert(huge.screenOrientationChoice == 2);

    Settings tiny;
    applyText(tiny,
              "game_speed=-1e30\nlanguage_choice=-1e30\nrender_scale=-1e30\n"
              "target_fps=-1e30\ndisplay_refresh_rate=-1e30\nright_stick_mode=-1e30\n"
              "graphics_backend=-1e30\nperf_hud=-1e30\nscreen_orientation=-1e30\n");
    assert(tiny.gameSpeed == 1);
    assert(tiny.languageChoice == 0);
    assert(tiny.renderScale == 0);
    assert(tiny.targetFps == 30);
    assert(tiny.preferredRefreshRate == 60);
    assert(tiny.rightStickMode == 0);
    assert(tiny.graphicsBackend == 0);
    assert(tiny.perfHudMode == 0);
    assert(tiny.screenOrientationChoice == 0);

    Settings unsupported;
    applyText(unsupported, "display_refresh_rate=75\ngame_speed=2100000000\n");
    assert(unsupported.preferredRefreshRate == 60);
    assert(unsupported.gameSpeed == 5);
    printf("[PASS] Huge and unsupported values clamp safely without wrapping\n");
}

static void testOlderSettingsFileStillLoads(void) {
    Settings settings;
    const size_t applied = applyText(
        settings,
        "show_controls=1\r\nfullscreen=1\r\nopacity=0.65\r\ngame_speed=4\r\n"
        "target_fps=60\r\nlanguage_choice=0\r\nrender_scale=0\r\n");
    assert(applied == 7);
    assert(settings.gameSpeed == 4);
    assert(gameSpeedMultiplier(settings.gameSpeed) == 3.0f);
    assert(settings.haptics == false);  // key absent from older files
    assert(settings.targetFps == 60);
    printf("[PASS] Older settings files with CRLF and missing keys still load\n");
}

int main(void) {
    printf("=== Running Settings Persistence Unit Tests ===\n");
    testCleanProfileDefaults();
    testSaveLoadRoundTrip();
    testEverySupportedSpeed();
    testSliderSnapsToCanonicalSpeeds();
    testInvalidAndOutOfRangeValues();
    testOverflowAndUnsupportedValues();
    testOlderSettingsFileStillLoads();
    testUiChangeSurvivesRestart();
    testStartupRefreshFollowsPersistedDisplay();
    testMissingAndUnknownKeysAreIgnored();
    printf("All settings persistence unit tests passed successfully.\n");
    return 0;
}
