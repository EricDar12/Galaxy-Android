#pragma once

#include <JSystem/JGeometry/TVec.hpp>
#include <revolution/types.h>

class LiveActor;

// The game's side of the port's pad assist (platform/include/port/
// pad_assist.h): with a gamepad there is no pointer, and the things the
// player points the remote at say here, each frame, that they are there to
// be grabbed, stretched, aimed or blown with the sticks and buttons instead.
// None of these does anything unless the port has the assist on.
namespace MR {
    /// @brief Whether the game is played with a gamepad's sticks and buttons alone.
    bool isPadAssist();

    /// @brief Where a point of the world is on screen, in the pointer's space (-1..1).
    /// @return `false` if it is off screen or behind the camera.
    bool calcPadAssistPointerPos(TVec2f* pPos, const TVec3f& rWorldPos);

    /// @brief The grab button can take this actor at its Star Pointer target (a Pull Star, a Sling Pod).
    /// @param flags PAD_ASSIST_GRAB_*.
    void offerPadAssistGrab(const LiveActor* pActor, u32 flags);

    /// @brief The grabbed actor is stretched from `rNeutralPos` by the stick, up to `reach`.
    void offerPadAssistSling(const TVec3f& rNeutralPos, f32 reach);

    /// @brief The stick blows the bubble at `rPos`: the pointer goes from `farDist` to `nearDist` behind it.
    void offerPadAssistBubble(const TVec3f& rPos, f32 nearDist, f32 farDist);

    /// @brief The stick moves the pointer about the screen (aiming a cannon, choosing in a menu).
    /// @param speed How fast, 1 being the cannon's pace.
    void offerPadAssistCursor(f32 speed);

    /// @brief Star bits can be shot: B sends them where Mario faces.
    void offerPadAssistShot();

    /// @brief Whether a star bit at `rPos` is near enough to Mario to fly to him by itself.
    bool isPadAssistCollect(const TVec3f& rPos);
};  // namespace MR

#define PAD_ASSIST_GRAB_ACTIVE 1  // already pulling Mario
#define PAD_ASSIST_GRAB_RIDING 2  // Mario is in it
