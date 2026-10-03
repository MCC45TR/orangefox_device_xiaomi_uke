# Coordinated six-LUN stock image checkpoint

Date: 3 October 2026. Source parent: `36d01d003fb1fbfed36cb05b030d2f19fef2ed0b`.
Source implementation: `6e8f1d4aeb6b075a5674d1f7deb42a8f8f4196f7`.
Local candidate: `artifacts/ure-stock-job-alpha/`. Nothing was published or
flashed. Earlier sealed candidates were retained.

This checkpoint implements the **regular-image** part of the second ordered
request: stock GPTs and explicitly selected Global OS programming extents across
all six Uke LUNs, managed through one recovery journal. It does not accept live
UFS writes, commercial model/SKU profiles, installed Android trust or physical
stock return. No full roadmap phase is marked complete.

## Implemented behavior

The C++ planner binds six different image identities, measured capacities,
sector geometry and original GPT GUIDs. Stock metadata is derived from the
verified Uke OEM inputs, with no generic template or Nabu identity substitution.
Unknown extra partitions are refused. Known custom OS visibility changes are
reviewed. All unprogrammed bytes remain protected, including early firmware,
calibration, unselected slots and source/destination tails.

The selected payload catalog binds ten OS files by ordinary source SHA-256,
source length, decoded length, encoding and exact destination/LUN. A/B choices
never switch the active slot. Metadata and userdata require a paired explicit
data-reset decision. Sparse DONT_CARE requires an explicit zero policy; a larger
userdata tail remains protected, so this is neither secure erasure nor accepted
Android FBE/boot reconstruction.

Native Android sparse-v1 decoding checks RAW/FILL/DONT_CARE/CRC structure,
boundaries and every declared checksum. Complete decoded-content digests bind
replacement mirrors to the reviewed plan. The range digest is explicitly named
`ure-image-range-sha256-tree-v1`, with domain-separated ordinary SHA-256 leaves
up to 4 MiB; it is not ordinary whole-file SHA-256. Independent byte/CRC oracles
cover holes, allocated zeros, offsets, leaf boundaries and malformed inputs.

Before any original write, private before/after copies of every changed range
are verified. The conservative budget is twice the programmed bytes plus
64 MiB. Reflinks, hole-aware copies and cached zero/repeated leaves reduce
unnecessary host work where supported, without reducing that budget. Application
uses fixed 16 MiB chunks, at most 4096, with 64 KiB I/O buffers. Existing target
writes are skipped only when an independent read equals desired bytes.

Payloads precede backup GPTs across all six LUNs, then primary tables/headers
and MBRs. Durable intent, synchronization and readback accompany each chunk.
All final GPTs and protected ranges are checked before commit. The coordinator
does not claim an atomic six-LUN commit. Recovery verifies all mirrors and
actual bytes, ignores saved progress as authority, permits expected original/
desired mixtures, and refuses unrelated divergence or identity changes.
Rollback retains its direction and restores original changed bytes. Recovery
does not require the OEM directory after the journal is ready.

The native CLI and seven new OrangeFox pages share this engine. GUI review
shows six measured capacities, declared model/SKU, A/B selections, selected
payloads, protected tails, staging budget and warnings. Selection changes clear
confirmation. Resume, rollback and unchanged-staging cancellation require a
separate bound journal inspection. Rendered tablet interaction is untested.

## Verified source measurements

The acquisition script verified the complete Global archive SHA-256
`f811ae6255b7535d32f80548d800487a6494a87ddb4cca592799337fab24cd0d`
and size before extracting a new derived data set. Ten selected OS images and
thirty GPT/XML files were acquired; no OEM scripts were executed and no early
firmware/calibration payloads were acquired. Native pins are independently
compared with [the catalog](../manifests/stock-payloads-global.json).

| Source | Source bytes | Decoded/programming bytes | Boundary |
|---|---:|---:|---|
| dtbo.img | 20971520 | 20971520 | Destination is 25165824 bytes; 4 MiB tail protected |
| metadata.img | 2236644 | 67108864 | Pinned sparse source expanded and independently checked in host fixtures |
| userdata.img | 1289147004 | 48318382080 | Actual OEM source inspection only; 45 GiB is not a tablet-capacity measurement |
| super.img | 8199567300 | 11274289152 | Actual OEM source inspection only; no full super programming acceptance |

Actual userdata inspection found 49 chunks and 46,812,844,032 DONT_CARE bytes,
with logical digest
`74305f9eca1b244e5af18eb2c2bf7b0cec284dd545acf28cd78fdd5dc7b9590a`.
Super inspection found 159 chunks and 3,050,336,256 DONT_CARE bytes, with digest
`bbdddea52b433cc608ce5fb2971fc7a4c3e24f95a22361e9d195bb3ea09e0dd5`.
Both files have zero CRC chunks; all declared checksum fields were checked,
without inventing checksum-chunk coverage. These read-only observations are
distinct from executing a large reset/super job or booting Android.

## Validation stages

| Stage | Result and boundary |
|---|---|
| Project policy | 19 host checks passed; no hardware test |
| Native C++ | All 21 CTest executables passed with warnings treated as errors; final run 184.18 seconds |
| New native fixtures | Independent logical-byte/CRC/OEM XML oracles, two capacity scales, both declared model tags, three pinned raw payloads and complete six-LUN logical rollback digests |
| Interruption | Actual child SIGKILL during boot payload application, torn primary/backup GPT bytes, inspected resume/rollback, failed-staging cancellation and terminal-direction refusal |
| Negative fixtures | Wrong profile/LUN/model, duplicate images/payloads, early firmware, mismatched original backup, wrong inode, altered source, forged extents, missing reset/zero decisions and unrelated byte divergence refused |
| Actual GUI callbacks | Six-LUN summary, one/both-slot boot-set plans, changed selection/journal refusal and full image rollback; stand-ins do not establish rendering |
| Sanitizers | All 21 executables passed pinned ASan/UBSan/leak checks with halt-on-error; final run 417.83 seconds; vptr remains excluded for the documented pinned host runtime limitation |
| Host CLI | Real executable stock source inspection, saved review, 33-region application/readback/inspection/rollback and strict confirmation/options passed; complete baseline CLI gates also run |
| AArch64 build | OrangeFox Android 16 recovery, native CLI/library and GUI compiled successfully with the preserved stock kernel |
| Extracted compressed payload | Header-v4 LZ4 ramdisk extracted; 206 AArch64 ELF objects have dependency closure; two embedded ZIPs pass recursive integrity/privacy/no-Python checks; target binaries/GUI match staging |
| Extracted CLI QEMU user | Real ARM64 stock image review/application/readback/rollback and prior file/GPT/backup/restore/layout/display/boot-codec fixtures passed on host-backed images |
| Package repeat | Two packaging runs agree on all six initially checksummed assets; package repeatability is not independently reproducible compilation |
| Generic stock-job guest reset | Not run for this CLI; both VM records remain null in this candidate |
| Physical acceptance | Not run: Pad 7 / POCO Pad X1 identity, SKU/capacity, firmware, live UFS writes, forced reboot, Android boot, rendering and rollback |

Final native/sanitizer receipts bind input-manifest SHA-256
`a9c0ecb3368d9664141d3c92cd6baaa6106030413ceae6e4713efd085266ef54`.
Host C++ compiler: GCC 16.2.1, Red Hat 16.2.1-2. The pinned host Clang executable
SHA-256 is `55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f`.
The extracted CLI below is the binary exercised by QEMU user. No additional
tablet runtime or project-owned Python was introduced.

The earlier partition candidate has a separate successful persistent generic
ARM64 guest emergency-reboot test against its own exact CLI. That evidence is
retained and is not transferred to this stock binary. Neither emulation record
is UFS, shipping-kernel or physical tablet acceptance.

## Failed trials and corrections

Initial warning-as-error failures were corrected in source: a fixture `write`
helper was renamed to avoid resolving to POSIX `write`; helper name parameters
use value `string_view`; misleading GUI indentation was separated. Compiler
flags were not relaxed. A 20 MiB fixture read exceeded the native API's 4 MiB
bound; the test now uses private 64 KiB streaming and an independent checksum.
The production bound remains intact.

The first CLI checker incorrectly treated stdout as a raw plan. Assertions now
use the established `.data` envelope while saved plan files remain raw JSON.
The first extracted ARM64 stock fixture failed before target creation because
its temporary directory was under the runner's read-only project mount. It now
uses writable disposable `/tmp`; the corrected actual-CLI run passed. Fresh
source-bound native/sanitizer receipts were produced after this script change.
These unsuccessful trials remain retained privately and are documented in
REC-S006. The upstream build's existing `local` outside-function packaging
warning is separate from final extracted-payload validation.

The DTBO whole-partition/file-checksum mismatch in the older live preflight and
installer policy remains open (REC-L006 / REC-S002). Correct handling in this
image coordinator does not authorize or repair that live route.

## Exact artifacts

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Recovery image | 104857600 | `ec13d6a0755a793f5bc64ef37a668c60423a8dfa397cefb5ad7f401b68d2b483` |
| Temporary fastboot-boot image | 100663296 | `05cb1a5a71f3214d28411a30763c938ed0ba8debd500ac5e66023761c57f371e` |
| Flashable ZIP | 33786154 | `9c37286fc002865d295f698c1e0be23be599fb3c70ef334e55a3a6defe4599e5` |
| Compressed shared ramdisk | 38662997 | `eae0f2f001fb4a91d11dd6eb6949de3d92bb0715f524c8b787415b93aac38e2e` |
| Native CLI | 1855120 | `7466424d2d8c6023f428bfaf4a8de584723781661fed89230bb8eca16df7152a` |
| Preserved stock kernel | 35432960 | `97b2c53022e8c0bd0b279c4c592a196126a6eb0a93647c2957b39dcd5f2dc1c9` |

The sealed manifest records its clean Git source base/tree, project inputs,
source archives, native/sanitizer receipts, extracted-payload audit and host
tool identities. `SHA256SUMS` covers every generated public artifact. AVB is
NONE; the ZIP is unsigned. The temporary-boot image must never be flashed.

## Remaining ordered work

The combined userdata-only filesystem/GPT image job from item 1 is retained.
This item 2 checkpoint implements six-LUN image orchestration; exact Pad 7 and
POCO Pad X1 firmware/capacity profiles and physical stock return remain open.
Next is item 3: the common live writer, model/SKU/range provenance and exact
forced-reboot durability, including correcting the DTBO preflight contract.

Items 4–8 remain in the owner's order: filesystem resize/repair/tool packaging;
Android trust/A-B/Virtual-A-B/super/OTA/isolation; real Arch/Fedora rescue and
cancellation; Btrfs kernel/receive/restore/paths/boot; full installed boot asset,
root, Uke DT, module ABI and signature closure. Shipping-kernel Btrfs remains
disabled and FBE remains blocked pending installed KeyMint/TEE trust.

Read [the operation contract](../docs/STOCK-IMAGE-RESTORE.md) and the root
[engineering lessons](../../docs/lessons/2026-10-03-RECOVERY-STOCK-JOBS.md) before
using this checkpoint as evidence for further work.
