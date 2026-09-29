# First local Uke recovery build

29 September 2026 · OrangeFox `fox_16.0` / R12.0 · target `twrp_uke-bp2a-eng`

Resolved 399-project manifest SHA-256: `a070c25b23cf69660ef525608f5d8a93c2df7886c7869c037f09362fcc096f07`. Global source firmware archive SHA-256: `f811ae6255b7535d32f80548d800487a6494a87ddb4cca592799337fab24cd0d`. The stock recovery layout and extraction method are recorded in [STOCK-LAYOUT.md](../docs/STOCK-LAYOUT.md).

The Global `OS3.0.303.0` stock profile and project-owned `device/xiaomi/uke` configuration produced a local OrangeFox recovery image. This is a source/build result, **not** evidence of a successful boot or safe installation on either POCO Pad X1 or Xiaomi Pad 7. No image is released for flashing.

## Build result

`mka recoveryimage -j4` completed successfully after applying [the project packaging patch](../patches/0001-preserve-recovery-vendor-directory.patch). Two earlier packaging attempts failed because the generated recovery root contains a real `vendor` directory while the base build expects a `vendor` symlink. The patch excludes the conflicting base-tree path during ramdisk assembly; it is applied idempotently by `scripts/prepare-build-tree.sh`. The first successful run was incremental after those attempts. A second run from a new, empty `out-clean` directory completed in 41 minutes 15 seconds. This verifies the build from fresh outputs with the same patched checkout; it is not a pristine re-sync or byte-for-byte reproducibility proof. Full logs remain in the ignored private reports directory.

| Local artifact | Size | SHA-256 |
|---|---:|---|
| `recovery.img` / `OrangeFox-R12.0-Unofficial-uke.img` | 104,857,600 bytes | `8b02f293c876476ca4ee36cee8641ba43e78d3a2fe2018c799b5ad0d5f1c55a5` |
| `OrangeFox-R12.0-Unofficial-uke.zip` | 52,441,726 bytes | `55c520b0904d37c15e9f644f2cd61d3aa2ff32a0a196d6dc45b09a2d699a0b99` |

The fresh-output run produced a 104,857,600-byte recovery image (SHA-256 `1ac8fb4edd9f5e93d1477f8f6087a5d0010928dca2b1ddc3347e1a94af2a68da`) and a 52,441,710-byte OrangeFox ZIP (SHA-256 `cd568c1d2b8a72ba3cf29c0105c4eceb7e3a5aaacfeb29c212b8c5b0c7de853e`). Different hashes mean byte-for-byte reproducibility has **not** been established.

Both recovery images use Android boot header version 4 with a zero-byte embedded kernel. The first has a 34,935,728-byte LZ4 ramdisk; the fresh-output run has a 34,935,750-byte ramdisk. This matches the Global stock recovery's separate-kernel structure. Each padded image equals the measured 100 MiB recovery partition; there is no spare partition-size margin. In both runs, ramdisk bytes extracted from the image match the separately generated ramdisk hash. Each ZIP passed `unzip -t`, and its embedded `recovery.img` matches its standalone image hash. The build printed Android 16 / `2025-06-05` image metadata; that date is a build-header value, **not** a claim about the device's security patch state, and must be reviewed before release.

`avbtool verify_image` confirmed each image's embedded recovery hash, but `avbtool info_image` reports AVB algorithm `NONE` on both. This is internal consistency, not an authenticated signature or proof that a locked device will accept either image. Both AVB fingerprints inherit the private build-account identifier.

Each ramdisk archive lists 4,085 entries. No Python interpreter, `pip`, `.py`, `.pyc` or `.pyo` payload was found in the first archive. The workspace's extracted-payload check passed on both staged recovery roots, covering Python shebangs, symlinks and ELF `libpython` dependencies. A `python.nanorc` syntax-highlighting configuration exists in the staged trees; it is not a Python runtime or script. No `.ko` module is embedded in either recovery root. The vendor-boot/module dependency and architecture must be checked against the exact installed firmware before physical testing.

The privacy audit **failed on both runs**: generated recovery properties contain the build account and host identifiers, and generated property/security-context files and several binaries contain absolute build paths. These are not credentials, but they are private build metadata prohibited in public project artifacts. The workspace's `scripts/check-target-privacy.sh` detects these leaks and rejects both payloads. Both images and ZIPs must stay local. Sanitizing and rechecking a later build is a release prerequisite; changing only the public report would not fix the payload.

## Release gates still open

- Repeat the build from a pristine, pinned checkout and review all packaged dependencies, image metadata and licenses. The fresh-output run alone does not close that gate.
- Remove private build identifiers and absolute host paths from the packaged payload, then rerun a full privacy scan on the final image and ZIP.
- Define the recovery AVB/signing and bootloader-state policy; do not treat the unsigned `NONE` footer as an authenticated release signature.
- Review the generated ZIP installer's target-selection, write behavior and signing-key provenance before considering it distributable; ZIP integrity alone is not an installation-safety test.
- Complete China and Turkey boot/layout analysis and compare against the Global configuration. A common `uke` target does not imply interchangeable firmware profiles.
- On each physical model/SKU, identify firmware, slot, panel/touch variant and stock recovery route before any controlled boot test.
- Verify boot, display, input, USB, storage and rollback separately. No userdata operation, partition change or flash is authorized by this report.

The host-only Android build tools may use upstream Python; the tablet payload must not.
