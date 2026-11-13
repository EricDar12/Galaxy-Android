// The gamepad's stand-ins for pointing the remote (port/pad_assist.h): what
// portPadAssistStep does to the pointer and the A/B buttons for what the
// game offers.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "port/controller_mapping.h"
#include "port/pad_assist.h"
#include "port/spin_button.h"

static const uint32_t kA = 0x0800, kB = 0x0400;

static PortPadState padWith(uint32_t buttons, float sx = 0.0f, float sy = 0.0f) {
    PortPadState pad;
    memset(&pad, 0, sizeof(pad));
    pad.connected = 1;
    pad.buttons = buttons;
    pad.stickX = sx;
    pad.stickY = sy;
    return pad;
}

static PortAssistGrab grabAt(uintptr_t id, float x, float y, float dist, uint32_t flags = 0) {
    PortAssistGrab g;
    memset(&g, 0, sizeof(g));
    g.id = id;
    g.x = x;
    g.y = y;
    g.dirX = x;  // Mario at the middle of the screen
    g.dirY = y;
    g.dist = dist;
    g.flags = flags;
    return g;
}

static bool near(float a, float b) { return fabsf(a - b) < 0.0001f; }

// Nothing on offer, nothing pressed: the pad passes through untouched, and
// the grab bit never reaches the game.
static void testIdle() {
    PortAssistState st;
    PortAssistFrame in;
    memset(&st, 0, sizeof(st));
    memset(&in, 0, sizeof(in));
    PortPadState pad = padWith(kA | PORT_BUTTON_SPIN | PORT_BUTTON_GRAB, 0.3f, -0.2f);
    portPadAssistStep(&st, &in, &pad);
    assert(pad.buttons == (kA | PORT_BUTTON_SPIN));
    assert(!pad.pointerValid);
    assert(near(pad.stickX, 0.3f) && near(pad.stickY, -0.2f));
}

// The grab button points at the offer first, and presses A only once the
// game has seen the pointer there.
static void testGrabLeadsWithThePointer() {
    PortAssistState st;
    PortAssistFrame in;
    memset(&st, 0, sizeof(st));
    memset(&in, 0, sizeof(in));
    in.grabCount = 1;
    in.grabs[0] = grabAt(7, 0.4f, -0.2f, 1000.0f);
    for (int frame = 0; frame < 20; frame++) {
        PortPadState pad = padWith(PORT_BUTTON_GRAB);
        portPadAssistStep(&st, &in, &pad);
        assert(pad.pointerValid && near(pad.pointerX, 0.4f) && near(pad.pointerY, -0.2f));
        assert(!(pad.buttons & PORT_BUTTON_GRAB));
        if (frame < PORT_ASSIST_LEAD_FRAMES) {
            assert(!(pad.buttons & kA));
        } else if (frame <= PORT_ASSIST_LEAD_FRAMES + PORT_ASSIST_RETRY_FRAMES) {
            assert(pad.buttons & kA);
        }
        // The thing starts pulling a few frames after A.
        if (frame == PORT_ASSIST_LEAD_FRAMES + 3) in.grabs[0].flags = PORT_ASSIST_GRAB_ACTIVE;
        if (frame > PORT_ASSIST_LEAD_FRAMES + 3) assert(pad.buttons & kA);
    }
    // The pointer follows the thing while it is held.
    in.grabs[0].x = -0.1f;
    PortPadState pad = padWith(PORT_BUTTON_GRAB);
    portPadAssistStep(&st, &in, &pad);
    assert(near(pad.pointerX, -0.1f) && (pad.buttons & kA));
    // Letting go lets go of A and of the pointer.
    pad = padWith(0);
    portPadAssistStep(&st, &in, &pad);
    assert(!(pad.buttons & kA) && !pad.pointerValid && st.lockId == 0);
}

// A grab the thing never answers (it says it is not pulling) is tried
// again: A comes up and goes down once more, for as long as the button is held.
static void testGrabRetries() {
    PortAssistState st;
    PortAssistFrame in;
    memset(&st, 0, sizeof(st));
    memset(&in, 0, sizeof(in));
    in.grabCount = 1;
    in.grabs[0] = grabAt(7, 0.0f, 0.5f, 1000.0f);
    int presses = 0, prevA = 0;
    for (int frame = 0; frame < 3 * (PORT_ASSIST_LEAD_FRAMES + PORT_ASSIST_RETRY_FRAMES + 2); frame++) {
        PortPadState pad = padWith(PORT_BUTTON_GRAB);
        portPadAssistStep(&st, &in, &pad);
        int a = (pad.buttons & kA) != 0;
        if (a && !prevA) presses++;
        prevA = a;
        assert(pad.pointerValid);
    }
    assert(presses == 3);
}

// Once held, the lock survives its offer going away (a Pull Star off screen
// while Mario flies to it), and nothing else steals it.
static void testHeldLockSurvivesLostOffer() {
    PortAssistState st;
    PortAssistFrame in;
    memset(&st, 0, sizeof(st));
    memset(&in, 0, sizeof(in));
    in.grabCount = 1;
    in.grabs[0] = grabAt(7, 0.4f, 0.4f, 1000.0f, PORT_ASSIST_GRAB_ACTIVE);
    for (int frame = 0; frame < 8; frame++) {
        PortPadState pad = padWith(PORT_BUTTON_GRAB);
        portPadAssistStep(&st, &in, &pad);
    }
    in.grabs[0] = grabAt(9, -0.5f, 0.0f, 500.0f);
    for (int frame = 0; frame < 30; frame++) {
        PortPadState pad = padWith(PORT_BUTTON_GRAB);
        portPadAssistStep(&st, &in, &pad);
        assert(st.lockId == 7);
        assert((pad.buttons & kA) && near(pad.pointerX, 0.4f));
    }
}

// Before A is down, a lock whose offer goes away is given up after a few
// frames and the next best taken.
static void testUnheldLockMovesOn() {
    PortAssistState st;
    PortAssistFrame in;
    memset(&st, 0, sizeof(st));
    memset(&in, 0, sizeof(in));
    in.grabCount = 1;
    in.grabs[0] = grabAt(7, 0.4f, 0.4f, 1000.0f);
    PortPadState pad = padWith(PORT_BUTTON_GRAB);
    portPadAssistStep(&st, &in, &pad);
    assert(st.lockId == 7);
    in.grabs[0] = grabAt(9, -0.5f, 0.0f, 500.0f);
    for (int frame = 0; frame < PORT_ASSIST_LOST_FRAMES + 1; frame++) {
        pad = padWith(PORT_BUTTON_GRAB);
        portPadAssistStep(&st, &in, &pad);
        assert(!(pad.buttons & kA));
    }
    assert(st.lockId == 9);
}

// Which offer a grab takes.
static void testPick() {
    PortAssistFrame in;
    memset(&in, 0, sizeof(in));
    in.grabCount = 3;
    in.grabs[0] = grabAt(1, 0.5f, 0.0f, 2000.0f);   // right, far
    in.grabs[1] = grabAt(2, -0.5f, 0.0f, 1000.0f);  // left, near
    in.grabs[2] = grabAt(3, 0.0f, -0.5f, 1500.0f);  // up
    // No stick: the nearest.
    assert(portPadAssistPick(&in, 0.0f, 0.0f)->id == 2);
    // The stick picks by direction (stick up is screen up, y negative).
    assert(portPadAssistPick(&in, 1.0f, 0.0f)->id == 1);
    assert(portPadAssistPick(&in, -1.0f, 0.0f)->id == 2);
    assert(portPadAssistPick(&in, 0.0f, 1.0f)->id == 3);
    // Nothing that way: the nearest after all.
    assert(portPadAssistPick(&in, 0.0f, -1.0f)->id == 2);
    // One that already pulls Mario, or that he has reached, only if there
    // is nothing else.
    in.grabs[1].flags = PORT_ASSIST_GRAB_ACTIVE;
    assert(portPadAssistPick(&in, 0.0f, 0.0f)->id == 3);
    in.grabs[1].flags = 0;
    in.grabs[1].dist = 100.0f;
    assert(portPadAssistPick(&in, 0.0f, 0.0f)->id == 3);
    in.grabCount = 2;
    in.grabs[0] = in.grabs[1];
    in.grabs[1] = grabAt(4, 0.1f, 0.1f, 50.0f, PORT_ASSIST_GRAB_RIDING);  // a pod Mario is in: there is the point
    assert(portPadAssistPick(&in, 0.0f, 0.0f)->id == 4);
    in.grabCount = 0;
    assert(portPadAssistPick(&in, 0.0f, 0.0f) == NULL);
}

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
    testIdle();
    testGrabLeadsWithThePointer();
    testGrabRetries();
    testHeldLockSurvivesLostOffer();
    testUnheldLockMovesOn();
    testPick();
    testSling();
    testCursor();
    testBubble();
    testShot();
    testMapping();
    printf("pad_assist: ok\n");
    return 0;
}
