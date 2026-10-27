# Source Audit — Galaxy Android

Date: 2026-10-16 · Re-verified and extended 2026-10-27 · Scope: whole repository (hand-written port + tooling)

## Scope and method

The repository has two very different kinds of code:

- `decomp/` (~2,300 C/C++ files) — reconstructed Super Mario Galaxy game code and
  SDK sources, carried over from the SMGCommunity/Petari and GalaxyQuest
  projects. This audit treats it as third-party derived and reviews it only where
  the port layer substitutes or wraps it.
- Everything else is this project's own code and was reviewed line by line:
  `platform/` (native port layer, Android launcher shims, mobile UI, renderers),
  `installer-mobile/` (JNI disc extractor, AES/Yaz0, embedded-Python bridge,
  cook scripts), `tools/` (cook pipeline, desktop installer, tests), the APK
  packaging scripts, and the CI workflows.

Checks performed: manual review of the files above, pattern scans for unsafe
string/memory functions, secrets and keystores, untracked-vs-committed file
comparison, and execution of the existing test suite.

Legend: **Critical** / **High** / **Medium** / **Low** / **Info**.

## Status of the findings below — re-verified 2026-10-27

Each row was checked against the current tree, not against the 2026-10-16 text.

| Finding | Status | Where it stands |
| --- | --- | --- |
| High — keystore committed | Fixed in the working tree; **key still in history** | `platform/mobile/release.keystore` is no longer tracked and `*.keystore` is ignored (.gitignore:44). The passphrase is read from the environment (`--ks-pass env:GALAXY_KEYSTORE_PASS`, package_mobile_apk.py:285) and an empty one is refused for both channels (tools/tests/test_release_gate.py). The old commits still contain the private key, so signing-key rotation and history rewriting remain the owner's actions — no release may be signed with that key. |
| High — debuggable release APK | Fixed as a documented trade-off, now gated | The flag is kept only for the tester channel because `wrap.sh` needs it (AndroidManifest.xml:20-38). Packaging refuses a debuggable APK for the production channel and refuses a non-deliberate one for the tester channel (`require_deliberate_debuggable`, package_mobile_apk.py:92-111), so the store path cannot be produced by accident. CI pins `GALAXY_RELEASE_CHANNEL: tester`. |
| Medium — extractor FST bounds and traversal | Fixed | `fstSize`/`numEntries` are range-checked with 64-bit arithmetic and every name offset is validated against the string table before use (`installer-mobile/jni/wii_extractor.cpp:73-142`). FST names go through `isSafeName`, which rejects `.`, `..`, `/`, `\`, `:`, control characters and over-long names (installer-mobile/jni/path_security.cpp:18-29), and writes happen only through `ensureSafeDirectories`/`openSafeFileForWrite` (O_NOFOLLOW, symlink and reparse-point rejection). Covered by `extraction_security_test` and `wii_extraction_limits_test`. |
| Medium — Python source injection | Fixed | Paths no longer appear in the script text: they are installed as interpreter globals through `PyUnicode_FromString` + `PyDict_SetItemString` (`setPythonGlobals`, installer-mobile/jni/py_bridge.cpp:37-54) and the two `PyRun_SimpleString` bodies are constants. |
| Low — engine FST loader bounds | Fixed | `nameOff` is checked against the string-table size and required to have a NUL inside it before dereference (platform/src/dvd/dvd_fst.cpp:75-79, :213-214), and every FST index is clamped to `sFst.size()` (dvd.cpp:52, :203, :217, :237). Covered by the `dvd_fst` test, which builds `dvd_fst.cpp`, `dvd_read.cpp` and `tests/test_dvd_io.cpp` together. |
| Low — `strcpy` past a truncated path | Fixed | The tail copy is a bounded `snprintf` with the remaining size computed (platform/android/launcher.c:90). |
| Low — unchecked platform-tools download | Fixed | A pinned size and SHA-256 for the exact archive Google serves are verified before extraction (`verify_download`, tools/build_installer.py:40-51), covered by tools/tests/test_installer.py. |
| Low — CI actions pinned to tags | Fixed | Every `uses:` line in the three workflows is a full commit SHA. |
| Low — JNI/Java robustness | Fixed | All `GetStringUTFChars` results are null-checked with the corresponding releases and global-ref cleanup (py_bridge.cpp:169-178). `MainActivity` marshals view access onto a main-looper `Handler` and reads the checkbox on the UI thread before starting the cooker task (MainActivity.java:36, :241-242). `GameActivity` is now `android:exported="false"` (AndroidManifest.xml:59). |
| Info — THP heuristics | Fixed | The scan bound is the real input size (`end = c + sInputSize`, platform/src/sdk/thp_video.cpp:183) and the header dimensions are validated against the frame limits (:106-112); `thp_bounds_test` covers it. |

## Findings

### High — Signing keystore committed to a public repository

- `platform/mobile/release.keystore` is tracked in git
  (`git ls-files` lists it).
- `package_mobile_apk.py:72` signs release APKs with it and supplies the
  password inline (`--ks-pass pass:android`, `--key-pass pass:android`), so the
  private key and its passphrase are both public.

Impact: anyone can produce an APK signed with the project's key. Android accepts
a replacement for an installed app only if signed by the same key, so a third
party could publish an "update" that existing users' devices would accept, and
could impersonate official releases. Even for a sideloaded, GPL app this is a
supply-chain compromise.

Recommendation: remove the keystore from git and add `*.keystore`/`*.jks` to
`.gitignore`; generate a fresh key locally and load the password from an
environment variable or a secrets store; rotate the published key. Note that the
keystore is now in history and must be considered compromised regardless.

### High — Shimmed release build shipped `android:debuggable="true"`

- `platform/mobile/AndroidManifest.xml` sets `android:debuggable="true"` (and
  `extractNativeLibs="true"`), which the same file explains is required for
  `wrap.sh` to preload `libreserve.so`.

Impact: the shipped APK runs with the debug flag set. Any process can attach a
debugger, and `run-as com.openport.galaxyandroid` can read the app-private data
directory. Google Play and most stores reject debuggable apps outright.

Recommendation: keep the debug flag only for internal/dev builds and enable
`wrap.sh` preloading through a non-debuggable path, or accept and document the
trade-off explicitly and never publish this APK through a store. At minimum,
add a release-build check that fails packaging when `debuggable="true"`.

### Medium — Out-of-bounds reads and path traversal in the disc extractor

`installer-mobile/jni/wii_extractor.cpp`, `extractWiiDisc()`:

- The FST is trusted after only a size check. `numEntries` is read from the file
  (`read_be32(fstData.data() + 8)`) and then used to form
  `stringTable = fstData.data() + numEntries * 12` with no check that
  `numEntries * 12 <= fstSize`. The multiplication is 32-bit and can also wrap.
  The loop then walks `fstData.data() + i * 12` for `i < numEntries`.
- Filenames are dereferenced with no bound on the name offset:
  `const char* name = stringTable + nameOff;` (`nameOff` up to `0xFFFFFF`) and
  copied into a `std::string`, which reads until a NUL — potentially far outside
  the buffer.
- `extractFileToDisk()` builds the destination as `filesDir + "/" + filePath`
  from FST names with no `..`/separator sanitisation.

Impact: opening a malformed or maliciously crafted disc image (`.iso`/`.wbfs`)
can cause out-of-bounds reads (crash, or leaking surrounding heap bytes into
filenames/logs) and can write files outside the chosen output directory.
Severity is bounded because the input is a locally selected file, not remote
data.

Recommendation: validate `fstSize >= 12` and `numEntries * 12 <= fstSize`
(64-bit arithmetic) before use; bound every `nameOff` against the string-table
length and reject entries that overflow it; reject FST names containing `/`,
`\`, or `..` and confirm each resolved output path stays under `filesDir`.

### Medium — Python source injection through concatenated paths

`installer-mobile/jni/py_bridge.cpp`, `runCooker()` builds the embedded Python
program by concatenating `scriptsDir`, `inputDir` and `outputDir` straight into
Python string literals:

```cpp
"sys.path.insert(0, '" + std::string(scriptsDir) + "')\n"
...
"data_dir='" + std::string(inputDir) + "',\n"
```

A path containing a single quote or newline breaks out of the literal and
executes arbitrary Python in the app process. The values originate in Java
(`MainActivity`) and are normally app-internal paths, so exploitation needs an
unusual storage layout — but the code is also simply fragile for any path with
an apostrophe or, for `sys.path`, a Windows-style backslash.

Recommendation: pass the paths as arguments instead of interpolating them into
source — e.g. `PyRun_SimpleString` with a fixed script that reads `sys.argv`, or
set them via `PySys_SetArgvEx` / a Python dict — or at minimum escape embedded
quotes and backslashes.

### Low — FST name offsets not validated in the engine's own loader

`platform/src/dvd/dvd.cpp`, `loadFst()` masks `nameOff` from each entry
(`be32(e) & 0x00FFFFFF`) and dereferences `strings + nameOff` with no bound
against the string-table size, and `findChild()`/`DVDConvertPathToEntrynum()`
index `sFst` with `next`/`parent` values read from the file without clamping to
`sFst.size()`. `readFst()` validates only that `count * 12 < size`. The same
class of issue as the extractor above, in the code that loads `sys/fst.bin`.

Impact: a corrupted or hand-modified game data tree can trigger out-of-bounds
reads or reads past the vector during boot. Not remotely reachable.

Recommendation: validate `nameOff < stringsSize` and clamp directory
`next`/`parent` to `sFst.size()` when building the table.

### Low — `strcpy` into a path buffer that may have been truncated

`platform/android/launcher.c:96`:

```c
snprintf(path, sizeof(path), "%s", info.dli_fname);
char* slash = strrchr(path, '/');
...
strcpy(slash + 1, "libgame.so");
```

`snprintf` can fill the whole 512-byte buffer; if `slash` lands near the end,
`strcpy(slash + 1, "libgame.so")` writes up to 10 bytes past the buffer. Only
reachable with a pathologically long installed library path.

Recommendation: use `snprintf` for the final copy as well, or check the
remaining space before writing.

### Low — Downloaded platform-tools not integrity-checked

`tools/build_installer.py:109` downloads Google's `platform-tools` zip over
HTTPS and extracts it with no checksum or signature verification (the SHA-256 it
computes at line 169 covers the produced installer, not the download). The
extraction itself is well defended (path-traversal and symlink checks, covered
by `tools/tests/test_installer.py`).

Recommendation: verify a pinned SHA-256 of the archive before extracting.

### Low — CI actions pinned to tags, not commit SHAs

`.github/workflows/release.yml` and `desktop-installers.yml` use
`actions/checkout@v4`, `actions/setup-java@v4`, `nttld/setup-ndk@v1`,
`softprops/action-gh-release@v2`, etc. Tag refs are mutable, so a compromised
third-party action can run in a workflow that has `contents: write` and signs
release artifacts. Recommendation: pin every action to a full commit SHA (and
scope `permissions` per job).

### Low — Robustness gaps in the JNI and Java layers

- `py_bridge.cpp`: `GetStringUTFChars` results are used without a null check
  (`isWiiDisc`, `extractDisc`, `runCooker`); constructing `std::string` from a
  null pointer is undefined behaviour on allocation failure. `GetMethodID`
  results for the callback are also unchecked before `CallVoidMethod`/`CallIntMethod`.
- `platform/mobile/src/com/openport/galaxyandroid/MainActivity.java`: the
  background cooking thread reads `chkIncludeMovies.isChecked()` off the UI
  thread; view access must happen on the main thread. `getPathFromUri()` falls
  back to matching a display name under `Downloads`/external storage, which can
  resolve to a different file than the one the user picked.
- `AndroidManifest.xml`: both `MainActivity` and `GameActivity` are
  `android:exported="true"` with no permission. Launching them from another app
  is largely benign here but is unnecessary surface.

### Info — `THPVideoDecode` and I8 tile writes assume well-formed movie data

`platform/src/sdk/thp_video.cpp` bounds the scan with a fixed
`end = file + (4 MiB)` window rather than a real buffer length, and
`storeBlock()` bounds rows by `height` but not columns by `width`. For sizes
that are not 16/8-aligned, a block can be written a tile past the plane. THP
video is fixed-size game data, so this is not currently reachable, but the
bounds are heuristic rather than structural.

## Positive observations

- The memory model (`platform/src/os/os_mem.cpp`) is careful: the 32-bit game
  window is reserved up front, `mmap` failures and misplacements are fatal
  rather than ignored, and uncached mirrors alias the same pages.
- The thread/interrupt layer (`platform/src/os/os_thread.cpp`) is a faithful
  port of the SDK scheduler with clearly documented single-CPU ownership,
  thread-local state, and interrupt delivery only at safe points.
- The textures decoder (`platform/src/gx/tex_decode.cpp`) and the Yaz0 decoder
  (`installer-mobile/jni/yaz0.c`) are properly bounds-checked, including the
  CMPR sub-block edge cases and the Yaz0 back-reference distance.
- The desktop installer separates UI from work threads, validates device
  identity and free space before copying, cleans up its own temporary
  workspaces, and rejects zip path traversal and symlinks
  (`tools/build_installer.py`, `tools/tests/test_installer.py`).
- Sample decoders cope with corrupt input by clamping rather than reading out of
  range in their hot paths (`thp_video.cpp` clamps IDCT output and caps
  `kNaturalOrder`).

## Verification performed

At the time of the original review:

- `python -m unittest discover -s tools/tests -p 'test_installer.py'` → 15 tests,
  all pass.
- `git ls-files` used to confirm the keystore is tracked and to separate
  committed files from ignored artifacts.

For the 2026-10-27 re-verification and the fixes listed above:

- `ninja -C build-mobile game main reserve installer vk_inventory_test` → exit 0,
  137 build edges. The flag change forced every owned translation unit to
  recompile — 134 in `game`, plus the five that `main`, `reserve` and
  `petari_headless` build — and the log holds **0 warnings and 0 errors**. The
  owned layer now builds under `-Wall -Wextra` plus the port-bug classes pinned
  by name in `OWNED_DIAGNOSTICS`, with `OWNED_WARNING_SUPPRESSIONS` reduced from
  the 33 inherited options to the single documented `-Wno-deprecated-declarations`.
  Both lists and the reason for every entry now live in
  `cmake/PortDiagnostics.cmake`, which the root `CMakeLists.txt` includes.
- `ctest --test-dir build-host` → 16/16 pass.
- `python -m pytest tools/tests -q` → 143 passed, 1 skipped, 2 subtests passed, and
  `python -m unittest discover -s tools/tests -p 'test_*.py'` → 144 tests, OK.
- `python -m unittest discover -s tests -p 'test_*.py'`, the in-tree suite CI runs,
  → 92 tests, OK. Eight files had drifted past the 300-line gate while these fixes
  were made, so they were split the way the repo already splits: extra
  non-`test_`-prefixed fixture modules in `tools/tests`, extra translation units in
  the *same* C++ targets (`SaveSyncManager.cpp` + `SaveSyncManagerStage.cpp`,
  `test_save_sync.cpp` + `test_save_sync_export.cpp`, `test_netplay.cpp` +
  `test_netplay_packets.cpp`), and `cook_blob`/`is_converted_kind` moved from both
  copies of `cook.py` into the mirrored `cook_archive.py`. Nothing stopped being
  tested: `save_sync` still reports its 62 checks, `netplay` its 7 `[PASS]` stages,
  the `add_test` names and both workflows' target lists are untouched, and
  re-configuring the two build trees changed **zero** compile command lines
  (`build-host/compile_commands.json` is byte-identical, the mobile one differs
  only by the added `SaveSyncManagerStage.cpp` entry).
- `python tools/check_loc.py` → all 443 maintained code files comply with ≤ 300
  lines.
- `parse check`: all three workflows load with `yaml.safe_load`, and
  `tools/tests/test_release_tag.py` asserts that no `${{ … }}` expression
  reaches any `run:` script line in any workflow (with a positive-control test
  that the detector still fires on a pasted expression).

## Still open

Deliberately left for the owner because they need hardware, a decision, or
infrastructure this machine does not have:

- **Device proof.** The one-device memory-reservation proof, the RMX3395 (Gyro
  star pointer) matrix and any multi-device confirmation cannot be produced
  without a phone; release stays blocked on them.
- **Signing key.** Nothing above rotates the key whose private half is still in
  git history, and `GALAXY_KEYSTORE_B64`/`GALAXY_KEYSTORE_PASS` must be supplied
  as repository secrets rather than committed.
- **Netplay has no protection.** `NetplayPackets.cpp:25` still sends a
  hard-coded `seq = 0`, there is no pairing or authentication step, and the
  socket layer (`NetSocket`) has no cross-thread open/close/receive guarantee.
  Fixing this is a protocol design task, and proving it needs two devices.
- **Toolchain mismatch.** CI builds with NDK r25c (release.yml:72) while the
  local mobile tree here is NDK r28.2 (`build-mobile/CMakeCache.txt`), so a
  green local build is not by itself a green CI build. Both use
  `-DCMAKE_BUILD_TYPE=Release`, so the optimisation flags do agree.
- **Host tree is Linux-only for `decomp`.** Under MinGW the whole `decomp` layer
  fails on UCRT `<cwchar>` (`-fshort-wchar`) and `platform/include/port/msl_ext.hpp`
  collides with libstdc++ 16.1's retained C++98 adaptors; the mobile-owned
  sources additionally need `android_native_app_glue.h`. A/B-compiling the same
  units with the pre-change flags (`-O3 -DNDEBUG=1`) produced byte-identical
  stderr, so this is pre-existing and unrelated to this round of edits. The CI
  host leg avoids it by building only the named test targets.
- **No thread-sanitizer leg.** The data-race fixes are covered by targeted host
  tests, not by TSan; that needs a Linux CI job.
- **Product decisions.** The settings drawer still ignores `activeBackend` and
  `themeColor` (the backend pill is hard-wired to "VULKAN 1.0"), and the
  anisotropy / LOD-bias sampler policy is a quality-versus-cost call.
- **Stricter RARC bounds** than the ones now in place would need a real disc
  cook to validate against.

## Limitations

- `decomp/` (reconstructed game and SDK sources) was not reviewed line by line;
  it is treated as third-party derived code.
- The 2026-10-16 pass was static only. The 2026-10-27 work was verified by
  compiling and running the host and Android (NDK r28, `Release`) trees and the
  test suites; **no Android device was used**, so nothing here is a claim that
  the game runs correctly on hardware.
- The large renderers (`vk_renderer.cpp`, `gl_renderer.cpp`, `gx_recorder.cpp`,
  `mobile_ui.cpp`, `vr_game.cpp`) were scanned for patterns and spot-checked but
  not fully read; a dedicated GPU/UI review is still worthwhile.
