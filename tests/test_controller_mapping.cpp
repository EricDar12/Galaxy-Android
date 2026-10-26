#include <assert.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "port/controller_mapping.h"
#include "port/input.h"
#include "input/GamepadInput.h"
#include "ui/Settings.h"
#include "ui/SettingsCodec.h"

using mobile_input::GamepadInput;
using mobile_ui::Settings;
using mobile_ui::applySettingLine;
using mobile_ui::serializeSettings;

static bool nearVal(float a, float b) {
    return std::fabs(a - b) < 0.001f;
}

static void testStandardSwitchGamecubeProfiles(void) {
    ControllerMapping stdMap = controller_mapping_get_default(CONTROLLER_PROFILE_STANDARD);
    assert(controller_mapping_get_action(&stdMap, BTN_SOUTH) == ACTION_JUMP);
    assert(controller_mapping_get_action(&stdMap, BTN_WEST) == ACTION_SPIN);
    assert(controller_mapping_get_action(&stdMap, BTN_L1) == ACTION_CAMERA_SNAP);
    assert(controller_mapping_get_action(&stdMap, BTN_L2) == ACTION_CROUCH_GROUNDPOUND);
    assert(controller_mapping_get_action(&stdMap, BTN_R1) == ACTION_POINTER_RECENTER);

    ControllerMapping swMap = controller_mapping_get_default(CONTROLLER_PROFILE_SWITCH);
    assert(controller_mapping_get_action(&swMap, BTN_SOUTH) == ACTION_JUMP);
    assert(controller_mapping_get_action(&swMap, BTN_WEST) == ACTION_SPIN);
    assert(controller_mapping_get_action(&swMap, BTN_NORTH) == ACTION_POINTER_RECENTER);
    assert(controller_mapping_get_action(&swMap, BTN_R1) == ACTION_SPIN);

    ControllerMapping gcMap = controller_mapping_get_default(CONTROLLER_PROFILE_GAMECUBE);
    assert(controller_mapping_get_action(&gcMap, BTN_SOUTH) == ACTION_JUMP);
    assert(controller_mapping_get_action(&gcMap, BTN_WEST) == ACTION_SPIN);
    assert(controller_mapping_get_action(&gcMap, BTN_L1) == ACTION_SPIN);
    assert(controller_mapping_get_action(&gcMap, BTN_R1) == ACTION_CAMERA_SNAP);
    assert(controller_mapping_get_action(&gcMap, BTN_L2) == ACTION_CROUCH_GROUNDPOUND);
    printf("[PASS] Standard, Switch Pro, and GameCube profile mappings verified\n");
}

static void testCustomProfileAndSerialization(void) {
    Settings orig;
    orig.controllerProfile = 3; // Custom
    orig.customButtonActions[BTN_SOUTH] = ACTION_CAMERA_SNAP;
    orig.customButtonActions[BTN_WEST] = ACTION_JUMP;
    orig.controllerDeadzone = 0.22f;
    orig.triggerThreshold = 0.55f;
    orig.swapTriggersBumpers = true;

    std::string text = serializeSettings(orig);
    Settings loaded;
    applySettingLine(loaded, text.c_str());
    const char* cur = text.c_str();
    while (*cur) {
        const char* next = strchr(cur, '\n');
        size_t len = next ? (size_t)(next - cur) : strlen(cur);
        std::string line(cur, len);
        applySettingLine(loaded, line.c_str());
        if (!next) break;
        cur = next + 1;
    }

    assert(loaded.controllerProfile == 3);
    assert(loaded.customButtonActions[BTN_SOUTH] == ACTION_CAMERA_SNAP);
    assert(loaded.customButtonActions[BTN_WEST] == ACTION_JUMP);
    assert(nearVal(loaded.controllerDeadzone, 0.22f));
    assert(nearVal(loaded.triggerThreshold, 0.55f));
    assert(loaded.swapTriggersBumpers == true);

    GamepadInput gp;
    gp.applySettings(loaded);
    assert(gp.getMapping().deadzone == 0.22f);
    assert(gp.getMapping().triggerThreshold == 0.55f);
    assert(gp.getMapping().swapTriggersBumpers == 1);

    PortPadState pad{};
    int spinFrames = 0;
    gp.handleKey(96 /* BUTTON_A / SOUTH */, 0 /* DOWN */, 0, pad, spinFrames);
    assert(gp.buttons() == 0x1000 /* ACTION_CAMERA_SNAP */);
    gp.handleKey(96, 1 /* UP */, 0, pad, spinFrames);
    assert(gp.buttons() == 0);
    printf("[PASS] Custom profile serialization, round-trip, and runtime binding verified\n");
}

static void testRuntimeBoundaryAndInvalidSettings(void) {
    Settings s;
    s.controllerProfile = 99; // Invalid profile
    s.controllerDeadzone = NAN; // Non-finite deadzone
    s.triggerThreshold = INFINITY; // Non-finite trigger threshold

    GamepadInput gp;
    gp.applySettings(s);
    assert(gp.getMapping().deadzone == 0.18f); // Safe fallback
    assert(gp.getMapping().triggerThreshold == 0.40f); // Safe fallback

    assert(controller_mapping_is_valid_profile(99) == 0);
    assert(controller_mapping_is_valid_profile(0) == 1);
    assert(controller_mapping_is_valid_action(99) == 0);
    assert(controller_mapping_is_valid_action(ACTION_JUMP) == 1);
    printf("[PASS] Runtime boundary safe fallback for invalid enum and NaN/inf verified\n");
}

static void testTriggerBumperSwapDigitalAndAnalog(void) {
    Settings s;
    s.controllerProfile = 0; // Standard
    s.swapTriggersBumpers = true;

    GamepadInput gp;
    gp.applySettings(s);

    PortPadState pad{};
    int spinFrames = 0;

    // Digital L1 (Bumper) should now trigger L2 action (Crouch 0x2000)
    gp.handleKey(102 /* BUTTON_L1 */, 0 /* DOWN */, 0, pad, spinFrames);
    assert(gp.buttons() == 0x2000);
    gp.handleKey(102, 1 /* UP */, 0, pad, spinFrames);
    assert(gp.buttons() == 0);

    // Digital L2 (Trigger key) should now trigger L1 action (Camera Snap 0x1000)
    gp.handleKey(104 /* BUTTON_L2 */, 0 /* DOWN */, 0, pad, spinFrames);
    assert(gp.buttons() == 0x1000);
    gp.handleKey(104, 1 /* UP */, 0, pad, spinFrames);
    assert(gp.buttons() == 0);

    // Analog L2 (Trigger axis) should now trigger L1 action (Camera Snap 0x1000)
    gp.handleMotion(0x01000010, 0.0f, 0.0f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, pad, spinFrames, false);
    assert(gp.buttons() == 0x1000);
    gp.handleMotion(0x01000010, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, pad, spinFrames, false);
    assert(gp.buttons() == 0);
    printf("[PASS] Digital and analog trigger/bumper swap symmetry verified\n");
}

static void testDeadzoneMathAndBoundaryCases(void) {
    float outX = -1.0f, outY = -1.0f;
    float deadzone = 0.20f;

    // 1. Zero input
    float norm = controller_mapping_apply_deadzone(0.0f, 0.0f, deadzone, &outX, &outY);
    assert(norm == 0.0f && outX == 0.0f && outY == 0.0f);

    // 2. Near-threshold sub-deadzone (mag = 0.1414 <= 0.20)
    norm = controller_mapping_apply_deadzone(0.10f, 0.10f, deadzone, &outX, &outY);
    assert(norm == 0.0f && outX == 0.0f && outY == 0.0f);

    // 3. Diagonal input at unit radius (mag = 1.0)
    norm = controller_mapping_apply_deadzone(0.707107f, 0.707107f, deadzone, &outX, &outY);
    assert(nearVal(norm, 1.0f));
    assert(nearVal(outX, 0.707107f) && nearVal(outY, 0.707107f));

    // 4. Out-of-range input (mag = 2.0)
    norm = controller_mapping_apply_deadzone(2.0f, 0.0f, deadzone, &outX, &outY);
    assert(nearVal(norm, 1.0f));
    assert(nearVal(outX, 1.0f) && nearVal(outY, 0.0f));

    // 5. Non-finite values safely rejected
    norm = controller_mapping_apply_deadzone(NAN, 0.5f, deadzone, &outX, &outY);
    assert(norm == 0.0f && outX == 0.0f && outY == 0.0f);
    norm = controller_mapping_apply_deadzone(0.5f, INFINITY, deadzone, &outX, &outY);
    assert(norm == 0.0f && outX == 0.0f && outY == 0.0f);
    printf("[PASS] Radial deadzone zero, near-threshold, diagonal, out-of-range, and NaN verified\n");
}

static void testTriggerCalibrationAndRestingState(void) {
    Settings s;
    s.triggerThreshold = 0.40f;
    GamepadInput gp;
    gp.applySettings(s);

    PortPadState pad{};
    int spinFrames = 0;

    // Resting trigger (0.0f or negative or NaN) must not activate
    gp.handleMotion(0x01000010, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, pad, spinFrames, false);
    assert(gp.buttons() == 0);
    gp.handleMotion(0x01000010, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, pad, spinFrames, false);
    assert(gp.buttons() == 0);
    gp.handleMotion(0x01000010, 0.0f, 0.0f, NAN, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, pad, spinFrames, false);
    assert(gp.buttons() == 0);

    // Below threshold (0.30f < 0.40f) -> inactive
    gp.handleMotion(0x01000010, 0.0f, 0.0f, 0.30f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, pad, spinFrames, false);
    assert(gp.buttons() == 0);

    // Above threshold (0.45f >= 0.40f) -> active (L2 = Crouch 0x2000 in Standard)
    gp.handleMotion(0x01000010, 0.0f, 0.0f, 0.45f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, pad, spinFrames, false);
    assert(gp.buttons() == 0x2000);

    // Recalibrate trigger to higher threshold (0.70f)
    s.triggerThreshold = 0.70f;
    gp.applySettings(s);
    // At 0.45f it should now be inactive
    gp.handleMotion(0x01000010, 0.0f, 0.0f, 0.45f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, pad, spinFrames, false);
    assert(gp.buttons() == 0);
    // At 0.75f it activates
    gp.handleMotion(0x01000010, 0.0f, 0.0f, 0.75f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, pad, spinFrames, false);
    assert(gp.buttons() == 0x2000);
    printf("[PASS] Trigger resting immunity and threshold calibration depth verified\n");
}

static void testHeldButtonsSymmetryAndProfileSwitch(void) {
    GamepadInput gp;
    PortPadState pad{};
    int spinFrames = 0;

    // Press BTN_SOUTH (Jump 0x0800)
    gp.handleKey(96 /* SOUTH */, 0 /* DOWN */, 0, pad, spinFrames);
    assert(gp.buttons() == 0x0800);
    assert(gp.isPhysicalButtonHeld(BTN_SOUTH));

    // Dynamic profile switch to Switch Pro while button is held
    Settings s;
    s.controllerProfile = 1; // Switch
    gp.applySettings(s);
    // Button is still held, mapping updated
    assert(gp.isPhysicalButtonHeld(BTN_SOUTH));
    assert(gp.buttons() == 0x0800);

    // Release button: clean release to 0, no stuck buttons
    gp.handleKey(96, 1 /* UP */, 0, pad, spinFrames);
    assert(gp.buttons() == 0);
    assert(!gp.isPhysicalButtonHeld(BTN_SOUTH));
    printf("[PASS] Dynamic profile remapping with held buttons verified\n");
}

static void testKeyRepeatSpinAndRecenter(void) {
    GamepadInput gp;
    PortPadState pad{};
    int spinFrames = 0;

    // Initial press of Spin (West in Standard): spinFrames becomes 6
    gp.handleKey(99 /* WEST */, 0 /* DOWN */, 0, pad, spinFrames);
    assert(spinFrames == 6);
    spinFrames = 4; // Simulate engine countdown

    // Android key repeat event (action = DOWN, repeatCount > 0)
    gp.handleKey(99, 0 /* DOWN */, 1 /* REPEAT */, pad, spinFrames);
    assert(spinFrames == 4); // Must NOT reset countdown to 6 on repeats!

    // Release
    gp.handleKey(99, 1 /* UP */, 0, pad, spinFrames);

    // Pointer Recenter (North in Switch Pro)
    Settings s;
    s.controllerProfile = 1;
    gp.applySettings(s);
    pad.pointerX = 0.75f;
    pad.pointerY = -0.50f;
    gp.handleKey(100 /* NORTH */, 0 /* DOWN */, 0, pad, spinFrames);
    assert(pad.pointerX == 0.0f && pad.pointerY == 0.0f);
    assert(pad.pointerValid == 1);

    // Subsequent pointer steering can move freely
    gp.handleMotion(0x01000010, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, pad, spinFrames, false);
    // Button release
    gp.handleKey(100, 1 /* UP */, 0, pad, spinFrames);
    printf("[PASS] Key repeat edge-triggering, spin delivery, and pointer recenter verified\n");
}

static void testResetLifecycle(void) {
    GamepadInput gp;
    PortPadState pad{};
    int spinFrames = 0;

    gp.handleKey(96, 0, 0, pad, spinFrames);
    gp.handleKey(97, 0, 0, pad, spinFrames);
    assert(gp.buttons() != 0);

    // Simulating pause / focus loss / disconnect
    gp.reset();
    assert(gp.buttons() == 0);
    assert(gp.stickX() == 0.0f && gp.stickY() == 0.0f);
    assert(!gp.isPhysicalButtonHeld(BTN_SOUTH));
    assert(!gp.isPhysicalButtonHeld(BTN_EAST));
    printf("[PASS] Reset lifecycle clears all held buttons without stuck inputs\n");
}

int main(void) {
    printf("=== Super Mario Galaxy Android — Controller Integration & Safety Suite ===\n");
    testStandardSwitchGamecubeProfiles();
    testCustomProfileAndSerialization();
    testRuntimeBoundaryAndInvalidSettings();
    testTriggerBumperSwapDigitalAndAnalog();
    testDeadzoneMathAndBoundaryCases();
    testTriggerCalibrationAndRestingState();
    testHeldButtonsSymmetryAndProfileSwitch();
    testKeyRepeatSpinAndRecenter();
    testResetLifecycle();
    printf("All 9 controller integration suites passed successfully!\n");
    return 0;
}
