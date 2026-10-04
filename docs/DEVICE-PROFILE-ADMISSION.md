# Exact device profile admission

No live device profile is accepted. `storage profile-status --profile PROFILE`
reports this compiled policy and its stable blockers without opening a block
device. A supplied system root cannot impersonate the current recovery root.
Current Android property observations remain distinct from installed-firmware
proof. Host environment variables, imported records and GUI preferences cannot
populate the accepted registry or enable the live writer.

The owner-provided record `STOCK-ADB-20261003-01` observes POCO Pad X1 model
`25099RP08G`, product `uke_p_global`, hardware SKU `ukepgl`, vendor SKU `cliffs`
and installed Global `OS2.0.205.0.VOZMIXM`. That record did not obtain full UFS
capacities, logical sector widths, original disk/partition GUIDs or complete
primary/backup GPT records. Its mounted `/data` filesystem capacity is not a
whole-LUN measurement. A declared 512 GB configuration is not a byte geometry.
The reviewed Global OS3.0.303.0 and CN OS3.0.302.0 firmware archives are source
profiles, not installed-unit acceptance for that OS2 specimen or another Pad 7.
Pad 7 and POCO Pad X1 require separately reviewed commercial/SKU/capacity
combinations even though both use the Uke codename.

Live preflight now refuses an unaccepted profile before hashing all boot media.
The prior Global package hash comparison cannot set installed-firmware identity
on an unaccepted unit. Accepted unit scope must precede any future native
six-LUN geometry collection and complete boot verification. This also avoids
repeated multi-gigabyte reads when the admission outcome is already refusal.

## Host declaration comparison

```text
uke-recoveryctl storage profile-compare-fixture CONTRACT --observation RECORD
```

This bounded native comparison is a **fixture declaration check**, not raw-data
verification, authenticated provenance, device collection or write admission.
It always reports `live_plan_allowed:false`, `profile_accepted:false` and
`physical_test_record:false`, including fully matching declarations. Android
refuses this command before reading imported paths. It is useful for testing
the exact future profile contract and finding missing or contradictory evidence.

The contract and observation require:

- Exact commercial model, model number, product, hardware/vendor SKU, installed
  firmware version and build fingerprint. A shared `uke` value is insufficient.
- Ordered LUNs 0–5 with exact byte capacity, logical sector width, usable GPT
  range, entry count/size and every partition's index, label, range, type GUID
  and attributes. Missing/extra partitions and changed protected fields fail.
- Nonzero unique unit disk/partition GUIDs, declared matching primary/backup
  GPT copies and both complete-record hashes. A disk GUID cannot double as a
  partition identity. Geometry is bounded, non-overlapping and metadata-safe.
- Separate original GPT backups bound to the same unit, LUN geometry, both GPT
  record hashes and complete original partition GUID set. A foreign unit's
  checksum-valid backup cannot substitute for this identity.
- Both complete `boot`, `init_boot`, `vendor_boot`, `dtbo` and `recovery` stacks.
  Each label maps to its declared LUN/range, exact capacity and whole-partition
  hash. An OEM prefix hash cannot represent a programmed gap or copied footer.
- Declared consistent two-slot state, known idle Virtual A/B state, unlocked
  bootloader and a bootable fallback. These remain observations until native
  collection and a physical fallback rehearsal independently establish them.

The fixture identity validator covers both stock recovery slots. A future custom
active recovery requires a separately authenticated release pin and a verified
stock inactive recovery; skipping an unknown active image is not equivalent to
validating it. GUIDs and complete private geometry belong in local evidence,
never public firmware catalogs or a transferable device profile.

## Acceptance still required

The synthetic comparator does not read GPT bytes or validate their CRCs. It
cannot independently verify imported flags, hashes, property provenance or a
stock fallback. A device backend must collect read-only data from retained,
revalidated kernel descriptors, validate both GPT copies using the native parser
and verify original backup bytes under the unit's scope. It must bind current
boot identity, check mount/snapshot state at the effect boundary and use the
shared persistent operation/lifecycle coordinator. Physical geometry and forced
restart tests must be recorded per commercial/SKU/firmware configuration.

Until those gates are accepted, source packages and fixtures cannot authorize
encrypted userdata shrink, live repartitioning or physical six-LUN restore.
