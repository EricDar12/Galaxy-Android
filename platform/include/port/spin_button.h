// A dedicated spin button standing in for shaking the Wii remote.
//
// The input layer sets PORT_BUTTON_SPIN in PortPadState.buttons while the
// spin button is held; wpad.cpp passes it through in KPADStatus.hold (above
// the 16 real button bits, below KPAD_BUTTON_RPT).  WPadHVSwing steps a
// PortSpinButton once per game frame and ORs the result into the remote's
// swing flags, so everything that reads a shake (MR::isPadSwing,
// isCorePadSwing, isCorePadSwingTrigger, Mario's spin) sees the button
// without a fake accelerometer signal.
#pragma once

#include <stdint.h>

#define PORT_BUTTON_SPIN 0x00010000u

// Frames isCorePadSwing reads true after a press, held or not: about as long
// as a real flick of the remote stays over WPadHVSwing's threshold.
#define PORT_SPIN_SWING_FRAMES 6
// While held, isCorePadSwingTrigger repeats like a continuous shake does
// (WPadHVSwing::update): first after this many held frames, then every
// PORT_SPIN_REPEAT_FRAMES.
#define PORT_SPIN_REPEAT_DELAY_FRAMES 30
#define PORT_SPIN_REPEAT_FRAMES 15

typedef struct PortSpinButton {
    int32_t heldFrames;   // consecutive frames held, 0 when released
    int32_t swingFrames;  // frames of the current swing pulse left
} PortSpinButton;

typedef struct PortSpinOutput {
    int swing;    // as WPadHVSwing::mIsSwing
    int trigger;  // as WPadHVSwing::mIsTriggerSwing
} PortSpinOutput;

// One game frame.  A press starts a swing pulse that runs its full length
// even if the button is let go at once, so a tap shorter than a frame still
// spins; holding never extends it, so each spin needs a new press, as each
// needs a new flick on the Wii.
static inline PortSpinOutput portSpinButtonStep(PortSpinButton* s, int held) {
    PortSpinOutput out = {0, 0};
    int pressed = held && s->heldFrames == 0;
    s->heldFrames = held ? s->heldFrames + 1 : 0;
    if (s->heldFrames >= PORT_SPIN_REPEAT_DELAY_FRAMES + PORT_SPIN_REPEAT_FRAMES) {
        s->heldFrames -= PORT_SPIN_REPEAT_FRAMES;  // same repeat phase, bounded however long it is held
    }
    if (pressed) {
        s->swingFrames = PORT_SPIN_SWING_FRAMES;
    }
    if (s->swingFrames > 0) {
        out.swing = 1;
        s->swingFrames--;
    }
    int repeat = s->heldFrames >= PORT_SPIN_REPEAT_DELAY_FRAMES &&
                 (s->heldFrames - PORT_SPIN_REPEAT_DELAY_FRAMES) % PORT_SPIN_REPEAT_FRAMES == 0;
    out.trigger = pressed || repeat;
    return out;
}
