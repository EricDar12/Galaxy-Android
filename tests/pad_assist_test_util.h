// Shared by the pad assist tests (test_pad_assist*.cpp).
#pragma once

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "port/controller_mapping.h"
#include "port/pad_assist.h"
#include "port/spin_button.h"

static const uint32_t kA = 0x0800, kB = 0x0400;

static inline PortPadState padWith(uint32_t buttons, float sx = 0.0f, float sy = 0.0f) {
    PortPadState pad;
    memset(&pad, 0, sizeof(pad));
    pad.connected = 1;
    pad.buttons = buttons;
    pad.stickX = sx;
    pad.stickY = sy;
    return pad;
}

static inline PortAssistGrab grabAt(uintptr_t id, float x, float y, float dist, uint32_t flags = 0) {
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

static inline bool near(float a, float b) { return fabsf(a - b) < 0.0001f; }
