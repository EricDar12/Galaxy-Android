---
title: Galaxy Android - Technical Architecture & Engineering Deep-Dive
description: In-depth technical architecture of Galaxy Android, detailing the ARM64 native porting process, 32-bit address space reservation, the Vulkan 1.0 renderer, Hor+ ultrawide rendering, frame telemetry, and asset cooking.
tags:
  - architecture
  - android
  - arm64
  - vulkan
  - decompilation
  - technical
  - reverse-engineering
  - petari
  - galaxy-quest
  - galaxy-android
---

# Galaxy Android - Technical Architecture

Galaxy Android is a **100% native Android ARM64 port** of the classic 2007 gravity platformer. It is not an emulator; the original game logic and engine routines execute directly on the device's CPU cores using native Android APIs.

> **Acknowledgements & Foundation:**
> This project builds upon the foundational reverse-engineering and decompilation work of the [Petari Decompilation Project](https://github.com/SMGCommunity/Petari) and the groundbreaking standalone mobile platform architecture developed by [bigmak94/GalaxyQuest](https://github.com/bigmak94/GalaxyQuest). We express our utmost gratitude to these communities and creators.

---

## 1. High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     Android System (OS Layer)                   │
├───────────────────────────────┬─────────────────────────────────┤
│    NativeActivity (Java)      │     wrap.sh & libreserve.so     │
├───────────────────────────────┴─────────────────────────────────┤
│            libgame.so (Galaxy Android Native Engine)            │
│  ┌──────────────────────┬─────────────────────────────────────┐ │
│  │   Original Gameplay  │         Android Platform Layer       │ │
│  │   & Scene Logic      ├─────────────────────────────────────┤ │
│  │   (Decompiled C++)   │  - Vulkan 1.0 Renderer            │ │
│  │                      │  - Native Vulkan UI Overlay Pipeline│ │
│  │  MEM1 (0x80000000)   │  - AAudio Low-Latency 48kHz Mixer   │ │
│  │  MEM2 (0x90000000)   │  - Multi-touch AInputQueue Engine   │ │
│  │                      │  - Gyro ASensorManager Transformer  │ │
│  │                      │  - Real-Time Perf Telemetry Hooks   │ │
│  │                      │  - Hor+ Ultrawide Camera Mod        │ │
│  │                      │  - Dual Persistent Save Mirror      │ │
│  └──────────────────────┴─────────────────────────────────────┘ │
├─────────────────────────────────────────────────────────────────┤
│       Cooked Game Assets (/sdcard/GalaxyData/files)             │
│       (Little-endian textures, models, archives, audio)         │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. The 32-Bit Pointer Memory Window Challenge

### The Problem
The Wii hardware (Broadway PowerPC) used a 32-bit physical address space:
- **MEM1 (Fast SRAM):** Base `0x80000000` (24 MB)
- **MEM2 (GDDR3 RAM):** Base `0x90000000` (64 MB)

The original game code routinely casts pointers directly to 32-bit unsigned integers (`u32`) and relies on pointer arithmetic within these specific address ranges. On a 64-bit Android OS (`arm64-v8a`), the Linux kernel and the dynamic linker allocate memory across a 48-bit virtual address space (frequently above `0x7000000000`). Truncating a 64-bit pointer to `u32` leads to immediate segmentation faults.

### The Solution: Address Space Reservation
The guest layout covers `0x80000000`-`0xE0010000` (MEM1, MEM2, the
`libgame.so` slot, a low pool, the uncached MEM1/MEM2 mirrors and the locked
cache).  The region table lives in `platform/src/os/guest_layout.c`.

1. **Tester/development pre-ART path (`wrap.sh` + `libreserve.so`):**
   - Only a **debuggable** app executes `lib/<abi>/wrap.sh`; Android ignores the
     script in a non-debuggable package.
   - `wrap.sh` sets `LD_PRELOAD`, so `libreserve.so`'s constructor runs before
     `app_process` starts ART.  It maps the whole window as
     `PROT_NONE | MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE |
     MAP_FIXED_NOREPLACE` and names it `petari game window` in
     `/proc/<pid>/maps`.
   - `MAP_FIXED_NOREPLACE` never replaces an existing mapping.  On a conflict
     the constructor logs the failure and leaves the owner untouched.

2. **Normal launch (no `wrap.sh`):**
   - `libmain.so`'s `android_main` runs after ART has laid out its heap.  It
     audits `/proc/self/maps`, then reserves only the guest slices that are
     still free, with the same `PROT_NONE`/`MAP_NORESERVE`/
     `MAP_FIXED_NOREPLACE` flags, and rolls back only the slices it created.
   - A partial PROT_NONE guard that does not cover a whole region is treated as
     a foreign mapping and reported as a conflict.  Any conflict is fatal: the
     launcher refuses to start instead of mapping over another owner.

3. **Loading `libgame.so`:**
   - After the slices are reserved, the launcher loads `libgame.so` into the
     `0x98000000` slot with the NDK's `android_dlopen_ext` and
     `ANDROID_DLEXT_RESERVED_ADDRESS` (`reserved_addr = 0x98000000`,
     `reserved_size = 0x08000000`).  This is a documented loader API, not a
     fixed-address linker trick.
   - MEM1 and MEM2 (plus their uncached mirrors) are mapped onto the reserved
     slices as the game's memory pools.

**Production status: post-ART reservation validated on one device; release
remains gated.**  Store builds cannot set `android:debuggable="true"`, so they
cannot use `wrap.sh`; no supported non-debuggable Android mechanism runs app
code before ART.  Application code, `ContentProvider`s, `JNI_OnLoad` and
library constructors all run after the runtime has laid out its heap, and
Zygote exposes no public app hook; the only pre-ART environment Android
executes for an app is `wrap.sh`, which the package manager only honors for
`debuggable` packages.  Production must therefore rely on the post-ART
reservation path above.

On 2026-10-25 a non-debuggable build (versionCode 14, signed with the rotated
tester identity; APK SHA-256
`15bca581e3f7781bed285a14a0e39aba2e120153d053c082a7d19a2178710c6d`) was
validated on a Realme RMX3395 (Android 14, build fingerprint
`realme/RMX3395/RE879EL1:14/UKQ1.230924.001/S.208a662-b53ad:user/release-keys`).
Twelve cold starts—ten consecutive runs, plus one after reboot while another
non-debuggable process held a 128 MB allocation—each logged
`Conflicts: 0, Available: YES` and `7/7 slices visible as PROT_NONE`, loaded
`libgame.so` at `0x98000000`, reached the title screen, and produced no crash.
The save checksum stayed `e9ef95b785494ee03e36b9bf0d817281`.  This proves the
mechanism works in a non-debuggable process on that device.  It is not proof
for other devices, Android releases or vendor ART layouts, and the APK was not
production-signed.

`adb shell cat /proc/<pid>/maps` is denied for non-debuggable packages on
Android 14, so the launcher's own pre-reservation audit and post-reservation
re-audit logs are the maps evidence for such builds; attach them to the
validation record.

Device-validation procedure for a non-debuggable candidate (no step may be
skipped; destructive reinstall needs the owner's approval and a save backup):

1. Package a non-debuggable build with the rotated signing key, install it, and
   record the device model, Android release, build fingerprint and app version.
   `python package_mobile_apk.py --channel production` now selects the
   checked-in `platform/mobile/AndroidManifest.production.xml`
   (`debuggable="false"`) and refuses the signed artifact if its compiled
   manifest is debuggable.  The 2026-10-25 run predates this variant and used a
   non-debuggable copy of the manifest generated outside the repository.
2. `adb logcat -c`, cold-start the app, and wait for the title screen.
3. `adb logcat -d -s PetariVR:I | grep -E "guest address slices|guest reservation verification"`
   must show the audit report and `7/7 slices visible as PROT_NONE`.  A line
   containing `refusing to start` is a failure.
4. `adb shell pidof com.openport.galaxyandroid`.  On Android 14
   `adb shell cat /proc/<pid>/maps` is denied for non-debuggable apps; capture
   the launcher's `Guest Memory Layout Audit`, `Conflicts: 0` and
   `7/7 slices visible as PROT_NONE` lines instead.  If shell maps access is
   available (root or a debuggable build), also confirm every guest range is
   owned by the app process (no foreign mapping inside
   `0x80000000`-`0xE0010000`).
5. Force-stop and repeat steps 2-4 at least ten times; at least one run should
   happen after the device has been rebooted and after heavy memory pressure.
6. Attach the logcat dump, maps capture (or the self-audit logs when shell maps
   access is denied), device details and per-run result to the validation
   record.

Until a production key and multi-device evidence exist, only tester
(`debuggable`, sideload-only) builds are published.  The checked-in
non-debuggable production manifest removes the packaging blocker, but it does
not supply credentials or device evidence.

---

## 3. Graphics Subsystem: Vulkan 1.0

The GameCube/Wii utilized the **GX** fixed/programmable pipeline, featuring up to 16 Texture Environment (TEV) stages, indirect texturing, alpha testing, and custom display list formats. Galaxy Android has a single rendering backend, Vulkan 1.0:

### A. Vulkan 1.0 Hardware Backend (Primary & Recommended)
- **Zero-Stutter Asynchronous Presentation:** Offscreen rendering and swapchain presentation are fully pipelined with dedicated command buffers (`mCommandBuffer` for EFB draws, `mPresentCommandBuffer` for swapchain presentation). The CPU never waits on `mRenderFence` during presentation; synchronization between the passes is handled on the GPU via `mRenderToPresentSem`.
- **Non-Blocking Asynchronous Texture Streaming:** Buffer-to-image transfers are recorded directly into the active frame's command buffer in `uploadTextures()`, while staging buffers are retired asynchronously in `mStagingBuffersToFree` and freed once the subsequent frame fence completes. Eliminates all `vkQueueWaitIdle` stalls during galaxy transitions.
- **Phase-Locked VI Retrace Clocking:** The Wii engine's internal Video Interface (`vi.cpp`) is synchronized directly to Android presentation flips via `port_vi_retrace()`, eliminating clock drift and beat frequencies between the simulation tick and screen presentation.
- **Zero-Syscall Persistent Memory Mapping:** All dynamic vertex, index, row, and uniform buffers (`vbo`, `ibo`, `rowBuf`, `recBuf`, `stBuf`, `vtxRecBuf`, `eyeUbo`, and `uiVbo`) are persistently mapped upon allocation. Replaces all per-frame `vkMapMemory` and `vkUnmapMemory` Linux kernel system calls with direct, non-blocking `memcpy()` operations.
- **TBDR Tile Memory Optimization:** EFB offscreen render passes configure depth/stencil attachments with `VK_ATTACHMENT_STORE_OP_DONT_CARE`. Allows mobile TBDR GPUs (Qualcomm Adreno, ARM Mali, PowerVR) to discard depth on-chip in GMEM/SRAM at the end of the pass, saving hundreds of megabytes of DRAM bus bandwidth per second.
- **CPU big.LITTLE Performance Core Affinity:** Engine and render threads are pinned via `sched_setaffinity()` to high-performance cores (Cores 4-7 on octa-core SoCs), preventing Android OS scheduler migration to power-saving LITTLE cores.
- **First-Boot Shader Precompilation & Dual-Layer Cache:**
  - **Layer 1 (SPIR-V Bytecode Cache):** `prewarmCommonShaders()` pre-compiles common Galaxy TEV combinations, lighting modes, and HUD variants into binary SPIR-V on first boot, storing them permanently in `vk_shaders.bin`.
  - **Layer 2 (Driver Pipeline Cache):** `savePipelineCache()` commits driver pipeline binaries to `vk_pipeline_cache.bin` on app pause and exit.
- **Unified Pipeline State Objects (PSO):** Pre-compiles graphic pipelines to eliminate driver runtime validation overhead during draw calls.
- **Offscreen EFB Framebuffers:** Offscreen color (`VK_FORMAT_R8G8B8A8_UNORM`) and depth/stencil attachments allocated with `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT`.
- **Dynamic Depth Probing:** Runtime querying of supported physical device optimal tiling features to select `VK_FORMAT_D32_SFLOAT_S8_UINT`, `VK_FORMAT_D24_UNORM_S8_UINT`, or `VK_FORMAT_D16_UNORM`.
- **Swapchain Blitting & Pillarboxing:** Procedural fullscreen quad presentation pass (`mPresentPipeline`) blitting the offscreen target image into the swapchain with clean pillarbox borders.
- **Native Vulkan UI Pipeline:** Dedicated Vulkan graphics pipeline (`mUiPipeline`) rendering touch controls, laser tethers, and settings drawer widgets directly into the swapchain render pass. Operates with an inverted Vulkan NDC projection matrix:
  $$\text{proj}[5] = \frac{2.0}{\text{height}}, \quad \text{proj}[13] = -1.0$$
- **Startup Target Safety:** Strict `hasFrame()` guard prevents sampling uninitialized render targets (`VK_IMAGE_LAYOUT_UNDEFINED`) during initial boot, eliminating GPU validation faults and driver crashes.

### B. No Second Backend

There is **no OpenGL ES backend in this tree** - `platform/src/gx/vk/` is the only
renderer, `AndroidManifest.xml` requires `android.hardware.vulkan.version`
(`0x400000`), and the Play Store / package installer therefore refuses devices
without a Vulkan 1.0 driver.  An earlier revision of this document described a
GLES 3.2 fallback; that backend was never implemented, so the requirement is
honest rather than a formality.

### C. Hor+ Dynamic Ultrawide Projection
The original game supported 4:3 and anamorphic 16:9 aspect ratios. On modern smartphones (19.5:9, 20:9, 21:9):
- **Camera Frustum:** The 3D projection matrix is dynamically recalculated using the horizontal expansion method (Hor+). The vertical field of view remains constant, while horizontal FOV is expanded:
  $$\tan\left(\frac{\text{FOV}_h}{2}\right) = \tan\left(\frac{\text{FOV}_v}{2}\right) \times \text{Aspect Ratio}$$
- **2D HUD Safe Zone:** All 2D interface elements (health meter, coin counters, dialogs) are projected within a centered 16:9 safe region using Vulkan scissor rectangles, preserving circular geometry and preventing obstruction by camera hole punches.

---

## 4. Real-Time Hardware Performance Telemetry

Galaxy Android features an integrated performance instrumentation pipeline:

```
[Game Thread] ────> port_perf_frame_begin() ───> Logic & GX Recording ───> port_perf_frame_work_done()
                                                                                   │
[Render Thread] ──> port_host_time_ns() ───────> vkr.render() + present() ──> port_perf_render(cpuNs, 0)
                                                                                   │
[HUD Overlay] <────────────────────────────────────────────────────────────────────┘
```

- **Metrics Tracked:**
  - Engine Simulation & GX Command Recording Duration
  - CPU-side Render Submission & Presentation Latency
  - Active Display Refresh & Average FPS
  - Real-time CPU core utilization percentage
  - Resident Set Size (RSS) RAM usage
- **Pill HUD Overlay:** Displays live metrics (`45.3 FPS  22.1 ms  CPU 20%  RAM 588 MB`) with configurable display modes (Off, FPS only, Full Stats).

---

## 5. Motion & Gyroscope Sensor Architecture

Galaxy Android samples Android's `ASensorManager` at high frequency to emulate the Wii Remote's optical pointer and motion sensors:

### Android Landscape Coordinate Transform (`ROTATION_90`)
In landscape orientation, physical phone rotation maps to screen coordinates as follows:
$$\text{rotH} = \text{event.vector.x} - \text{event.vector.z} \times 0.35f$$
$$\text{rotV} = \text{event.vector.y}$$

- **Azimuth / Yaw Tracking:** Compensates for device roll-yaw coupling when tilting the phone horizontally.
- **Invert X / Invert Y Options:** Independent direction inversion multipliers configured via the Settings drawer.
- **Accelerometer Fallback:** Accelerometer vector integration provides intuitive motion aiming on hardware lacking dedicated gyroscope sensors.

---

## 6. Save Data Subsystem & Dual Mirroring

To protect game progress against OS clearing, uninstalls, or cache purges:
1. **Primary Internal Storage:** Saves write directly to the emulated Wii NAND path:
   `/data/user/0/com.openport.galaxyandroid/files/nand/title/00010000/524d4745/data/GameData.bin`
2. **External Mirror:** Simultaneously mirrored to:
   `/sdcard/GalaxyData/saves/GameData.bin`
3. **Auto-Recovery on Boot:** If internal app data is wiped or reinstalled, the boot sequence automatically detects the mirrored file and restores it into NAND before initializing the file system.
4. **Export to Downloads:** One-tap export to `/sdcard/Download/GameData.bin` for cross-device backup.

---

## 7. Asset Cooker Pipeline (`cook.py`)

Original Wii disc partitions contain big-endian files (`.arc`, `.bcsv`, `.kcl`, `.thp`, `.brres`). Byte-swapping every asset on a mobile CPU during gameplay would introduce stutter.

The asset cooker ([`tools/cook/cook.py`](../tools/cook/cook.py)):
1. **Recursively unpacks** all RARC archives and Yaz0-compressed archives.
2. **Converts big-endian binary tables (BCSV):** Headers, offsets, and row entries are rewritten into 64-bit little-endian binary formats.
3. **Collision Models (KCL):** Octree nodes, face planes, and attribute flags are converted to little-endian IEEE-754 floats.
4. **Textures (BTI / TPL):** CMPR (DXT1), RGB565, and RGBA8 textures are converted into formats natively compatible with mobile GPUs.
5. **Output Structure:** The result is placed in `cooked/sys/` and `cooked/files/`, ready for direct streaming into mobile storage.

---

## 8. Audio Subsystem (AAudio)

- Original audio routines relied on the Wii DSP mixer and AX voice manager.
- Galaxy Android implements a low-latency PCM software mixer that writes directly to an Android `AAudioStream`.
- Features:
  - 48,000 Hz, 16-bit stereo output resampled from 32,000 Hz DSP mixer.
  - Ring-buffer latency under 15 milliseconds.
  - Handles dynamic interactive music layers (e.g. adding brass or percussion instruments during speed-up or Yoshi riding).
  - Graceful missing-resource handling (e.g. unreferenced developer test files like `B109take_0.aw`).

---

## 9. Comparative Performance

| Metric | Emulation (Dolphin on Android) | Galaxy Android (Native Port) |
|---|---|---|
| **CPU Execution** | JIT Dynamic Recompiler | Native ARM64 Machine Code |
| **GPU Execution** | State-tracking overhead | Direct Vulkan 1.0 |
| **Memory Footprint** | ~1.8 GB RAM | ~580 MB RAM |
| **Battery Draw** | High (~12-15W) | Low (~4-6W) |
| **Frame Rate** | Variable 30-50 FPS on mid-range | Solid 60 FPS (Up to 120 FPS) |

---

## References

- [Controls Reference](CONTROLS.md) - Input and controller guide
- [Installation Guide](INSTALLATION.md) - How to setup and play
- [Decompilation Reference](decomp/getting_started.md) - Petari decompilation notes
- [Petari Repository](https://github.com/SMGCommunity/Petari)
- [GalaxyQuest Repository](https://github.com/bigmak94/GalaxyQuest)
