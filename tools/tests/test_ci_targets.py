import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CMAKELISTS = os.path.join(ROOT, "tests", "CMakeLists.txt")
HOST_WORKFLOW = os.path.join(ROOT, ".github", "workflows", "host-tests.yml")
RELEASE_WORKFLOW = os.path.join(ROOT, ".github", "workflows", "release.yml")


def registered_test_targets(text):
    return set(re.findall(r"add_test\(\s*NAME\s+\w+\s+COMMAND\s+(\w+)", text))


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


if __name__ == "__main__":
    unittest.main()

