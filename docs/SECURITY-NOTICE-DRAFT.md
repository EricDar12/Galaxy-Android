# DRAFT security notice - signing key rotation

**Status: draft for maintainer review. Do not publish until the rotated release
is ready and the maintainer approves the wording.**

## Summary

The Android signing key used by earlier Galaxy Android builds
(`CN=GalaxyAndroid Debug, O=Local, C=US`, certificate SHA-256
`1883CD600AACD5628513E23CE646F4C8ED19C040A14575FBC922D33F6686FEFF`) was
committed to the public repository together with its password.  Anyone with
access to the repository history can create an APK that Android accepts as an
update to an affected installation.  The key must be considered compromised.

We cannot determine whether the key was misused.  We are not aware of a
malicious build in circulation, but we cannot rule one out.

## What affected users should do

1. Do not install APKs for this app from sources other than the project's
   release page.
2. When the rotated build is published, verify its certificate SHA-256 against
   the value published with that release.  It will **not** be the fingerprint
   above.
3. Installs signed with the old key cannot be updated in place.  Uninstalling
   the old app is required before installing the rotated build.  Back up
   anything important first; saves normally live under
   `/sdcard/GalaxyData/saves/`, but uninstall or storage changes can still
   remove data.
4. If an unexpected "update" was installed, treat the device as untrusted:
   uninstall the app, scan the device, and change any credentials entered into
   the app.

## What we have changed

- The compromised certificate is blocked by fingerprint in the packager, the
  release gate and the legacy packaging scripts.  It cannot sign a new build.
- Production packaging now requires an explicitly pinned signing certificate
  (`GALAXY_EXPECTED_SIGNER_SHA256`) and a non-default store password.
- Release builds require the same pin in CI, so an unpinned or differently
  signed artifact is not published.
- The signing keystore and password are no longer stored in the repository.
  The historical copy cannot be erased from existing clones; rotation, not
  history rewriting, is the security boundary.

## Technical details

- Affected artifact family: APKs signed with the certificate fingerprint above.
- Root cause: the keystore and its password were committed to the repository.
- Remediation: new offline-generated key, new alias, new password, new
  fingerprint published with the release, and a one-time uninstall/reinstall
  for existing users.
- Existing saves are kept in shared storage by design, but the app's private
  data and the old APK are removed by uninstall.

## Publication checklist (for the maintainer)

- [ ] Generate and securely store the replacement key.
- [ ] Set `GALAXY_KEYSTORE_B64`, `GALAXY_KEYSTORE_PASS`,
      `GALAXY_KEY_ALIAS` and `GALAXY_EXPECTED_SIGNER_SHA256`.
- [ ] Build and verify the first rotated release; confirm its fingerprint in
      the release notes.
- [ ] Approve this notice and add the release date and new fingerprint.
- [ ] Publish the notice together with the rotated release.
