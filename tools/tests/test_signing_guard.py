import base64
import os
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from tools.apk_env import (
    COMPROMISED_KEY_SHA256, keystore_fingerprint, prepare_signing_key,
    signing_alias, verify_signed_apk,
)

NEW_SIGNER = "aa" * 32
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


class TestSigningGuard(unittest.TestCase):
    def test_malformed_base64_is_rejected_before_any_key_is_written(self):
        encoded = base64.b64encode(b"k" * 256).decode() + "!!!!"
        with tempfile.TemporaryDirectory() as tmp, \
             mock.patch.dict(os.environ, {
                 "GALAXY_KEYSTORE_B64": encoded,
                 "GALAXY_KEYSTORE_PASS": "secret"}, clear=False), \
             mock.patch("tools.apk_env.ROOT", tmp):
            with self.assertRaises(SystemExit) as ctx:
                prepare_signing_key(channel="tester")
            self.assertIn("base64", str(ctx.exception))
            self.assertFalse(os.path.exists(os.path.join(tmp, "out")))

    def test_production_requires_explicit_key_alias(self):
        with mock.patch.dict(os.environ, {"GALAXY_KEYSTORE_PASS": "secret"}, clear=False):
            os.environ.pop("GALAXY_KEY_ALIAS", None)
            with self.assertRaises(SystemExit) as ctx:
                prepare_signing_key(channel="production")
            self.assertIn("GALAXY_KEY_ALIAS", str(ctx.exception))

    def test_production_rejects_empty_or_whitespace_alias(self):
        for alias in ("", "   ", "\t"):
            with mock.patch.dict(os.environ, {"GALAXY_KEY_ALIAS": alias}, clear=False):
                with self.assertRaises(SystemExit) as ctx:
                    signing_alias("production")
                self.assertIn("GALAXY_KEY_ALIAS", str(ctx.exception))

    def test_production_rejects_absent_pinned_fingerprint(self):
        result = mock.Mock(returncode=0, stderr="",
                           stdout="certificate SHA-256 digest: " + NEW_SIGNER)
        with mock.patch.dict(os.environ, {}, clear=False), \
             mock.patch("tools.apk_env.subprocess.run", return_value=result):
            os.environ.pop("GALAXY_EXPECTED_SIGNER_SHA256", None)
            with self.assertRaises(SystemExit) as ctx:
                verify_signed_apk("mock.apk", channel="production")
            self.assertIn("GALAXY_EXPECTED_SIGNER_SHA256", str(ctx.exception))

    def test_unsigned_or_certificate_less_apk_is_rejected(self):
        for returncode, stdout in ((1, ""), (0, "no certificates found")):
            result = mock.Mock(returncode=returncode, stderr="", stdout=stdout)
            with mock.patch.dict(os.environ, {"GALAXY_EXPECTED_SIGNER_SHA256": NEW_SIGNER}), \
                 mock.patch("tools.apk_env.subprocess.run", return_value=result):
                with self.assertRaises(SystemExit) as ctx:
                    verify_signed_apk("mock.apk", channel="tester")
                self.assertIn("REFUSING TO RELEASE", str(ctx.exception))

    def test_verify_accepts_single_pinned_signer(self):
        result = mock.Mock(returncode=0, stderr="",
                           stdout="certificate SHA-256 digest: " + NEW_SIGNER)
        with mock.patch.dict(os.environ, {"GALAXY_EXPECTED_SIGNER_SHA256": NEW_SIGNER}), \
             mock.patch("tools.apk_env.subprocess.run", return_value=result):
            self.assertEqual(verify_signed_apk("mock.apk", channel="production"), NEW_SIGNER)

    def test_keytool_checks_the_selected_alias(self):
        result = mock.Mock(stdout="", stderr="", returncode=1)
        with mock.patch.dict(os.environ, {
                "GALAXY_KEYSTORE_PASS": "secret",
                "GALAXY_KEY_ALIAS": "rotated-release"}, clear=False), \
             mock.patch("tools.apk_env.subprocess.run", return_value=result) as run:
            self.assertIsNone(keystore_fingerprint("store.keystore"))
            cmd = run.call_args.args[0]
            self.assertEqual(signing_alias("tester"), "rotated-release")
            self.assertEqual(cmd[cmd.index("-alias") + 1], "rotated-release")

    def test_verify_signed_apk_rejects_compromised_or_mismatched_certificate(self):
        for fingerprint in (COMPROMISED_KEY_SHA256, "bb" * 32):
            result = mock.Mock(returncode=0, stderr="",
                               stdout="certificate SHA-256 digest: " + fingerprint)
            with mock.patch.dict(os.environ, {"GALAXY_EXPECTED_SIGNER_SHA256": NEW_SIGNER}), \
                 mock.patch("tools.apk_env.subprocess.run", return_value=result):
                with self.assertRaises(SystemExit) as ctx:
                    verify_signed_apk("mock.apk", channel="tester")
                self.assertIn("REFUSING TO RELEASE", str(ctx.exception))

    def test_multiple_signers_are_rejected_even_when_one_matches(self):
        result = mock.Mock(returncode=0, stderr="", stdout=(
            "Signer #1 certificate SHA-256 digest: " + NEW_SIGNER + "\n"
            "Signer #2 certificate SHA-256 digest: " + "cc" * 32 + "\n"))
        with mock.patch.dict(os.environ, {"GALAXY_EXPECTED_SIGNER_SHA256": NEW_SIGNER}), \
             mock.patch("tools.apk_env.subprocess.run", return_value=result):
            with self.assertRaises(SystemExit) as ctx:
                verify_signed_apk("mock.apk", channel="tester")
            self.assertIn("one signer", str(ctx.exception))

    @unittest.skipIf(os.name == "nt", "POSIX signal cleanup")
    def test_sigterm_removes_extracted_keystore(self):
        with tempfile.TemporaryDirectory() as tmp:
            script = (
                "import base64, os, signal\n"
                "import tools.apk_env as env\n"
                f"env.ROOT = {tmp!r}\n"
                "env.keystore_fingerprint = lambda *a, **k: 'aa' * 32\n"
                "os.environ['GALAXY_KEYSTORE_B64'] = base64.b64encode(b'k' * 256).decode()\n"
                "os.environ['GALAXY_KEYSTORE_PASS'] = 'secret'\n"
                "os.environ['GALAXY_EXPECTED_SIGNER_SHA256'] = 'aa' * 32\n"
                "print(env.prepare_signing_key('tester'), flush=True)\n"
                "os.kill(os.getpid(), signal.SIGTERM)\n")
            proc = subprocess.run([sys.executable, "-c", script], cwd=ROOT,
                                  capture_output=True, text=True)
            self.assertEqual(proc.returncode, 143, proc.stderr)
            self.assertFalse(os.path.exists(proc.stdout.strip().splitlines()[-1]))


if __name__ == "__main__":
    unittest.main()
