# Uke OrangeFox device profile

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
