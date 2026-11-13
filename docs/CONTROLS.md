---
title: Galaxy Android - Control Schemes and Input Mapping
description: Complete input reference guide for Galaxy Android, detailing touch controls, pointer tracking, physical Bluetooth/USB gamepads, motion gyroscope aiming, and custom layout configuration.
tags:
  - android
  - controls
  - gamepad
  - touch-overlay
  - star-pointer
  - gyroscope
  - input-mapping
  - galaxy-android
---

# Galaxy Android - Controls & Input Guide

> **Acknowledgements & References:**
> Galaxy Android builds upon the Wii controller abstractions reconstructed in the [Petari Decompilation Project](https://github.com/SMGCommunity/Petari) and the custom platform input layer pioneered by [bigmak94/GalaxyQuest](https://github.com/bigmak94/GalaxyQuest). We extend our deepest thanks to both projects and their contributors.

Galaxy Android translates Android touchscreen interactions, physical Bluetooth/USB controller events, and hardware gyroscope motion into native Wii Remote + Nunchuk signals with zero emulation latency.

---

## 1. On-Screen Touch Controls

The on-screen overlay is engineered with a modern frosted translucent dark glass aesthetic with neon tactile micro-accents.

### Touch Layout Overview

| On-Screen Element | Controller Equivalent | In-Game Action |
|---|---|---|
| **Analog Thumbstick** (Left) | Analog Stick | 360° character movement, swimming, rolling |
| **2nd Joystick** (Right, Optional) | IR Pointer / Camera | Dedicated analog Star Pointer aiming and free camera orbit |
| **A Button** (Cyan `#38BDF8`) | Button **A** | Jump, confirm in menus, talk, hold to fast-forward dialogue |
| **SPIN Button** (Emerald `#34D399`) | Motion Shake | Star Spin attack, spin jump, activate Launch Stars |
| **B Button** (Amber `#FBBF24`) | Trigger **B** | Shoot collected Star Bits at enemies / obstacles |
| **Z Button** (Violet `#A78BFA`) | Trigger **Z** | Crouch, ground pound (in air), long jump / backflip (with A) |
| **Camera D-Pad** (Dial) | **D-Pad** | Rotate camera (Left/Right), first-person view (Up), reset view (Down) |
| **Pause Button (+)** | **+** / Start | Pause menu, star checklist, options |
| **Screen Surface Drag** | IR Pointer | 1:1 direct finger aim: gather Star Bits, activate Pull Stars |

---

### Dual Analog Joysticks & Sensitivity Calibration

- **Primary Movement Stick (Left):**
  - **Dynamic Floating Mode (Default):** Tap and drag anywhere on the left third of your screen. The thumbstick anchors dynamically under your finger.
  - **Fixed Mode:** Locks the joystick in a fixed physical position on screen (can be repositioned in the Layout Editor).
  - **Movement Sensitivity:** Configurable in the Settings drawer between 75%, 100%, 125%, and 150% to match small or large phone screens.
  - **Luminous Laser Tether:** A vector laser line connects the joystick center to the thumb puck, providing immediate visual feedback of analog angle and deflection magnitude.

- **Dedicated 2nd Joystick (Right / Star Pointer & Camera):**
  - Enables smooth dual-stick control without requiring finger swipes across the game view.
  - **Freely Repositionable:** Positioned by default above the action buttons; can be dragged to any location on the screen in **Touch Edit Mode**.
  - **Star Stick Sensitivity:** Independent sensitivity multiplier in Settings (50%, 75%, 100%, 150%, and 200%) for high-speed star bit sweeping or precision pull-star targeting.

---

### Star Pointer & Interactive Touch Tracking

- **Direct Touch Gathering:** Sweep your finger over Star Bits floating in space to pull them directly into your inventory.
- **Pull Stars:** Touch and hold Pull Stars directly on screen to grapple toward them.
- **Star Bit Shooting:** Tap the **B** button to fire Star Bits toward the player's facing direction, or tap-and-hold the screen with a second finger to rapid-fire directly at specific targets.

---

### Directional Camera Dial

- Replicates controller D-Pad camera controls:
  - **Left / Right Chevrons (◀ ▶):** Orbit the camera 45° around the player.
  - **Top Chevron (▲):** Switch to first-person perspective.
  - **Bottom Chevron (▼):** Recenter the camera behind the player.
  - **Center Camera Shutter Hub:** Snaps camera orientation directly behind the player.
- Fully toggleable in settings (can be placed on either the left or right side).

---

## 2. Motion & Gyroscope Aiming

Galaxy Android features motion controls powered by Android's `ASensorManager`:

### Star Pointer Gyro Aiming
- **Natural Motion Tracking:** Move and tilt your phone to steer the star pointer across the cosmos. Gather star bits and target pull stars effortlessly.
- **Invert X / Invert Y Controls:** Independent horizontal and vertical direction toggles in Row 19b of the Settings drawer let you configure aiming to match your natural muscle memory.
- **Accelerometer Fallback:** Devices without a dedicated gyroscope sensor automatically switch to accelerometer vector tracking.

### Special Stage Tilting (Star Ball & Surfing Ray)
- **Touch Virtual Tilt (Default):** Touch and slide on the virtual direction ring to steer the Star Ball and Ray with responsive precision.
- **Device Gyroscope (Supported Devices):** Tilt your phone naturally to balance the Star Ball or carve turns on the Surfing Ray.

---

## 3. Physical Gamepads & Native Controller Mapping (Bluetooth & USB)

Galaxy Android automatically detects physical gamepads connected via Bluetooth or USB-C (including Xbox Wireless, PlayStation DualShock 4 / DualSense, Switch Pro Controller, Razer Kishi, and Backbone One).

### Controller Profiles

Galaxy Android includes hardware layout presets accessible through the Settings configuration:

| Physical Button | Standard / Xbox Layout | Switch Pro Layout | GameCube Retro Layout |
|---|---|---|---|
| **South Button** (A / B / ✕) | **Jump / Confirm** (WPAD A) | **Jump / Confirm** (WPAD A) | **Jump / Confirm** (WPAD A) |
| **West Button** (X / Y / □) | **Star Spin Attack** | **Star Spin Attack** | **Star Spin Attack** |
| **East Button** (B / A / ○) | **Shoot Star Bits / Cancel** | **Shoot Star Bits / Cancel** | **Shoot Star Bits / Cancel** |
| **North Button** (Y / X / △) | **Star Spin Attack** | **Pointer Recenter** | **Pointer Recenter** |
| **Left Bumper (L1 / LB)** | **Camera Snap Behind** (WPAD C) | **Camera Snap Behind** (WPAD C) | **Star Spin Attack** |
| **Right Bumper (R1 / RB)** | **Grab** (see below) | **Grab** | **Grab** |
| **Left Trigger (L2 / LT)** | **Crouch / Ground Pound** (WPAD Z) | **Crouch / Ground Pound** (WPAD Z) | **Crouch / Ground Pound** (WPAD Z) |
| **Right Trigger (R2 / RT)** | **Shoot Star Bits** (WPAD B) | **Shoot Star Bits** (WPAD B) | **Shoot Star Bits** (WPAD B) |
| **L3 (Left Stick Click)** | **Crouch / Ground Pound** | **Crouch / Ground Pound** | **Crouch / Ground Pound** |
| **R3 (Right Stick Click)** | **Recenter Star Pointer** | **Recenter Star Pointer** | **Recenter Star Pointer** |
| **D-Pad (Left / Right)** | **Camera Orbit 45°** | **Camera Orbit 45°** | **Camera Orbit 45°** |
| **D-Pad Up** | **First-Person View** | **First-Person View** | **First-Person View** |
| **D-Pad Down** | **Recenter Camera** | **Recenter Camera** | **Recenter Camera** |
| **Start / + / Menu** | **Pause Menu** (WPAD +) | **Pause Menu** (WPAD +) | **Pause Menu** (WPAD +) |

### Playing Without Pointing or Tilting

Once a gamepad is in use, nothing in the game needs the Star Pointer, the touchscreen or the device's motion sensors. Touch still works wherever it did, and a finger on the screen always takes precedence over the stick.

| Mechanic | With a gamepad |
|---|---|
| **Pull Stars** (and the pull star in each Observatory dome) | Hold **Grab** (R1): Mario is pulled to the nearest one. Push the **left stick** towards another before pressing to choose it. Release to let go; press again to move on to the next. |
| **Sling Pods** | Once Mario is stuck to the pod, hold **Grab**, pull the pod back with the **left stick**, and release to launch the other way. |
| **Cannons** | The **left stick** moves the aiming reticle (it stays where you leave it). **Jump** fires, **Shoot / Cancel** climbs out. |
| **Bubbles** | Hold **Jump** and push the **left stick** the way the bubble should go. A harder push blows harder. |
| **Star Ball** | The **left stick** rolls the ball. **Jump** hops. |
| **Manta Ray** | **Left stick** left / right steers, **Jump** accelerates, **Spin** hops. |
| **Collecting Star Bits** | Star Bits near Mario fly to him by themselves. |
| **Shooting Star Bits** | **Shoot** fires where Mario is facing; hold to keep firing. Tap the screen to hit an exact spot. |
| **Menus** (file select, star select, pause menu, galaxy selector, yes / no) | The **left stick** moves the hand cursor, **Jump** confirms. |
| **Camera** | The **right stick** presses the game's own camera controls: left / right turns the camera a step round Mario (hold to keep turning), up is the first-person view, down puts the camera back behind Mario. Where a level fixes the camera, it stays fixed, as on the Wii. The D-pad does the same turns. |

The first few times a Pull Star, Sling Pod, cannon or bubble comes up, a short prompt on screen names the control (for example *Hold R1 to grab the Pull Star*).

While the gamepad is in use the device's tilt sensors are ignored, so holding the device at an angle never steers the Star Ball or the ray.

Not covered from the gamepad: spinning Mario or a bubble by drawing circles with the pointer, and stunning enemies by pointing at them. Both are optional, and both still work by touch.

### Deadzone & Trigger Calibration

- **Analog Stick Deadzone**: Configurable radial deadzone (`controller_deadzone=0.18`, 5% to 40%). Clamps unwanted drift on older physical thumbsticks without deadzone latency.
- **Trigger Actuation Threshold**: Configurable analog threshold (`trigger_threshold=0.40`, 10% to 80%). Controls how far L2 / R2 must be pressed before registering in-game crouch or shoot actions.
- **Trigger / Bumper Swap**: Toggle `swap_triggers_bumpers=1` to switch L1/L2 and R1/R2 on controllers with digital microswitch triggers.
- **Dual-Stick Camera & Star Pointer Modes**:
  - `rightStickMode = 0`: Camera. With a gamepad, the game's own stepped camera turns (see above); with touch controls only, a free orbit around Mario.
  - `rightStickMode = 1`: Direct analog Star Pointer navigation with sensitivity scaling (`starStickSensitivity`).
- **One-Tap Pointer Recenter**: Pressing R3 (Right Stick Click) or the mapped recenter bumper snaps the Star Pointer directly to screen center `(0.0, 0.0)`.

---

## 4. Customizing Controls & Settings Drawer

Open the slide-out **Material 3 Settings Drawer** by tapping the gear icon (`Gear`) at the top-left edge of the screen:

- ** Quick Save Anywhere:** Instantly write your game state, coins, star bits, and comet schedule to persistent storage without opening the pause menu or finishing the stage. Confirmed with an animated HUD toast.
- **Touch Controls Visibility:** Toggle on/off entirely when using a physical gamepad or playing on a dedicated gaming handheld (Retroid Pocket, Odin, Logitech G Cloud).
- **Button Opacity:** Adjust between 25%, 50%, 75%, and 100% transparency.
- **Button Scale:** Scale all touch buttons between 0.75x and 1.5x for phones or tablets.
- **Layout Editor:** Tap **"Edit On-Screen Layout"** to drag any button or joystick cluster to your preferred position. Positions are saved permanently to `settings.ini`.
- **Gyro Aiming & Axis Inversion:** Toggle gyro aiming, Invert X, and Invert Y.
- **Performance HUD Mode:** Switch between Off, FPS Only, and Full Hardware Stats.

---

## References

- [Technical Documentation](TECHNICAL.md) - Architecture of the native port
- [Installation Guide](INSTALLATION.md) - How to install and play
- [Petari Repository](https://github.com/SMGCommunity/Petari) - Decompilation research project
- [GalaxyQuest Repository](https://github.com/bigmak94/GalaxyQuest) - Standalone port architecture
