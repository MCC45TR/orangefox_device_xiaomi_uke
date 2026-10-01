# USB-C monitor and input checkpoint

1 October 2026. Local unsigned Global OS3.0.303.0.WOZMIXM candidate. GitHub
publication remains deferred. The complete recovery roadmap, comprehensive
partition manager and Linux partition/home/Btrfs backup requirements remain
unfinished. No tablet boot, monitor scanout, USB input or hardware rollback
acceptance is recorded.

**Settings → USB monitor, mouse and keyboard** exposes output enable/disable,
resolution and refresh selections, the applied mode and the monitor's reported
modes. Select **2560×1440** and **75 Hz**, then apply, for the intended QHD75
test. One external display mirrors the rotated recovery canvas and cursor with
aspect-preserving borders. Existing interface scale applies to both displays.
Choices last for the recovery session. See
[EXTERNAL-MONITOR.md](../docs/EXTERNAL-MONITOR.md) for controls, source evidence
and acceptance limits.

The native sink uses the existing renderer's DRM fd and an unused CRTC/plane.
Panel resources are excluded. Only reported progressive timings within the
resolution/rate limits are considered; no timing is synthesized. TEST_ONLY
atomic checks precede activation and mode changes. Failed selections/checks
retain an existing working external mode. New buffers are prepared before old
scanout resources are released. Failed disable retains resources and reports
failure. Render-thread processing owns resource changes, including blanking;
action threads publish preferences.

Content updates are bounded to about 30 frames/sec independently of the selected
scanout Hz. Idle canvases are not recopied; coordinate columns/property IDs are
cached and connector probes are limited to once per second. Output buffers are
bounded. USB input supports mouse movement/click/drag/back, wheel focus movement,
US-layout text, Esc and F6 menu navigation. Rescans/dropped input queues cancel
held key/drag state without generating an action-producing release.

| Evidence class | Verified result and limits |
|---|---|
| Root host suite | All 19 checks pass; proposed publication indexes pass privacy and no-reply identity checks |
| Native host suite | All 12 CTest executables and complete CLI fixtures pass against exact source-input receipts |
| Native monitor | Actual sink/layout with fake DRM IOCTLs covers rotation/padding, QHD75/59.94 timing selection, unavailable modes, failed tests/commits, occupied resources, primary-resource exclusion, same-choice retry, hotplug, blanking/on/off, idle/cached paths and resource cleanup |
| Native input | Exact reviewed keyboard/evdev functions with host platform stand-ins cover navigation, consumed releases, reset, one-button mice, nanosecond hotplug, reused device state, hangup and dropped queues; no real evdev device is read |
| Sanitizers | All 12 executables pass ASan/UBSan with leak detection; the final three monitor/input executables pass again after deferring blanking to the render thread. The pinned runtime requires excluding vptr |
| Android | Final recoveryimage completes in 1:57; native sink, renderer/input hooks, settings pages and GUI actions compile for AArch64 |
| Actual compressed ramdisk | No-Python/privacy, two recursive ZIP scans, XML/tool manifest and dependency closure for 205 AArch64 ELF files pass; the extracted renderer exactly matches the newly built library and exports the required monitor APIs |
| Extracted AArch64 QEMU | Existing CLI, partition, stock/GPT/raw/host-stream recovery, display geometry/private settings and utility fixtures pass; GPU, USB and monitor rendering are not emulated |
| Package | Two packaging runs have identical asset hashes; independent binary reproduction remains open |

The host callback installs the exact source-built renderer into the ramdisk,
and the final-image auditor compares it and checks monitor exports. This closes
the incremental-build case where a new GUI could otherwise import APIs absent
from a stale staged renderer. Reviewed patches reconstruct the active recovery
source exactly. Source snapshots include libdrm and original licenses.

Current local candidate: `artifacts/ure-usb-monitor-qhd75-alpha/`.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Recovery IMG | 104857600 | `25144242eea49360e8f0bd1affb1734b02240fc73e6139f0d8f4da1c6181da7c` |
| Temporary-boot IMG | 100663296 | `ee39e851db19a7ce0f933afeb86a1b7358ca7087d475db7ee500ec87b30f194e` |
| Installer ZIP | 32574318 | `7559fed4e1025ee220ce1525fafd6d5d104ab071310b42be96bbe114bacff15e` |

Compressed ramdisk: 37320299 bytes, SHA-256
`859f79802df313be718ab6f26a6c0a76f70a7c6752c4552ff5702ff5bc4bb50a`.
Extracted native CLI:
`750625eb028f12b8877fa1a67f7b7e1fdbc18196b679f11d84b8aa9ffa69d119`.
Extracted renderer:
`794848e312b6ec089edc94263ca0306fe18fbe8ec119149446f3b557beb3385b`.
The original stock kernel remains unchanged. AVB is NONE and the ZIP is unsigned.
The temporary-boot IMG must never be flashed; preserve firmware-matched stock
recovery and the inactive stock slot as described in the candidate instructions.

The intended physical setup is the supplied Juo JH925 hub and one QHD75 monitor.
OEM DP/USB source/config/module inventory is source evidence, not a module-load
or link-negotiation result. Recovery boot, panel/mirror coexistence, EDID/QHD75,
output toggles and mode-failure behavior, reconnect, blank/unblank, simultaneous
mouse/keyboard, scale/hit coordinates and stock return require tablet tests.
Dual-monitor MST, 4K, HDMI audio, Ethernet and hub PD acceptance are separate.
