#include "GamepadInput.h"

#include <algorithm>
#include <cmath>

#if defined(__ANDROID__)
#include <android/keycodes.h>
#include "../ui/UIOverlay.h"
#endif

#include "../ui/Settings.h"

namespace mobile_input {

GamepadInput::GamepadInput() {
    mMapping = controller_mapping_get_default(CONTROLLER_PROFILE_STANDARD);
}

void GamepadInput::setProfile(ControllerProfile profile) {
    if (!controller_mapping_is_valid_profile((int)profile)) {
        profile = CONTROLLER_PROFILE_STANDARD;
    }
    mMapping = controller_mapping_get_default(profile);
    updateButtonMask();
}

void GamepadInput::setMapping(const ControllerMapping& mapping) {
    mMapping = mapping;
    updateButtonMask();
}

void GamepadInput::applySettings(const mobile_ui::Settings& s) {
    int prof = s.controllerProfile;
    if (!controller_mapping_is_valid_profile(prof)) prof = CONTROLLER_PROFILE_STANDARD;
    ControllerProfile profile = static_cast<ControllerProfile>(prof);
    ControllerMapping mapping = controller_mapping_get_default(profile);

    if (profile == CONTROLLER_PROFILE_CUSTOM) {
        bool hasCustom = false;
        for (int i = 0; i < BTN_COUNT; i++) {
            uint8_t act = s.customButtonActions[i];
            if (controller_mapping_is_valid_action(act)) {
                mapping.buttonActions[i] = act;
                if (act != ACTION_NONE) hasCustom = true;
            }
        }
        if (!hasCustom) mapping = controller_mapping_get_default(CONTROLLER_PROFILE_STANDARD);
    }

    float dz = s.controllerDeadzone;
    if (!std::isfinite(dz)) dz = 0.18f;
    mapping.deadzone = std::max(0.05f, std::min(0.40f, dz));

    float tt = s.triggerThreshold;
    if (!std::isfinite(tt)) tt = 0.40f;
    mapping.triggerThreshold = std::max(0.10f, std::min(0.80f, tt));

    mapping.swapTriggersBumpers = s.swapTriggersBumpers ? 1 : 0;
    mStickSensitivity = std::isfinite(s.stickSensitivity) ? std::max(0.25f, std::min(3.0f, s.stickSensitivity)) : 1.0f;
    mStarStickSensitivity = std::isfinite(s.starStickSensitivity) ? std::max(0.25f, std::min(3.0f, s.starStickSensitivity)) : 1.0f;
    mRightStickMode = (s.rightStickMode == 1) ? 1 : 0;

    setMapping(mapping);
}

void GamepadInput::updateButtonMask() {
    uint32_t mask = 0;
    for (int i = 0; i < BTN_COUNT; i++) {
        if (mHeldPhysicalButtons & (1 << i)) {
            ControllerAction act = controller_mapping_get_action(&mMapping, (ControllerButton)i);
            mask |= controller_mapping_action_to_wpad(act);
        }
    }
    if (mHeldTriggerL2) {
        ControllerAction act = controller_mapping_get_action(&mMapping, BTN_L2);
        mask |= controller_mapping_action_to_wpad(act);
    }
    if (mHeldTriggerR2) {
        ControllerAction act = controller_mapping_get_action(&mMapping, BTN_R2);
        mask |= controller_mapping_action_to_wpad(act);
    }
    if (mHeldHatX < 0) mask |= controller_mapping_action_to_wpad(ACTION_DPAD_LEFT);
    else if (mHeldHatX > 0) mask |= controller_mapping_action_to_wpad(ACTION_DPAD_RIGHT);
    if (mHeldHatY < 0) mask |= controller_mapping_action_to_wpad(ACTION_DPAD_UP);
    else if (mHeldHatY > 0) mask |= controller_mapping_action_to_wpad(ACTION_DPAD_DOWN);
    if (mHeldBack) mask |= 0x0010;

    mPhysicalButtons = mask;
}

bool GamepadInput::handleKey(int32_t keyCode, int32_t action, int32_t repeatCount, PortPadState& pad, int& spinFrames) {
    if (keyCode == 24 /* AKEYCODE_VOLUME_UP */ || keyCode == 25 /* AKEYCODE_VOLUME_DOWN */ || keyCode == 164 /* AKEYCODE_VOLUME_MUTE */) {
        return false;
    }
    if (!mGamepadDetected && (keyCode >= 96 /* AKEYCODE_BUTTON_A */ && keyCode <= 110 /* AKEYCODE_BUTTON_MODE */)) {
        mGamepadDetected = true;
#if defined(__ANDROID__)
        mobile_ui::UIOverlay::get().showToast("Gamepad Connected");
#endif
    }

    bool down = (action == 0 /* AKEY_EVENT_ACTION_DOWN */);
    if (keyCode == 4 /* AKEYCODE_BACK */ || keyCode == 111 /* AKEYCODE_ESCAPE */) {
        if (down) {
#if defined(__ANDROID__)
            if (mobile_ui::UIOverlay::get().isSettingsOpen()) {
                mobile_ui::UIOverlay::get().closeSettings();
                return true;
            }
#endif
            mHeldBack = true;
        } else {
            mHeldBack = false;
        }
        updateButtonMask();
        return true;
    }

    ControllerButton btn = controller_mapping_from_android_key(keyCode);
    if (btn != BTN_COUNT) {
        pad.connected = 1;
        if (down) {
            bool wasHeld = (mHeldPhysicalButtons & (1 << btn)) != 0;
            if (!wasHeld) {
                mHeldPhysicalButtons |= (1 << btn);
                updateButtonMask();
                ControllerAction act = controller_mapping_get_action(&mMapping, btn);
                if (controller_mapping_is_spin(act)) spinFrames = 6;
                if (controller_mapping_is_pointer_recenter(act)) {
                    pad.pointerX = 0.0f;
                    pad.pointerY = 0.0f;
                    pad.pointerValid = 1;
#if defined(__ANDROID__)
                    mobile_ui::UIOverlay::get().setPointerPos(0.0f, 0.0f);
#endif
                }
            }
        } else {
            mHeldPhysicalButtons &= ~(1 << btn);
            updateButtonMask();
        }
        return true;
    }
    return false;
}

bool GamepadInput::handleMotion(int32_t source, float rawX, float rawY, float rawL, float rawBrake,
                                float rawR, float rawGas, float rightX, float rightY,
                                float hatX, float hatY, PortPadState& pad, int& spinFrames, bool touchscreenDown) {
    bool isJoystick = (source & 0x01000010 /* AINPUT_SOURCE_CLASS_JOYSTICK */) != 0 ||
                      ((source & 0x00000401 /* AINPUT_SOURCE_GAMEPAD */) == 0x00000401) ||
                      ((source & 0x01000010 /* AINPUT_SOURCE_JOYSTICK */) == 0x01000010);
    if (!isJoystick) return false;

    if (!mGamepadDetected) {
        mGamepadDetected = true;
#if defined(__ANDROID__)
        mobile_ui::UIOverlay::get().showToast("Gamepad Connected");
#endif
    }

    float normX = 0.0f, normY = 0.0f;
    float deadzone = std::max(0.05f, std::min(0.40f, mMapping.deadzone));
    float normMag = controller_mapping_apply_deadzone(rawX, rawY, deadzone, &normX, &normY);
    if (normMag <= 0.0001f) {
        mPhysicalStickX = 0.0f;
        mPhysicalStickY = 0.0f;
    } else {
        mPhysicalStickX = std::max(-1.0f, std::min(1.0f, normX * mStickSensitivity));
        mPhysicalStickY = std::max(-1.0f, std::min(1.0f, -normY * mStickSensitivity));
    }

    mHeldHatX = (hatX < -0.5f) ? -1 : (hatX > 0.5f) ? 1 : 0;
    mHeldHatY = (hatY < -0.5f) ? -1 : (hatY > 0.5f) ? 1 : 0;

    if (!std::isfinite(rawL)) rawL = 0.0f;
    if (!std::isfinite(rawBrake)) rawBrake = 0.0f;
    float lTrig = std::max(0.0f, std::min(1.0f, std::max(rawL, rawBrake)));

    if (!std::isfinite(rawR)) rawR = 0.0f;
    if (!std::isfinite(rawGas)) rawGas = 0.0f;
    float rTrig = std::max(0.0f, std::min(1.0f, std::max(rawR, rawGas)));

    float trigThreshold = std::max(0.10f, std::min(0.80f, mMapping.triggerThreshold));
    bool lActive = (lTrig >= trigThreshold);
    if (lActive != mHeldTriggerL2) {
        mHeldTriggerL2 = lActive;
        if (lActive) {
            ControllerAction act = controller_mapping_get_action(&mMapping, BTN_L2);
            if (controller_mapping_is_spin(act)) spinFrames = 6;
        }
    }

    bool rActive = (rTrig >= trigThreshold);
    if (rActive != mHeldTriggerR2) {
        mHeldTriggerR2 = rActive;
        if (rActive) {
            ControllerAction act = controller_mapping_get_action(&mMapping, BTN_R2);
            if (controller_mapping_is_spin(act)) spinFrames = 6;
        }
    }

    updateButtonMask();

    float rStickX = 0.0f, rStickY = 0.0f;
    float rNorm = controller_mapping_apply_deadzone(rightX, rightY, 0.08f, &rStickX, &rStickY);
    rStickY = -rStickY;
    if (mMapping.invertCameraY) rStickY = -rStickY;

    if (mRightStickMode == 0) {
        pad.rightStickX = std::max(-1.0f, std::min(1.0f, -rStickX * mStarStickSensitivity));
        pad.rightStickY = std::max(-1.0f, std::min(1.0f, rStickY * mStarStickSensitivity));
    } else {
        pad.rightStickX = 0.0f;
        pad.rightStickY = 0.0f;
        if (rNorm > 0.001f && !touchscreenDown) {
            float speed = 0.035f * mStarStickSensitivity;
            pad.pointerX = std::max(-1.0f, std::min(1.0f, pad.pointerX + rStickX * speed));
            pad.pointerY = std::max(-1.0f, std::min(1.0f, pad.pointerY - rStickY * speed));
            pad.pointerValid = 1;
#if defined(__ANDROID__)
            mobile_ui::UIOverlay::get().setPointerPos(pad.pointerX, pad.pointerY);
#endif
        }
    }

    pad.connected = 1;
    return true;
}

bool GamepadInput::handleMotionEvent(AInputEvent* event, PortPadState& pad, int& spinFrames, bool touchscreenDown) {
#if defined(__ANDROID__)
    int32_t source = AInputEvent_getSource(event);
    float rawX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
    float rawY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
    float rawL = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_LTRIGGER, 0);
    float rawBrake = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_BRAKE, 0);
    float rawR = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RTRIGGER, 0);
    float rawGas = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_GAS, 0);
    float rightX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0);
    float rightY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0);
    if (fabsf(rightX) <= 0.05f && fabsf(rightY) <= 0.05f) {
        rightX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RX, 0);
        rightY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RY, 0);
    }
    float hatX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_X, 0);
    float hatY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_Y, 0);
    return handleMotion(source, rawX, rawY, rawL, rawBrake, rawR, rawGas, rightX, rightY, hatX, hatY, pad, spinFrames, touchscreenDown);
#else
    (void)event; (void)pad; (void)spinFrames; (void)touchscreenDown;
    return false;
#endif
}

bool GamepadInput::handleKeyEvent(AInputEvent* event, PortPadState& pad, int& spinFrames) {
#if defined(__ANDROID__)
    int32_t keyCode = AKeyEvent_getKeyCode(event);
    int32_t action = AKeyEvent_getAction(event);
    int32_t repeatCount = AKeyEvent_getRepeatCount(event);
    return handleKey(keyCode, action, repeatCount, pad, spinFrames);
#else
    (void)event; (void)pad; (void)spinFrames;
    return false;
#endif
}

void GamepadInput::reset() {
    mHeldPhysicalButtons = 0;
    mHeldBack = false;
    mHeldTriggerL2 = false;
    mHeldTriggerR2 = false;
    mHeldHatX = 0;
    mHeldHatY = 0;
    mPhysicalButtons = 0;
    mPhysicalStickX = 0.0f;
    mPhysicalStickY = 0.0f;
}

}  // namespace mobile_input
