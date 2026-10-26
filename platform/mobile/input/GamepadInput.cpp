#include "GamepadInput.h"

#include <android/keycodes.h>
#include <algorithm>
#include <math.h>

#include "../ui/UIOverlay.h"

namespace mobile_input {

GamepadInput::GamepadInput() {
    mMapping = controller_mapping_get_default(CONTROLLER_PROFILE_STANDARD);
}

void GamepadInput::setProfile(ControllerProfile profile) {
    mMapping = controller_mapping_get_default(profile);
}

void GamepadInput::setMapping(const ControllerMapping& mapping) {
    mMapping = mapping;
}

bool GamepadInput::handleMotionEvent(AInputEvent* event, PortPadState& pad, int& spinFrames, bool touchscreenDown) {
    int32_t source = AInputEvent_getSource(event);
    bool isJoystick = (source & AINPUT_SOURCE_CLASS_JOYSTICK) != 0 ||
                      ((source & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD) ||
                      ((source & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK);
    if (!isJoystick) return false;

    if (!mGamepadDetected) {
        mGamepadDetected = true;
        mobile_ui::UIOverlay::get().showToast("Gamepad Connected");
    }

    // Left Analog Stick with calibrated deadzone
    float rawX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
    float rawY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
    float deadzone = std::max(0.05f, std::min(0.40f, mMapping.deadzone));
    float normX = 0.0f, normY = 0.0f;
    float normMag = controller_mapping_apply_deadzone(rawX, rawY, deadzone, &normX, &normY);
    if (normMag <= 0.0001f) {
        mPhysicalStickX = 0.0f;
        mPhysicalStickY = 0.0f;
    } else {
        float sens = mobile_ui::UIOverlay::get().settings().stickSensitivity;
        mPhysicalStickX = std::max(-1.0f, std::min(1.0f, normX * sens));
        mPhysicalStickY = std::max(-1.0f, std::min(1.0f, -normY * sens)); // Invert Y for Wii convention
    }

    // D-Pad Hat axes
    float hatX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_X, 0);
    float hatY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_Y, 0);
    if (hatX < -0.5f) { mPhysicalAxisButtons |= 0x0001; mPhysicalAxisButtons &= ~0x0002; }
    else if (hatX > 0.5f) { mPhysicalAxisButtons |= 0x0002; mPhysicalAxisButtons &= ~0x0001; }
    else { mPhysicalAxisButtons &= ~0x0003; }

    if (hatY < -0.5f) { mPhysicalAxisButtons |= 0x0008; mPhysicalAxisButtons &= ~0x0004; }
    else if (hatY > 0.5f) { mPhysicalAxisButtons |= 0x0004; mPhysicalAxisButtons &= ~0x0008; }
    else { mPhysicalAxisButtons &= ~0x000C; }

    // Analog Triggers (L2 / R2) with configurable threshold
    float rawL = std::max(0.0f, AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_LTRIGGER, 0));
    float rawBrake = std::max(0.0f, AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_BRAKE, 0));
    float lTrig = std::max(rawL, rawBrake);

    float rawR = std::max(0.0f, AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RTRIGGER, 0));
    float rawGas = std::max(0.0f, AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_GAS, 0));
    float rTrig = std::max(rawR, rawGas);
    float trigThreshold = std::max(0.10f, std::min(0.80f, mMapping.triggerThreshold));

    ControllerAction actL2 = controller_mapping_get_action(&mMapping, BTN_L2);
    ControllerAction actR2 = controller_mapping_get_action(&mMapping, BTN_R2);

    if (lTrig >= trigThreshold) {
        mPhysicalAxisButtons |= controller_mapping_action_to_wpad(actL2);
        if (controller_mapping_is_spin(actL2)) spinFrames = 6;
    } else {
        mPhysicalAxisButtons &= ~controller_mapping_action_to_wpad(actL2);
    }

    if (rTrig >= trigThreshold) {
        mPhysicalAxisButtons |= controller_mapping_action_to_wpad(actR2);
        if (controller_mapping_is_spin(actR2)) spinFrames = 6;
    } else {
        mPhysicalAxisButtons &= ~controller_mapping_action_to_wpad(actR2);
    }

    // Right Stick (Orbital camera & Star Pointer)
    float rightX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0);
    float rightY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0);
    if (fabsf(rightX) <= 0.05f && fabsf(rightY) <= 0.05f) {
        rightX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RX, 0);
        rightY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RY, 0);
    }
    float rStickX = 0.0f, rStickY = 0.0f;
    float rNorm = controller_mapping_apply_deadzone(rightX, rightY, 0.08f, &rStickX, &rStickY);
    rStickY = -rStickY; // Invert to standard coordinate
    if (mMapping.invertCameraY) rStickY = -rStickY;

    float starSens = mobile_ui::UIOverlay::get().settings().starStickSensitivity;
    if (mobile_ui::UIOverlay::get().settings().rightStickMode == 0) {
        pad.rightStickX = std::max(-1.0f, std::min(1.0f, -rStickX * starSens));
        pad.rightStickY = std::max(-1.0f, std::min(1.0f, rStickY * starSens));
    } else {
        pad.rightStickX = 0.0f;
        pad.rightStickY = 0.0f;
        if (rNorm > 0.001f && !touchscreenDown) {
            float speed = 0.035f * starSens;
            pad.pointerX = std::max(-1.0f, std::min(1.0f, pad.pointerX + rStickX * speed));
            pad.pointerY = std::max(-1.0f, std::min(1.0f, pad.pointerY - rStickY * speed));
            pad.pointerValid = 1;
            mobile_ui::UIOverlay::get().setPointerPos(pad.pointerX, pad.pointerY);
        }
    }

    pad.connected = 1;
    return true;
}

bool GamepadInput::handleKeyEvent(AInputEvent* event, PortPadState& pad, int& spinFrames) {
    int32_t keyCode = AKeyEvent_getKeyCode(event);
    if (keyCode == AKEYCODE_VOLUME_UP || keyCode == AKEYCODE_VOLUME_DOWN || keyCode == AKEYCODE_VOLUME_MUTE) {
        return false;
    }

    if (!mGamepadDetected && (keyCode >= AKEYCODE_BUTTON_A && keyCode <= AKEYCODE_BUTTON_16)) {
        mGamepadDetected = true;
        mobile_ui::UIOverlay::get().showToast("Gamepad Connected");
    }

    int32_t keyAction = AKeyEvent_getAction(event);
    bool down = (keyAction == AKEY_EVENT_ACTION_DOWN);

    if (keyCode == AKEYCODE_BACK || keyCode == AKEYCODE_ESCAPE) {
        if (down) {
            if (mobile_ui::UIOverlay::get().isSettingsOpen()) {
                mobile_ui::UIOverlay::get().closeSettings();
            } else {
                mPhysicalButtons |= 0x0010;
            }
        } else {
            mPhysicalButtons &= ~0x0010;
        }
        return true;
    }

    ControllerButton btn = controller_mapping_from_android_key(keyCode);
    if (btn != BTN_COUNT) {
        pad.connected = 1;
        ControllerAction act = controller_mapping_get_action(&mMapping, btn);
        uint32_t mask = controller_mapping_action_to_wpad(act);
        if (down) {
            mPhysicalButtons |= mask;
            if (controller_mapping_is_spin(act)) spinFrames = 6;
            if (controller_mapping_is_pointer_recenter(act)) {
                pad.pointerX = 0.0f;
                pad.pointerY = 0.0f;
                pad.pointerValid = 1;
                mobile_ui::UIOverlay::get().setPointerPos(0.0f, 0.0f);
            }
        } else {
            mPhysicalButtons &= ~mask;
        }
        return true;
    }

    return false;
}

void GamepadInput::reset() {
    mPhysicalButtons = 0;
    mPhysicalAxisButtons = 0;
}

}  // namespace mobile_input
