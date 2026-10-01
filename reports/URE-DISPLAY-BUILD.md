# Adjustable tablet interface checkpoint

1 October 2026. Local unsigned Global OS3.0.303.0.WOZMIXM candidate. GitHub
publication remains deferred. The full recovery roadmap, expanded partition
manager and Linux partition/home/Btrfs backup requirements remain active.
No tablet display, physical touch, GUI rendering or hardware rollback result
is recorded.

**Settings → Interface scale for tablet** and the URE menu expose 50–100 percent
scaling, seven presets, a 75 percent tablet default/reset, current percentage,
custom numeric input and explicit save/load. One uniform density applies to
text, icons, controls and their physical hit rectangles. Full-screen edges,
centers and input widths use the actual rotated framebuffer's logical canvas.
Existing rotation is preserved. Invalid percentages cannot change saved data.

Reloading is deferred to the render thread with atomic request flags. The
built-in theme is reloaded without the upstream settings flush, custom ZIP-theme
lookup, userdata mount or calibration write. Failure retries the prior
applied percentage; a second failure explicitly requests a recovery restart.
Private synced settings use a dedicated selected filesystem with schema,
permission, ownership, single-link and readback checks. Volatile filesystem
records are disclosed. Startup can load an already accessible dedicated
`/mnt/uke-settings`; unavailable or invalid storage uses the tablet default.
No automatic mount or cross-boot storage availability is claimed. See
[DISPLAY-SCALING.md](../docs/DISPLAY-SCALING.md).

Package recreation focuses the scale page directly, avoiding main-page startup,
reboot and ADB/MTP actions. Existing URE selections/review fields survive theme
defaults and the original future start-page route is restored. The first local
`ure-tablet-scale-alpha` checkpoint is superseded: its reload could re-enter the
upstream main page. Its existing artifact/source receipts remain historical.

| Evidence class | Verified result and limits |
|---|---|
| Root host suite | All 19 checks and publication privacy pass |
| Native host suite | All nine CTest executables and complete CLI fixtures pass |
| Actual GUI functions with host stand-ins | Project density/variable hooks and reviewed upstream percentage parser, coordinate scaling and deferred reload compile directly from their source; all presets, three framebuffer geometries, bottom-row rectangles, no immediate resource destruction, no mount/flush/startup replay, URE selection and start-route preservation, lock-state preservation and single/double failure behavior pass; this is not graphical rendering |
| Private settings | Creation/replacement/readback, invalid input, unsafe permissions, hardlink/symlink and malformed records pass |
| Sanitizers | All nine CTest executables and display CLI pass ASan/UBSan with leak detection; vptr remains excluded due to the pinned runtime |
| Android | Final recoveryimage completes in 2:06; the actual density hooks, GUI adapter, settings page, atomic reload path and native CLI compile for AArch64 |
| Extracted ramdisk | Actual compressed ramdisk, recursive ZIP scans, no-Python/privacy, XML/tool manifest, staged binary matching and ELF dependency closure pass |
| Extracted AArch64 QEMU | Display geometry/private-settings CLI and prior partition, stock/GPT/raw/host-stream recovery fixtures pass; no renderer, display or tablet result |
| Package | Two packaging runs from the same built image have identical hashes; independent binary reproduction remains open |

Current local candidate: `artifacts/ure-tablet-scale-alpha2/`.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Recovery IMG | 104857600 | `2ae8c7a3e4ab4da904d06e5aeffc65f6fc78d6eca974962c32e0add2af655856` |
| Temporary-boot IMG | 100663296 | `1dc1f782665347ec977c6b1a0097d9c08b816d61a0b122a8b4ec5ab0c588f7b8` |
| Installer ZIP | 32556982 | `001310d54ce8eed1fc53576cbba6241181bce8dbb11b490cb903f8fb53e52bc1` |

Compressed ramdisk: 37305011 bytes, SHA-256
`43823acaaebd5b334aefad3e7b688e11051a8d2ba4d2189fb46910a58bfec5af`.
Extracted native CLI:
`750625eb028f12b8877fa1a67f7b7e1fdbc18196b679f11d84b8aa9ffa69d119`.
The original stock kernel is unchanged. AVB is NONE, the ZIP is unsigned, and
the temporary-boot IMG must never be flashed. Physical display/input acceptance,
complete GUI/session policy and all remaining roadmap work stay open.
