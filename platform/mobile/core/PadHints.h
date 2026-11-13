// The mobile app's side of the pad assist (port/pad_assist.h): switching it
// on with the gamepad, and prompting on screen for what its buttons do.
#pragma once

#include "../input/GamepadInput.h"

namespace mobile_core {

// Once per display frame.  The assist is on once a gamepad has been seen;
// when the game offers something a button does (a Pull Star in reach, a pod
// Mario is in, a cannon, a bubble), a toast names the control.
void updatePadAssist(const mobile_input::GamepadInput& gamepad);

}  // namespace mobile_core
