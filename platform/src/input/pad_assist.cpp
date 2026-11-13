// The port's controller pointer assist: what the game offered lately, and the
// state of the rules in port/pad_assist.h.
#include "port/pad_assist.h"

#include <stdio.h>
#include <stdlib.h>

#include <atomic>

namespace {

// Offers outlive the frame they were made in by a read or two, so a frame
// in which the game reads the controller without moving its actors (or the
// other way round) does not drop what is held.
const int kOfferReads = 3;

std::atomic<int> sEnabled{0};
PortAssistState sState;
PortAssistFrame sFrame;
int sGrabAge[PORT_ASSIST_MAX_GRABS];
int sSlingAge, sBubbleAge, sCursorAge, sShotAge;

void ageOffers() {
    int kept = 0;
    for (int i = 0; i < sFrame.grabCount; i++) {
        if (++sGrabAge[i] >= kOfferReads) continue;
        sFrame.grabs[kept] = sFrame.grabs[i];
        sGrabAge[kept] = sGrabAge[i];
        kept++;
    }
    sFrame.grabCount = kept;
    if (sFrame.sling && ++sSlingAge >= kOfferReads) sFrame.sling = 0;
    if (sFrame.bubble && ++sBubbleAge >= kOfferReads) sFrame.bubble = 0;
    if (sFrame.cursor && ++sCursorAge >= kOfferReads) sFrame.cursor = 0;
    if (sFrame.shot && ++sShotAge >= kOfferReads) sFrame.shot = 0;
}

}  // namespace

extern "C" {

void port_pad_assist_set_enabled(int enabled) { sEnabled.store(enabled ? 1 : 0); }
int port_pad_assist_enabled(void) { return sEnabled.load(); }

void port_pad_assist_offer_grab(const void* id, float x, float y, float dirX, float dirY, float dist, uint32_t flags) {
    if (!sEnabled.load() || !id) return;
    int slot = -1;
    for (int i = 0; i < sFrame.grabCount; i++) {
        if (sFrame.grabs[i].id == (uintptr_t)id) slot = i;
    }
    if (slot < 0) {
        if (sFrame.grabCount >= PORT_ASSIST_MAX_GRABS) return;
        slot = sFrame.grabCount++;
    }
    PortAssistGrab& g = sFrame.grabs[slot];
    g.id = (uintptr_t)id;
    g.x = x;
    g.y = y;
    g.dirX = dirX;
    g.dirY = dirY;
    g.dist = dist;
    g.flags = flags;
    sGrabAge[slot] = 0;
}

void port_pad_assist_offer_sling(float x, float y, float ux, float uy, float reach) {
    if (!sEnabled.load()) return;
    sFrame.sling = 1;
    sFrame.slingX = x;
    sFrame.slingY = y;
    sFrame.slingUX = ux;
    sFrame.slingUY = uy;
    sFrame.slingReach = reach;
    sSlingAge = 0;
}

void port_pad_assist_offer_bubble(float x, float y, float ux, float uy, float nearDist, float farDist) {
    if (!sEnabled.load()) return;
    sFrame.bubble = 1;
    sFrame.bubbleX = x;
    sFrame.bubbleY = y;
    sFrame.bubbleUX = ux;
    sFrame.bubbleUY = uy;
    sFrame.bubbleNear = nearDist;
    sFrame.bubbleFar = farDist;
    sBubbleAge = 0;
}

void port_pad_assist_offer_cursor(void) {
    if (!sEnabled.load()) return;
    sFrame.cursor = 1;
    sCursorAge = 0;
}

void port_pad_assist_offer_shot(float x, float y) {
    if (!sEnabled.load()) return;
    sFrame.shot = 1;
    sFrame.shotX = x;
    sFrame.shotY = y;
    sShotAge = 0;
}

float port_pad_assist_collect_radius(void) { return sEnabled.load() ? 900.0f : 0.0f; }

void port_pad_assist_apply(PortPadState* pad) {
    if (!sEnabled.load()) {
        pad->buttons &= ~PORT_BUTTON_GRAB;
        memset(&sFrame, 0, sizeof(sFrame));
        memset(&sState, 0, sizeof(sState));
        return;
    }
    PortPadState before = *pad;
    portPadAssistStep(&sState, &sFrame, pad);
    // PETARI_ASSISTLOG=1: a line whenever what the assist does changes.
    static const int sLog = getenv("PETARI_ASSISTLOG") ? atoi(getenv("PETARI_ASSISTLOG")) : 0;
    if (sLog) {
        static uintptr_t sLastLock;
        static uint32_t sLastButtons;
        static int sLastValid, sLastOffers, sFrames;
        int offers = sFrame.grabCount | sFrame.sling << 8 | sFrame.bubble << 9 | sFrame.cursor << 10 | sFrame.shot << 11;
        uint32_t pressed = (pad->buttons ^ before.buttons) | (before.buttons & PORT_BUTTON_GRAB);
        bool changed = sState.lockId != sLastLock || pressed != sLastButtons || pad->pointerValid != sLastValid || offers != sLastOffers;
        if (changed || (pad->pointerValid && !before.pointerValid && ++sFrames % 30 == 0)) {
            fprintf(stderr, "assist: grabs %d sling %d bubble %d cursor %d shot %d | lock %lx frames %d | pointer %d (%.3f, %.3f) | buttons %05x -> %05x\n",
                    sFrame.grabCount, sFrame.sling, sFrame.bubble, sFrame.cursor, sFrame.shot, (unsigned long)sState.lockId, sState.lockFrames,
                    pad->pointerValid, pad->pointerX, pad->pointerY, before.buttons, pad->buttons);
        }
        sLastLock = sState.lockId;
        sLastButtons = pressed;
        sLastValid = pad->pointerValid;
        sLastOffers = offers;
    }
    ageOffers();
}

}  // extern "C"
