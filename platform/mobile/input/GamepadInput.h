#pragma once

#include <stdint.h>
#include "port/input.h"
#include "port/controller_mapping.h"

#if defined(__ANDROID__)
#include <android/input.h>
#else
struct AInputEvent;
#endif

namespace mobile_ui {
struct Settings;
}

namespace mobile_input {

class GamepadInput {
public:
    GamepadInput();

    // Handles Android input events
    bool handleMotionEvent(AInputEvent* event, PortPadState& pad, int& spinFrames, bool touchscreenDown);
    bool handleKeyEvent(AInputEvent* event, PortPadState& pad, int& spinFrames);

    // Platform-agnostic event dispatch for testing and runtime decoupled verification
    bool handleKey(int32_t keyCode, int32_t action, int32_t repeatCount, PortPadState& pad, int& spinFrames);
    bool handleMotion(int32_t source, float rawX, float rawY, float rawL, float rawBrake,
                      float rawR, float rawGas, float rightX, float rightY,
                      float hatX, float hatY, PortPadState& pad, int& spinFrames, bool touchscreenDown);

    // Resets held buttons and state (pause, focus loss, disconnect)
    void reset();

    // Profile and mapping configuration
    void setProfile(ControllerProfile profile);
    void setMapping(const ControllerMapping& mapping);
    const ControllerMapping& getMapping() const { return mMapping; }

    // Wire persisted Settings into active mapping & calibration
    void applySettings(const mobile_ui::Settings& s);

    // Returns whether a physical controller has been detected
    bool isGamepadDetected() const { return mGamepadDetected; }

    // Physical analog stick values
    float stickX() const { return mPhysicalStickX; }
    float stickY() const { return mPhysicalStickY; }

    // Combined physical button masks
    uint32_t buttons() const { return mPhysicalButtons; }

    // Check if a specific physical button is currently held
    bool isPhysicalButtonHeld(ControllerButton btn) const {
        return (btn < BTN_COUNT) && ((mHeldPhysicalButtons & (1 << btn)) != 0);
    }

private:
    void updateButtonMask();

    bool mGamepadDetected = false;
    uint32_t mHeldPhysicalButtons = 0;
    bool mHeldBack = false;
    bool mHeldTriggerL2 = false;
    bool mHeldTriggerR2 = false;
    int32_t mHeldHatX = 0;
    int32_t mHeldHatY = 0;

    uint32_t mPhysicalButtons = 0;
    float mPhysicalStickX = 0.0f;
    float mPhysicalStickY = 0.0f;

    float mStickSensitivity = 1.0f;
    float mStarStickSensitivity = 1.0f;
    int mRightStickMode = 0;

    ControllerMapping mMapping;
};

}  // namespace mobile_input

