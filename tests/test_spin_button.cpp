#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "port/controller_mapping.h"
#include "port/input_script.h"
#include "port/spin_button.h"

// The spin button stands in for shaking the remote: WPadHVSwing ORs
// portSpinButtonStep into mIsSwing / mIsTriggerSwing once per frame, and
// Mario spins on each rising edge of mIsSwing (MarioActor::updateControllerSwing).

static const uint32_t kKpadButtonMask = 0x0000ffffu;  // revolution/kpad.h
static const uint32_t kKpadButtonRpt = 0x80000000u;

// Runs `frames` frames with the button held for the first `heldFor` of them.
// Counts swing frames, trigger frames and the rising edges Mario spins on.
struct Run {
    int swingFrames = 0, triggers = 0, edges = 0;
    int triggerAt[16] = {};
};

static Run run(PortSpinButton* s, int frames, int heldFor) {
    Run r;
    int lastSwing = 0;
    for (int f = 1; f <= frames; f++) {
        PortSpinOutput o = portSpinButtonStep(s, f <= heldFor);
        r.swingFrames += o.swing;
        if (o.trigger && r.triggers < 16) r.triggerAt[r.triggers] = f;
        r.triggers += o.trigger;
        r.edges += o.swing && !lastSwing;
        lastSwing = o.swing;
    }
    return r;
}

static void testTapSpinsOnce(void) {
    PortSpinButton s = {};
    Run r = run(&s, 40, 1);  // released before the second frame
    assert(r.swingFrames == PORT_SPIN_SWING_FRAMES);
    assert(r.edges == 1);
    assert(r.triggers == 1 && r.triggerAt[0] == 1);
    printf("[PASS] a one-frame tap gives one full swing pulse\n");
}

static void testHoldDoesNotRespin(void) {
    PortSpinButton s = {};
    Run r = run(&s, 100, 100);
    assert(r.swingFrames == PORT_SPIN_SWING_FRAMES);
    assert(r.edges == 1);
    // The swimming spin (isCorePadSwingTrigger) repeats while held, as a
    // continuous shake does: frame 1, then 30, 45, 60, 75, 90.
    const int expected[] = {1, 30, 45, 60, 75, 90};
    assert(r.triggers == 6);
    for (int i = 0; i < 6; i++) assert(r.triggerAt[i] == expected[i]);
    printf("[PASS] holding spins once and repeats only the trigger\n");
}

static void testEachPressSpins(void) {
    PortSpinButton s = {};
    int edges = 0, lastSwing = 0;
    for (int press = 0; press < 5; press++) {
        for (int f = 0; f < 12; f++) {  // 3 frames held, 9 released
            PortSpinOutput o = portSpinButtonStep(&s, f < 3);
            edges += o.swing && !lastSwing;
            lastSwing = o.swing;
        }
    }
    assert(edges == 5);
    printf("[PASS] each press gives its own spin\n");
}

static void testLongHoldStaysBounded(void) {
    PortSpinButton s = {};
    int triggers = 0;
    for (int f = 1; f <= 1000000; f++) {
        PortSpinOutput o = portSpinButtonStep(&s, 1);
        triggers += o.trigger;
        assert(s.heldFrames > 0 && s.heldFrames < PORT_SPIN_REPEAT_DELAY_FRAMES + PORT_SPIN_REPEAT_FRAMES);
    }
    // The press, then one every PORT_SPIN_REPEAT_FRAMES from frame 30 on.
    assert(triggers == 1 + 1 + (1000000 - PORT_SPIN_REPEAT_DELAY_FRAMES) / PORT_SPIN_REPEAT_FRAMES);
    portSpinButtonStep(&s, 0);
    assert(s.heldFrames == 0);
    printf("[PASS] a held button keeps its repeat phase with a bounded counter\n");
}

static void testBitLayout(void) {
    assert((PORT_BUTTON_SPIN & kKpadButtonMask) == 0);  // no real remote / Nunchuk button
    assert((PORT_BUTTON_SPIN & kKpadButtonRpt) == 0);   // not the repeat flag WPadButton reads
    assert(controller_mapping_action_to_wpad(ACTION_SPIN) == PORT_BUTTON_SPIN);
    for (int a = 0; a < ACTION_COUNT; a++) {
        if (a != ACTION_SPIN) assert((controller_mapping_action_to_wpad((ControllerAction)a) & PORT_BUTTON_SPIN) == 0);
    }
    printf("[PASS] the spin bit sits clear of every real button bit\n");
}

static void testScriptItems(void) {
    PortInputEvent ev[4];
    char spec[] = "1:SPIN,2:SHAKE";
    int n = portParseInputScript(spec, ev, 4);
    assert(n == 2);
    PortPadState pad = {};
    portApplyInputScript(ev, n, 1050, &pad);
    assert(pad.buttons & PORT_BUTTON_SPIN);
    assert(pad.accX == 0.0f && pad.accY == 0.0f);
    pad = PortPadState{};
    portApplyInputScript(ev, n, 2050, &pad);
    assert(!(pad.buttons & PORT_BUTTON_SPIN));
    assert(pad.accX != 0.0f);
    printf("[PASS] PETARI_INPUT SPIN presses the button and SHAKE shakes the remote\n");
}

int main(void) {
    testTapSpinsOnce();
    testHoldDoesNotRespin();
    testEachPressSpins();
    testLongHoldStaysBounded();
    testBitLayout();
    testScriptItems();
    printf("All spin button tests passed.\n");
    return 0;
}
