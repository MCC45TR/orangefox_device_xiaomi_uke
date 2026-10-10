# Private partition backups on the host

`scripts/backup-important-partitions.sh` connects the existing native storage
backup and host receiver. It discovers physical labels from the current Uke
storage graph; each A/B slot is separate and physical Super is selected once.
It never mounts, decrypts, formats or writes a source partition. Only native
plan records are stored in recovery's `/tmp`; image chunks reside on the host.

First review the selection and total capacity:

```sh
bash scripts/backup-important-partitions.sh --list --adb-serial SERIAL
```

The default set includes available Boot, Init Boot, Vendor Boot, Recovery,
DTBO, VBMeta, VBMeta System, Bluetooth, DSP and Modem slots, plus Metadata,
Userdata, Persist, Misc, FRP, Super, FSG, FSC and Modemst1/2. Labels are Uke
inventory evidence, not a transfer of another device's partition layout.
An absent default label is omitted. Explicit selections must all exist uniquely:

```sh
bash scripts/backup-important-partitions.sh --list --adb-serial SERIAL \
  --partition recovery_a --partition recovery_b --partition boot_a --partition boot_b
bash scripts/backup-important-partitions.sh --capture --adb-serial SERIAL \
  --profile FIRMWARE_PROFILE --partition recovery_a --partition recovery_b
```

Without `--output NEW_DIR`, capture creates a mode-0700 directory under ignored
`reports/private/partition-backups/`. Manifests, partition images and calibration
or identity contents are private. Userdata is a full raw encrypted image and may
require hundreds of gigabytes; it does not expose decrypted Android files.
Other physical labels, including early firmware, require explicit `--partition`.

Each native plan includes source identity, exact byte capacity, per-chunk and
full SHA-256, and a sealed plan digest. The existing receiver checks native
manifests, transfer status, byte counts, every chunk and the full stored digest.
The top-level manifest records each verified source size and digest. A failed
partition leaves the whole set `PARTIAL`; no success is inferred from the menu.

```sh
bash scripts/backup-important-partitions.sh --resume PRIVATE_DIR --adb-serial SERIAL
bash scripts/backup-important-partitions.sh --verify PRIVATE_DIR
```

Resume uses retained verified chunks and the original native plans. Do not
reboot recovery or remove its `/tmp` plans before resuming. An interrupted plan
creation can require a fresh output directory; the tool does not overwrite an
existing remote plan. Offline verification requires the existing host
`build/ure-host/uke-recoveryctl`, or an explicit `--host-cli PATH`.

ADB uses a serial-bound `shell -T -e none` connection with `shell_v2`, preserving
binary stdout, separate stderr and the remote exit code. This is the same
binary host transfer purpose often served by `exec-out`, with an explicit
remote failure channel. Every argument is quoted independently. Planning and
receiving have a 12-hour process deadline; partial files remain inspectable.

Native unit/LUN/boot identity, healthy GPT, usage and exclusive read-only block
claim checks remain mandatory. Mounted Metadata/Persist or mapped Super can
refuse capture. The tool does not unmount anything or bypass these checks.
Backup verification is neither an atomic snapshot nor permission to restore;
live restore, wipe, flash and Android decryption remain separately unavailable.

Recovery's native checkbox list may expose the explicit-slot selections through
the existing theme. A host-backup information action should direct the user to
this workflow; it must not suggest that the legacy Backup swipe has a writable
destination or that read-only backup selections enable Wipe/Restore.

Validation: mocked serial-bound discovery, quoting and refused-plan/partial-set
controls passed. Live read-only discovery found 30 unique physical partitions.
Complete orchestration capture, physical Uke transfer and restoration are not
validated; the existing native backup/receiver tests have separate evidence.
