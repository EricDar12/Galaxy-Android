// Internal to the WPAD emulation: see wpad_tilt.cpp.
#pragma once

#include "port/input.h"
#include "revolution/kpad.h"

// Gravity as the remote's accelerometer reads it, in g (KPAD axes), for the
// controller state `pad`.  Once per KPADRead: it also steps the stick-as-tilt
// filter.
Vec port_wpad_remote_gravity(const PortPadState* pad);
// The filtered sideways tilt (-1..1) the stick stands for, for the pointer's
// horizon.
float port_wpad_tilt_roll(void);
