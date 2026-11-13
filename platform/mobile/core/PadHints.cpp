#include "PadHints.h"

#include <string>

#include "../ui/UIOverlay.h"
#include "port/controller_mapping.h"
#include "port/pad_hint.h"

namespace mobile_core {

// What the player's pad calls the button an action is on, or `fallback` for
// the buttons whose name depends on the pad (A is not in the same place on
// every one).
static std::string buttonFor(const ControllerMapping& mapping, ControllerAction action, const char* fallback) {
    const char* label = controller_mapping_button_label(controller_mapping_find_button(&mapping, action));
    return label ? label : fallback;
}

void updatePadAssist(const mobile_input::GamepadInput& gamepad) {
    port_pad_assist_set_enabled(gamepad.isGamepadDetected() ? 1 : 0);

    static PortHintPrompter sPrompter;
    const ControllerMapping& mapping = gamepad.getMapping();
    std::string grab = buttonFor(mapping, ACTION_GRAB, "Grab");
    std::string jump = buttonFor(mapping, ACTION_JUMP, "Jump");
    std::string text;
    switch (portPadHintStep(&sPrompter, port_pad_assist_hint())) {
    case PORT_ASSIST_HINT_GRAB:
        text = "Hold " + grab + " to grab the Pull Star";
        break;
    case PORT_ASSIST_HINT_SLING:
        text = "Hold " + grab + ", pull back with the Left Stick, release to launch";
        break;
    case PORT_ASSIST_HINT_CANNON:
        text = "Left Stick aims  -  " + jump + " fires";
        break;
    case PORT_ASSIST_HINT_BUBBLE:
        text = "Hold " + jump + " and push the Left Stick to blow the bubble";
        break;
    default:
        return;
    }
    mobile_ui::UIOverlay::get().showToast(text);
}

}  // namespace mobile_core
