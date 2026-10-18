# Recovery with Linux in both boot slots

Requirement recorded on **18 October 2026** for Xiaomi Pad 7 / POCO Pad X1
(`uke`): recovery must remain available when both `boot_a` and `boot_b` contain
Linux, including the project's 7.2.10 series. Android installation must not be
a prerequisite for entering recovery.

## Current result

| Item | Evidence | Remaining work |
|---|---|---|
| Existing dedicated recovery | Header v4, no kernel; uses the selected slot's boot stack | Does not meet this requirement |
| Project Linux 7.2.10-20 | Published configuration forces its embedded Fedora initramfs and command line | Cannot execute the existing external OrangeFox ramdisk unchanged |
| Kernel-bearing v4 recovery | Qualcomm reference loader substitutes `init_boot`; recovery ramdisk selection differs | Adding a kernel alone is insufficient |
| Kernel-bearing v3 recovery | Reference AVB recovery branch omits the subsequently required `vendor_boot` | Not selected as the independent candidate |
| Self-contained v2 candidate | Own kernel, module ramdisk, DTB and recovery DTBO | Prototype checks and Uke boot acceptance remain separate |

The installed Uke ABL is not established as byte-equivalent to the reference
loader. Host parser or source-branch tests cannot prove its behavior.

On 18 October, a private 100 MiB v2 prototype passed section readback, reference
ABL parser/overflow controls, AVB structure, payload privacy, no-Python and ELF
dependency checks. It uses historical recovery ramdisk `67ffc810…`, not a fresh
build containing the retained source changes. Its SHA-256 is
`4637e71ea2cf5e8ea26501642e7e36fde8be9c2a1451536b32cc595cbc29924b`.
The prototype is not an installable release.

## Image contract

The candidate carries a pinned recovery kernel and its matching module set,
device tree and overlay. It must not obtain its kernel or first-stage files
from either Linux boot image, `init_boot` or `vendor_boot`. Preserve the original
early firmware and all partition geometry.

The initial prototype uses the existing Global OS3.0.303.0 reference kernel
**6.1.138** and matching stock vendor ramdisk. This means a separate recovery
kernel alongside Linux 7.2.10; it does not port OrangeFox to the native kernel.
The OEM touch provider remains pinned to its different 6.1.175 profile and
unavailable. No module ABI override or automatic storage writer is admitted.

The final prototype preserves the exact raw kernel and compresses only the
ramdisk with Zstd. The kernel's embedded configuration enables that decoder.
This avoids the different compressed-kernel load path found during the first
prototype. OEM loader acceptance and actual memory placement remain untested;
fitting or decompressing the image does not establish a safe boot.

The complete image, including its unsigned development AVB footer, must fit
**104857600 bytes**. Verify the actual sections, transitive payload, module
identity, initialization path, command-line safeguards and kernel decompression.
Compressed size alone is insufficient. Prototype output is private development
material and cannot replace the matching-source release audit.

## Required tablet tests

1. Preserve verified recovery backups and a working bootloader return route.
   Test the candidate through a separately authorized temporary boot first.
2. Confirm the running recovery kernel and build identity, ADB transfer and
   reconnect, storage inventory, display, physical input and clean shutdown.
3. Verify recovery entry with Linux in slot A, then slot B, and finally both
   slots. Test cold key entry and the supported software recovery request.
4. Check return to the intended Linux root and retain the other recovery image
   until its fallback has been tested. A reboot into Linux is not a recovery test.

Both-slot Linux provisioning remains unqualified until those recovery tests
pass. Existing ESP/Linux allocations must not be recreated or formatted.

Reference interpretation follows [Qualcomm BootLinux.c](https://github.com/CodeLinaro-mirror/la_abl_tianocore_edk2/blob/aef4e86045bd151a54d33bab8ae5e8e62462a8f4/QcomModulePkg/Library/BootLib/BootLinux.c),
[VerifiedBoot.c](https://github.com/CodeLinaro-mirror/la_abl_tianocore_edk2/blob/aef4e86045bd151a54d33bab8ae5e8e62462a8f4/QcomModulePkg/Library/avb/VerifiedBoot.c)
and the [AOSP recovery-image specification](https://source.android.com/docs/core/architecture/bootloader/recovery-images).
Source comparison and dated tests are recorded as REC-FB145 in the central
Uke knowledge repository.
