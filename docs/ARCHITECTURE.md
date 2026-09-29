# Recovery architecture

A C++ management library owns discovery, validation, operation plans and structured results. OrangeFox UI calls that library through a narrow interface. Read-only discovery cannot write partitions, create mappers or unmount filesystems. Host scripts prepare artifacts; the tablet carries no Python.

A future operation plan contains schema version, operation ID, model/SKU, firmware profile, LUN identity, sector size, partition GUIDs/ranges, selected slot, snapshot state, source artifact hashes, backup manifest, expected changes and recovery steps. Validate the same identity immediately before execution. Unknown inputs fail with an actionable explanation. Do not hard-code Nabu offsets or inherit source-time side effects.

UI sections: device status, boot profile, backup/restore, image installation, storage plan, USB modes, logs and settings. Persist project preferences outside calibration storage. Storage-changing controls require an explicit reviewed plan; a displayed feature is not marked working until its acceptance evidence exists.

The first `uke` BoardConfig and read-only recovery fstab are derived from the verified Global stock package. `scripts/prepare-build-tree.sh` stages the device tree and applies `patches/0001-preserve-recovery-vendor-directory.patch` to the active Android build tree. The patch keeps recovery's vendor metadata directory intact while the base ramdisk is copied. The source audit and feature matrix define the remaining implementation work. No partition-writing command is provided by this profile.
