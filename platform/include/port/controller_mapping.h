#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Core game actions mapped to controller inputs
typedef enum ControllerAction {
    ACTION_NONE = 0,
    ACTION_JUMP = 1,              // WPAD_BUTTON_A (0x0800)
    ACTION_SPIN = 2,              // Spin attack / shake
    ACTION_SHOOT_CANCEL = 3,       // WPAD_BUTTON_B (0x0400)
    ACTION_CROUCH_GROUNDPOUND = 4,// WPAD_BUTTON_Z (0x2000)
    ACTION_CAMERA_SNAP = 5,       // WPAD_BUTTON_C (0x1000)
    ACTION_POINTER_RECENTER = 6,  // Recenter Star Pointer to screen center
    ACTION_PAUSE = 7,             // WPAD_BUTTON_PLUS (0x0010)
    ACTION_DPAD_UP = 8,           // Wii D-Pad Up (0x0008)
    ACTION_DPAD_DOWN = 9,         // Wii D-Pad Down (0x0004)
    ACTION_DPAD_LEFT = 10,        // Wii D-Pad Left (0x0001)
    ACTION_DPAD_RIGHT = 11,       // Wii D-Pad Right (0x0002)
    ACTION_COUNT = 12
} ControllerAction;

// Pre-configured native controller layout profiles
typedef enum ControllerProfile {
    CONTROLLER_PROFILE_STANDARD = 0, // Xbox / Android standard (South=Jump, West=Spin, East=Shoot, North=Spin)
    CONTROLLER_PROFILE_SWITCH = 1,   // Nintendo Switch (South/B=Jump, West/Y=Spin, East/A=Shoot, North/X=Spin)
    CONTROLLER_PROFILE_GAMECUBE = 2, // GameCube style (South/A=Jump, West/B=Spin, East/X=Shoot, North/Y=Shoot)
    CONTROLLER_PROFILE_CUSTOM = 3,   // User customized mapping
    CONTROLLER_PROFILE_COUNT = 4
} ControllerProfile;

// Physical abstract controller button indices
typedef enum ControllerButton {
    BTN_SOUTH = 0,     // A (Xbox) / B (Switch) / Cross (PlayStation)
    BTN_EAST = 1,      // B (Xbox) / A (Switch) / Circle (PlayStation)
    BTN_WEST = 2,      // X (Xbox) / Y (Switch) / Square (PlayStation)
    BTN_NORTH = 3,     // Y (Xbox) / X (Switch) / Triangle (PlayStation)
    BTN_L1 = 4,        // Left Bumper / L
    BTN_R1 = 5,        // Right Bumper / R
    BTN_L2 = 6,        // Left Trigger / ZL
    BTN_R2 = 7,        // Right Trigger / ZR
    BTN_THUMBL = 8,    // L3 / Left Stick Click
    BTN_THUMBR = 9,    // R3 / Right Stick Click
    BTN_START = 10,    // Start / Options / Plus
    BTN_SELECT = 11,   // Select / Share / Minus
    BTN_DPAD_UP = 12,
    BTN_DPAD_DOWN = 13,
    BTN_DPAD_LEFT = 14,
    BTN_DPAD_RIGHT = 15,
    BTN_COUNT = 16
} ControllerButton;

typedef struct ControllerMapping {
    uint8_t buttonActions[BTN_COUNT];
    float deadzone;
    float triggerThreshold;
    uint8_t swapTriggersBumpers;
    uint8_t invertCameraY;
} ControllerMapping;

ControllerMapping controller_mapping_get_default(ControllerProfile profile);
ControllerAction controller_mapping_get_action(const ControllerMapping* mapping, ControllerButton button);
uint32_t controller_mapping_action_to_wpad(ControllerAction action);
int controller_mapping_is_spin(ControllerAction action);
int controller_mapping_is_pointer_recenter(ControllerAction action);
float controller_mapping_apply_deadzone(float rawX, float rawY, float deadzone, float* outX, float* outY);
ControllerButton controller_mapping_from_android_key(int32_t keyCode);

#ifdef __cplusplus
}
#endif
