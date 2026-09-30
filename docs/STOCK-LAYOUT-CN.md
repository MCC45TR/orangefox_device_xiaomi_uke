# China stock boot layout: Uke platform

Baseline: China `OS3.0.302.0.WOZCNXM` fastboot package. The 9,979,118,103-byte
archive was SHA-256 verified as
`b58bce97a14d2723e2b17031199c9165e89b51ad26044e01f2bd22c6e864dfa7`.
Its bounded extraction and private per-file hashes are retained locally. The
six extracted `rawprogram*.xml` files are byte-identical to those of the
measured Global `OS3.0.303.0.WOZMIXM` package; the read-only Uke inventory
also produced identical entries. This is package evidence, **not** evidence
that either commercial model has that layout on its installed storage.

| Component | Measured China package result |
|---|---|
| Logical UFS sector | 4096 bytes |
| `recovery_a` and `recovery_b` | Separate 100 MiB partitions |
| Stock `recovery.img` | Header v4, no kernel, 28,074,571-byte ramdisk |
| Stock `boot.img` | Header v4, 35,432,960-byte kernel, no ramdisk |
| `init_boot.img` | Header v4, 2,308,992-byte ramdisk |
| `vendor_boot.img` | Header v4, 4096-byte page, 34,052,291-byte ramdisk and 1,885,961-byte DTB |
| `dtbo` | 24 MiB partition |
| `super` | 11,274,289,152-byte rawprogram range |
| `userdata` | Open-ended in rawprogram; capacity must not be guessed |
| AVB | Stock vbmeta flags 0; recovery chain rollback location 1; stock recovery uses SHA256_RSA2048 |

The embedded China stock kernel configuration also has `CONFIG_BTRFS_FS` unset;
the current recovery kernel cannot supply Btrfs mounting for either measured
firmware profile.

The [China boot profile](../manifests/boot-profile-cn.json) and
[China partition inventory](../manifests/stock-layout-cn.json) remain distinct
from the Global profiles even where layout entries match. Ramdisk and DTB
sizes differ. Do not mix China/Global modules, DT overlays, firmware or AVB
material; do not infer an installed device's active slot from either package.

The bounded native-DTC inspection finds four complete concatenated FDT entries
in both profiles: Cliffs (`0x266`), Cliffs 7 (`0x278`), Cliffs7P (`0x283`) and
CliffsP (`0x282`), with `qcom,cliffs` / `qcom,cliffsp` root compatibles. These
are package base trees, not the selected installed-device DTBO/SKU or a mainline
device tree. Derived DTS and per-entry hashes remain in ignored local areas.
`scripts/inspect-stock-dtb.sh` rejects oversized, truncated and unknown-trailer
inputs and uses the host DTC built from the pinned Linux source.
