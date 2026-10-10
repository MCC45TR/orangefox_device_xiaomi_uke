# Uke dualboot setup

`partition` is a native alias of `uke-recoveryctl dualboot setup` in the recovery
ramdisk. It provides an interactive preview and explicit application flow.
The new **Extra → Setup Multiboot** wizard has a separate seven-role planner,
sequential selection/review pages and localized consent. See
[the graphical wizard guide](MULTIBOOT-WIZARD.md). This document describes the
earlier five-role shell interface and its distinct backend prerequisites.

All OS allocations come from the **original userdata extent**. Setup preserves
the userdata start, GPT entry and partition GUID, every other partition record
and payload, and the usable disk boundaries. Necessary primary and backup GPT
metadata changes are reviewed separately. Nabu offsets and donor shell scripts
are never used.

| Selection | Physical order within original userdata |
|---|---|
| Linux | userdata → optional ESP → Linux |
| Linux with separate boot | userdata → optional ESP → linux_boot → Linux |
| Windows | userdata → ESP → Windows |
| Linux and Windows | userdata → ESP → Linux → Windows |
| Both with separate Linux boot | userdata → ESP → linux_boot → Linux → Windows |

ESP uses FAT32, separate Linux boot uses ext4, and Windows uses NTFS. Linux
accepts ext4, Btrfs or F2FS; ext4 is the recommended default. Windows requires
an ESP. A Linux-only layout can omit it, provided an existing compatible ESP or
another configured boot route is available. A new ESP defaults to **512 MiB**
and must contain at least **512 MB (512,000,000 bytes)** after alignment.
The optional JSON field `esp_enabled` defaults to true for older requests;
a disabled ESP must not specify a capacity. Sizes accept decimal MB/GB, binary MiB/GiB, and a
percentage of the original userdata capacity. Userdata receives the aligned
remainder. Alignment and filesystem minimums are checked before confirmation.
Creating partitions does not install either OS or configure its bootloader.
New GPT names are `esp`, `linux_boot`, `linux` and `windows`, matching the Linux
installer's physical partition contract. Existing `uke_` names remain readable
and protected; setup never renames or reclaims them merely to match this convention.

## Preview and consent

The shell shows the selected order as a text bar, partition sizes and LBAs,
bounded native operations, actual formatter arguments, and review notes. The
private JSON plan binds the target identity, GPT changes and command list to a
SHA-256 digest. Without an application journal, setup only saves the preview.

Application requires `APPLY`, the full plan digest, and the exact data policy
phrase. Erase mode requires `ERASE USERDATA`. Ending input, cancelling or
providing the wrong consent prevents application. A changed target or GPT
invalidates the plan.

The graphical wizard always uses explicit userdata erase/recreation. It shows
the data-loss warning before planning and requires `ERASE USERDATA` on a
separate confirmation screen before applying the exact reviewed plan. Changing
a checkbox, capacity, unit, filesystem, target or journal invalidates approval.
It does not offer a misleading encrypted-data shrink option. The Linux-only
no-ESP layout still protects every partition outside the original userdata pool.
Device discovery is read-only; host execution requires an explicitly selected
disposable regular image. A blocked device preview explains its prerequisites
and does not display an application button.

## Regular-image execution

Host setup requires an explicit regular image; it never selects a PC disk.

```sh
uke-recoveryctl dualboot setup --image disposable.img --sector-size 4096 \
  --profile fixture --output setup.json --journal new-private-journal
```

This route uses the existing filesystem/GPT transaction engine. It supports
erase/recreate and a separately verified supported unencrypted preserve/shrink
path, with staged filesystems, readback and complete image rollback. The journal
needs up to three original-userdata copies plus 64 MiB. This image rollback
capability must not be confused with the narrower live-device recovery route.

## Live-device implementation and limits

The narrow live backend implements **explicit F2FS userdata erase and recreate**.
It remains physically unaccepted. General OrangeFox format, flash, restore,
installer, slot and OTA mutation routes retain their independent write gate.

The backend requires the current Uke identity, unlocked bootloader, measured
writable 4 KiB UFS LUN 0, matching healthy GPT, authoritative idle A/B and Virtual
A/B state, full visible ownership observations, packaged formatters/checkers,
the installed wrapped-key F2FS policy, and an existing exact `boot-recovery`
command in the protected misc partition. The command is read through a retained
read-only descriptor and checked again before irreversible effects. Setup never
writes misc. An empty or different BCB command blocks application because the
downstream init recovery-reboot path would require a separately denied write.
Default-fstab resolution and alias equivalence still require target validation;
this prerequisite does not establish successful physical reboot.

The existing metadata-encryption map
must already be opened and idle. The measured F2FS metadata partition must
already be mounted read-only with `norecovery`; setup never mounts it or opens,
exports or recreates encryption keys. The current recovery profile cannot pass
these prerequisites: it disables FBE decryption, and its boot-control service
admission rejects the authoritative slot/snapshot queries. The implementation
does not bypass either restriction.

```sh
uke-recoveryctl dualboot preflight setup.json --object LIVE_LUN_ID
uke-recoveryctl dualboot execute setup.json --object LIVE_LUN_ID \
  --journal NEW_EXTERNAL_USB_DIRECTORY --confirm PLAN_SHA256 \
  --data-policy 'ERASE USERDATA'
```

The journal must be private and on external USB storage. Formatters receive
inherited descriptors for exact native DM-linear views, never a whole LUN or
unbounded userdata node. The Android F2FS formatter disables discard and uses
the reviewed Android policy. Protected payloads are hashed before and after
application. GPT regions are synchronized and read back, backup first.

Each formatter and read-only checker receives an inherited descriptor for its
exact bounded mapper view. The wrapper validates exclusive idle probes before
and after the tool, releasing its probe during invocation so the tool's own
exclusive open can succeed. A separate identity descriptor remains open;
mapper identity, geometry, event, flags, access mode and opener count must match
at every boundary. The checker receives a read-only descriptor. Live NTFS
formatting does not use its mounted-volume force override. These checks work
with the cooperating runtime lease and lifecycle quarantine; they do not claim
continuous kernel exclusion against privileged external writers.

Live Uke setup uses the same **512 MB minimum** when a new ESP is selected;
512 MiB remains the default. Formatting explicitly selects one native sector
per cluster; the pinned tool's automatic choice can produce too few FAT32 data
clusters on 4 KiB storage despite returning success. Its FAT32 boot sector,
data-cluster count, FAT capacity and root cluster are checked after formatting. Smaller regular-image
fixtures using 512-byte filesystem sectors do not establish live ESP support.

After the first destructive effect, recovery components are quarantined from
mounting or reusing stale partition nodes. A verified terminal state permits
only the minimal recovery reboot path. Pending or malformed states deny managed
reboots and lifecycle changes. Runtime locks coordinate participating recovery
components; they do not prevent a privileged raw shell from bypassing them.

## Interrupted live operation

GPT restoration repairs any recorded torn metadata region before advancing the
durable active-region marker. Repeated interruption must not make an unresolved
earlier region disappear from the journal's permitted recovery state. Unknown
bytes still stop restoration before writes. This restores partition metadata
only; it cannot reconstruct userdata erased by filesystem creation.

```sh
uke-recoveryctl dualboot device-inspect EXTERNAL_USB_JOURNAL
uke-recoveryctl dualboot device-restore-gpt EXTERNAL_USB_JOURNAL \
  --confirm PLAN_SHA256 \
  --data-policy 'RESTORE GPT ONLY; USERDATA REMAINS ERASED'
```

Live GPT restoration **cannot recover erased userdata**. Filesystem formatting is
never silently replayed. Inspect the durable journal, restore GPT when the exact
identity and ownership checks permit it, and reboot recovery before using any
partition nodes. Forced-restart recovery, encryption reopening and actual
device boot/input remain separate physical acceptance tests.

## Disposable formatter VM

`tests/check-formatter-handoff-vm.sh` exercises the shared claim-handoff helper
with the packaged ARM64 F2FS, ext4, FAT32 and NTFS formatters and read-only
checkers. It creates one regular-file-backed 4 KiB-sector guest disk, checks
every surrounding sentinel byte, verifies checker SHA-256 stability and tests
foreign claims, extra opens, changed identity and mounted-volume refusals.
The earlier 300 MiB standalone FAT32 formatter view also preserves the entire
unused tail. That formatter result predates the new 512 MB setup policy and
does not authorize a smaller ESP through the current wizard. Optional Btrfs
tools are tested only when packaged; their absence is an explicit skip.

```sh
bash tests/check-formatter-handoff-vm.sh GENERIC_ARM64_KERNEL \
  MATCHING_DM_MODULE EXTRACTED_RECOVERY_ROOT MATCHING_SEALED_RECOVERY_IMAGE
```

The runner records and rechecks source, transitive headers, compiler/CRT,
tool, runtime and kernel/module identities. Supplying the sealed image is
required for packed-payload acceptance. An image-free development run is
explicitly recorded as unsealed. The fixture has no GPT writer and does not
exercise production quarantine, encryption or the shipping ownership backend.
Generic guest tool success does not establish live Uke execution or boot.
