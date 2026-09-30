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
  35,432,960-byte stock GKI kernel, and the same 33,978,937-byte recovery ramdisk.
- Recovery SHA-256: `86dd42d807900ccbd134c5101c35419a89f737e4e2aea462562c67115e73b2b4`.
- Temporary-boot SHA-256: `87bca37e57eae1e1779d55f1db401086d5ee9b913c4ba2f378ee0bebd6e9bdbf`.
- ZIP SHA-256: `b4876973d373109994dc984228e955ffadcd15d9f4d98ed2f56e6b1941fb8c1c`;
  ZIP integrity passes and its recovery entry matches the standalone IMG.
- Complete staged ramdisk privacy and Python file/entrypoint/link/dependency
  audits pass. Neutral properties are checked independently. The remaining
  embedded certificate ZIP passes bounded extraction and the same audits;
  generic addon ZIPs were omitted after one failed the nested privacy scan.
- Repeated packaging of the same sealed build inputs produces identical hashes
  for all three assets. Temporary-image AVB salt is fixed from the kernel hash;
  this package-repeat test does not establish independent binary reproduction.
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
It checks the stock boot/init_boot/vendor_boot/dtbo stack on both slots, rather
than relying only on the inactive slot's bootable flag. A mismatched older
inactive firmware is a refusal too. Its checks are deliberately stricter than
a generic recovery flash.
