# Native partition-map checkpoint

1 October 2026. Local unsigned Global OS3.0.303.0.WOZMIXM candidate;
GitHub publication is deferred. This is read-only source, host and emulation
evidence, with no tablet, live storage write or rendered GUI acceptance.

The native map reports all GPT partitions and explicit OEM reserved records,
byte ranges, protected label hints and bounded partition-relative filesystem
signatures. Reservations are not free space. Gaps and aligned starts require
healthy agreeing GPT copies; unhealthy geometry produces no free-space guess.
At most 128 partitions receive two small signature reads totaling 8192 bytes
each. Remaining rows are retained with an explicit unprobed status. Revalidation
checks retained storage identity and both GPT copies around mapping.

Label hints do not prove OS ownership, installed firmware, Android FBE access,
filesystem consistency or write eligibility. Private JSON exports use mode
0600. The CLI and native menu expose the map; graphical rendering remains open.
The expanded Linux partition, home-tree and Btrfs subvolume backup requirements
are recorded in the partition-manager contract and remain under implementation.

| Evidence | Result |
|---|---|
| Root host suite | All 19 policy, archive, privacy and plan checks pass |
| Native suite | Eight CTest executables and the complete native CLI suite pass |
| New fixtures | 512/4096 geometry, relative NTFS/Btrfs/LUKS probes, reservations/gaps, invalid GPT, 132-row probe limit, stale identity, out-of-range reads, private export and unchanged image checks pass |
| Sanitizers | All eight CTest executables and new CLI pass ASan/UBSan with leak detection; vptr remains excluded due to the pinned runtime |
| Android build | Recoveryimage completes in 3:26; map CLI and menu compile for AArch64 |
| Final ramdisk | Actual compressed ramdisk, recursive embedded ZIP scans, privacy/no-Python, XML/tool manifests, staged binary matching and ELF dependency closure pass |
| QEMU | Extracted AArch64 CLI passes the partition-map tests and prior file/GPT/raw/host-stream/stock recovery tests; no physical result |
| Package | Two runs from the same built image have identical hashes; independent binary reproduction remains open |

Local candidate: `artifacts/ure-partition-map-alpha/`.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Recovery IMG | 104857600 | `f917e34088d30230d0983da87120032a23b39e869fbaa22b0da355f3f5fcf13d` |
| Temporary-boot IMG | 100663296 | `3652a9c80de57e826607cdece1c01d25ecc7e2dedafbdf285041806da8d56a3e` |
| Installer ZIP | 32538524 | `a48fdd47e5745eedfca791d17a9b2771af6e000a77e72ec28947b552df81b510` |

Compressed ramdisk: 37284677 bytes, SHA-256
`13d333f099bbb6ab8075e6d166fc5e6e09c5945b189b3f02bca905012cf9a652`.
Extracted AArch64 CLI:
`30c6b31a06e138dc33e13a9439589ace79adc46b59cbf1bae2d008ee02f2c6ed`.
The stock kernel is unchanged, AVB is NONE and the ZIP is unsigned. The
temporary-boot image must never be flashed. Full partition management,
filesystem/data migration, multi-LUN orchestration, complete backup workflows
and hardware acceptance remain unfinished; no roadmap phase is marked complete.
