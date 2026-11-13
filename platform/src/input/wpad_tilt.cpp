// The Wii remote's tilt for the rides steered by it (Star Ball, ray surfing):
// when the game asks for it (port_input_use_tilt), and the gravity the
// remote's accelerometer then reads.  Used by the KPAD emulation in wpad.cpp.
#include "wpad_tilt.h"

#include <atomic>

#include "port/pad_assist.h"
#include "port/port.h"

static std::atomic<int64_t> sTiltUsedAt{0};
static std::atomic<float> sTiltNeutralPitch{0.0f};

extern "C" void port_input_use_tilt(float neutralPitchDeg) {
    sTiltNeutralPitch.store(neutralPitchDeg);
    sTiltUsedAt.store(port_host_time_ns());
}
extern "C" int port_input_is_tilt_active(void) {
    return (port_host_time_ns() - sTiltUsedAt.load() < 2000000000LL) ? 1 : 0;
}
extern "C" float port_input_get_tilt_neutral_pitch(void) { return sTiltNeutralPitch.load(); }

static float sFilteredTiltX = 0.0f;
static float sFilteredTiltY = 0.0f;

// Gravity as the remote's accelerometer reads it, in g.  KPAD's axes: x left,
// y out of the button face, z where the remote points; held level, (0, -1, 0)
// (KPADReset).  The game turns these into its own (WPadAcceleration::update:
// x = -acc.x, y = acc.z, z = -acc.y).  While the game steers by tilt the
// remote is the right controller pitched up by the neutral angle, so holding
// the controller level gives the grip the game asks for; otherwise the remote
// is held level, and aiming the laser never moves it.
Vec port_wpad_remote_gravity(const PortPadState* pad) {
    const PortPadState& s = *pad;
    Vec g = {0.0f, -1.0f, 0.0f};
    if (port_host_time_ns() - sTiltUsedAt.load() >= 2000000000LL) {  // 2.0 s
        sFilteredTiltX = 0.0f;
        sFilteredTiltY = 0.0f;
        return g;
    }
    // A controller whose orientation is unknown counts as held level.  With
    // the pad assist the sticks steer (the rides read them directly) and the
    // device's own tilt is left out of it: what still reads this gravity,
    // the rides' tutorials, gets the stick as a tilt.
    bool tilted = s.tilted && !port_pad_assist_enabled();
    Vec down = tilted ? Vec{s.downX, s.downY, s.downZ} : Vec{0.0f, -1.0f, 0.0f};
    if (!tilted) {
        // Fallback for mobile touchscreen and gamepads: steer ball and surfing ray using analog stick.
        // Responsive stick-to-tilt mapping:
        float maxDelta = (sTiltNeutralPitch.load() == 0.0f) ? 0.12f : 0.06f;
        float targetX = s.stickX;
        // The ray only rolls; with the pad assist, pushing the stick up or
        // down as well must not read as standing the remote up, which its
        // tutorial counts as a mistake.
        float targetY = (port_pad_assist_enabled() && sTiltNeutralPitch.load() == 0.0f) ? 0.0f : s.stickY;
        float dx = targetX - sFilteredTiltX;
        float dy = targetY - sFilteredTiltY;
        if (dx > maxDelta) sFilteredTiltX += maxDelta;
        else if (dx < -maxDelta) sFilteredTiltX -= maxDelta;
        else sFilteredTiltX = targetX;

        if (dy > maxDelta) sFilteredTiltY += maxDelta;
        else if (dy < -maxDelta) sFilteredTiltY -= maxDelta;
        else sFilteredTiltY = targetY;

        float maxTiltAngle = 1.35f; // ~77 degrees for responsive full deflection in SurfRay and Tamakoro
        float tiltPitch = sFilteredTiltY * maxTiltAngle;
        float tiltRoll = sFilteredTiltX * maxTiltAngle;
        // In Galaxy: accel.x = -pStatus->acc.x = -g.x = down.x.
        // For turn right, game expects accel.x > 0; down.x must be positive for positive stickX.
        down.x = __builtin_sinf(tiltRoll);
        down.z = __builtin_sinf(tiltPitch);
        float rem = 1.0f - (down.x * down.x + down.z * down.z);
        down.y = -(rem > 0.0f ? __builtin_sqrtf(rem) : 0.0f);
    }
    float p = sTiltNeutralPitch.load() * (3.14159265f / 180.0f);
    float c = __builtin_cosf(p), sn = __builtin_sinf(p);
    g.x = -down.x;
    g.y = down.y * c - down.z * sn;
    g.z = down.z * c + down.y * sn;
    return g;
}

float port_wpad_tilt_roll(void) { return sFilteredTiltX; }
