#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "Settings.h"

namespace mobile_ui {

// Clamp before converting: a finite persisted float can still exceed int
// range, and an out-of-range float-to-int conversion is undefined behavior.
inline int roundedClamp(float value, int lo, int hi) {
    if (value <= (float)lo) return lo;
    if (value >= (float)hi) return hi;
    return (int)std::lround(value);
}

// One persisted key=value line from touch_settings.ini. Unknown, malformed and
// non-finite values are ignored so a corrupted file cannot change live state.
inline bool applySettingLine(Settings& s, const char* line) {
    if (!line) return false;

    char key[64] = {0};
    float value = 0.0f;
    if (sscanf(line, "%63[^=]=%f", key, &value) != 2) return false;
    if (!std::isfinite(value)) return false;

    const bool on = value > 0.5f;
    if (!strcmp(key, "show_controls")) s.showControls = on;
    else if (!strcmp(key, "fullscreen")) s.fullscreen = on;
    else if (!strcmp(key, "opacity")) s.opacity = std::max(0.1f, std::min(1.0f, value));
    else if (!strcmp(key, "scale")) s.scale = std::max(0.6f, std::min(1.6f, value));
    else if (!strcmp(key, "dynamic_stick")) s.dynamicStick = on;
    else if (!strcmp(key, "stick_offset_x")) s.stickOffsetX = value;
    else if (!strcmp(key, "stick_offset_y")) s.stickOffsetY = value;
    else if (!strcmp(key, "button_offset_x")) s.buttonOffsetX = value;
    else if (!strcmp(key, "button_offset_y")) s.buttonOffsetY = value;
    else if (!strcmp(key, "btn_a_offset_x")) s.btnAOffsetX = value;
    else if (!strcmp(key, "btn_a_offset_y")) s.btnAOffsetY = value;
    else if (!strcmp(key, "btn_b_offset_x")) s.btnBOffsetX = value;
    else if (!strcmp(key, "btn_b_offset_y")) s.btnBOffsetY = value;
    else if (!strcmp(key, "btn_z_offset_x")) s.btnZOffsetX = value;
    else if (!strcmp(key, "btn_z_offset_y")) s.btnZOffsetY = value;
    else if (!strcmp(key, "btn_spin_offset_x")) s.btnSpinOffsetX = value;
    else if (!strcmp(key, "btn_spin_offset_y")) s.btnSpinOffsetY = value;
    else if (!strcmp(key, "star_stick_offset_x")) s.starStickOffsetX = value;
    else if (!strcmp(key, "star_stick_offset_y")) s.starStickOffsetY = value;
    else if (!strcmp(key, "show_dpad")) s.showDpad = on;
    else if (!strcmp(key, "dpad_on_right")) s.dpadOnRight = on;
    else if (!strcmp(key, "dpad_offset_x")) s.dpadOffsetX = value;
    else if (!strcmp(key, "dpad_offset_y")) s.dpadOffsetY = value;
    else if (!strcmp(key, "mod_shield_textures")) s.modShieldTextures = on;
    else if (!strcmp(key, "mod_shield_cutscenes")) s.modShieldCutscenes = on;
    else if (!strcmp(key, "mod_shield_controls")) s.modShieldControls = on;
    else if (!strcmp(key, "mod_classic_gamepad_ui")) s.modClassicGamepadUI = on;
    else if (!strcmp(key, "mod_riivolution")) s.modRiivolution = on;
    else if (!strcmp(key, "mod_fly")) s.modFly = on;
    else if (!strcmp(key, "mod_skip_cutscenes")) s.modSkipCutscenes = on;
    else if (!strcmp(key, "mod_infinite_health")) s.modInfiniteHealth = on;
    else if (!strcmp(key, "mod_infinite_lives")) s.modInfiniteLives = on;
    else if (!strcmp(key, "mod_infinite_starbits")) s.modInfiniteStarBits = on;
    else if (!strcmp(key, "game_speed")) s.gameSpeed = roundedClamp(value, 1, kGameSpeedOptionCount);
    else if (!strcmp(key, "language_choice")) s.languageChoice = roundedClamp(value, 0, 5);
    else if (!strcmp(key, "render_scale")) s.renderScale = roundedClamp(value, 0, 3);
    else if (!strcmp(key, "target_fps")) {
        if (value >= 144.0f) s.targetFps = 144;
        else if (value >= 120.0f) s.targetFps = 120;
        else if (value >= 90.0f) s.targetFps = 90;
        else if (value >= 60.0f) s.targetFps = 60;
        else s.targetFps = 30;
    }
    else if (!strcmp(key, "display_refresh_rate")) {
        if (value >= 144.0f) s.preferredRefreshRate = 144;
        else if (value >= 120.0f) s.preferredRefreshRate = 120;
        else if (value >= 90.0f) s.preferredRefreshRate = 90;
        else s.preferredRefreshRate = 60;
    }
    else if (!strcmp(key, "adaptive_fps")) s.adaptiveFps = on;
    else if (!strcmp(key, "battery_saver")) s.batterySaver = on;
    else if (!strcmp(key, "gyro_aiming")) s.gyroAiming = on;
    else if (!strcmp(key, "gyro_invert_x")) s.gyroInvertX = on;
    else if (!strcmp(key, "gyro_invert_y")) s.gyroInvertY = on;
    else if (!strcmp(key, "star_stick")) s.starStick = on;
    else if (!strcmp(key, "stick_sensitivity")) s.stickSensitivity = std::max(0.25f, std::min(3.0f, value));
    else if (!strcmp(key, "star_stick_sensitivity")) s.starStickSensitivity = std::max(0.25f, std::min(3.0f, value));
    else if (!strcmp(key, "right_stick_mode")) s.rightStickMode = roundedClamp(value, 0, 1);
    else if (!strcmp(key, "touch_camera")) s.touchCamera = on;
    else if (!strcmp(key, "graphics_backend")) s.graphicsBackend = roundedClamp(value, 0, 1);
    else if (!strcmp(key, "relative_pointer")) s.relativePointer = on;
    else if (!strcmp(key, "perf_hud")) s.perfHudMode = roundedClamp(value, 0, 2);
    else if (!strcmp(key, "full_touch_mode")) s.fullTouchMode = on;
    else if (!strcmp(key, "screen_orientation")) s.screenOrientationChoice = roundedClamp(value, 0, 2);
    else if (!strcmp(key, "speedrun_hud")) s.speedrunHud = on;
    else if (!strcmp(key, "haptics")) s.haptics = on;
    else return false;
    return true;
}

inline void appendSetting(std::string& out, const char* key, int value) {
    char line[64];
    snprintf(line, sizeof(line), "%s=%d\n", key, value);
    out += line;
}

inline void appendSetting(std::string& out, const char* key, float value, int decimals) {
    char line[72];
    snprintf(line, sizeof(line), "%s=%.*f\n", key, decimals, (double)value);
    out += line;
}

// Matches the persisted format consumed by applySettingLine; kept next to the
// parser so a new field cannot be saved without a load path.
inline std::string serializeSettings(const Settings& s) {
    std::string out;
    out.reserve(1024);
    appendSetting(out, "show_controls", s.showControls ? 1 : 0);
    appendSetting(out, "fullscreen", s.fullscreen ? 1 : 0);
    appendSetting(out, "full_touch_mode", s.fullTouchMode ? 1 : 0);
    appendSetting(out, "perf_hud", s.perfHudMode);
    appendSetting(out, "graphics_backend", s.graphicsBackend);
    appendSetting(out, "relative_pointer", s.relativePointer ? 1 : 0);
    appendSetting(out, "screen_orientation", s.screenOrientationChoice);
    appendSetting(out, "target_fps", s.targetFps);
    appendSetting(out, "display_refresh_rate", s.preferredRefreshRate);
    appendSetting(out, "battery_saver", s.batterySaver ? 1 : 0);
    appendSetting(out, "gyro_aiming", s.gyroAiming ? 1 : 0);
    appendSetting(out, "gyro_invert_x", s.gyroInvertX ? 1 : 0);
    appendSetting(out, "gyro_invert_y", s.gyroInvertY ? 1 : 0);
    appendSetting(out, "star_stick", s.starStick ? 1 : 0);
    appendSetting(out, "stick_sensitivity", s.stickSensitivity, 2);
    appendSetting(out, "star_stick_sensitivity", s.starStickSensitivity, 2);
    appendSetting(out, "right_stick_mode", s.rightStickMode);
    appendSetting(out, "touch_camera", s.touchCamera ? 1 : 0);
    appendSetting(out, "opacity", s.opacity, 2);
    appendSetting(out, "scale", s.scale, 2);
    appendSetting(out, "dynamic_stick", s.dynamicStick ? 1 : 0);
    appendSetting(out, "stick_offset_x", s.stickOffsetX, 1);
    appendSetting(out, "stick_offset_y", s.stickOffsetY, 1);
    appendSetting(out, "button_offset_x", s.buttonOffsetX, 1);
    appendSetting(out, "button_offset_y", s.buttonOffsetY, 1);
    appendSetting(out, "btn_a_offset_x", s.btnAOffsetX, 1);
    appendSetting(out, "btn_a_offset_y", s.btnAOffsetY, 1);
    appendSetting(out, "btn_b_offset_x", s.btnBOffsetX, 1);
    appendSetting(out, "btn_b_offset_y", s.btnBOffsetY, 1);
    appendSetting(out, "btn_z_offset_x", s.btnZOffsetX, 1);
    appendSetting(out, "btn_z_offset_y", s.btnZOffsetY, 1);
    appendSetting(out, "btn_spin_offset_x", s.btnSpinOffsetX, 1);
    appendSetting(out, "btn_spin_offset_y", s.btnSpinOffsetY, 1);
    appendSetting(out, "star_stick_offset_x", s.starStickOffsetX, 1);
    appendSetting(out, "star_stick_offset_y", s.starStickOffsetY, 1);
    appendSetting(out, "show_dpad", s.showDpad ? 1 : 0);
    appendSetting(out, "dpad_on_right", s.dpadOnRight ? 1 : 0);
    appendSetting(out, "dpad_offset_x", s.dpadOffsetX, 1);
    appendSetting(out, "dpad_offset_y", s.dpadOffsetY, 1);
    appendSetting(out, "mod_shield_textures", s.modShieldTextures ? 1 : 0);
    appendSetting(out, "mod_shield_cutscenes", s.modShieldCutscenes ? 1 : 0);
    appendSetting(out, "mod_shield_controls", s.modShieldControls ? 1 : 0);
    appendSetting(out, "mod_classic_gamepad_ui", s.modClassicGamepadUI ? 1 : 0);
    appendSetting(out, "mod_riivolution", s.modRiivolution ? 1 : 0);
    appendSetting(out, "mod_fly", s.modFly ? 1 : 0);
    appendSetting(out, "mod_skip_cutscenes", s.modSkipCutscenes ? 1 : 0);
    appendSetting(out, "mod_infinite_health", s.modInfiniteHealth ? 1 : 0);
    appendSetting(out, "mod_infinite_lives", s.modInfiniteLives ? 1 : 0);
    appendSetting(out, "mod_infinite_starbits", s.modInfiniteStarBits ? 1 : 0);
    appendSetting(out, "game_speed", s.gameSpeed);
    appendSetting(out, "language_choice", s.languageChoice);
    appendSetting(out, "render_scale", s.renderScale);
    appendSetting(out, "haptics", s.haptics ? 1 : 0);
    appendSetting(out, "adaptive_fps", s.adaptiveFps ? 1 : 0);
    appendSetting(out, "speedrun_hud", s.speedrunHud ? 1 : 0);
    return out;
}

}  // namespace mobile_ui
