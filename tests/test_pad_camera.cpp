// The right stick as the game's camera buttons (port/pad_camera.h), and the
// on-screen prompts for the pad assist's buttons (port/pad_hint.h).
#include "pad_assist_test_util.h"
#include "port/pad_camera.h"
#include "port/pad_hint.h"

static const uint32_t kLeft = 0x0001, kRight = 0x0002, kBehind = 0x4000, kUp = 0x0008;

// The buttons the stick presses, for a stick pushed (right, up).
static uint32_t cameraStep(PortCameraStick* st, float right, float up) {
    PortPadState pad = padWith(0);
    pad.rightStickX = -right;  // the pad state's right stick is left-positive
    pad.rightStickY = up;
    portPadCameraStep(st, &pad);
    return pad.buttons;
}

// At rest, or pushed only a little, the stick presses nothing, and leaves
// the other buttons alone.
static void testRest() {
    PortCameraStick st;
    memset(&st, 0, sizeof(st));
    assert(cameraStep(&st, 0.0f, 0.0f) == 0);
    assert(cameraStep(&st, 0.4f, 0.0f) == 0);
    assert(cameraStep(&st, 0.0f, 0.7f) == 0);  // up and down need a firm push
    PortPadState pad = padWith(kA);
    pad.rightStickX = -1.0f;
    portPadCameraStep(&st, &pad);
    assert(pad.buttons == (kA | kRight));
}

// Held to one side, the stick asks for a turn again and again: the button
// comes up before each new press, since the game acts on the press.
static void testTurnRepeats() {
    PortCameraStick st;
    memset(&st, 0, sizeof(st));
    int presses = 0;
    uint32_t prev = 0;
    for (int frame = 0; frame < 3 * PORT_CAMERA_REPEAT_FRAMES; frame++) {
        uint32_t b = cameraStep(&st, 1.0f, 0.2f);
        assert(b == 0 || b == kRight);
        if (b && !prev) presses++;
        prev = b;
    }
    assert(presses == 3);
    // Straight over to the other side: a press that way at once.
    assert(cameraStep(&st, -1.0f, 0.0f) == kLeft);
    assert(cameraStep(&st, 0.0f, 0.0f) == 0);
}

// Up and down are one press per push, however long the stick is held, and
// a diagonal push counts as a turn: the first-person view never comes up
// by accident.
static void testViewOncePerPush() {
    PortCameraStick st;
    memset(&st, 0, sizeof(st));
    for (int frame = 0; frame < 5 * PORT_CAMERA_REPEAT_FRAMES; frame++) {
        assert(cameraStep(&st, 0.0f, 1.0f) == kUp);  // held throughout: one press as the game sees it
    }
    assert(cameraStep(&st, 0.0f, 0.0f) == 0);
    assert(cameraStep(&st, 0.0f, -1.0f) == kBehind);
    assert(cameraStep(&st, 0.9f, 0.9f) == kRight);
    assert(cameraStep(&st, -0.9f, -0.88f) == kLeft);
}

static PortAssistFrame frameWithGrab(uint32_t flags) {
    PortAssistFrame in;
    memset(&in, 0, sizeof(in));
    in.grabCount = 1;
    in.grabs[0] = grabAt(7, 0.2f, 0.2f, 1000.0f, flags);
    return in;
}

// What there is to prompt for.
static void testHint() {
    PortAssistState st;
    PortAssistFrame in;
    memset(&st, 0, sizeof(st));
    memset(&in, 0, sizeof(in));
    assert(portPadAssistHint(&st, &in) == PORT_ASSIST_HINT_NONE);
    in = frameWithGrab(0);
    assert(portPadAssistHint(&st, &in) == PORT_ASSIST_HINT_GRAB);
    in = frameWithGrab(PORT_ASSIST_GRAB_ACTIVE);  // already pulling: nothing to say
    assert(portPadAssistHint(&st, &in) == PORT_ASSIST_HINT_NONE);
    in = frameWithGrab(PORT_ASSIST_GRAB_RIDING);
    assert(portPadAssistHint(&st, &in) == PORT_ASSIST_HINT_SLING);
    st.lockId = 7;  // the button is held: the player knows
    assert(portPadAssistHint(&st, &in) == PORT_ASSIST_HINT_NONE);
    st.lockId = 0;
    memset(&in, 0, sizeof(in));
    in.bubble = 1;
    assert(portPadAssistHint(&st, &in) == PORT_ASSIST_HINT_BUBBLE);
    in.bubble = 0;
    in.cursor = 1;
    in.cursorSpeed = 1.0f;
    assert(portPadAssistHint(&st, &in) == PORT_ASSIST_HINT_CANNON);
    in.cursorSpeed = 1.6f;  // a menu's cursor needs no prompt
    assert(portPadAssistHint(&st, &in) == PORT_ASSIST_HINT_NONE);
}

// A prompt shows once the hint has stood a moment, not again for a while,
// and only a few times in all.
static void testPrompter() {
    PortHintPrompter p;
    memset(&p, 0, sizeof(p));
    int shows = 0;
    for (int frame = 0; frame < 200; frame++) {
        int shown = portPadHintStep(&p, PORT_ASSIST_HINT_GRAB);
        if (shown) {
            assert(shown == PORT_ASSIST_HINT_GRAB && frame == PORT_HINT_STEADY_FRAMES - 1);
            shows++;
        }
    }
    assert(shows == 1);
    // A hint that flickers on and off never shows.
    memset(&p, 0, sizeof(p));
    for (int frame = 0; frame < 400; frame++) {
        assert(portPadHintStep(&p, (frame / 5) % 2 ? PORT_ASSIST_HINT_BUBBLE : PORT_ASSIST_HINT_NONE) == 0);
    }
    // Coming back to it: quiet for a while, then again, up to the limit.
    memset(&p, 0, sizeof(p));
    shows = 0;
    for (int visit = 0; visit < 10; visit++) {
        for (int frame = 0; frame < PORT_HINT_QUIET_FRAMES + 10; frame++) {
            if (portPadHintStep(&p, frame < 100 ? PORT_ASSIST_HINT_GRAB : PORT_ASSIST_HINT_NONE)) shows++;
        }
    }
    assert(shows == PORT_HINT_SHOWS);
    memset(&p, 0, sizeof(p));
    shows = 0;
    for (int visit = 0; visit < 4; visit++) {  // back within the quiet time: not again
        for (int frame = 0; frame < 200; frame++) {
            if (portPadHintStep(&p, frame < 100 ? PORT_ASSIST_HINT_GRAB : PORT_ASSIST_HINT_NONE)) shows++;
        }
    }
    assert(shows == 1);
    // Each hint keeps its own count; out-of-range values are no hint.
    assert(portPadHintStep(&p, 99) == 0);
    for (int frame = 0; frame < PORT_HINT_STEADY_FRAMES; frame++) {
        shows = portPadHintStep(&p, PORT_ASSIST_HINT_CANNON);
    }
    assert(shows == PORT_ASSIST_HINT_CANNON);
}

// A prompt names the button by what every pad calls it, where there is such a name.
static void testButtonLabels() {
    ControllerMapping m = controller_mapping_get_default(CONTROLLER_PROFILE_STANDARD);
    assert(controller_mapping_find_button(&m, ACTION_GRAB) == BTN_R1);
    assert(!strcmp(controller_mapping_button_label(BTN_R1), "R1"));
    m.swapTriggersBumpers = 1;
    assert(controller_mapping_find_button(&m, ACTION_GRAB) == BTN_R2);
    assert(!strcmp(controller_mapping_button_label(BTN_R2), "R2"));
    assert(controller_mapping_button_label(controller_mapping_find_button(&m, ACTION_JUMP)) == nullptr);  // a face button
    for (int b = 0; b < BTN_COUNT; b++) m.buttonActions[b] = ACTION_NONE;
    assert(controller_mapping_find_button(&m, ACTION_GRAB) == BTN_COUNT);
    assert(controller_mapping_button_label(BTN_COUNT) == nullptr);
}

int main() {
    testRest();
    testTurnRepeats();
    testViewOncePerPush();
    testHint();
    testPrompter();
    testButtonLabels();
    puts("pad_camera: ok");
    return 0;
}
