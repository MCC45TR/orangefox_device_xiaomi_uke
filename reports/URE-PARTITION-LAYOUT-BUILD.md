# Userdata layout designer checkpoint

1 October 2026. Local unsigned Global OS3.0.303.0.WOZMIXM candidate.
GitHub publication remains deferred. This delivers the shared layout planner,
graphical selection/review pages and image GPT transaction software; it is not
a complete filesystem repartitioning job or a physical-device success record.

New ESP, Linux and Windows allocations use only the original userdata extent.
Standard mode preserves userdata start, slot, type and GUID and every other GPT
record. Keyboard input supports GB/GiB/MiB/percent, exact fixed-point decimal
arithmetic, MiB alignment and a disclosed percentage basis. Userdata can receive
remaining space; new OS roles can be disabled with zero. No arbitrary free gap,
existing OS partition or firmware/reservation range becomes allocatable.

Advanced mode permits explicit userdata GUID and selected existing-record
GUID/content requests. Unselected records and non-userdata ranges are preserved.
Before-userdata placement requires explicit erase/recreate and declares Android
data loss. It never claims to preserve encrypted data by changing its offset.
GUI and CLI enforce the same policies. Switching back to standard mode clears
advanced requests. Selections changed after review require a new plan.

The allocation widget is integrated into the actual OrangeFox page loader through
reviewed patch `0008-partition-layout-graph.patch`. The renderer owns its bounded
graph cache, recalculates integer pixel proportions when resized, follows page
conditions and clears invalid previews. Colors and sizes are tested by compiling
the actual widget with host graphics stand-ins; no second renderer algorithm is
substituted. Native Android compilation covers the actual GUI actions and pages.
Rendering/touch on either tablet and the external monitor remains untested.

Reviewed execution is **GPT_METADATA_ONLY** on regular disk images. It retains
original/proposed regions in a private durable journal, writes backup GPT before
primary GPT, verifies current bytes and provides inspected exact rollback.
Changing a GUID does not format contents; a format request remains separate
required payload work. Results explicitly report no filesystem formatting,
no data migration and no complete partition job. Advanced mode never bypasses
the common live-write gate.

| Evidence class | Verified result and limits |
|---|---|
| Root host suite | All 19 checks pass; no hardware operations |
| Native host suite | All 15 CTest executables and complete CLI fixtures pass against exact source receipts |
| Layout fixtures | 512/4096 sector images; integer decimal/binary units and percent overflow; original-userdata bounds, identity preservation, role/filesystem rules, invalid inputs, fixed graph widths, deterministic resolved previews, advanced gating, front erase/recreate, image commit/readback and exact rollback |
| Actual graph widget | Factory registration, proportional role colors, resize invalidation, conditional visibility, invalid/oversize/overlapping graph clearing pass using host graphics stand-ins |
| Sanitizers | All 15 executables and layout CLI fixtures pass ASan/UBSan with leak detection; the pinned runtime requires excluding vptr |
| Android | Recovery image build completes in 4:12; shared layout engine, CLI, GUI actions, custom graph object and XML pages compile for AArch64 |
| Extracted ramdisk | Privacy/no-Python, two recursive ZIP scans, exact GUI/tool manifests, staged binaries and dependency closure for 205 AArch64 ELF files pass |
| Extracted AArch64 QEMU | Actual layout CLI preview/plan, advanced GUID/front-recreate policy, image metadata execution/readback/rollback and prior storage/GPT/stream/tree/display/utility fixtures pass; no filesystem mount or GUI rendering |
| Package | Two packaging runs produce identical hashes for the three assets and accompanying fixed inputs; independent binary reproducibility remains open |

Local candidate: `artifacts/ure-userdata-layout-alpha/`.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Recovery IMG | 104857600 | `c7e5be5163905602af208411d89ef837f962485f482b6fb7a5d29a7588ea71f9` |
| Temporary-boot IMG | 100663296 | `32a7cd12d962bcdce6fc7290b27573b0ae76a7d248770e63602fc7289bf773b5` |
| Installer ZIP | 32768420 | `4c81f024eb3dfbf2b1c881ea440be14906408b13e56ea81679b532a0c279ed84` |

Compressed ramdisk: 37533016 bytes, SHA-256
`6e6fdeb3b015c27b3bc204f1c6f6ba77404a478a569b710118bc282c87370b57`.
Extracted CLI:
`3e94eafa6ecf3a219685723f02282bd8d80bc6a6e6dc5d75d14d13a6efd12620`.
The original stock kernel remains unchanged. AVB is NONE; the ZIP is unsigned.
The temporary-boot image must never be flashed. Preserve the matched stock
recovery, inactive stock slot and documented recovery route.

Next implementation requirements are supported filesystem shrink or an
Android-compatible erase/recreate transaction, formatter/copy/readback adapters,
verified full data backups and interruption recovery, installed Android FBE and
snapshot/merge checks, model/SKU/firmware identity, coordinated six-LUN stock
restoration and separate Pad 7/POCO Pad X1 physical acceptance. Neither a
filesystem choice nor an advanced-mode switch closes these requirements.
The [partition-manager contract](../docs/PARTITION-MANAGER.md) records APIs,
policies and remaining work. [UEFI GPT](https://uefi.org/specs/UEFI/2.10/05_GUID_Partition_Table_Format.html)
defines the metadata format; [AOSP Virtual A/B](https://source.android.com/docs/core/ota/virtual_ab/implement)
describes snapshot/merge checks required around userdata/metadata erasure.
No A/B slot change, super mutation, firmware restoration or physical success
record has been created.
