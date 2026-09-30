# Experimental public recovery build

30 September 2026. OrangeFox R12.0 / `fox_16.0` at
`3d733672081bca3af42475a286145f4a8cdce4e7`, 399 project revisions locked in
the resolved Android 16 manifest, target `twrp_uke-bp2a-eng recoveryimage`.
The Global stock303 profile remains separate from China and Turkey inputs.

## Build and package evidence

A new output directory built at the neutral `/mnt` source path with build
identity `uke-builder` / `uke-build` completed in 27 minutes 16 seconds after
an interrupted earlier invocation was resumed. Subsequent incremental builds
added the native installer, source-built Bash staging and licensed font aliases.
This is a new-output build plus reviewed incremental changes, not an independent
pristine re-sync or byte-for-byte reproduction.

The Android compiler produced a static AArch64 `uke-recovery-install` using
`libcrypto_static`; no ELF interpreter or dynamic dependency is present. Host
fixtures exercise both slots, locked/missing/contradictory evidence, all rejected
snapshot states, fallback refusal, malformed hashes, truncated reads, copies
and corrupted read-back. A QEMU invocation with a valid image refused absent
boot-control evidence before any partition access. This is userspace emulation,
not a recovery boot or hardware test.

Final package checks confirm:

- Recovery IMG: 104,857,600 bytes, header v4, zero embedded kernel.
- Temporary-boot IMG: 100,663,296 bytes, header v4, the exact unmodified
  35,432,960-byte stock GKI kernel, and the same 33,978,510-byte recovery ramdisk.
- Recovery SHA-256: `b58c0297a666baf74ae0b68c7bf5f58dbb731ef2e11343f252a7cb1894a48bcd`.
- Temporary-boot SHA-256: `0227fc42238b5a796d67823c00bec439dc7ef0bc433740e72709d2165cacc48a`.
- ZIP SHA-256: `e8e9deb8b3fa21492b8d16e2469ce8271b89d413eefc0bfd707f1f723c0d0547`;
  ZIP integrity passes and its recovery entry matches the standalone IMG.
- Complete staged ramdisk privacy and Python file/entrypoint/link/dependency
  audits pass. Neutral properties are checked independently. The remaining
  embedded certificate ZIP passes bounded extraction and the same audits;
  generic addon ZIPs were omitted after one failed the nested privacy scan.
- Stock GKI source identification matches its embedded official ACK commit;
  the configuration and exact source snapshot are supplied. Upstream Magisk
  utility matches the identified release APK byte-for-byte; recursive dependency
  snapshots and original licenses accompany it. Neither binary was reproduced.
- Font payloads are aliases of Apache-2.0 AOSP static Roboto with its notice;
  unrelated font binaries and generic security-write addon recipes are omitted.

`ARTIFACT-MANIFEST.json`, `PAYLOAD-FILES.tsv`, AVB descriptors, full SHA-256 sums,
source snapshots and the resolved Android manifest accompany the release. The
file inventory is not a complete dependency/license SBOM or security audit.

## Limitations

AVB algorithm is `NONE`, rollback fields are zero, and the ZIP is unsigned;
these are unlocked-bootloader experiments, not OEM-authenticated images.
Touch, display, temporary boot, USB, slot HALs, OTA, installed partition sizes,
POCO/Xiaomi variant compatibility and the stock-return procedure are untested.
Decryption, Btrfs, flashlight and Fedora/UEFI selection remain unavailable.
The generic OrangeFox UI is not universally guarded by the project installer.
No physical device was flashed, formatted, mounted or switched between slots.

The raw upstream ZIP is not published: it writes both recovery slots. The
project ZIP uses the tested native policy and preserves inactive stock recovery.
Its safety checks are deliberately stricter than a generic recovery flash.
