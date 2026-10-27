"""Archive and download integrity for the installer: no archive is unpacked
before its pinned bytes are verified, and no member may escape its prefix."""
import hashlib
import os
from pathlib import Path
import re
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_installer
import package_mobile_apk


class InstallerArchiveTests(unittest.TestCase):
    def test_zip_extraction_rejects_path_traversal(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            archive = root / 'bad.zip'
            with zipfile.ZipFile(archive, 'w') as source:
                source.writestr('platform-tools/../../escape.txt', 'bad')
            with self.assertRaisesRegex(ValueError, 'Unsafe'):
                build_installer.extract_tools(archive, root / 'assets')
            self.assertFalse((root / 'escape.txt').exists())

    def test_zip_extraction_rejects_absolute_member_paths(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            archive = root / 'absolute.zip'
            with zipfile.ZipFile(archive, 'w') as source:
                source.writestr('/escape.txt', 'bad')
                source.writestr('..\\escape2.txt', 'bad')
            with self.assertRaisesRegex(ValueError, 'Unsafe'):
                build_installer.extract_zip(archive, root / 'dest')
            self.assertFalse((root / 'escape.txt').exists())
            self.assertFalse((root / 'escape2.txt').exists())

    def test_zip_extraction_rejects_symlink_members(self):
        # A symlink writes its target as file *content*, so extraction would
        # happily create a link-shaped decoy; the archive must not need one.
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            archive = root / 'links.zip'
            with zipfile.ZipFile(archive, 'w') as source:
                source.writestr('prefab/keep.txt', 'data')
                info = zipfile.ZipInfo('prefab/link.txt')
                info.external_attr = (0o120777 << 16)
                source.writestr(info, '../../../escape.txt')
            with self.assertRaisesRegex(ValueError, 'symlink'):
                build_installer.extract_zip(archive, root / 'dest')

    def test_zip_extraction_writes_only_the_requested_prefix(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            archive = root / 'mixed.zip'
            with zipfile.ZipFile(archive, 'w') as source:
                source.writestr('platform-tools/adb.exe', 'adb')
                source.writestr('third_party/other.txt', 'other')
                source.writestr('leaked.txt', 'leak')
            build_installer.extract_zip(archive, root / 'dest', 'platform-tools/')
            self.assertEqual((root / 'dest' / 'platform-tools' / 'adb.exe').read_text(), 'adb')
            self.assertFalse((root / 'dest' / 'third_party').exists())
            self.assertFalse((root / 'dest' / 'leaked.txt').exists())

    def test_packager_refuses_an_unbuilt_engine(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(SystemExit, 'REFUSING TO PACKAGE') as refusal:
                package_mobile_apk.require_built_libraries(tmp)
            for lib in package_mobile_apk.REQUIRED_LIBS:
                self.assertIn(lib, str(refusal.exception))
            for lib in package_mobile_apk.REQUIRED_LIBS:
                (Path(tmp) / lib).write_bytes(b'\x7fELF')
            with mock.patch.object(package_mobile_apk, 'find_installer_library',
                                   return_value=None):
                with self.assertRaisesRegex(SystemExit, 'libinstaller.so'):
                    package_mobile_apk.require_built_libraries(tmp)
            with mock.patch.object(package_mobile_apk, 'find_installer_library',
                                   return_value=str(Path(tmp) / 'libinstaller.so')):
                package_mobile_apk.require_built_libraries(tmp)

    def test_openxr_loader_archive_stays_pinned(self):
        script = (Path(__file__).resolve().parents[2] / 'tools' / 'fetch_openxr.sh').read_text(
            encoding='utf-8')
        pinned = dict(re.findall(r'^(VERSION|SIZE|SHA256)=(\S+)$', script, re.MULTILINE))
        self.assertEqual(set(pinned), {'VERSION', 'SIZE', 'SHA256'})
        self.assertEqual(len(pinned['SHA256']), 64)
        self.assertGreater(int(pinned['SIZE']), 1_000_000)
        self.assertIn('openxr_loader_for_android-$VERSION.aar', script)
        self.assertIn('repo1.maven.org', script)
        self.assertNotIn('latest', script)
        # The pinned bytes are verified before anything is unpacked, and the
        # members are resolved one at a time instead of extractall().
        self.assertLess(script.index('verify_download'), script.index('extract_zip'))
        self.assertEqual([line for line in script.splitlines()
                          if 'extractall' in line and not line.lstrip().startswith('#')], [])

    def test_packager_refuses_a_debuggable_release(self):
        # The attribute is read as XML, so the manifest's own comment about
        # android:debuggable="true" does not count as setting it.
        self.assertTrue(package_mobile_apk.manifest_is_debuggable())
        with tempfile.TemporaryDirectory() as tmp:
            clean = Path(tmp) / 'AndroidManifest.xml'
            clean.write_text(
                '<?xml version="1.0" encoding="utf-8"?>\n'
                '<manifest xmlns:android="http://schemas.android.com/apk/res/android">\n'
                '  <!-- android:debuggable="true" is only mentioned here -->\n'
                '  <application android:debuggable="false" />\n'
                '</manifest>\n', encoding='utf-8')
            self.assertFalse(package_mobile_apk.manifest_is_debuggable(clean))
        with mock.patch.dict(os.environ, {}, clear=False):
            os.environ.pop('GALAXY_ALLOW_DEBUGGABLE', None)
            with self.assertRaisesRegex(SystemExit, 'REFUSING TO PACKAGE'):
                package_mobile_apk.require_deliberate_debuggable()
        with mock.patch.dict(os.environ, {'GALAXY_ALLOW_DEBUGGABLE': '1'}):
            package_mobile_apk.require_deliberate_debuggable()

    def test_platform_tools_download_stays_pinned(self):
        for name, (archive, size, sha256) in build_installer.PLATFORM_TOOLS.items():
            self.assertTrue(archive.startswith('platform-tools_r'))
            self.assertTrue(archive.endswith('.zip'))
            self.assertEqual(len(sha256), 64)
            self.assertGreater(size, 1_000_000)
            url = build_installer.DOWNLOADS[name]
            self.assertTrue(url.endswith('/' + archive), url)
            self.assertNotIn('latest', url)

    def test_platform_tools_archive_is_verified_before_extraction(self):
        payload = b'not the pinned platform-tools archive'
        with tempfile.TemporaryDirectory() as tmp:
            archive = Path(tmp) / 'platform-tools.zip'
            archive.write_bytes(payload)
            with self.assertRaisesRegex(RuntimeError, 'is %d bytes, expected' % len(payload)):
                build_installer.verify_download(archive, len(payload) + 1, hashlib.sha256(payload).hexdigest())
            with self.assertRaisesRegex(RuntimeError, 'SHA-256'):
                build_installer.verify_download(archive, len(payload), '0' * 64)
            build_installer.verify_download(archive, len(payload), hashlib.sha256(payload).hexdigest())


if __name__ == '__main__':
    unittest.main()
