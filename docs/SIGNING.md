# Signing Galaxy Android

Android installs an update only when the old and new APKs carry the same
certificate.  A release signed with the wrong key produces
`INSTALL_FAILED_UPDATE_INCOMPATIBLE` and, worse, a key that has leaked lets
anyone else produce a malicious "update" that Android accepts.

## Incident: the former project key is compromised

The former key (`CN=GalaxyAndroid Debug, O=Local, C=US`, alias
`androiddebugkey`) was committed to this public repository together with its
store password.  It has been removed from the current tree and is git-ignored,
but it remains recoverable from history and **must be treated as public**.

| | |
| --- | --- |
| Old certificate SHA-256 | `1883CD600AACD5628513E23CE646F4C8ED19C040A14575FBC922D33F6686FEFF` |
| Status | Compromised. Never sign with it again. |

The fingerprint is public information - Android compares certificates by it - so
publishing it discloses nothing new.  The private key and its password are the
secret, and both are already in history.

`tools/apk_env.py` pins this fingerprint as `COMPROMISED_KEY_SHA256` and
`check_signer()` refuses it on **every** channel.  Removing the file from
`git` is not enough; the key itself has to be abandoned.

For local sideload builds the old key has been replaced by a **tester**
identity kept outside the repository (alias `galaxy-tester`, certificate
`78aed23b...`; see the 2026-10-25 validation record).  That identity is not a
production key and must never be pinned as one.  No production key exists in
this checkout, and the GitHub repository has no signing secrets or variables
configured, so the release workflow still fails deliberately at its pinned
certificate check.  That is the intended fail-closed state, not a regression.

## Production migration checklist

1. The owner creates the production key offline, stores its password in a
   password manager, and keeps both out of the repository and chat logs.
2. Configure the four CI entries in step 3 below and confirm the release
   workflow refuses to build until the pin and alias are non-empty.
3. Confirm that `python package_mobile_apk.py --channel production` selects
   `platform/mobile/AndroidManifest.production.xml` (`debuggable="false"`) and
   reads the compiled flag back from the signed APK; the packager refuses a
   debuggable finished artifact on this channel.
4. Back up app-private data and `/sdcard/GalaxyData`, then perform the single
   uninstall/reinstall needed to move from the tester certificate to the
   production certificate.  Restore and verify the save checksum afterward.
5. Validate startup, 32-bit guest reservation, gameplay and save integrity on
   more than one device/Android release before publishing.  The 2026-10-25
   non-debuggable run covers one RMX3395 only.

## Rotate to a new signing identity

Run these steps on a trusted machine, offline where possible.

1. Create the new key.  Choose a long, unique password and store it in a
   password manager.  Use a new alias (the old builds used
   `androiddebugkey`, but a distinct alias makes rotation visible).

   ```bash
   keytool -genkeypair -keystore release.keystore \
     -alias galaxy-release -keyalg RSA -keysize 4096 -validity 10000 \
     -dname "CN=Galaxy Android Release, O=Galaxy Android, C=US"
   ```

   Use the same password for the store and the key (the default PKCS12
   keystore requires this, and CI supplies only `GALAXY_KEYSTORE_PASS`).

2. Read its fingerprint:

   ```bash
   keytool -list -v -keystore release.keystore -alias galaxy-release
   ```

   Copy the `SHA-256:` value and remove the colons.

3. Configure CI (repository settings, never committed):

   | Name | Kind | Value |
   | --- | --- | --- |
   | `GALAXY_KEYSTORE_B64` | secret | `base64 -w0 release.keystore` |
   | `GALAXY_KEYSTORE_PASS` | secret | the new store password |
   | `GALAXY_KEY_ALIAS` | secret | `galaxy-release` |
   | `GALAXY_EXPECTED_SIGNER_SHA256` | variable | the new fingerprint |

   The release workflow refuses to build or publish while
   `GALAXY_EXPECTED_SIGNER_SHA256` or `GALAXY_KEY_ALIAS` is empty, and the
   packager refuses any certificate that does not match the pin.  It signs with
   the named alias and then reads the finished APK's certificate back.

4. Local builds use the same policy:

   ```bash
   export GALAXY_KEYSTORE=/absolute/path/to/release.keystore
   export GALAXY_KEYSTORE_PASS='...'
   export GALAXY_KEY_ALIAS=galaxy-release
   export GALAXY_EXPECTED_SIGNER_SHA256='<new fingerprint>'
   python package_mobile_apk.py --channel tester
   ```

5. Delete or archive the old keystores (`out/release.keystore`,
   `out/debug.keystore`, `platform/mobile/release.keystore`).  The tools now
   reject them by fingerprint, but keeping them invites a future mistake.

## Installing the rotated build

APKs signed with the old key cannot update APKs signed with the new key.
Existing installations need one uninstall before installing the rotated APK.
Back up anything important first: the app stores saves under
`/sdcard/GalaxyData/saves/`, which is shared storage, but uninstall and
storage changes can still remove data.  Do not accept an "update" from an
unknown source that is signed with the old fingerprint.

## Build channels

1. **Tester channel (`--channel tester` / `GALAXY_RELEASE_CHANNEL=tester`)**
   - Sideload-only pre-release artifacts.
   - Permits `android:debuggable="true"` only with `GALAXY_ALLOW_DEBUGGABLE=1`;
     Android requires a debuggable app to run `lib/<abi>/wrap.sh`, which
     preloads `libreserve.so` to reserve the game's 32-bit address window.
   - A local key is accepted only if it is not the compromised key.  Set
     `GALAXY_EXPECTED_SIGNER_SHA256` to pin it.

2. **Production channel (`--channel production`)**
   - `android:debuggable="true"` is forbidden.
   - `GALAXY_KEYSTORE_PASS` and `GALAXY_EXPECTED_SIGNER_SHA256` are required;
     there is no default password and no fallback to a debug key.
   - `GALAXY_ALLOW_NEW_KEY` is not accepted.
   - The finished APK's certificate is read back and must match the pin.
   - **Not publishable yet.** A non-debuggable app never executes `wrap.sh`,
     so `libreserve.so` cannot claim the guest window before ART; production
     relies on the launcher's post-ART reservation, which aborts on any
     conflict instead of overwriting memory.  That path was validated in a
     non-debuggable build on one RMX3395 on 2026-10-25: twelve cold starts
     (including one after reboot and one with a 128 MB allocation held by
     another process) reported zero conflicts and 7/7 reserved slices and
     reached the title screen.  See `docs/TECHNICAL.md` for the evidence.
     The packager now selects the checked-in
     `platform/mobile/AndroidManifest.production.xml` (`debuggable="false"`)
     and verifies the compiled manifest inside the signed APK.  The owner's
     production signing key and pin and multi-device validation are still
     required.  Do not publish a production APK until those exist.

## What the packager enforces

`package_mobile_apk.py` compiles the tester variant from
`platform/mobile/AndroidManifest.xml` (debuggable) and the production variant
from `platform/mobile/AndroidManifest.production.xml` (non-debuggable).  It
refuses a production source manifest that sets `android:debuggable="true"` and,
after signing, reads the flag back from the manifest compiled into the finished
APK; a debuggable production artifact is refused regardless of environment
overrides.

It resolves a key from, in order,
`GALAXY_KEYSTORE_B64`, `GALAXY_KEYSTORE`, `out/release.keystore`,
`out/debug.keystore`, `platform/mobile/release.keystore`, then:

1. rejects the compromised fingerprint and any certificate that does not match
   `GALAXY_EXPECTED_SIGNER_SHA256`;  the pre-check inspects the certificate of
   `GALAXY_KEY_ALIAS`, the alias that will sign, not just the first key in the
   store;
2. requires `GALAXY_KEYSTORE_PASS` and `GALAXY_EXPECTED_SIGNER_SHA256` for
   production;
3. signs the APK with `GALAXY_KEY_ALIAS`;
4. reads the certificate back with `apksigner verify` and refuses to hand over
   the APK unless it carries exactly one signer matching the same policy.

No channel has a default password.  A missing key is an error unless
`GALAXY_ALLOW_NEW_KEY=1` explicitly asks for a local development key, in which
case `GALAXY_KEYSTORE_PASS` must still be supplied.

The legacy shell packagers (`package_mobile_apk.sh`, `tools/package_apk.sh`)
apply the same policy: they require an explicit password, refuse to create a
key without `GALAXY_ALLOW_NEW_KEY=1`, sign with `GALAXY_KEY_ALIAS`, and verify
the finished APK's certificate.  `installer-mobile/build.py` uses the shared
policy for the installer APK.

`tools/verify_release_apk.py` applies the policy to the artifact that is
actually uploaded, not just the one the packager saw.  Besides the signer it
rejects an APK with more than one signer certificate, a debuggable manifest in
production, missing or duplicate native
entries, a second ABI, empty entries, non-AArch64 libraries, a `wrap.sh` that
is not a LF-only shebang script and a `classes.dex` without the Dalvik magic.
The release workflow runs it once after packaging and again on the downloaded
artifact in the job that publishes it.

## Verifying by hand

```bash
apksigner verify --print-certs out-mobile/GalaxyAndroid.apk
adb install --no-incremental -r out-mobile/GalaxyAndroid.apk
```

The printed SHA-256 must equal `GALAXY_EXPECTED_SIGNER_SHA256` and must not be
the compromised fingerprint.  History has not been rewritten: purging the
old key from Git history would not make it secret again and would break every
existing clone.  Rotation is the remedy.
