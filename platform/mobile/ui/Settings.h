#pragma once

#include <stdint.h>
#include <string>

namespace mobile_ui {

// Canonical selectable speed contract: persisted index i selects
// kGameSpeedMultipliers[i - 1]. These are the only speeds the settings UI
// offers; 0.5x exists only as an engine timing edge case, not a saved option.
constexpr int kGameSpeedOptionCount = 5;
constexpr float kGameSpeedMultipliers[kGameSpeedOptionCount] = {1.0f, 1.5f, 2.0f, 3.0f, 4.0f};

inline int gameSpeedClampIndex(int index) {
    return index < 1 ? 1 : (index > kGameSpeedOptionCount ? kGameSpeedOptionCount : index);
}

inline float gameSpeedMultiplier(int index) {
    return kGameSpeedMultipliers[gameSpeedClampIndex(index) - 1];
}

// The menu slider reports a continuous multiplier; snap it to the nearest
// canonical option, preferring the faster option on an exact midpoint.
inline int gameSpeedIndexForMultiplier(float multiplier) {
    int best = 1;
    float bestDelta = multiplier - kGameSpeedMultipliers[0];
    if (bestDelta < 0.0f) bestDelta = -bestDelta;
    for (int i = 1; i < kGameSpeedOptionCount; ++i) {
        float delta = multiplier - kGameSpeedMultipliers[i];
        if (delta < 0.0f) delta = -delta;
        if (delta <= bestDelta) {
            best = i + 1;
            bestDelta = delta;
        }
    }
    return best;
}

struct Settings {
    bool showControls = true;
    bool fullscreen = true;
    float opacity = 0.65f;
    float scale = 1.0f;
    bool dynamicStick = true;
    float stickOffsetX = 0.0f;
    float stickOffsetY = 0.0f;
    float buttonOffsetX = 0.0f;
    float buttonOffsetY = 0.0f;
    float btnAOffsetX = 0.0f;
    float btnAOffsetY = 0.0f;
    float btnBOffsetX = 0.0f;
    float btnBOffsetY = 0.0f;
    float btnZOffsetX = 0.0f;
    float btnZOffsetY = 0.0f;
    float btnSpinOffsetX = 0.0f;
    float btnSpinOffsetY = 0.0f;
    float starStickOffsetX = 0.0f;
    float starStickOffsetY = 0.0f;
    bool showDpad = true;
    bool dpadOnRight = false;
    float dpadOffsetX = 0.0f;
    float dpadOffsetY = 0.0f;

    // NVIDIA Shield and Remaster Mods
    bool modShieldTextures = true;
    bool modShieldCutscenes = true;
    bool modShieldControls = true;
    bool modClassicGamepadUI = true;
    bool modRiivolution = true;

    // Advanced Gameplay Mods
    bool modFly = false;
    bool modSkipCutscenes = false;
    bool modInfiniteHealth = false;
    bool modInfiniteLives = false;
    bool modInfiniteStarBits = false;
    bool speedrunHud = false;

    // System Settings
    int gameSpeed = 1;
    int languageChoice = 0;
    int renderScale = 0;
    bool haptics = false;
    int targetFps = 60;
    int preferredRefreshRate = 60;
    bool adaptiveFps = true;
    bool batterySaver = false;
    bool gyroAiming = false;
    bool gyroInvertX = false;
    bool gyroInvertY = false;

    bool starStick = false;
    float stickSensitivity = 1.0f;
    float starStickSensitivity = 1.0f;
    int rightStickMode = 0;
    bool touchCamera = true;

    bool fullTouchMode = true;
    int perfHudMode = 0;
    int graphicsBackend = 1;
    bool relativePointer = false;
    int screenOrientationChoice = 0;

    // Physical Controller Mapping Settings
    int controllerProfile = 0; // 0 = Standard/Xbox, 1 = Switch, 2 = GameCube, 3 = Custom
    float controllerDeadzone = 0.18f;
    float triggerThreshold = 0.40f;
    bool swapTriggersBumpers = false;
    uint8_t customButtonActions[16] = {
        1,  // BTN_SOUTH = ACTION_JUMP
        3,  // BTN_EAST = ACTION_SHOOT_CANCEL
        2,  // BTN_WEST = ACTION_SPIN
        2,  // BTN_NORTH = ACTION_SPIN
        5,  // BTN_L1 = ACTION_CAMERA_SNAP
        6,  // BTN_R1 = ACTION_POINTER_RECENTER
        4,  // BTN_L2 = ACTION_CROUCH_GROUNDPOUND
        3,  // BTN_R2 = ACTION_SHOOT_CANCEL
        4,  // BTN_THUMBL = ACTION_CROUCH_GROUNDPOUND
        6,  // BTN_THUMBR = ACTION_POINTER_RECENTER
        7,  // BTN_START = ACTION_PAUSE
        7,  // BTN_SELECT = ACTION_PAUSE
        8,  // BTN_DPAD_UP = ACTION_DPAD_UP
        9,  // BTN_DPAD_DOWN = ACTION_DPAD_DOWN
        10, // BTN_DPAD_LEFT = ACTION_DPAD_LEFT
        11  // BTN_DPAD_RIGHT = ACTION_DPAD_RIGHT
    };

    void load(const std::string& path);
    void save(const std::string& path) const;
};

// Startup restores the persisted display mode, not the pacing target.
inline int startupDisplayRefreshRate(const Settings& settings) {
    return settings.batterySaver ? 30 : settings.preferredRefreshRate;
}

}  // namespace mobile_ui
