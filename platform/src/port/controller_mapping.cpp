#include "port/controller_mapping.h"

#include <algorithm>
#include <cmath>
#include <cstring>

ControllerMapping controller_mapping_get_default(ControllerProfile profile) {
    ControllerMapping m;
    std::memset(&m, 0, sizeof(m));
    m.deadzone = 0.18f;
    m.triggerThreshold = 0.40f;
    m.swapTriggersBumpers = 0;
    m.invertCameraY = 0;

    // Common navigation bindings
    m.buttonActions[BTN_DPAD_UP] = ACTION_DPAD_UP;
    m.buttonActions[BTN_DPAD_DOWN] = ACTION_DPAD_DOWN;
    m.buttonActions[BTN_DPAD_LEFT] = ACTION_DPAD_LEFT;
    m.buttonActions[BTN_DPAD_RIGHT] = ACTION_DPAD_RIGHT;
    m.buttonActions[BTN_START] = ACTION_PAUSE;
    m.buttonActions[BTN_SELECT] = ACTION_PAUSE;
    m.buttonActions[BTN_THUMBL] = ACTION_CROUCH_GROUNDPOUND;
    m.buttonActions[BTN_THUMBR] = ACTION_POINTER_RECENTER;

    switch (profile) {
    case CONTROLLER_PROFILE_SWITCH:
        m.buttonActions[BTN_SOUTH] = ACTION_JUMP;              // Physical B
        m.buttonActions[BTN_EAST]  = ACTION_SHOOT_CANCEL;     // Physical A
        m.buttonActions[BTN_WEST]  = ACTION_SPIN;              // Physical Y
        m.buttonActions[BTN_NORTH] = ACTION_POINTER_RECENTER;  // Physical X
        m.buttonActions[BTN_L1]    = ACTION_CAMERA_SNAP;
        m.buttonActions[BTN_R1]    = ACTION_SPIN;
        m.buttonActions[BTN_L2]    = ACTION_CROUCH_GROUNDPOUND;
        m.buttonActions[BTN_R2]    = ACTION_SHOOT_CANCEL;
        break;

    case CONTROLLER_PROFILE_GAMECUBE:
        m.buttonActions[BTN_SOUTH] = ACTION_JUMP;              // Main A
        m.buttonActions[BTN_EAST]  = ACTION_SHOOT_CANCEL;     // Y button
        m.buttonActions[BTN_WEST]  = ACTION_SPIN;              // B button
        m.buttonActions[BTN_NORTH] = ACTION_POINTER_RECENTER;  // X button
        m.buttonActions[BTN_L1]    = ACTION_SPIN;              // Z trigger
        m.buttonActions[BTN_R1]    = ACTION_CAMERA_SNAP;
        m.buttonActions[BTN_L2]    = ACTION_CROUCH_GROUNDPOUND;// L trigger
        m.buttonActions[BTN_R2]    = ACTION_SHOOT_CANCEL;     // R trigger
        break;

    case CONTROLLER_PROFILE_STANDARD:
    case CONTROLLER_PROFILE_CUSTOM:
    default:
        m.buttonActions[BTN_SOUTH] = ACTION_JUMP;              // A button
        m.buttonActions[BTN_EAST]  = ACTION_SHOOT_CANCEL;     // B button
        m.buttonActions[BTN_WEST]  = ACTION_SPIN;              // X button
        m.buttonActions[BTN_NORTH] = ACTION_SPIN;              // Y button
        m.buttonActions[BTN_L1]    = ACTION_CAMERA_SNAP;       // LB
        m.buttonActions[BTN_R1]    = ACTION_POINTER_RECENTER;  // RB
        m.buttonActions[BTN_L2]    = ACTION_CROUCH_GROUNDPOUND;// LT
        m.buttonActions[BTN_R2]    = ACTION_SHOOT_CANCEL;     // RT
        break;
    }

    return m;
}

ControllerAction controller_mapping_get_action(const ControllerMapping* mapping, ControllerButton button) {
    if (!mapping || (int)button < 0 || button >= BTN_COUNT) return ACTION_NONE;
    if (mapping->swapTriggersBumpers) {
        if (button == BTN_L1) button = BTN_L2;
        else if (button == BTN_L2) button = BTN_L1;
        else if (button == BTN_R1) button = BTN_R2;
        else if (button == BTN_R2) button = BTN_R1;
    }
    return (ControllerAction)mapping->buttonActions[button];
}

uint32_t controller_mapping_action_to_wpad(ControllerAction action) {
    switch (action) {
    case ACTION_JUMP:              return 0x0800; // WPAD_BUTTON_A
    case ACTION_SHOOT_CANCEL:       return 0x0400; // WPAD_BUTTON_B
    case ACTION_CROUCH_GROUNDPOUND:return 0x2000; // WPAD_BUTTON_Z
    case ACTION_CAMERA_SNAP:       return 0x1000; // WPAD_BUTTON_C
    case ACTION_PAUSE:             return 0x0010; // WPAD_BUTTON_PLUS
    case ACTION_DPAD_UP:           return 0x0008; // WPAD_BUTTON_UP
    case ACTION_DPAD_DOWN:         return 0x0004; // WPAD_BUTTON_DOWN
    case ACTION_DPAD_LEFT:         return 0x0001; // WPAD_BUTTON_LEFT
    case ACTION_DPAD_RIGHT:        return 0x0002; // WPAD_BUTTON_RIGHT
    default:                       return 0;
    }
}

int controller_mapping_is_spin(ControllerAction action) {
    return action == ACTION_SPIN ? 1 : 0;
}

int controller_mapping_is_pointer_recenter(ControllerAction action) {
    return action == ACTION_POINTER_RECENTER ? 1 : 0;
}

float controller_mapping_apply_deadzone(float rawX, float rawY, float deadzone, float* outX, float* outY) {
    float mag = std::sqrt(rawX * rawX + rawY * rawY);
    if (mag <= deadzone || mag <= 0.0001f) {
        if (outX) *outX = 0.0f;
        if (outY) *outY = 0.0f;
        return 0.0f;
    }
    float norm = std::min(1.0f, (mag - deadzone) / (1.0f - deadzone));
    if (outX) *outX = (rawX / mag) * norm;
    if (outY) *outY = (rawY / mag) * norm;
    return norm;
}

ControllerButton controller_mapping_from_android_key(int32_t keyCode) {
    switch (keyCode) {
    case 96:  /* AKEYCODE_BUTTON_A */
    case 188: /* AKEYCODE_BUTTON_1 */
    case 23:  /* AKEYCODE_DPAD_CENTER */
    case 66:  /* AKEYCODE_ENTER */
    case 160: /* AKEYCODE_NUMPAD_ENTER */
        return BTN_SOUTH;
    case 97:  /* AKEYCODE_BUTTON_B */
    case 189: /* AKEYCODE_BUTTON_2 */
    case 98:  /* AKEYCODE_BUTTON_C */
        return BTN_EAST;
    case 99:  /* AKEYCODE_BUTTON_X */
    case 190: /* AKEYCODE_BUTTON_3 */
        return BTN_WEST;
    case 100: /* AKEYCODE_BUTTON_Y */
    case 191: /* AKEYCODE_BUTTON_4 */
        return BTN_NORTH;
    case 102: /* AKEYCODE_BUTTON_L1 */
    case 192: /* AKEYCODE_BUTTON_5 */
        return BTN_L1;
    case 103: /* AKEYCODE_BUTTON_R1 */
    case 193: /* AKEYCODE_BUTTON_6 */
        return BTN_R1;
    case 104: /* AKEYCODE_BUTTON_L2 */
    case 194: /* AKEYCODE_BUTTON_7 */
    case 101: /* AKEYCODE_BUTTON_Z */
        return BTN_L2;
    case 105: /* AKEYCODE_BUTTON_R2 */
    case 195: /* AKEYCODE_BUTTON_8 */
        return BTN_R2;
    case 106: /* AKEYCODE_BUTTON_THUMBL */
    case 198: /* AKEYCODE_BUTTON_11 */
        return BTN_THUMBL;
    case 107: /* AKEYCODE_BUTTON_THUMBR */
    case 199: /* AKEYCODE_BUTTON_12 */
        return BTN_THUMBR;
    case 108: /* AKEYCODE_BUTTON_START */
    case 197: /* AKEYCODE_BUTTON_10 */
    case 82:  /* AKEYCODE_MENU */
        return BTN_START;
    case 109: /* AKEYCODE_BUTTON_SELECT */
    case 196: /* AKEYCODE_BUTTON_9 */
    case 110: /* AKEYCODE_BUTTON_MODE */
        return BTN_SELECT;
    case 19:  /* AKEYCODE_DPAD_UP */
        return BTN_DPAD_UP;
    case 20:  /* AKEYCODE_DPAD_DOWN */
        return BTN_DPAD_DOWN;
    case 21:  /* AKEYCODE_DPAD_LEFT */
        return BTN_DPAD_LEFT;
    case 22:  /* AKEYCODE_DPAD_RIGHT */
        return BTN_DPAD_RIGHT;
    default:
        return BTN_COUNT;
    }
}
