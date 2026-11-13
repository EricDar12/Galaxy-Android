// The gamepad's stand-ins for pointing the remote (port/pad_assist.h): the
// stick as the pointer.  Stretching a held pod, the cursor of cannons and
// menus, blowing a bubble, shooting star bits, and the grab button's mapping.
#include "pad_assist_test_util.h"

// A held pod is stretched by the stick from its rest position; A stays down
// until the grab button is let go.
static void testSling() {
    PortAssistState st;
    PortAssistFrame in;
    memset(&st, 0, sizeof(st));
    memset(&in, 0, sizeof(in));
    in.grabCount = 1;
    in.grabs[0] = grabAt(5, 0.1f, 0.2f, 0.0f, PORT_ASSIST_GRAB_RIDING);
    for (int frame = 0; frame < PORT_ASSIST_LEAD_FRAMES + 2; frame++) {
        PortPadState pad = padWith(PORT_BUTTON_GRAB, -1.0f, 0.0f);
        portPadAssistStep(&st, &in, &pad);
        assert(near(pad.pointerX, 0.1f));  // not stretched before it is held
    }
    in.grabCount = 0;
    in.sling = 1;
    in.slingX = 0.1f;
    in.slingY = 0.2f;
    in.slingUX = 0.001f;
    in.slingUY = 0.002f;
    in.slingReach = 300.0f;
    PortPadState pad = padWith(PORT_BUTTON_GRAB, -1.0f, 0.0f);
    portPadAssistStep(&st, &in, &pad);
    assert((pad.buttons & kA) && near(pad.pointerX, 0.1f - 0.3f) && near(pad.pointerY, 0.2f));
    pad = padWith(PORT_BUTTON_GRAB, 0.0f, 0.5f);  // stick up: up the screen
    portPadAssistStep(&st, &in, &pad);
    assert((pad.buttons & kA) && near(pad.pointerX, 0.1f) && near(pad.pointerY, 0.2f - 0.3f));
    pad = padWith(0, 0.0f, 0.5f);
    portPadAssistStep(&st, &in, &pad);
    assert(!(pad.buttons & kA) && !pad.pointerValid);
    // A pod stretched by a finger (no grab button) is left alone.
    pad = padWith(kA, 1.0f, 0.0f);
    pad.pointerValid = 1;
    pad.pointerX = 0.7f;
    portPadAssistStep(&st, &in, &pad);
    assert(near(pad.pointerX, 0.7f));
}

// Aiming a cannon: the stick moves the pointer from the middle of the
// screen, and it stays where it is left.
static void testCursor() {
    PortAssistState st;
    PortAssistFrame in;
    memset(&st, 0, sizeof(st));
    memset(&in, 0, sizeof(in));
    in.cursor = 1;
    PortPadState pad = padWith(0);
    portPadAssistStep(&st, &in, &pad);
    assert(pad.pointerValid && near(pad.pointerX, 0.0f) && near(pad.pointerY, 0.0f));
    for (int frame = 0; frame < 10; frame++) {
        pad = padWith(0, 1.0f, 0.0f);
        portPadAssistStep(&st, &in, &pad);
    }
    float x = pad.pointerX;
    assert(x > 0.1f && x < 0.5f && near(pad.pointerY, 0.0f));
    pad = padWith(kA);
    portPadAssistStep(&st, &in, &pad);
    assert(near(pad.pointerX, x) && (pad.buttons & kA));  // A fires where it was left
    for (int frame = 0; frame < 1000; frame++) {
        pad = padWith(0, 1.0f, 1.0f);
        portPadAssistStep(&st, &in, &pad);
    }
    assert(pad.pointerX <= 0.97f && pad.pointerY >= -0.97f && pad.pointerY < -0.9f);  // stays on screen; up is up
    // A finger on the screen takes over, and the stick carries on from there.
    pad = padWith(0);
    pad.pointerValid = 1;
    pad.pointerX = -0.3f;
    pad.pointerY = 0.3f;
    portPadAssistStep(&st, &in, &pad);
    assert(near(pad.pointerX, -0.3f));
    pad = padWith(0);
    portPadAssistStep(&st, &in, &pad);
    assert(pad.pointerValid && near(pad.pointerX, -0.3f) && near(pad.pointerY, 0.3f));
    // The next cannon starts from the middle again.
    in.cursor = 0;
    pad = padWith(0);
    portPadAssistStep(&st, &in, &pad);
    assert(!pad.pointerValid);
    in.cursor = 1;
    pad = padWith(0);
    portPadAssistStep(&st, &in, &pad);
    assert(near(pad.pointerX, 0.0f));
    // Menus ask for a quicker hand than the cannon's.
    memset(&st, 0, sizeof(st));
    in.cursorSpeed = 2.0f;
    for (int frame = 0; frame < 10; frame++) {
        pad = padWith(0, 1.0f, 0.0f);
        portPadAssistStep(&st, &in, &pad);
    }
    assert(near(pad.pointerX, 2.0f * x));
}

// A bubble is blown the way the stick points: the pointer sits behind it,
// nearer the harder the stick is pushed.
static void testBubble() {
    PortAssistState st;
    PortAssistFrame in;
    memset(&st, 0, sizeof(st));
    memset(&in, 0, sizeof(in));
    in.bubble = 1;
    in.bubbleX = 0.0f;
    in.bubbleY = 0.1f;
    in.bubbleUX = 0.001f;
    in.bubbleUY = 0.001f;
    in.bubbleNear = 200.0f;
    in.bubbleFar = 600.0f;
    PortPadState pad = padWith(kA);
    portPadAssistStep(&st, &in, &pad);
    assert(!pad.pointerValid);  // no stick, no wind
    pad = padWith(kA, 1.0f, 0.0f);
    portPadAssistStep(&st, &in, &pad);
    assert(pad.pointerValid && near(pad.pointerX, -0.2f) && near(pad.pointerY, 0.1f) && (pad.buttons & kA));
    pad = padWith(kA, 0.0f, 1.0f);  // up: the wind comes from below
    portPadAssistStep(&st, &in, &pad);
    assert(near(pad.pointerX, 0.0f) && near(pad.pointerY, 0.1f + 0.2f));
    pad = padWith(kA, 0.3f, 0.0f);  // a light push: from further away
    portPadAssistStep(&st, &in, &pad);
    assert(pad.pointerX < -0.5f);
    // A finger blows from where it is.
    pad = padWith(kA, 1.0f, 0.0f);
    pad.pointerValid = 1;
    pad.pointerX = 0.6f;
    portPadAssistStep(&st, &in, &pad);
    assert(near(pad.pointerX, 0.6f));
}

// B with no pointer on screen: the pointer goes where Mario faces, and B
// follows once the game has seen it, even for the shortest tap.
static void testShot() {
    PortAssistState st;
    PortAssistFrame in;
    memset(&st, 0, sizeof(st));
    memset(&in, 0, sizeof(in));
    in.shot = 1;
    in.shotX = 0.2f;
    in.shotY = -0.3f;
    PortPadState pad = padWith(0);
    portPadAssistStep(&st, &in, &pad);
    assert(!pad.pointerValid);
    int bFrames = 0;
    for (int frame = 0; frame < 12; frame++) {
        pad = padWith(frame == 0 ? kB : 0);  // a one-frame tap
        portPadAssistStep(&st, &in, &pad);
        if (frame < PORT_ASSIST_LEAD_FRAMES) {
            assert(pad.pointerValid && near(pad.pointerX, 0.2f) && near(pad.pointerY, -0.3f));
            assert(!(pad.buttons & kB));
        }
        if (pad.buttons & kB) {
            assert(pad.pointerValid);
            bFrames++;
        }
    }
    assert(bFrames == 2);
    assert(!pad.pointerValid);
    // Held: B stays down (the game repeats the shot) and the aim follows.
    for (int frame = 0; frame < 40; frame++) {
        in.shotX = 0.2f + frame * 0.01f;
        pad = padWith(kB);
        portPadAssistStep(&st, &in, &pad);
        assert(pad.pointerValid && near(pad.pointerX, in.shotX));
        assert(((pad.buttons & kB) != 0) == (frame >= PORT_ASSIST_LEAD_FRAMES));
    }
    // A finger on the screen aims by itself, at once.
    pad = padWith(0);
    portPadAssistStep(&st, &in, &pad);
    pad = padWith(kB);
    pad.pointerValid = 1;
    pad.pointerX = -0.7f;
    portPadAssistStep(&st, &in, &pad);
    assert((pad.buttons & kB) && near(pad.pointerX, -0.7f));
    // Where star bits cannot be shot, B is B.
    in.shot = 0;
    pad = padWith(0);
    portPadAssistStep(&st, &in, &pad);
    pad = padWith(kB);
    portPadAssistStep(&st, &in, &pad);
    assert((pad.buttons & kB) && !pad.pointerValid);
}

// The grab action maps to the grab bit, which is clear of the Wii buttons
// and of the spin bit, on R1 in every preset.
static void testMapping() {
    assert(controller_mapping_action_to_wpad(ACTION_GRAB) == PORT_BUTTON_GRAB);
    assert((PORT_BUTTON_GRAB & 0xFFFFu) == 0 && PORT_BUTTON_GRAB != PORT_BUTTON_SPIN);
    assert(controller_mapping_is_valid_action(ACTION_GRAB));
    for (int p = 0; p < CONTROLLER_PROFILE_COUNT; p++) {
        ControllerMapping m = controller_mapping_get_default((ControllerProfile)p);
        assert(controller_mapping_get_action(&m, BTN_R1) == ACTION_GRAB);
        int spins = 0;
        for (int b = 0; b < BTN_COUNT; b++) {
            if (controller_mapping_get_action(&m, (ControllerButton)b) == ACTION_SPIN) spins++;
        }
        assert(spins >= 1);
    }
}

int main() {
    testSling();
    testCursor();
    testBubble();
    testShot();
    testMapping();
    printf("pad_assist_aim: ok\n");
    return 0;
}
