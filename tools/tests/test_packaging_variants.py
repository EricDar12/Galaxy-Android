"""Packaging channel variants: manifest selection and packaged-manifest policy."""
import os
from pathlib import Path
import sys
import unittest
import xml.etree.ElementTree as ET
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import package_mobile_apk as packager

ANDROID_NS = "{http://schemas.android.com/apk/res/android}"
MOBILE = Path(__file__).resolve().parents[2] / "platform" / "mobile"
TESTER_MANIFEST = MOBILE / "AndroidManifest.xml"
PRODUCTION_MANIFEST = MOBILE / "AndroidManifest.production.xml"
DEBUGGABLE_LINE = "  A: http://schemas.android.com/apk/res/android:debuggable(0x0101000f)=%s\n"


def manifest_root(path):
    return ET.parse(path).getroot()


def normalized(path):
    """The manifest with the channel-specific debug flag removed."""
    root = manifest_root(path)
    root.find("application").attrib.pop(ANDROID_NS + "debuggable", None)
    return ET.tostring(root)


def aapt_result(stdout="", returncode=0, stderr=""):
    return mock.Mock(returncode=returncode, stdout=stdout, stderr=stderr)


class TestManifestVariants(unittest.TestCase):
    def test_tester_manifest_is_debuggable(self):
        self.assertTrue(packager.manifest_is_debuggable(TESTER_MANIFEST))

    def test_production_manifest_is_not_debuggable(self):
        self.assertTrue(PRODUCTION_MANIFEST.is_file())
        self.assertFalse(packager.manifest_is_debuggable(PRODUCTION_MANIFEST))

    def test_variants_differ_only_in_the_debug_flag(self):
        # Package name, version, SDK, permissions, features and activities must
        # not drift between channels; otherwise the production APK is a
        # different application than the one that was validated.
        self.assertEqual(normalized(PRODUCTION_MANIFEST), normalized(TESTER_MANIFEST))

    def test_channel_selects_its_manifest(self):
        self.assertEqual(Path(packager.manifest_for_channel("tester")).resolve(),
                         TESTER_MANIFEST.resolve())
        self.assertEqual(Path(packager.manifest_for_channel("production")).resolve(),
                         PRODUCTION_MANIFEST.resolve())
        with self.assertRaisesRegex(SystemExit, "unknown channel"):
            packager.manifest_for_channel("staging")

    def test_production_accepts_its_manifest_and_refuses_the_tester_one(self):
        with mock.patch.dict(os.environ, {"GALAXY_ALLOW_DEBUGGABLE": "1"}, clear=False):
            packager.require_deliberate_debuggable("production", str(PRODUCTION_MANIFEST))
            with self.assertRaisesRegex(SystemExit, "REFUSING TO PACKAGE"):
                packager.require_deliberate_debuggable("production", str(TESTER_MANIFEST))

    def test_tester_still_requires_debuggable_opt_in(self):
        with mock.patch.dict(os.environ, {}, clear=False):
            os.environ.pop("GALAXY_ALLOW_DEBUGGABLE", None)
            with self.assertRaisesRegex(SystemExit, "GALAXY_ALLOW_DEBUGGABLE"):
                packager.require_deliberate_debuggable("tester", str(TESTER_MANIFEST))
        with mock.patch.dict(os.environ, {"GALAXY_ALLOW_DEBUGGABLE": "1"}):
            packager.require_deliberate_debuggable("tester", str(TESTER_MANIFEST))

    def test_missing_manifest_is_refused(self):
        with self.assertRaisesRegex(SystemExit, "manifest not found"):
            packager.require_deliberate_debuggable("production", str(MOBILE / "missing.xml"))


class TestPackagedManifestPolicy(unittest.TestCase):
    def test_reads_the_compiled_debug_flag(self):
        with mock.patch.object(packager.subprocess, "run",
                               return_value=aapt_result(DEBUGGABLE_LINE % "true")):
            self.assertTrue(packager.packaged_manifest_debuggable("app.apk"))
        with mock.patch.object(packager.subprocess, "run",
                               return_value=aapt_result(DEBUGGABLE_LINE % "false")):
            self.assertFalse(packager.packaged_manifest_debuggable("app.apk"))

    def test_absent_flag_is_not_debuggable(self):
        with mock.patch.object(packager.subprocess, "run",
                               return_value=aapt_result("  A: package=com.example\n")):
            self.assertFalse(packager.packaged_manifest_debuggable("app.apk"))

    def test_unreadable_packaged_manifest_fails_closed(self):
        with mock.patch.object(packager.subprocess, "run",
                               return_value=aapt_result(returncode=1, stderr="bad archive")):
            with self.assertRaisesRegex(SystemExit, "cannot read the packaged manifest"):
                packager.packaged_manifest_debuggable("app.apk")

    def test_production_refuses_a_debuggable_packaged_manifest_even_with_opt_in(self):
        with mock.patch.dict(os.environ, {"GALAXY_ALLOW_DEBUGGABLE": "1"}), \
             mock.patch.object(packager, "packaged_manifest_debuggable", return_value=True):
            with self.assertRaisesRegex(SystemExit, "packaged manifest"):
                packager.require_packaged_manifest_policy("app.apk", "production")

    def test_tester_refuses_a_debuggable_packaged_manifest_without_opt_in(self):
        with mock.patch.dict(os.environ, {}, clear=False), \
             mock.patch.object(packager, "packaged_manifest_debuggable", return_value=True):
            os.environ.pop("GALAXY_ALLOW_DEBUGGABLE", None)
            with self.assertRaisesRegex(SystemExit, "GALAXY_ALLOW_DEBUGGABLE"):
                packager.require_packaged_manifest_policy("app.apk", "tester")

    def test_tester_accepts_a_debuggable_packaged_manifest_with_opt_in(self):
        with mock.patch.dict(os.environ, {"GALAXY_ALLOW_DEBUGGABLE": "1"}), \
             mock.patch.object(packager, "packaged_manifest_debuggable", return_value=True):
            packager.require_packaged_manifest_policy("app.apk", "tester")

    def test_non_debuggable_packaged_manifest_passes_every_channel(self):
        with mock.patch.dict(os.environ, {}, clear=False), \
             mock.patch.object(packager, "packaged_manifest_debuggable", return_value=False):
            os.environ.pop("GALAXY_ALLOW_DEBUGGABLE", None)
            packager.require_packaged_manifest_policy("app.apk", "production")
            packager.require_packaged_manifest_policy("app.apk", "tester")


if __name__ == "__main__":
    unittest.main()
