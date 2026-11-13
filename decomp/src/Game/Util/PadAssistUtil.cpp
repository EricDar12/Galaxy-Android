// Port-only (no counterpart in the original game): see PadAssistUtil.hpp.
#include "Game/Util/PadAssistUtil.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Screen/StarPointerTarget.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <revolution/mtx.h>

namespace {
    // More than the radius of the pointer's cursor on screen (StarPointerLayout::getRadius: 15 to 20).
    static const f32 sPointerMarginPixels = 35.0f;

    // Where a point is on screen in the pointer's space, as the Star
    // Pointer's own view of the scene has it (the one its targets are tested
    // in: StarPointerTarget::isPointing).
    bool calcPointerPos(TVec2f* pPos, const TVec3f& rWorldPos) {
        StarPointerTarget target(0.0f, &rWorldPos, nullptr, TVec3f(0.0f, 0.0f, 0.0f));
        TVec2f screenPos(0.0f, 0.0f);
        bool isInScreen = target.calcScreenPosition(&screenPos);
        pPos->x = screenPos.x / (MR::getScreenWidth() * 0.5f) - 1.0f;
        pPos->y = screenPos.y / (MR::getScreenHeight() * 0.5f) - 1.0f;

        TVec3f viewPos;
        PSMTXMultVec(MR::getStarPointerViewMtx(), &rWorldPos, &viewPos);
        return isInScreen && viewPos.z < 0.0f;  // in front of the camera
    }

    // Pointer units per world unit, across and up the screen, at a point.
    void calcScreenScale(f32* pUX, f32* pUY, const TVec3f& rPos, const TVec2f& rScreenPos) {
        TVec2f side, up;
        calcPointerPos(&side, rPos + MR::getCamXdir() * 100.0f);
        calcPointerPos(&up, rPos + MR::getCamYdir() * 100.0f);
        *pUX = MR::abs(side.x - rScreenPos.x) / 100.0f;
        *pUY = MR::abs(up.y - rScreenPos.y) / 100.0f;
    }
};  // namespace

namespace MR {
    bool isPadAssist() {
        return port_pad_assist_enabled() != 0;
    }

    bool calcPadAssistPointerPos(TVec2f* pPos, const TVec3f& rWorldPos) {
        return ::calcPointerPos(pPos, rWorldPos);
    }

    void offerPadAssistGrab(const LiveActor* pActor, u32 flags) {
        if (!isPadAssist() || pActor->mStarPointerTarget == nullptr) {
            return;
        }

        TVec3f targetPos;
        pActor->mStarPointerTarget->calcPosition(&targetPos);

        TVec2f pos, playerPos;
        if (!calcPadAssistPointerPos(&pos, targetPos)) {
            return;
        }
        calcPadAssistPointerPos(&playerPos, *MR::getPlayerCenterPos());

        f32 aspect = static_cast< f32 >(MR::getScreenWidth()) / MR::getScreenHeight();
        port_pad_assist_offer_grab(pActor, pos.x, pos.y, (pos.x - playerPos.x) * aspect, pos.y - playerPos.y,
                                   MR::getPlayerCenterPos()->distance(targetPos), flags);
    }

    void offerPadAssistSling(const TVec3f& rNeutralPos, f32 reach) {
        if (!isPadAssist()) {
            return;
        }

        TVec2f screenPos;
        ::calcPointerPos(&screenPos, rNeutralPos);
        f32 ux, uy;
        ::calcScreenScale(&ux, &uy, rNeutralPos, screenPos);
        port_pad_assist_offer_sling(screenPos.x, screenPos.y, ux, uy, reach);
    }

    void offerPadAssistBubble(const TVec3f& rPos, f32 nearDist, f32 farDist) {
        if (!isPadAssist()) {
            return;
        }

        TVec2f screenPos;
        ::calcPointerPos(&screenPos, rPos);
        f32 ux, uy;
        ::calcScreenScale(&ux, &uy, rPos, screenPos);
        if (ux > 0.0f) {
            // The pointer counts as on the bubble (which does not blow) a
            // cursor's width beyond its edge: stay clear of that too.
            nearDist += ::sPointerMarginPixels / (ux * MR::getScreenWidth() * 0.5f);
            farDist = MR::max(farDist, nearDist + 100.0f);
        }
        port_pad_assist_offer_bubble(screenPos.x, screenPos.y, ux, uy, nearDist, farDist);
    }

    void offerPadAssistCursor() {
        port_pad_assist_offer_cursor();
    }

    void offerPadAssistShot() {
        if (!isPadAssist()) {
            return;
        }

        // A point ahead of Mario at chest height: the shot's goal is what
        // the camera sees through it, the ground or wall beyond.
        TVec3f front;
        MR::getPlayerFrontVec(&front);
        TVec3f aimPos = *MR::getPlayerCenterPos() + front * 700.0f;
        TVec2f screenPos;
        TVec3f viewPos;
        PSMTXMultVec(MR::getStarPointerViewMtx(), &aimPos, &viewPos);
        if (viewPos.z > -100.0f) {
            // At or behind the camera (Mario faces it, close up): at Mario himself.
            ::calcPointerPos(&screenPos, *MR::getPlayerCenterPos());
        } else {
            ::calcPointerPos(&screenPos, aimPos);
        }
        port_pad_assist_offer_shot(MR::clamp(screenPos.x, -0.9f, 0.9f), MR::clamp(screenPos.y, -0.9f, 0.9f));
    }

    bool isPadAssistCollect(const TVec3f& rPos) {
        f32 radius = port_pad_assist_collect_radius();
        return radius > 0.0f && MR::getPlayerCenterPos()->squared(rPos) < radius * radius;
    }
};  // namespace MR
