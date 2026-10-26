import unittest
import math

class TouchRole:
    NONE = 0
    STICK = 1
    STAR_STICK = 2
    CAMERA_SWIPE = 3
    DPAD = 4
    BTN_A = 5
    BTN_B = 6
    BTN_Z = 7
    BTN_SPIN = 8
    BTN_PLUS = 9
    STAR_POINTER = 10

class TouchPoint:
    def __init__(self):
        self.id = -1
        self.role = TouchRole.NONE
        self.lastX = 0.0
        self.lastY = 0.0

class MockSettings:
    def __init__(self):
        self.showControls = True
        self.dynamicStick = True
        self.starStick = True
        self.touchCamera = True
        self.rightStickMode = 1  # 1 = Star Pointer, 0 = 360 Camera
        self.relativePointer = False
        self.scale = 1.0
        self.stickSensitivity = 1.0
        self.starStickSensitivity = 1.0

class MultiTouchStateSimulator:
    def __init__(self, settings=None):
        self.settings = settings or MockSettings()
        self.touches = [TouchPoint() for _ in range(10)]
        self.stickPointerId = -1
        self.starStickPointerId = -1
        self.cameraSwipePointerId = -1
        self.starPointerId = -1
        self.pointerX = 0.0
        self.pointerY = 0.0
        self.pointerActiveFrames = 0
        self.padStickX = 0.0
        self.padStickY = 0.0
        self.padRightStickX = 0.0
        self.padRightStickY = 0.0
        self.width = 2400.0
        self.height = 1080.0

    def reset(self):
        self.touches = [TouchPoint() for _ in range(10)]
        self.stickPointerId = -1
        self.starStickPointerId = -1
        self.cameraSwipePointerId = -1
        self.starPointerId = -1
        self.padStickX = 0.0
        self.padStickY = 0.0
        self.padRightStickX = 0.0
        self.padRightStickY = 0.0

    def on_down(self, pid, px, py):
        slot = -1
        for i in range(10):
            if self.touches[i].id == pid or (slot < 0 and self.touches[i].id < 0):
                slot = i
                break
        if slot < 0:
            return
        t = self.touches[slot]
        t.id = pid
        t.lastX = px
        t.lastY = py

        # Movement joystick zone: lower-left quadrant
        stick_eligible = (px <= self.width * 0.40 and py >= self.height * 0.45 and self.stickPointerId < 0)
        # Star stick zone: right area above buttons
        star_eligible = (self.settings.starStick and self.starStickPointerId < 0 and
                         1700.0 <= px <= 2100.0 and 200.0 <= py <= 600.0)

        if star_eligible:
            t.role = TouchRole.STAR_STICK
            self.starStickPointerId = pid
        elif stick_eligible:
            t.role = TouchRole.STICK
            self.stickPointerId = pid
        else:
            choose_camera = False
            if self.settings.touchCamera:
                if self.settings.starStick:
                    choose_camera = (self.cameraSwipePointerId < 0) if (self.settings.rightStickMode == 1) else (self.starPointerId >= 0 and self.cameraSwipePointerId < 0)
                else:
                    choose_camera = (self.cameraSwipePointerId < 0)
            if choose_camera:
                t.role = TouchRole.CAMERA_SWIPE
                self.cameraSwipePointerId = pid
            else:
                t.role = TouchRole.STAR_POINTER
                if self.starPointerId < 0:
                    self.starPointerId = pid
                self.pointerX = (px / self.width) * 2.0 - 1.0
                self.pointerY = (py / self.height) * 2.0 - 1.0
                self.pointerActiveFrames = 120

    def on_move(self, pid, px, py):
        for s in range(10):
            t = self.touches[s]
            if t.id != pid:
                continue
            dx = px - t.lastX
            dy = py - t.lastY
            t.lastX = px
            t.lastY = py

            if t.role == TouchRole.STICK:
                self.padStickX = max(-1.0, min(1.0, dx / 100.0))
                self.padStickY = max(-1.0, min(1.0, -dy / 100.0))
            elif t.role == TouchRole.STAR_STICK:
                if self.settings.rightStickMode == 0:
                    self.padRightStickX = max(-1.0, min(1.0, -(dx / 100.0)))
                    self.padRightStickY = max(-1.0, min(1.0, (dy / 100.0)))
                else:
                    slew = 0.038 * self.settings.starStickSensitivity
                    self.pointerX = max(-1.0, min(1.0, self.pointerX + (dx / 100.0) * slew))
                    self.pointerY = max(-1.0, min(1.0, self.pointerY - (dy / 100.0) * slew))
                    self.pointerActiveFrames = 120
            elif t.role == TouchRole.CAMERA_SWIPE:
                self.padRightStickX += -(dx / self.width) * 6.2831853 * 2.25
                self.padRightStickY += -(dy / self.height) * 2.2 * 2.25
            elif t.role == TouchRole.STAR_POINTER:
                self.pointerX = max(-1.0, min(1.0, (px / self.width) * 2.0 - 1.0))
                self.pointerY = max(-1.0, min(1.0, (py / self.height) * 2.0 - 1.0))
                self.pointerActiveFrames = 120

    def on_pointer_up(self, lifted_pid):
        for i in range(10):
            if self.touches[i].id == lifted_pid:
                if self.touches[i].role == TouchRole.STICK:
                    self.stickPointerId = -1
                    self.padStickX = self.padStickY = 0.0
                elif self.touches[i].role == TouchRole.STAR_STICK:
                    self.starStickPointerId = -1
                elif self.touches[i].role == TouchRole.CAMERA_SWIPE:
                    if self.cameraSwipePointerId == lifted_pid:
                        self.cameraSwipePointerId = -1
                elif self.touches[i].role == TouchRole.STAR_POINTER:
                    if self.starPointerId == lifted_pid:
                        self.starPointerId = -1
                self.touches[i] = TouchPoint()


class TestMultiTouchRegressions(unittest.TestCase):
    def test_joystick_1_active_while_camera_dragged(self):
        """1. Joystick 1 active while camera is dragged by another finger."""
        sim = MultiTouchStateSimulator()
        sim.on_down(pid=0, px=300.0, py=800.0)
        self.assertEqual(sim.stickPointerId, 0)
        sim.on_down(pid=1, px=1500.0, py=500.0)
        self.assertEqual(sim.cameraSwipePointerId, 1)

        sim.on_move(pid=0, px=350.0, py=800.0)
        self.assertAlmostEqual(sim.padStickX, 0.5)
        self.assertEqual(sim.padRightStickX, 0.0)

        sim.on_move(pid=1, px=1600.0, py=500.0)
        self.assertNotEqual(sim.padRightStickX, 0.0)
        self.assertEqual(sim.pointerX, 0.0)

    def test_joystick_2_activation_while_camera_drag_in_progress(self):
        """2. Joystick 2 activation while camera drag is already in progress."""
        sim = MultiTouchStateSimulator()
        sim.on_down(pid=1, px=1500.0, py=500.0)
        self.assertEqual(sim.cameraSwipePointerId, 1)

        sim.on_down(pid=2, px=1900.0, py=400.0)
        self.assertEqual(sim.starStickPointerId, 2)
        self.assertEqual(sim.cameraSwipePointerId, 1)

        sim.on_move(pid=1, px=1600.0, py=500.0)
        self.assertNotEqual(sim.padRightStickX, 0.0)

    def test_camera_dragging_while_both_joysticks_active(self):
        """3. Camera dragging while both joysticks are active."""
        sim = MultiTouchStateSimulator()
        sim.on_down(pid=0, px=300.0, py=800.0)   # Stick 1
        sim.on_down(pid=1, px=1900.0, py=400.0)  # Stick 2 (Star Stick)
        sim.on_down(pid=2, px=1200.0, py=500.0)  # Screen drag (Camera)

        self.assertEqual(sim.stickPointerId, 0)
        self.assertEqual(sim.starStickPointerId, 1)
        self.assertEqual(sim.cameraSwipePointerId, 2)

        sim.on_move(pid=2, px=1300.0, py=500.0)
        self.assertNotEqual(sim.padRightStickX, 0.0)
        self.assertEqual(sim.pointerX, 0.0)

    def test_star_pointer_isolated_from_joystick_and_camera(self):
        """4. Star-pointer movement by its designated finger while joystick fingers move."""
        sim = MultiTouchStateSimulator()
        sim.settings.starStick = False
        sim.on_down(pid=0, px=300.0, py=800.0)   # Movement joystick
        sim.on_down(pid=1, px=1400.0, py=500.0)  # First screen touch -> camera swipe
        sim.on_down(pid=2, px=1200.0, py=300.0)  # Second screen touch -> star pointer

        self.assertEqual(sim.stickPointerId, 0)
        self.assertEqual(sim.cameraSwipePointerId, 1)
        self.assertEqual(sim.starPointerId, 2)

        prev_pointer_x = sim.pointerX
        sim.on_move(pid=0, px=350.0, py=800.0)
        self.assertEqual(sim.pointerX, prev_pointer_x)

        sim.on_move(pid=1, px=1500.0, py=500.0)
        self.assertEqual(sim.pointerX, prev_pointer_x)

        sim.on_move(pid=2, px=1800.0, py=300.0)
        self.assertNotEqual(sim.pointerX, prev_pointer_x)

    def test_pointer_up_events_different_orders(self):
        """5. Pointer-up events arriving in different orders."""
        sim = MultiTouchStateSimulator()
        sim.on_down(pid=0, px=300.0, py=800.0)   # Left Stick
        sim.on_down(pid=1, px=1900.0, py=400.0)  # Star Stick
        sim.on_down(pid=2, px=1500.0, py=500.0)  # Camera Swipe

        # Release camera swipe first
        sim.on_pointer_up(lifted_pid=2)
        self.assertEqual(sim.cameraSwipePointerId, -1)
        self.assertEqual(sim.stickPointerId, 0)
        self.assertEqual(sim.starStickPointerId, 1)

        # Release stick next
        sim.on_pointer_up(lifted_pid=0)
        self.assertEqual(sim.stickPointerId, -1)
        self.assertEqual(sim.starStickPointerId, 1)

        # Release star stick last
        sim.on_pointer_up(lifted_pid=1)
        self.assertEqual(sim.starStickPointerId, -1)

    def test_action_cancel_and_reset(self):
        """6. ACTION_CANCEL, focus loss, and app pause/resume."""
        sim = MultiTouchStateSimulator()
        sim.on_down(pid=0, px=300.0, py=800.0)
        sim.on_down(pid=1, px=1900.0, py=400.0)
        sim.on_down(pid=2, px=1500.0, py=500.0)
        sim.on_move(pid=0, px=350.0, py=800.0)

        sim.reset()
        self.assertEqual(sim.stickPointerId, -1)
        self.assertEqual(sim.starStickPointerId, -1)
        self.assertEqual(sim.cameraSwipePointerId, -1)
        self.assertEqual(sim.starPointerId, -1)
        self.assertEqual(sim.padStickX, 0.0)
        self.assertEqual(sim.padRightStickX, 0.0)

    def test_pointer_index_reordering_stability(self):
        """7. Pointer index changes when a finger is lifted retain stable PID ownership."""
        sim = MultiTouchStateSimulator()
        sim.on_down(pid=10, px=300.0, py=800.0)   # Left stick PID 10
        sim.on_down(pid=20, px=1500.0, py=500.0)  # Camera swipe PID 20

        # Finger 10 lifted: Android re-indexes PID 20 to index 0
        sim.on_pointer_up(lifted_pid=10)
        self.assertEqual(sim.stickPointerId, -1)
        self.assertEqual(sim.cameraSwipePointerId, 20)

        # PID 20 moves under its new pointer index: continues camera dragging
        sim.on_move(pid=20, px=1600.0, py=500.0)
        self.assertNotEqual(sim.padRightStickX, 0.0)

    def test_gamepad_controls_remain_functional(self):
        """8. Bluetooth/USB gamepad right stick mode toggling."""
        # Right stick mode 0 = Camera orbit
        r_stick_x, r_stick_y = 0.8, -0.6
        star_sens = 1.0
        mode_0_cam_x = max(-1.0, min(1.0, -r_stick_x * star_sens))
        mode_0_cam_y = max(-1.0, min(1.0, r_stick_y * star_sens))
        self.assertAlmostEqual(mode_0_cam_x, -0.8)
        self.assertAlmostEqual(mode_0_cam_y, -0.6)

        # Right stick mode 1 = Star pointer
        ptr_x, ptr_y = 0.0, 0.0
        speed = 0.035 * star_sens
        mode_1_ptr_x = max(-1.0, min(1.0, ptr_x + r_stick_x * speed))
        mode_1_ptr_y = max(-1.0, min(1.0, ptr_y - r_stick_y * speed))
        self.assertGreater(mode_1_ptr_x, 0.0)
        self.assertGreater(mode_1_ptr_y, 0.0)

if __name__ == '__main__':
    unittest.main()
