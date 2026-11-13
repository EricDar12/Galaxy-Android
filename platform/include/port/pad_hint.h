// What the pad assist (port/pad_assist.h) could do for the player right now,
// for an on-screen prompt: the game offers a Pull Star, and nothing tells
// someone holding a gamepad that a button grabs it.
#pragma once

#include "port/pad_assist.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_ASSIST_HINT_NONE 0
#define PORT_ASSIST_HINT_GRAB 1    // something in reach the grab button takes (a Pull Star)
#define PORT_ASSIST_HINT_SLING 2   // Mario is in a pod: grab, pull back, let go
#define PORT_ASSIST_HINT_CANNON 3  // the stick aims the cannon
#define PORT_ASSIST_HINT_BUBBLE 4  // the stick blows the bubble
#define PORT_ASSIST_HINT_COUNT 5

// The prompt for the state after a step: nothing while the grab button is
// already doing its job, and nothing for menus (their cursor speaks for itself).
static inline int portPadAssistHint(const PortAssistState* st, const PortAssistFrame* in) {
    if (st->lockId) return PORT_ASSIST_HINT_NONE;
    for (int i = 0; i < in->grabCount; i++) {
        if (in->grabs[i].flags & PORT_ASSIST_GRAB_RIDING) return PORT_ASSIST_HINT_SLING;
    }
    if (in->cursor && in->cursorSpeed == 1.0f) return PORT_ASSIST_HINT_CANNON;
    if (in->bubble) return PORT_ASSIST_HINT_BUBBLE;
    for (int i = 0; i < in->grabCount; i++) {
        if (!(in->grabs[i].flags & PORT_ASSIST_GRAB_ACTIVE)) return PORT_ASSIST_HINT_GRAB;
    }
    return PORT_ASSIST_HINT_NONE;
}

// When to put a prompt on screen, so that it teaches without nagging: once a
// hint has stood for a moment, at most PORT_HINT_SHOWS times each per run of
// the game, and not again within PORT_HINT_QUIET_FRAMES.
#define PORT_HINT_STEADY_FRAMES 20
#define PORT_HINT_SHOWS 3
#define PORT_HINT_QUIET_FRAMES 1800

typedef struct PortHintPrompter {
    int current;  // the hint as of the last frame
    int frames;   // frames it has stood
    int shown[PORT_ASSIST_HINT_COUNT];
    int quiet[PORT_ASSIST_HINT_COUNT];
} PortHintPrompter;

// One display frame with the current hint: the hint to show now, or
// PORT_ASSIST_HINT_NONE.
static inline int portPadHintStep(PortHintPrompter* p, int hint) {
    for (int i = 0; i < PORT_ASSIST_HINT_COUNT; i++) {
        if (p->quiet[i] > 0) p->quiet[i]--;
    }
    if (hint < 0 || hint >= PORT_ASSIST_HINT_COUNT) hint = PORT_ASSIST_HINT_NONE;
    if (hint != p->current) {
        p->current = hint;
        p->frames = 0;
    }
    if (p->frames <= PORT_HINT_STEADY_FRAMES) p->frames++;
    if (hint == PORT_ASSIST_HINT_NONE || p->frames != PORT_HINT_STEADY_FRAMES) return PORT_ASSIST_HINT_NONE;
    if (p->shown[hint] >= PORT_HINT_SHOWS || p->quiet[hint] > 0) return PORT_ASSIST_HINT_NONE;
    p->shown[hint]++;
    p->quiet[hint] = PORT_HINT_QUIET_FRAMES;
    return hint;
}

// The current prompt (any thread).
int port_pad_assist_hint(void);

#ifdef __cplusplus
}
#endif
