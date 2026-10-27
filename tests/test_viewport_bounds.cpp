// Render-target and viewport sizing must never emit an extent derived from a
// division by a window that has no size yet.
#include <math.h>
#include <stdio.h>

#include <string>

#include "ViewportManager.h"

using mobile_display::Rect;
using mobile_display::ViewportManager;

namespace {

int failures = 0;

void check(const char* name, bool ok, const std::string& detail = "") {
    printf("%-56s %s%s%s\n", name, ok ? "OK" : "FAIL", detail.empty() ? "" : " - ",
           detail.c_str());
    if (!ok) ++failures;
}

std::string dims(int w, int h) {
    return std::to_string(w) + "x" + std::to_string(h);
}

bool isUsableExtent(int w, int h) {
    return w >= 16 && h >= 16 && w <= 8192 && h <= 8192 &&
           std::isfinite((float)w / (float)h);
}

void target(int w, int h, int scale, bool saver, bool fullscreen, int& outW, int& outH) {
    ViewportManager::computeTargetSize(w, h, scale, saver, fullscreen, outW, outH);
}

void testDegenerateWindowsStillYieldAValidTarget() {
    const int sizes[][2] = {{0, 0}, {0, 1080}, {1920, 0}, {-1, -1}, {1920, -1}, {-1, 1080}};
    for (const auto& s : sizes) {
        for (int scale = 0; scale <= 4; ++scale) {
            int w = 0, h = 0;
            target(s[0], s[1], scale, false, true, w, h);
            char name[96];
            snprintf(name, sizeof(name), "window %dx%d scale %d fullscreen is valid", s[0], s[1],
                     scale);
            check(name, isUsableExtent(w, h), dims(w, h));
            snprintf(name, sizeof(name), "window %dx%d scale %d letterbox is valid", s[0], s[1],
                     scale);
            target(s[0], s[1], scale, false, false, w, h);
            check(name, isUsableExtent(w, h), dims(w, h));
        }
    }

    int w = 0, h = 0;
    target(0, 0, 4, true, true, w, h);
    check("battery saver on a sizeless window clamps scale", h == 720 && w == 1280, dims(w, h));
}

void testTargetSizeMatchesTheExpectedLadder() {
    struct Case {
        int screenW, screenH, scale;
        bool saver, fullscreen;
        int expectW, expectH;
    };
    const Case cases[] = {
        {2160, 1080, 0, false, false, 853, 480},
        {2160, 1080, 1, false, true, 1440, 720},
        {1920, 1080, 2, false, true, 1920, 1080},
        {3840, 2160, 2, false, true, 1920, 1080},
        {1080, 2160, 3, false, true, 2880, 1440},
        {3000, 1200, 4, false, true, 3000, 1200},
        {3000, 1200, 4, false, false, 2133, 1200},
        {1920, 1080, 2, true, true, 1280, 720},
    };
    for (const Case& c : cases) {
        int w = 0, h = 0;
        target(c.screenW, c.screenH, c.scale, c.saver, c.fullscreen, w, h);
        char name[96];
        snprintf(name, sizeof(name), "%dx%d scale %d saver %d fullscreen %d", c.screenW, c.screenH,
                 c.scale, (int)c.saver, (int)c.fullscreen);
        check(name, w == c.expectW && h == c.expectH,
              dims(w, h) + " expected " + dims(c.expectW, c.expectH));
    }
}

void testViewportNeverLeavesTheScreen() {
    struct Case {
        int screenW, screenH;
        bool fullscreen;
        Rect expect;
    };
    const Case cases[] = {
        {0, 0, false, {0, 0, 0, 0}},
        {-1, 1080, true, {0, 0, 0, 0}},
        {1920, 1080, true, {0, 0, 1920, 1080}},
        {1920, 1080, false, {0, 0, 1920, 1080}},
        {2560, 1080, false, {320, 0, 1920, 1080}},
        // 1080 / (16/9) rounds to 607.5f and lroundf() breaks the tie away from
        // zero, so the letterbox is 1080x608 centred at y=656.
        {1080, 1920, false, {0, 656, 1080, 608}},
    };
    for (const Case& c : cases) {
        ViewportManager vp;
        vp.update(c.screenW, c.screenH, c.fullscreen);
        char name[96];
        snprintf(name, sizeof(name), "viewport for %dx%d fullscreen %d", c.screenW, c.screenH,
                 (int)c.fullscreen);
        bool fits = vp.w() >= 0 && vp.h() >= 0 && vp.x() >= 0 && vp.y() >= 0 &&
                    vp.x() + vp.w() <= (c.screenW > 0 ? c.screenW : 0) &&
                    vp.y() + vp.h() <= (c.screenH > 0 ? c.screenH : 0);
        check(name, fits && vp.x() == c.expect.x && vp.y() == c.expect.y && vp.w() == c.expect.w &&
                        vp.h() == c.expect.h,
              dims(vp.w(), vp.h()) + " at " + std::to_string(vp.x()) + "," + std::to_string(vp.y()));

        Rect bounds;
        ViewportManager::computePointerBounds(c.screenW, c.screenH, c.fullscreen,
                                              vp.getViewport(), bounds);
        snprintf(name, sizeof(name), "pointer bounds for %dx%d fullscreen %d", c.screenW, c.screenH,
                 (int)c.fullscreen);
        check(name, bounds.w == vp.w() && bounds.h == vp.h());
    }
}

}  // namespace

int main() {
    testDegenerateWindowsStillYieldAValidTarget();
    testTargetSizeMatchesTheExpectedLadder();
    testViewportNeverLeavesTheScreen();

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
