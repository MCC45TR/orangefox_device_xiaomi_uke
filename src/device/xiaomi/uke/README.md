# Uke OrangeFox device profile

The recovery identifies itself as **Xiaomi Pad 7 / POCO Pad X1**, with the
compatibility codename **`uke`**. Both products use the **Snapdragon 7+ Gen 3
Mobile Platform**; Android's technical SoC identifier remains **`SM7675`**.
The long processor name appears separately in About and JSON diagnostics.
These build declarations describe the target family and do not identify the
installed firmware, commercial SKU or physical unit for storage admission.

Product references: [Xiaomi Pad 7 specifications](https://www.mi.com/my/product/xiaomi-pad-7/specs/)
and [POCO Pad X1 specifications](https://www.mi.com/tr/product/poco-pad-x1/specs/).

This is the first source-owned recovery profile for POCO Pad X1 and Xiaomi Pad
7 (`uke`). It is
deliberately limited to properties measured from stock firmware. The profile
builds a separate recovery ramdisk for the stock 100 MiB `recovery_a` and
`recovery_b` partitions using Android boot header v4 and LZ4 compression.

The initial fstab is read-only. Android data decryption, formatting, backup
restore, slot changes and partition writes remain disabled until their firmware
dependencies and physical-device recovery path pass the documented gates. The
donor's synthetic security patch level and conflicting super size are excluded.

`scripts/prepare-build-tree.sh` installs this directory into the ignored Android
build checkout and extracts the kernel from a verified stock package. Binary
firmware and prebuilts are never committed to this repository.

The bundled [Linux/ESP controls](../../../../docs/LINUX-ESP-TOOLS.md) can
inventory prospective partitions and mount only a matching named PARTUUID
read-only. Partition creation, formatting and resizing remain gated.
