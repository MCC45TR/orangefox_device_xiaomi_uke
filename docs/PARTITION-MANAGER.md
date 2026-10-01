# Comprehensive partition manager and stock-layout restoration

The 1 October scope expansion explicitly requires a comprehensive partition
manager and restoration of the device's default partitions, with bounded memory
and practical large-object I/O. This extends roadmap §8, §17–20, §58 and §70–77;
it does not replace the original 103-topic roadmap. This document records the
implementation and acceptance requirements. It is not a completed manager.

## Current foundation

The shared native library already inspects both GPT copies, CRCs, GUIDs,
partition ranges, overlaps, alignment and protective MBR. Private GPT backups
carry the selected target/profile, raw regions, partition-table JSON and hashes.
Reviewed repair/restore/rollback works on disposable images with durable
journals and actual-byte inspection. Storage identity/ownership observations
and raw local/host-assisted backup/restore provide additional building blocks.
Physical writes, layout migration, multi-LUN orchestration and comprehensive
partition-management pages remain unfinished.

## Device-derived stock inputs

The measured Global `OS3.0.303.0.WOZMIXM` archive has SHA-256
`f811ae6255b7535d32f80548d800487a6494a87ddb4cca592799337fab24cd0d`.
`manifests/stock-layout-global.json` records 138 rawprogram entries over UFS LUNs
0–5, with 4096-byte sectors. These entries include GPT programming regions and
open-ended ranges; they are not 138 interchangeable user partitions.
The matched CN profile is separate even where its rawprogram map agrees.

The archive includes `gpt_main0..5.bin`, `gpt_backup0..5.bin`, `gpt_both0..5.bin`
and `patch0..5.xml`. Stock restoration must derive metadata from these verified
inputs or a verified device-original backup. It must not copy Nabu tables,
assume a 512 GB SKU from a filename, execute OEM/donor flash scripts, or replace
unit-bound manifests with generic offsets.

`scripts/describe-stock-gpt.sh` rechecks the complete OEM archive and all thirty
GPT/rawprogram/patch inputs against the original extraction record, validates
XML and native range inventory, and compares it with the public stock map.
`manifests/stock-gpt-global.json` records exact source-file hashes without private
paths. This catalog is input provenance, not reconstructed device metadata or
permission to restore it.

LUN0's rawprogram leaves `userdata` open-ended at sector 2961408. Its patch
records update the last partition endpoint, last usable LBA, alternate/current
header LBAs, backup-table location, table CRCs and header CRCs from the actual
`NUM_DISK_SECTORS`. Other LUNs also contain capacity-dependent terminal metadata.
A template's fixed header values are not proof of a device's capacity. Native
reconstruction must evaluate only explicitly supported bounded expressions,
recompute CRCs and validate both resulting copies against measured capacity.

## Required manager behavior

| Area | Required behavior and evidence |
|---|---|
| Discovery | Whole-unit/LUN capacity and sector geometry, primary/backup GPT health, type and unique GUIDs, labels, ranges, gaps, alignment, bounded filesystem/encryption probes and explicit unknown OS ownership; source and host fixtures precede physical acceptance |
| Planning | Create/delete/rename/type/attribute/resize/move previews, capacity-derived stock/Android/Fedora/Windows/custom layouts, all affected installations and exact before/after ranges; no guessed free space or filesystem shrink minimum |
| Protection | Preserve stock early firmware, calibration/security ranges, unknown partitions and other ESP files; distinguish metadata-only changes, data migration, filesystem work and boot-policy changes |
| Data operations | Format/check/resize/copy/restore only through reviewed filesystem adapters and mature transaction/backup gates; verify hashes and metadata, expose meaningful cancellation, interruption and rollback semantics |
| Stock restoration | Explicit model/SKU/profile/unit/LUN/sector/capacity match; verified source files and original multi-LUN/boot-chain backup; show every removed/resized partition and affected OS; restore the selected stock layout without claiming user data or calibration was recreated |
| Multi-LUN journal | Durable per-LUN intent, original/desired records, ordered writes/readback, uncertainty and recovery routes; never claim atomic all-LUN commit across interruption |
| UI | A complete partition map, capacity/ownership state, operation queue, warnings tied to affected data, exact plan review and current-byte recovery actions; require rendering/touch acceptance separately |
| Optimization | Bound metadata/read buffers and caches, avoid full-object scans per chunk, copy unchanged ranges efficiently, retain complete boundary verification and truthful progress; no fabricated throughput claims |

Stock partition metadata restoration does not itself restore Android partition
contents. A separate firmware-matched image/boot-chain plan must identify the
payload for every affected partition and preserve the known-good recovery route.
Formatting userdata destroys its contents; it does not unlock wrapped keys or
prove installed KeyMint/TEE trust. Multi-OS layout selection cannot implicitly
change default boot policy.

## Completion evidence

Acceptance needs malformed/overflow/overlap and wrong-unit/profile/SKU tests,
512/4096 synthetic disks, six-LUN stock reconstruction from actual OEM inputs,
capacity variation and open-ended range tests, isolated filesystem/migration
fixtures, actual interrupted operation and recovery tests, extracted AArch64
payload checks, and a rendered operation/review UI. Live acceptance additionally
needs an identified Uke, installed firmware/slot/snapshot/ownership gates,
preserved calibration and stock firmware, and a rehearsed stock-return route.
Current host evidence does not prove these physical results. No full manager,
stock-layout restore contract or roadmap phase is marked complete.
