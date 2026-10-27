"""One version per release: the manifest is where it comes from.

The settings drawer used to print a hand-written v0.6.0 while the APK was packaged
as 0.0.4a, so a player reporting a version was describing a build that did not exist.
These checks keep the packaged version the only version there is.
"""
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
ANDROID_NS = "{http://schemas.android.com/apk/res/android}"
MOBILE = ROOT / "platform" / "mobile"
MANIFEST = MOBILE / "AndroidManifest.xml"

VERSION_LITERAL = re.compile(r"\bv\d+\.\d+\.\d+[a-z]?\b")


def manifest_version_name():
    return ET.parse(MANIFEST).getroot().attrib[ANDROID_NS + "versionName"]


class TestAppVersion(unittest.TestCase):
    def test_cmake_takes_the_version_from_the_manifest(self):
        text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("PORT_APP_VERSION", text)
        self.assertIn("${PORT_MANIFEST}", text)
        self.assertIn("CMAKE_CONFIGURE_DEPENDS", text,
                      "editing the manifest has to re-run the configure")

    def test_owned_sources_carry_no_version_literal(self):
        offenders = []
        for source in sorted((ROOT / "platform").rglob("*")):
            if source.suffix not in (".cpp", ".h", ".c", ".hpp"):
                continue
            for number, line in enumerate(source.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
                for match in VERSION_LITERAL.finditer(line):
                    offenders.append(f"{source.relative_to(ROOT)}:{number}: {match.group(0)}")
        self.assertEqual(offenders, [],
                         "print PORT_APP_VERSION instead: it is defined from the manifest")

    def test_the_drawer_prints_the_build_time_version(self):
        drawer = (MOBILE / "ui" / "SettingsDrawerChrome.cpp").read_text(encoding="utf-8")
        self.assertIn('PORT_APP_VERSION', drawer)

    def test_android_resources_agree_with_the_manifest(self):
        version = manifest_version_name()
        layout = (MOBILE / "res" / "layout" / "activity_main.xml").read_text(encoding="utf-8")
        strings = (MOBILE / "res" / "values" / "strings.xml").read_text(encoding="utf-8")
        self.assertIn(f"v{version}", layout, "activity_main.xml still shows an old version")
        self.assertIn(f"v{version}", strings, "strings.xml still shows an old version")


if __name__ == "__main__":
    unittest.main()
