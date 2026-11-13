// The right stick as the game's own camera buttons.
//
// The game turns its camera in steps, and only where the level allows it:
// D-pad left and right go round Mario, D-pad up is the first-person view,
// and the Nunchuk's C puts the camera back behind him.  With the pad assist
// (port/pad_assist.h) the right stick presses those buttons, so it can never
// take the camera anywhere the game would not: no free orbit through floors
// or away from a level's set angles.
//
//   left / right   a turn, repeated while the stick is held over
//   up             first-person view (once per push)
//   down           camera back behind Mario (once per push)
#pragma once

#include <math.h>
#include <stdint.h>

#include "port/input.h"

// A turn takes about this long to play out; holding the stick asks for the
// next one as the last finishes.
#define PORT_CAMERA_REPEAT_FRAMES 24
// Sideways, a push well off centre; up and down need a firmer one, and the
// stick has to be nearer that axis than the other, so that turning never
// drops into the first-person view by accident.
#define PORT_CAMERA_TURN_PUSH 0.55f
#define PORT_CAMERA_VIEW_PUSH 0.85f

typedef struct PortCameraStick {
    uint32_t held;  // the D-pad bit the stick stands for now, 0 at rest
    int frames;     // frames it has
} PortCameraStick;

// One game frame: ORs the D-pad bit the right stick stands for into
// pad->buttons.  PortPadState's right stick is (left, up) positive, as the
// input layer fills it in.
static inline void portPadCameraStep(PortCameraStick* st, PortPadState* pad) {
    const uint32_t kLeft = 0x0001u, kRight = 0x0002u, kBehind = 0x4000u, kUp = 0x0008u;
    float right = -pad->rightStickX, up = pad->rightStickY;
    uint32_t want = 0;
    if (fabsf(right) >= PORT_CAMERA_TURN_PUSH && fabsf(right) >= fabsf(up)) {
        want = right > 0.0f ? kRight : kLeft;
    } else if (fabsf(up) >= PORT_CAMERA_VIEW_PUSH) {
        want = up > 0.0f ? kUp : kBehind;
    }
    if (want != st->held) {
        st->held = want;
        st->frames = 0;
    }
    if (!want) return;
    // The game acts on the press, not the hold: for a repeated turn the
    // button comes up for two frames before each new one.
    int turning = want == kLeft || want == kRight;
    int phase = st->frames % PORT_CAMERA_REPEAT_FRAMES;
    if (!turning || phase < PORT_CAMERA_REPEAT_FRAMES - 2) pad->buttons |= want;
    if (turning || st->frames < PORT_CAMERA_REPEAT_FRAMES) st->frames++;
}
