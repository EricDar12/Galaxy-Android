// The gamepad's stand-ins for pointing the remote (port/pad_assist.h): the grab
// button.  What portPadAssistStep does to the pointer and to A for the Pull
// Stars and pods the game offers.
#include "pad_assist_test_util.h"

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

int main() {
    testIdle();
    testGrabLeadsWithThePointer();
    testGrabRetries();
    testHeldLockSurvivesLostOffer();
    testUnheldLockMovesOn();
    testPick();
    printf("pad_assist: ok\n");
    return 0;
}
