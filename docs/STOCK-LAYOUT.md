# Global stock boot layout: Uke platform

Baseline: Global `OS3.0.303.0.WOZMIXM` fastboot package. The complete archive was downloaded from the pinned Xiaomi URL, size-checked and SHA-256 verified. The archive checksum, extracted-file checksums and machine-readable partition records are in `manifests/firmware.lock.json`, `manifests/boot-profile-global.json` and `manifests/stock-layout-global.json`. Raw images and full extraction logs remain local under `referances/firmware/global/` and `reports/private/`.

| Component | Measured Global result |
|---|---|
| Logical UFS sector | 4096 bytes |
| LUNs in rawprogram | Six, numbered 0–5 |
| `recovery_a` and `recovery_b` | Separate 25,600-sector partitions: 100 MiB each |
| Stock `recovery.img` | Android boot header v4; no embedded kernel; 28,069,198-byte ramdisk |
| Stock `boot.img` | Android boot header v4; 35,432,960-byte kernel; no ramdisk |
| `init_boot` | Header v4; 2,309,001-byte ramdisk; 8 MiB partition |
| `vendor_boot` | Header v4; 4096-byte page; 34,052,447-byte vendor ramdisk and 1,885,913-byte DTB |
| `dtbo` | 24 MiB partition |
| `super` | 2,752,512 sectors; 11,274,289,152 bytes in the rawprogram map |
| `userdata` | Start sector found, size left open by rawprogram; requires SKU/device evidence |
| AVB | Stock vbmeta flags 0; recovery chain descriptor uses rollback index location 1 |

The stock package programs A-slot images while the B-slot recovery/boot partitions exist. An A/B slot is not a separate user-data store. This source analysis does not identify the installed device's current slot or partition contents.

The Uke donor's recovery size and kernel-exclusion settings match this Global package. Its declared super partition size does **not** match the Global rawprogram size; copying the entire BoardConfig would be unsafe. The separately measured [China package](STOCK-LAYOUT-CN.md) has byte-identical rawprogram maps but different image payload sizes; neither package proves a physical tablet's installed layout or cross-profile firmware compatibility.

`uke-partition-inventory` is a read-only C++ parser using libxml2. It rejects overlapping numeric ranges and arithmetic overflow, and retains symbolic backup-GPT sectors without guessing disk capacity. Its output is an inventory, not a flash plan. It never opens a block device. See `tests/check-inventory.sh` for fixture tests.
