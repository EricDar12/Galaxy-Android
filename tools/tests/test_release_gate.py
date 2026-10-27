import contextlib
import io
import os
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
from release_fixtures import elf64, make_apk, write_apk  # noqa: E402

from tools.apk_env import (
    COMPROMISED_KEY_SHA256, certificate_fingerprints, check_signer,
)
from tools.verify_release_apk import (
    check_archive, check_contents, check_entries, parse_debuggable,
    tester_debuggable_opt_in as _debuggable_opt_in,
)

NEW_SIGNER = "aa" * 32

GOOD = [
    "AndroidManifest.xml",
    "classes.dex",
    "assets/galaxy_shader_manifest.bin",
    "lib/arm64-v8a/libgame.so",
    "lib/arm64-v8a/libmain.so",
    "lib/arm64-v8a/libreserve.so",
    "lib/arm64-v8a/libinstaller.so",
    "lib/arm64-v8a/wrap.sh",
]


class TestReleaseGate(unittest.TestCase):
    def test_complete_apk_has_no_problems(self):
        self.assertEqual(check_entries(GOOD), [])

    def test_complete_payload_has_no_problems(self):
        with make_apk() as apk:
            self.assertEqual(check_contents(apk), [])

    def test_archive_checks_run_without_the_android_build_tools(self):
        # The desktop installer builders have no SDK, but they still copy this
        # APK into what players run, so the archive alone has to be checkable.
        import tools.verify_release_apk as vr
        with tempfile.TemporaryDirectory() as tmp:
            apk = write_apk(os.path.join(tmp, "GalaxyQuest.apk"))
            with mock.patch.object(vr, "APKSIGNER", ""), mock.patch.object(vr, "AAPT2", ""):
                with contextlib.redirect_stdout(io.StringIO()) as out:
                    self.assertEqual(vr.main(["--structure-only", apk]), 0)
                self.assertIn("structure checks", out.getvalue())
                with contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(vr.main([apk]), 1)

    def test_structure_only_gate_refuses_a_foreign_payload(self):
        import tools.verify_release_apk as vr
        with tempfile.TemporaryDirectory() as tmp:
            apk = write_apk(os.path.join(tmp, "other.apk"),
                            **{"classes.dex": b"PK\x03\x04" + b"\x00" * 64,
                               "lib/arm64-v8a/libgame.so": None})
            with contextlib.redirect_stdout(io.StringIO()) as out:
                self.assertEqual(vr.main(["--structure-only", apk]), 1)
            report = out.getvalue()
            self.assertIn("missing lib/arm64-v8a/libgame.so", report)
            self.assertIn("classes.dex is not a Dalvik executable", report)

    def test_unreadable_apk_is_a_reported_problem_not_a_crash(self):
        with tempfile.TemporaryDirectory() as tmp:
            truncated = os.path.join(tmp, "truncated.apk")
            with open(truncated, "wb") as handle:
                handle.write(b"PK\x03\x04" + b"\x00" * 30)
            self.assertIn("cannot be read as an APK archive", check_archive(truncated)[0])
            self.assertIn("cannot be read as an APK archive",
                          check_archive(os.path.join(tmp, "absent.apk"))[0])

    def test_empty_native_library_is_reported(self):
        with make_apk(**{"lib/arm64-v8a/libgame.so": b""}) as apk:
            self.assertEqual(check_contents(apk), [
                "lib/arm64-v8a/libgame.so is empty",
                "lib/arm64-v8a/libgame.so is not a little-endian 64-bit ELF",
            ])

    def test_foreign_architecture_is_reported(self):
        with make_apk(**{"lib/arm64-v8a/libgame.so": elf64(machine=62)}) as apk:
            self.assertEqual(check_contents(apk), ["lib/arm64-v8a/libgame.so is not an AArch64 library"])

    def test_32bit_elf_is_reported(self):
        truncated = bytearray(elf64())
        truncated[4] = 1
        with make_apk(**{"lib/arm64-v8a/libgame.so": bytes(truncated)}) as apk:
            self.assertEqual(check_contents(apk), [
                "lib/arm64-v8a/libgame.so is not a little-endian 64-bit ELF"])

    def test_crlf_wrap_script_is_reported(self):
        with make_apk(**{"lib/arm64-v8a/wrap.sh": b"#!/system/bin/sh\r\nexec \"$0\"\r\n"}) as apk:
            self.assertEqual(check_contents(apk), ["lib/arm64-v8a/wrap.sh has CRLF line endings"])

    def test_wrap_script_without_shebang_is_reported(self):
        with make_apk(**{"lib/arm64-v8a/wrap.sh": b"exec \"$0\"\n"}) as apk:
            self.assertEqual(check_contents(apk), ["lib/arm64-v8a/wrap.sh is not a shebang script"])

    def test_bad_classes_dex_is_reported(self):
        with make_apk(**{"classes.dex": b"PK\x03\x04" + b"\x00" * 64}) as apk:
            self.assertEqual(check_contents(apk), ["classes.dex is not a Dalvik executable"])

    def test_missing_native_library_is_reported(self):
        names = [n for n in GOOD if not n.endswith("libgame.so")]
        problems = check_entries(names)
        self.assertEqual(problems, ["missing lib/arm64-v8a/libgame.so"])

    def test_duplicate_entry_is_reported(self):
        problems = check_entries(GOOD + ["assets/galaxy_shader_manifest.bin"])
        self.assertEqual(problems, ["assets/galaxy_shader_manifest.bin appears 2 times"])

    def test_unexpected_abi_is_reported(self):
        problems = check_entries(GOOD + ["lib/x86_64/libgame.so"])
        self.assertEqual(problems, ["unexpected ABI directory lib/x86_64"])

    def test_signer_fingerprint_is_normalized(self):
        text = ("Signer #1 certificate SHA-256 digest: "
                "aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:"
                "aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:aa")
        self.assertEqual(certificate_fingerprints(text), [NEW_SIGNER])
        self.assertEqual(certificate_fingerprints("no certificate here"), [])

    def test_every_signer_certificate_is_reported(self):
        text = ("Signer #1 certificate SHA-256 digest: " + NEW_SIGNER + "\n"
                "Signer #2 certificate SHA-256 digest: " + "bb" * 32)
        self.assertEqual(len(certificate_fingerprints(text)), 2)

    def test_compromised_key_is_rejected_on_every_channel(self):
        for channel in ("tester", "production"):
            problem = check_signer(COMPROMISED_KEY_SHA256, channel=channel)
            self.assertIn("repository history", problem or "", channel)

    def test_production_requires_a_pinned_signer(self):
        with mock.patch.dict(os.environ, {}, clear=False):
            os.environ.pop("GALAXY_EXPECTED_SIGNER_SHA256", None)
            self.assertIsNotNone(check_signer(NEW_SIGNER, channel="production"))
            self.assertIsNone(check_signer(NEW_SIGNER, channel="tester"))

    def test_pinned_signer_mismatch_is_rejected(self):
        with mock.patch.dict(os.environ, {"GALAXY_EXPECTED_SIGNER_SHA256": NEW_SIGNER}):
            self.assertIsNone(check_signer(NEW_SIGNER, channel="production"))
            self.assertIsNone(check_signer(NEW_SIGNER, channel="tester"))
            self.assertIsNotNone(check_signer("bb" * 32, channel="tester"))

    def test_tester_debuggable_requires_explicit_opt_in(self):
        with mock.patch.dict(os.environ, {}, clear=False):
            os.environ.pop("GALAXY_ALLOW_DEBUGGABLE", None)
            self.assertFalse(_debuggable_opt_in())
        with mock.patch.dict(os.environ, {"GALAXY_ALLOW_DEBUGGABLE": "1"}):
            self.assertTrue(_debuggable_opt_in())

    def test_debuggable_flag_parsing(self):
        self.assertTrue(parse_debuggable('  A: android:debuggable(0x0101000f)=true\n'))
        self.assertFalse(parse_debuggable('  A: android:debuggable(0x0101000f)=false\n'))
        self.assertIsNone(parse_debuggable("  A: android:label(0x01010001)=Galaxy\n"))

    def test_resolve_keystore_pass_production_rejects_empty(self):
        from tools.apk_env import resolve_keystore_pass
        import os
        old = os.environ.get("GALAXY_KEYSTORE_PASS")
        try:
            if "GALAXY_KEYSTORE_PASS" in os.environ:
                del os.environ["GALAXY_KEYSTORE_PASS"]
            with self.assertRaises(SystemExit) as ctx:
                resolve_keystore_pass(channel="production")
            self.assertIn("GALAXY_KEYSTORE_PASS", str(ctx.exception))
        finally:
            if old is not None:
                os.environ["GALAXY_KEYSTORE_PASS"] = old

    def test_resolve_keystore_pass_production_accepts_secret(self):
        from tools.apk_env import resolve_keystore_pass
        import os
        old = os.environ.get("GALAXY_KEYSTORE_PASS")
        try:
            os.environ["GALAXY_KEYSTORE_PASS"] = "secure_custom_pass"
            self.assertEqual(resolve_keystore_pass(channel="production"), "secure_custom_pass")
        finally:
            if old is not None:
                os.environ["GALAXY_KEYSTORE_PASS"] = old
            else:
                os.environ.pop("GALAXY_KEYSTORE_PASS", None)

    def test_resolve_keystore_pass_tester_requires_secret(self):
        from tools.apk_env import resolve_keystore_pass
        import os
        old = os.environ.get("GALAXY_KEYSTORE_PASS")
        try:
            if "GALAXY_KEYSTORE_PASS" in os.environ:
                del os.environ["GALAXY_KEYSTORE_PASS"]
            with self.assertRaises(SystemExit) as ctx:
                resolve_keystore_pass(channel="tester")
            self.assertIn("GALAXY_KEYSTORE_PASS", str(ctx.exception))
        finally:
            if old is not None:
                os.environ["GALAXY_KEYSTORE_PASS"] = old

    def test_base64_keystore_is_private_and_removed(self):
        import base64
        import tempfile
        import tools.apk_env as env
        with tempfile.TemporaryDirectory() as tmp, \
             mock.patch.dict(os.environ, {
                 "GALAXY_KEYSTORE_B64": base64.b64encode(b"k" * 256).decode(),
                 "GALAXY_KEYSTORE_PASS": "secret"}, clear=False), \
             mock.patch.object(env, "ROOT", tmp), \
             mock.patch.object(env, "keystore_fingerprint", return_value=NEW_SIGNER):
            path = env.prepare_signing_key(channel="tester")
            self.assertTrue(os.path.isfile(path))
            self.assertEqual(os.path.dirname(path), os.path.join(tmp, "out"))
            self.assertNotEqual(path, os.path.join(tmp, "out", "release.keystore"))
            env.cleanup_extracted_keystore()
            self.assertFalse(os.path.exists(path))
            self.assertEqual(env.EXTRACTED_KEYSTORE, "")

    def test_production_mode_rejects_debuggable_even_with_allow_var(self):
        import tools.verify_release_apk as vr
        with mock.patch.dict(os.environ, {"GALAXY_EXPECTED_SIGNER_SHA256": NEW_SIGNER}), \
             mock.patch.object(vr, "run") as mock_run, mock.patch("zipfile.ZipFile"):
            mock_run.side_effect = [
                mock.Mock(returncode=0, stdout="certificate SHA-256 digest: " + NEW_SIGNER, stderr=""),
                mock.Mock(returncode=0, stdout='  A: android:debuggable(0x0101000f)=true\n', stderr=""),
                mock.Mock(returncode=0, stdout="", stderr=""),
            ]
            with mock.patch.object(vr, "check_entries", return_value=[]), \
                 mock.patch.object(vr, "check_contents", return_value=[]):
                problems = vr.inspect("mock.apk", channel="production", allow_debuggable=True)
                self.assertTrue(any("forbidden in production" in p for p in problems))

    def test_tester_mode_permits_debuggable_with_optin(self):
        import tools.verify_release_apk as vr
        with mock.patch.dict(os.environ, {"GALAXY_EXPECTED_SIGNER_SHA256": NEW_SIGNER}), \
             mock.patch.object(vr, "run") as mock_run, mock.patch("zipfile.ZipFile"):
            mock_run.side_effect = [
                mock.Mock(returncode=0, stdout="certificate SHA-256 digest: " + NEW_SIGNER, stderr=""),
                mock.Mock(returncode=0, stdout='  A: android:debuggable(0x0101000f)=true\n', stderr=""),
                mock.Mock(returncode=0, stdout="", stderr=""),
            ]
            with mock.patch.object(vr, "check_entries", return_value=[]), \
                 mock.patch.object(vr, "check_contents", return_value=[]):
                problems = vr.inspect("mock.apk", channel="tester", allow_debuggable=True)
                self.assertEqual(problems, [])

    def test_release_gate_rejects_compromised_signer_and_unpinned_production(self):
        import tools.verify_release_apk as vr
        for channel, fingerprint, pin in (
                ("tester", COMPROMISED_KEY_SHA256, NEW_SIGNER),
                ("production", NEW_SIGNER, None)):
            env = {"GALAXY_EXPECTED_SIGNER_SHA256": pin} if pin else {}
            with mock.patch.dict(os.environ, env, clear=False), \
                 mock.patch.object(vr, "run") as mock_run, mock.patch("zipfile.ZipFile"):
                if not pin:
                    os.environ.pop("GALAXY_EXPECTED_SIGNER_SHA256", None)
                mock_run.side_effect = [
                    mock.Mock(returncode=0, stdout="certificate SHA-256 digest: " + fingerprint, stderr=""),
                    mock.Mock(returncode=0, stdout="", stderr=""),
                    mock.Mock(returncode=0, stdout="", stderr=""),
                ]
                with mock.patch.object(vr, "check_entries", return_value=[]), \
                     mock.patch.object(vr, "check_contents", return_value=[]):
                    problems = vr.inspect("mock.apk", channel=channel, allow_debuggable=False)
                self.assertTrue(problems, channel)


if __name__ == "__main__":
    unittest.main()
