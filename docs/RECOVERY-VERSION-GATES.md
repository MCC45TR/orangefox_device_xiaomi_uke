# Uke recovery: A/B, OTA, boot selection and encryption gates

This document separates source configuration from working device support. The
same `uke` codename covers POCO Pad X1 and Xiaomi Pad 7, but firmware region,
Android version, bootloader state and SKU are release compatibility fields.

## Source review

| Source | Useful evidence | Why it cannot be imported as a working profile |
|---|---|---|
| [twrpdtgen generated tree](https://github.com/twrpdtgen/android_device_xiaomi_uke/tree/69165f781ebf9b62075922bb4a36abfa7adee9da) | Uke fstab and recovery service names for comparison | One generated commit; `ALLOW_MISSING_DEPENDENCIES`, recovery-as-boot, a fixed super size, synthetic 2099 security date and disabled AVB checks conflict with the measured Global stock layout/policy. No decryption or OTA result. |
| [93ir88 untested tree](https://github.com/93ir88/-UNOFFICIAL-twrp-for-xiaomi-pad-7-UNTESTED-/tree/fbe4571eca5b19b73ae3691b6077d3e9ddbdb905) | Virtual A/B, FBE and boot-control configuration hypotheses | Its README says Android 14/vendor-boot recovery, while its BoardConfig mixes `crow`, vendor-boot placement, incompatible boot/super sizes, test AVB key and a synthetic security date. The prebuilt directory contains no kernel. Claims are not hardware evidence. |
| [hasan6034 beta source candidate](https://github.com/hasan6034/uke_test/tree/a79f3835ba1917051640a88f6fabbda1d5c605c3) | 2 August 2026 commit describes a touchscreen fix: wait for touch nodes after module loading, then start the touch HAL; the repo also has A/B and recovery configuration | The supplied Telegram screenshot links a beta download, but no artifact-to-commit hash or independent boot log was found. The tree bundles proprietary services/firmware, a prebuilt kernel, permissive SELinux and broad touch-device permissions; these must not be copied. Its super size and synthetic security date conflict with our gates. |
| [darkstride OrangeFox tree](https://github.com/darkstride/Ofox-uke-Tree/tree/1a1433f2fcd64ab60786c5c6a82dbc36b01f0d4e) | Separate recovery partition, image limits and touch-module list for comparison | Generated-tree `ALLOW_MISSING_DEPENDENCIES`, hard-coded dynamic partition sizes, AVB flags 3 and synthetic 2099 security patch date are unsafe to inherit. No verified decryption or hardware result. |
| [xiaomi-uke recovery tree](https://github.com/xiaomi-uke/android_device_xiaomi_uke-recovery/tree/d037467a1643d64979337ad79267c8b8f2e4ca17) | Rotation setting and recovery service/module candidates for investigation | Bundled opaque KeyMint and other vendor libraries, synthetic 2127 patch date, missing-dependency allowance and hard-coded super size cannot establish Android 14–17 decryption or a safe flash profile. |
| [Measured Global stock package](STOCK-LAYOUT.md) | Header v4, separate 100 MiB recovery slots, stock boot/vendor-boot split, super size and AVB chain | Firmware artifact evidence only; not proof of the installed tablet's slot or later Android 14–17 variants. |
| [Measured China stock package](STOCK-LAYOUT-CN.md) | Byte-identical rawprogram map to the pinned Global package; separate boot/recovery/vendor-boot payload sizes and AVB record | Package-level comparison only; no model/SKU or installed-device compatibility claim. |

No donor script, binary, fstab or security workaround is executed or copied into
the Uke profile. The added references are shallow, exact-commit clones under
the ignored `referances/recovery/` directory.

The useful touchscreen hypothesis is specific: module readiness, device-node
permissions and touch HAL startup order may be coupled. The OEM Uke device tree
also contains `xiaomi_touch` nodes, supporting investigation, but not the
donor's hard-coded module list or service behavior. On-device work should
record node creation, module load order, service state and touch events before
changing init. No opaque touch or KeyMint binary will be redistributed.

The current OrangeFox build already packages `bootctl`, `fastbootd` and
`update_engine_sideload`. Once recovery itself boots, read-only diagnostics can
start with `bootctl get-number-slots`, `bootctl get-current-slot`,
`bootctl is-slot-bootable 0`, `bootctl is-slot-bootable 1` and
`bootctl get-snapshot-merge-status`. The exact HAL and return codes still need
on-device validation. Do not use `bootctl set-active-boot-slot` from this
unverified profile.

The [Thanick50 OrangeFox release](https://github.com/Thanick50/ofox_device_xiaomi_uke/releases/tag/latest)
offers a recovery image and ZIP as external comparison artifacts. Their
existence does not validate this project's image, flashing method or ZIP
installer. Community reports also disagree about TWRP flashing on recent
Android 16/17 ROMs; test each exact firmware/ROM pair before declaring
compatibility.

## Acceptance matrix

| Capability | Current source/build state | Device gate before support claim |
|---|---|---|
| Native A/B and fastbootd | A/B partition list, boot-control service and fastbootd are present in the source profile; a local recovery image built | Inspect boot-control HAL on the installed firmware, both slots, slot success/retry flags and rollback after interruption. |
| Seamless Virtual A/B OTA | `update_engine_sideload` is present in the prior local ramdisk; `snapuserd` was not found there | Verify snapshot state, userspace merge availability, COW ownership and interrupted merge on each firmware. Never change slots or wipe metadata while merging. |
| Boot profile selection | No write-capable selector is enabled | Prove bootloader/UEFI chain, active slot, all affected partitions, firmware version, backup and Android return; reject unknown or merging snapshot state. |
| Android 14/15/16/17 decryption | Crypto/FBE switches intentionally disabled | For every installed firmware: match fstab, filesystem, KeyMint/TEE services and wrapped-key implementation, then prove credential unlock without altering userdata. A version number alone is not compatibility. |
| Linux/ESP and Btrfs | Read-only identity/mount planning; ext4/FAT tools in source profile | Verify GPT and block identity on both SKUs. Global stock kernel has no Btrfs driver; a compatible recovery kernel, toolchain and on-device mount tests are required. |
| Four-way rotation and flashlight | Rotation property control is host-tested; flashlight UI disabled | Validate display/touch mapping and persistence. Identify a real Uke LED path before enabling flashlight. |

Android 17 does not automatically make an existing `wrappedkey_v0` device use
the new `wrappedkey` format; the on-disk format and hardware trust path must be
matched to the installed firmware. No password, PIN, token, userdata mapper or
write test is attempted by this project until that trust path is proven.

The first public image release also needs a clean privacy audit, AVB/bootloader
policy, a tested boot route, ZIP installer target review and a model-specific
stock restoration procedure. See [the pre-release gate](PRE-RELEASE.md).
