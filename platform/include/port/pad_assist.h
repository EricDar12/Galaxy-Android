// Controller stand-ins for pointing the Wii remote at things.
//
// With a gamepad there is no pointer, so the mechanics that need one get a
// control of their own.  The game says each frame what is on offer (a Pull
// Star in reach, a Sling Pod being stretched, a cannon being aimed, ...) and
// the wpad emulation turns the sticks and buttons into the pointer position
// and button presses the unmodified game logic expects:
//
//   grab button (PORT_BUTTON_GRAB)  the pointer goes to the best thing on
//       offer and A is held: grabs Pull Stars and Sling Pods.  The left stick
//       picks between several, and stretches a pod once it is held; letting
//       go of the button lets go of A.
//   cannon      the left stick moves the pointer about the screen.
//   bubble      the left stick blows: the pointer sits on the far side of
//               the bubble, nearer the harder the stick is pushed.
//   B           with no pointer on screen, star bits fly where Mario faces.
//
// A real pointer (a finger on the screen) always wins over the last three.
// The game's side is Game/Util/PadAssistUtil; the rules are the pure
// portPadAssistStep below (tests/test_pad_assist.cpp).
#pragma once

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "port/input.h"

#ifdef __cplusplus
extern "C" {
#endif

// In PortPadState.buttons, beside PORT_BUTTON_SPIN; never reaches the game.
#define PORT_BUTTON_GRAB 0x00020000u

// The game reads the pointer a frame late (StarPointerController keeps the
// previous frame's), and wants it on a target before the button goes down:
// a synthesized pointer leads its button by this many frames.
#define PORT_ASSIST_LEAD_FRAMES 3
#define PORT_ASSIST_MAX_GRABS 16
// A grab offer missing for this many frames (and not yet held) is given up.
#define PORT_ASSIST_LOST_FRAMES 5
// Frames after A goes down by which a grabbed thing says it is pulling
// (PORT_ASSIST_GRAB_ACTIVE); one that does not is grabbed again.
#define PORT_ASSIST_RETRY_FRAMES 10

#define PORT_ASSIST_GRAB_ACTIVE 1u  // already pulling Mario
#define PORT_ASSIST_GRAB_RIDING 2u  // Mario is in it (Sling Pod)

// Screen positions are in pointer space: -1..1, right and down positive.
typedef struct PortAssistGrab {
    uintptr_t id;
    float x, y;        // where it is on screen
    float dirX, dirY;  // from Mario to it on screen, aspect-corrected
    float dist;        // from Mario, world units
    uint32_t flags;
} PortAssistGrab;

typedef struct PortAssistFrame {
    int grabCount;
    PortAssistGrab grabs[PORT_ASSIST_MAX_GRABS];
    // A held Sling Pod: its rest position, pointer units per world unit
    // there, and how far (world units) a full stick pulls it.
    int sling;
    float slingX, slingY, slingUX, slingUY, slingReach;
    // A bubble Mario is in: the pointer blows from nearDist (hard) to
    // farDist (gentle) away.
    int bubble;
    float bubbleX, bubbleY, bubbleUX, bubbleUY, bubbleNear, bubbleFar;
    int cursor;  // the stick moves the pointer freely (cannon)
    int shot;    // star bits can be shot: where B aims them
    float shotX, shotY;
} PortAssistFrame;

typedef struct PortAssistState {
    uintptr_t lockId;
    float lockX, lockY;
    int lockFrames;  // frames the pointer has been on the lock
    int lockLost;    // frames the lock has not been offered
    int cursorOn;
    float cursorX, cursorY;
    int shotAge;  // frames since B went down, 0 when idle
    int prevB;
} PortAssistState;

static inline float portAssistClamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static inline void portAssistPoint(PortPadState* pad, float x, float y) {
    pad->pointerValid = 1;
    pad->pointerX = portAssistClamp(x, -0.97f, 0.97f);
    pad->pointerY = portAssistClamp(y, -0.97f, 0.97f);
}

// The offer a new grab takes: the nearest, or with the stick pushed, the
// nearest of those in that direction from Mario.  One that already pulls
// him, or that he has reached, only if there is nothing else.
static inline const PortAssistGrab* portPadAssistPick(const PortAssistFrame* in, float stickX, float stickY) {
    const PortAssistGrab* best = NULL;
    float bestScore = 0.0f;
    float mag = sqrtf(stickX * stickX + stickY * stickY);
    for (int i = 0; i < in->grabCount; i++) {
        const PortAssistGrab* g = &in->grabs[i];
        float score = g->dist;
        float dirLen = sqrtf(g->dirX * g->dirX + g->dirY * g->dirY);
        if (mag > 0.5f && dirLen > 0.0001f) {
            float c = (g->dirX * stickX - g->dirY * stickY) / (dirLen * mag);  // the screen's y runs down
            if (c >= 0.5f) {
                score *= 1.5f - c;
            } else {
                score += 1.0e5f;
            }
        }
        if (g->flags & PORT_ASSIST_GRAB_ACTIVE) score += 1.0e6f;
        if (!(g->flags & PORT_ASSIST_GRAB_RIDING) && g->dist < 300.0f) score += 1.0e6f;
        if (!best || score < bestScore) {
            best = g;
            bestScore = score;
        }
    }
    return best;
}

// One game frame: rewrites the pointer and the A/B buttons of `pad` (the
// controller as the input layer reports it) for what the game offered.
static inline void portPadAssistStep(PortAssistState* st, const PortAssistFrame* in, PortPadState* pad) {
    const uint32_t kA = 0x0800u, kB = 0x0400u;
    int grab = (pad->buttons & PORT_BUTTON_GRAB) != 0;
    int realPointer = pad->pointerValid;
    int bHeld = (pad->buttons & kB) != 0;
    int bPressed = bHeld && !st->prevB;
    float sx = pad->stickX, sy = pad->stickY;
    float mag = sqrtf(sx * sx + sy * sy);
    st->prevB = bHeld;
    pad->buttons &= ~PORT_BUTTON_GRAB;
    if (!in->cursor) st->cursorOn = 0;

    if (!grab) {
        st->lockId = 0;
        st->lockFrames = 0;
        st->lockLost = 0;
    } else {
        int held = st->lockId && st->lockFrames > PORT_ASSIST_LEAD_FRAMES;
        if (held && in->sling) {
            // Stretching a pod: pulled the way the stick is pushed, it flies
            // the other way when the button lets go.
            portAssistPoint(pad, in->slingX + sx * in->slingReach * in->slingUX, in->slingY - sy * in->slingReach * in->slingUY);
            pad->buttons |= kA;
            st->shotAge = 0;
            return;
        }
        const PortAssistGrab* cur = NULL;
        for (int i = 0; st->lockId && i < in->grabCount; i++) {
            if (in->grabs[i].id == st->lockId) cur = &in->grabs[i];
        }
        if (cur) {
            st->lockLost = 0;
            if (held && !(cur->flags & PORT_ASSIST_GRAB_ACTIVE) && st->lockFrames > PORT_ASSIST_LEAD_FRAMES + PORT_ASSIST_RETRY_FRAMES) {
                // A went down and it still is not pulling (the pointer was
                // off it, or it was out of reach): let go of A and press
                // again, for as long as the button is held.
                st->lockFrames = 0;
                held = 0;
            }
        } else if (!held && (!st->lockId || ++st->lockLost > PORT_ASSIST_LOST_FRAMES)) {
            cur = portPadAssistPick(in, sx, sy);
            st->lockId = cur ? cur->id : 0;
            st->lockFrames = 0;
            st->lockLost = 0;
        }
        if (cur) {
            st->lockX = cur->x;
            st->lockY = cur->y;
        }
        if (st->lockId) {
            portAssistPoint(pad, st->lockX, st->lockY);
            if (cur || held) {  // not while waiting to see whether a lost offer comes back
                if (st->lockFrames >= PORT_ASSIST_LEAD_FRAMES) pad->buttons |= kA;
                if (st->lockFrames < 10000) st->lockFrames++;
            }
            st->shotAge = 0;
            return;
        }
    }

    if (in->cursor) {
        if (!st->cursorOn) {
            st->cursorOn = 1;
            st->cursorX = 0.0f;
            st->cursorY = 0.0f;
        }
        if (realPointer) {
            st->cursorX = pad->pointerX;
            st->cursorY = pad->pointerY;
        } else {
            // Slow near the centre of the stick for fine aim (the cannon's
            // view turns with the pointer as well); the screen is wider
            // than tall, so y covers its range faster.
            float m = portAssistClamp(mag, 0.0f, 1.0f);
            float speed = 0.004f + 0.018f * m * m;
            st->cursorX = portAssistClamp(st->cursorX + sx * speed, -0.95f, 0.95f);
            st->cursorY = portAssistClamp(st->cursorY - sy * speed * 1.6f, -0.95f, 0.95f);
            portAssistPoint(pad, st->cursorX, st->cursorY);
        }
        st->shotAge = 0;
        return;
    }

    if (realPointer) {
        st->shotAge = 0;
        return;
    }

    if (in->bubble && mag > 0.25f) {
        float t = portAssistClamp((mag - 0.25f) / 0.75f, 0.0f, 1.0f);
        float r = in->bubbleFar + (in->bubbleNear - in->bubbleFar) * t;
        portAssistPoint(pad, in->bubbleX - sx / mag * r * in->bubbleUX, in->bubbleY + sy / mag * r * in->bubbleUY);
        st->shotAge = 0;
        return;
    }

    if (!in->shot) {
        st->shotAge = 0;
        return;
    }
    if (bPressed) {
        st->shotAge = 1;
    } else if (st->shotAge > 0 && st->shotAge < 1000) {
        st->shotAge++;
    }
    // A press lasts long enough to be seen after the pointer's lead, however
    // short the tap.
    if (st->shotAge > 0 && !bHeld && st->shotAge > PORT_ASSIST_LEAD_FRAMES + 2) st->shotAge = 0;
    if (st->shotAge > 0) {
        portAssistPoint(pad, in->shotX, in->shotY);
        if (st->shotAge <= PORT_ASSIST_LEAD_FRAMES) {
            pad->buttons &= ~kB;
        } else {
            pad->buttons |= kB;
        }
    }
}

// --- the port's instance (platform/src/input/pad_assist.cpp) ---------------

// On while a gamepad is the way the game is played (the mobile app: once a
// gamepad is seen; headless: unless PETARI_ASSIST=0).  Off, nothing here does
// anything and the game plays as the pointer and tilt build.
void port_pad_assist_set_enabled(int enabled);
int port_pad_assist_enabled(void);

// From the game, each frame the thing is there (game thread).
void port_pad_assist_offer_grab(const void* id, float x, float y, float dirX, float dirY, float dist, uint32_t flags);
void port_pad_assist_offer_sling(float x, float y, float ux, float uy, float reach);
void port_pad_assist_offer_bubble(float x, float y, float ux, float uy, float nearDist, float farDist);
void port_pad_assist_offer_cursor(void);
void port_pad_assist_offer_shot(float x, float y);
// Star bits this close to Mario fly to him by themselves (world units; 0:
// they do not).
float port_pad_assist_collect_radius(void);

// From KPADRead, once per read of controller 0 (game thread).
void port_pad_assist_apply(PortPadState* pad);

#ifdef __cplusplus
}
#endif
