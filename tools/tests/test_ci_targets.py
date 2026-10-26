import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CMAKELISTS = os.path.join(ROOT, "tests", "CMakeLists.txt")
WORKFLOW = os.path.join(ROOT, ".github", "workflows", "host-tests.yml")


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
        with open(WORKFLOW, encoding="utf-8") as handle:
            self.workflow = handle.read()

    def test_every_registered_test_is_built_before_ctest(self):
        # CTest discovers tests but does not build their executables. The prior
        # CI failure was a registered settings persistence test that was never
        # in the explicit target list, so keep both directions of this set
        # comparison exact.
        registered = registered_test_targets(self.cmake)
        built = built_test_targets(self.workflow)
        self.assertTrue(registered)
        self.assertEqual(registered - built, set(), "CI does not build these test targets")
        self.assertEqual(built - registered, set(), "CI builds targets no test registers")
        self.assertIn("ctest --test-dir build-host --output-on-failure", self.workflow)


if __name__ == "__main__":
    unittest.main()
