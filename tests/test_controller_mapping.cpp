#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "port/controller_mapping.h"

static bool near(float a, float b) {
    return fabsf(a - b) < 0.001f;
}

static void testStandardProfile(void) {
    ControllerMapping map = controller_mapping_get_default(CONTROLLER_PROFILE_STANDARD);
    assert(map.deadzone == 0.18f);
    assert(map.triggerThreshold == 0.40f);
    assert(map.swapTriggersBumpers == 0);

    // South button is Jump
    ControllerAction actSouth = controller_mapping_get_action(&map, BTN_SOUTH);
    assert(actSouth == ACTION_JUMP);
    assert(controller_mapping_action_to_wpad(actSouth) == 0x0800);

    // West button is Spin
    ControllerAction actWest = controller_mapping_get_action(&map, BTN_WEST);
    assert(actWest == ACTION_SPIN);
    assert(controller_mapping_is_spin(actWest) == 1);

    // L1 is Camera Snap (C button)
    ControllerAction actL1 = controller_mapping_get_action(&map, BTN_L1);
    assert(actL1 == ACTION_CAMERA_SNAP);
    assert(controller_mapping_action_to_wpad(actL1) == 0x1000);

    // L2 is Crouch / Ground Pound (Z button)
    ControllerAction actL2 = controller_mapping_get_action(&map, BTN_L2);
    assert(actL2 == ACTION_CROUCH_GROUNDPOUND);
    assert(controller_mapping_action_to_wpad(actL2) == 0x2000);

    // R1 is Pointer Recenter
    ControllerAction actR1 = controller_mapping_get_action(&map, BTN_R1);
    assert(actR1 == ACTION_POINTER_RECENTER);
    assert(controller_mapping_is_pointer_recenter(actR1) == 1);

    printf("[PASS] Standard profile mappings verified\n");
}

static void testSwitchProfile(void) {
    ControllerMapping map = controller_mapping_get_default(CONTROLLER_PROFILE_SWITCH);
    // On Switch Pro Controller, physical B is South, Y is West, A is East, X is North
    assert(controller_mapping_get_action(&map, BTN_SOUTH) == ACTION_JUMP);
    assert(controller_mapping_get_action(&map, BTN_WEST) == ACTION_SPIN);
    assert(controller_mapping_get_action(&map, BTN_EAST) == ACTION_SHOOT_CANCEL);
    assert(controller_mapping_get_action(&map, BTN_NORTH) == ACTION_POINTER_RECENTER);

    printf("[PASS] Nintendo Switch layout profile verified\n");
}

static void testGameCubeProfile(void) {
    ControllerMapping map = controller_mapping_get_default(CONTROLLER_PROFILE_GAMECUBE);
    assert(controller_mapping_get_action(&map, BTN_SOUTH) == ACTION_JUMP);
    assert(controller_mapping_get_action(&map, BTN_WEST) == ACTION_SPIN);
    assert(controller_mapping_get_action(&map, BTN_L2) == ACTION_CROUCH_GROUNDPOUND);
    assert(controller_mapping_get_action(&map, BTN_R1) == ACTION_CAMERA_SNAP);

    printf("[PASS] GameCube layout profile verified\n");
}

static void testTriggerBumperSwap(void) {
    ControllerMapping map = controller_mapping_get_default(CONTROLLER_PROFILE_STANDARD);
    map.swapTriggersBumpers = 1;

    // L1 should now trigger L2 action (Crouch)
    assert(controller_mapping_get_action(&map, BTN_L1) == ACTION_CROUCH_GROUNDPOUND);
    // L2 should now trigger L1 action (Camera Snap)
    assert(controller_mapping_get_action(&map, BTN_L2) == ACTION_CAMERA_SNAP);
    // R1 should now trigger R2 action (Shoot)
    assert(controller_mapping_get_action(&map, BTN_R1) == ACTION_SHOOT_CANCEL);
    // R2 should now trigger R1 action (Pointer Recenter)
    assert(controller_mapping_get_action(&map, BTN_R2) == ACTION_POINTER_RECENTER);

    printf("[PASS] Trigger and Bumper swap logic verified\n");
}

static void testDeadzoneCalculation(void) {
    float outX = 0.0f, outY = 0.0f;
    float deadzone = 0.20f;

    // Below deadzone: clamped to exactly 0.0
    float norm = controller_mapping_apply_deadzone(0.10f, 0.10f, deadzone, &outX, &outY);
    assert(norm == 0.0f);
    assert(outX == 0.0f && outY == 0.0f);

    // Above deadzone: scaled correctly
    norm = controller_mapping_apply_deadzone(1.0f, 0.0f, deadzone, &outX, &outY);
    assert(near(norm, 1.0f));
    assert(near(outX, 1.0f));
    assert(near(outY, 0.0f));

    // Midpoint: deflection (0.6, 0.0) -> (0.6 - 0.2) / 0.8 = 0.5
    norm = controller_mapping_apply_deadzone(0.60f, 0.0f, deadzone, &outX, &outY);
    assert(near(norm, 0.5f));
    assert(near(outX, 0.5f));

    printf("[PASS] Deadzone deflection mathematics verified\n");
}

static void testAndroidKeyTranslation(void) {
    assert(controller_mapping_from_android_key(96) == BTN_SOUTH);
    assert(controller_mapping_from_android_key(97) == BTN_EAST);
    assert(controller_mapping_from_android_key(99) == BTN_WEST);
    assert(controller_mapping_from_android_key(100) == BTN_NORTH);
    assert(controller_mapping_from_android_key(102) == BTN_L1);
    assert(controller_mapping_from_android_key(103) == BTN_R1);
    assert(controller_mapping_from_android_key(104) == BTN_L2);
    assert(controller_mapping_from_android_key(105) == BTN_R2);
    assert(controller_mapping_from_android_key(108) == BTN_START);
    assert(controller_mapping_from_android_key(19) == BTN_DPAD_UP);
    assert(controller_mapping_from_android_key(999) == BTN_COUNT);

    printf("[PASS] Android keycode translation verified\n");
}

int main(void) {
    printf("=== Super Mario Galaxy Android — Controller Mapping Suite ===\n");
    testStandardProfile();
    testSwitchProfile();
    testGameCubeProfile();
    testTriggerBumperSwap();
    testDeadzoneCalculation();
    testAndroidKeyTranslation();
    printf("All controller mapping tests passed successfully!\n");
    return 0;
}
