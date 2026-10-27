import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CMAKELISTS = os.path.join(ROOT, "tests", "CMakeLists.txt")
TOP_CMAKELISTS = os.path.join(ROOT, "CMakeLists.txt")
HOST_WORKFLOW = os.path.join(ROOT, ".github", "workflows", "host-tests.yml")
RELEASE_WORKFLOW = os.path.join(ROOT, ".github", "workflows", "release.yml")

# Built for Android only, so the host ctest run cannot execute them and no CI leg
# has a headset attached: release.yml compiles them, and running them is a manual
# step on hardware.  A new target here means a test that nothing runs.
DEVICE_ONLY_TEST_TARGETS = {"vk_inventory_test"}


def registered_test_targets(text):
    return set(re.findall(r"add_test\(\s*NAME\s+\w+\s+COMMAND\s+(\w+)", text))


def declared_test_targets(text):
    return set(re.findall(r"add_executable\(\s*([A-Za-z0-9_]+_test)\b", text))


def built_test_targets(text):
    lines = text.splitlines()
    for index, line in enumerate(lines):
        if "cmake --build build-host --parallel" in line:
            command = line
            while command.rstrip().endswith("\\"):
                index += 1
                command += " " + lines[index].strip()
            return set(re.findall(r"\b([A-Za-z0-9_]+_test)\b", command))
    return set()


class CiTargetCoverageTest(unittest.TestCase):
    def setUp(self):
        with open(CMAKELISTS, encoding="utf-8") as handle:
            self.cmake = handle.read()
        with open(HOST_WORKFLOW, encoding="utf-8") as handle:
            self.host_workflow = handle.read()
        with open(RELEASE_WORKFLOW, encoding="utf-8") as handle:
            self.release_workflow = handle.read()

    def test_umbrella_host_tests_target_defined(self):
        self.assertIn("add_custom_target(host_tests", self.cmake,
                      "tests/CMakeLists.txt must define host_tests umbrella target")

    def test_host_workflow_builds_all_registered_tests(self):
        registered = registered_test_targets(self.cmake)
        built = built_test_targets(self.host_workflow)
        self.assertTrue(registered)
        self.assertEqual(registered - built, set(), "host-tests.yml does not build these test targets")
        self.assertEqual(built - registered, set(), "host-tests.yml builds targets no test registers")
        self.assertIn("host_tests", self.host_workflow, "host-tests.yml must invoke host_tests umbrella target")
        self.assertIn("ctest --test-dir build-host --output-on-failure", self.host_workflow)

    def test_release_workflow_builds_all_registered_tests(self):
        registered = registered_test_targets(self.cmake)
        built = built_test_targets(self.release_workflow)
        self.assertTrue(registered)
        self.assertEqual(registered - built, set(), "release.yml does not build these test targets")
        self.assertEqual(built - registered, set(), "release.yml builds targets no test registers")
        self.assertIn("host_tests", self.release_workflow, "release.yml must invoke host_tests umbrella target")
        self.assertIn("ctest --test-dir build-host --output-on-failure", self.release_workflow)

    def test_no_test_target_is_built_but_never_run(self):
        with open(TOP_CMAKELISTS, encoding="utf-8") as handle:
            top = handle.read()
        declared = declared_test_targets(self.cmake) | declared_test_targets(top)
        registered = registered_test_targets(self.cmake) | registered_test_targets(top)
        self.assertTrue(declared)
        self.assertEqual(declared - registered - DEVICE_ONLY_TEST_TARGETS, set(),
                         "these test targets exist but no ctest registration runs them")
        self.assertTrue(DEVICE_ONLY_TEST_TARGETS <= declared,
                        "DEVICE_ONLY_TEST_TARGETS names a target no CMakeLists builds")

    def test_device_only_tests_are_still_compiled_by_ci(self):
        for target in sorted(DEVICE_ONLY_TEST_TARGETS):
            self.assertIn(target, self.release_workflow,
                          "release.yml does not even build the device-only test " + target)


if __name__ == "__main__":
    unittest.main()

