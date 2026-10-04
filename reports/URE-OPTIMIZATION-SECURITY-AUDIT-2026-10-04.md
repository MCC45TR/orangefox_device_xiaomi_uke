# Recovery optimization and safety audit — 4 October 2026

The reviewed recovery remains an experimental, partially implemented candidate.
It is not ready for unrestricted device writes. The most consequential finding
is that stock OrangeFox formatting can reach a live partition without the URE
write policy. Read-only fstab entries and disabled decryption do not close that
path. A malformed-text decoder defect was also reproduced with AddressSanitizer.

**Superseding AUD-001 remediation:** the new [shared native policy](../docs/RECOVERY-WRITE-POLICY.md)
refuses managed legacy writers before their side effects. Its 111-entry census,
27 native tests, 27 sanitizer tests and 21 focused generic-guest requests passed;
both complete disposable media hashes were unchanged. The review also corrected
fastbootd's readonly partition-size caller. This update applies to the new source
and payload, not previously sealed candidates. Broad package acceptance and
physical device writes remain unavailable. The original findings below retain
their historical evidence; the P1/P2 follow-up is separate.

This report lists 37 findings and follow-up requirements. They are not 37
reproduced failures: each entry identifies its evidence class. Performance gains
are proposals until a controlled benchmark establishes them. No safety check,
checksum or durable write should be removed solely to improve a timing result.

## Scope and method

- Parent baseline: `c8938d1c0b9431177b872b03d512f951d954f79c`.
- Recovery baseline: `6b5f3adfc44c2f46f132ef55670791f6cd010ab0`.
- The current localization and capacity-checker work is uncommitted and unbuilt.
  Findings about that work are explicitly marked **draft**.
- Three specialist agent reviews were started for storage safety, performance,
  and hardware/release policy. Their partial findings were consolidated and
  checked against source by the primary reviewer. This is not a claim that all
  three independent reviews completed or that every upstream line was audited.
- Methods: targeted source/call-chain inspection, review of existing sealed
  receipts, two small host ASan/UBSan probes, font character-map inspection, and
  read-only image/header/profile capacity checks. No new full build or VM run
  was performed for this audit. The interactive emulator was closed at the
  owner's request; no QEMU process was present at the final inspection.
- No tablet, host block device, credentials, security keys or private calibration
  were accessed or modified. Probes and raw logs remain in ignored build storage.
- All source line references describe this checkout. Upstream references refer
  to its pinned, patched build tree, not an arbitrary latest upstream revision.
  `src/device/xiaomi/uke/` is abbreviated as `device/` below; native-library
  paths are relative to `device/recoveryctl/libuke/`.

**P0** blocks enabling device operations. **P1** requires resolution or an
explicitly unavailable capability before the affected device workflow is
accepted. **P2** is a reliability, performance, test or usability improvement.
These are engineering priorities, not CVSS scores.

## Storage, recovery and text safety

### AUD-001 — P0: stock formatting bypasses the URE mutation policy

**Confirmed source defect.** `partition.cpp:1262–1264` enables wiping for a
filesystem partition. `Wipe():1870–1878` checks `Can_Be_Wiped`, not the URE gate
or read-only mount policy. `gui/action.cpp:1578–1601`, the `formatdata_confirm`
page, `partitionmanager.cpp:1959` and `Wipe_Encryption():2294` form a source
call chain into formatting `/data`. There are existing stock reboot/merge
checks; they do not establish the installed-profile URE trust contract.
`device/recovery/root/system/etc/recovery.fstab:13–16` marks data, metadata and
persist read-only, while `BoardConfig.mk:88–92` incorrectly implies that the
crypto flags disable destructive data operations. `FEATURE-PARITY.md` also
overstates the shared preflight coverage. No real format was attempted.

**Change:** put the common policy at native mutation sinks and enumerate stock
GUI, ORS, ZIP/install, fastbootd, logical-partition and CLI paths. Keep normal
mode restricted to the original userdata allocation; require separately
reviewed advanced-mode authorization for protected partitions. Unknown profile,
ownership, slot or snapshot state must refuse. Correct the two misleading claims.
**Acceptance:** exercise stock Format Data and every other identified entry
against refused fixtures; independently verify unchanged target bytes. Do not
infer that every enabled writer is exploitable from a build flag alone.

### AUD-002 — P1: malformed UTF-8 reads beyond the string allocation

**Reproduced host defect.** `minuitwrp/truetype.cpp:42–76` decodes up to six-byte
sequences without remaining-length or continuation checks. Its renderer calls
this function while advancing an unbounded C-string pointer. An unchanged
extraction of that function, given a two-byte allocation `{0xf8,0x00}`, reports
`heap-buffer-overflow`, a one-byte read, under pinned Clang ASan. This proves the
decoder defect, not a tablet crash or an exploitable code-execution chain.

**Change:** a bounded decoder, valid Unicode scalar checks, explicit replacement
or rejection of malformed bytes, and an end pointer throughout the rendering
loop. Preserve original filename bytes for file operations. **Acceptance:**
truncated sequences, overlong forms, surrogates, invalid continuation bytes and
valid multilingual filenames, with sanitizer and renderer tests. Unicode's
[UTF guidance](https://www.unicode.org/faq/utf_bom.html) supports treating
ill-formed input as an error or replacing it rather than decoding it as a character.

### AUD-003 — P1: text raster allocation is not checked or globally bounded

**Confirmed source defect; exhaustion not triggered.** `truetype.cpp:354` sizes
an index allocation from all input bytes; `:395–396` multiplies integer width
and height, calls `malloc`, then calls `memset` without checking its result.
Long text or allocation failure can terminate recovery; integer overflow needs
its own reachability test rather than an assumed exploit.

**Change:** checked size arithmetic, pixel/character budgets, allocation failure
handling, clipped drawing and a bounded cache. **Acceptance:** oversized names,
multiple scales, injected allocation failures and ASan/UBSan. Keep errors visible
without attempting a second large allocation to render the error.

### AUD-004 — P1: installer backup is volatile across forced restart

**Confirmed source limitation.** `device/recoveryctl/installer.cpp:198–215`
verifies a 100 MiB old-image backup in `/tmp`, announces that it is volatile,
then writes the active recovery. A readback failure returns an error; there is
no persistent installer journal, host-transfer acknowledgement or automatic
rollback. A verified inactive stock fallback is a useful mitigation, not proof
that its recovery route works after interruption on either model.

**Change:** establish a durable backup and receipt before writing, with a
reviewed recovery route and explicit uncertain/partial-write state. Prefer a
host acknowledgement when local trusted storage is unavailable. **Acceptance:**
forced restart before, during and after the write on an isolated model of both
slots; separately rehearse the physical fallback before any device deployment.

### AUD-005 — P1: mutations need one ownership and lifetime boundary

**Source gap and hardening requirement.** The installer rechecks block identity
but opens its output at `installer.cpp:208` without a common exclusive job
ownership mechanism. URE image jobs have locks and reject loop aliases; these
controls do not cover independently invoked stock recovery writers. A root
shell can also operate outside a UI mutex.

**Change:** one transaction coordinator for target identity, mount/snapshot
ownership, active-job locks and reboot/unmount lifetime. Use exclusive block
opens where supported; retain revalidation and explain their limits. **Acceptance:**
concurrent CLI/GUI jobs, mount and loop-alias races, target replacement and reboot
requests during writes. An in-process lock alone is insufficient.

### AUD-006 — P1: the owner's installed profile and physical geometry are open

**Known validation gap.** The read-only stock inventory establishes names and
Android observations, not all six LUN GPT extents or recovery block sizes.
Locked-stock reads denied that geometry. The observed installed OS2 profile
differs from the candidate's reviewed Global OS3 inputs; Pad 7 and POCO Pad X1
still need separate SKU/capacity acceptance. A common Uke codename is not enough.

**Change:** match model/SKU, firmware, logical sector size, disk GUIDs, complete
partition ranges and both boot stacks before accepting a live plan. Keep unknown
combinations unavailable. **Acceptance:** independent read-only geometry and
whole-partition evidence for each supported configuration; never borrow Nabu
offsets or another unit's identity-specific backup.

### AUD-007 — P1: DTBO prefix restore is not full boot-layout restoration

**Documented limitation, independently rechecked.**
`docs/STOCK-BOOT-PREFLIGHT.md:13–70` explains a 20 MiB OEM DTBO image inside a
24 MiB physical partition. Canonical boot validation includes a zero gap and
duplicated end footer. The stock job restores the approved prefix and preserves
the existing tail. If that tail is already wrong, prefix completion cannot
establish a canonical boot-ready partition.

**Change:** distinguish restored payload ranges from boot-ready verification;
add an exact-profile whole-partition restoration policy only after review.
**Acceptance:** damaged gap, missing/end-shifted footer, good untouched tail and
wrong-profile cases. Do not indiscriminately erase preserved partition tails.

### AUD-008 — P2: partial-write eligibility ignores the recorded write frontier

**Confirmed policy breadth; no final-content corruption reproduced.**
`partition_job.cpp:267–285` and `stock_job.cpp:298–321` accept bytes individually
matching either the original or desired byte across any planned chunk. The
journal records an active chunk and verified frontier, but inspection does not
restrict mixed content to the in-flight region. Intentional torn-write handling
can therefore classify unrelated old/new mixtures as an owned interrupted write.
Final resume and rollback still verify complete desired/original bytes.

**Change:** validate direction, state, ordered frontier and the exact in-flight
chunk; document the concurrent privileged-writer threat. **Acceptance:** two
nonadjacent mixed chunks, impossible ordering, foreign bytes, rollback interruption
and legitimate interrupted writes. Preserve the existing full readback guarantees.

### AUD-009 — P1: safe image repartitioning is not a completed live feature

**Capability gap, with correct refusal.** `partition_job.cpp:79` and
`stock_job.cpp:69` refuse live block targets. Combined userdata jobs are bounded
to 32 MiB–512 GiB and reject fscrypt/encryption for preservation at
`partition_job.cpp:330–340`. The existing filesystem/GPT and restart tests are
valuable, but cannot establish encrypted Android userdata shrink or coordinated
six-LUN device restore.

**Change:** retain those refusals while implementing the installed-firmware
trust, real-device writer and geometry contracts. Preserve existing ESP bytes;
before-userdata placement must keep its explicit erase/recreate implications.
**Acceptance:** populated data preservation, agreed advanced-mode erase/recreate,
ESP preservation, forced-restart recovery and wrong-profile refusal. No FBE
credential use, mapper creation or mount before KeyMint/TEE trust is established.

### AUD-010 — P1: filesystem acceptance is too narrow for real user data

**Validation gap.** Existing guest results cover five formats, clean repair,
empty ext4/F2FS/NTFS shrink and complete original-image rollback. They do not
cover heavily populated shrink, fragmentation, genuine damage, quota/label/xattr
combinations or encrypted data. `reports/URE-FUNCTION-VM-REVIEW.md` states the
fixture scope; it should remain the limit of the claim.

**Change:** build populated, fragmented and damaged regular-file fixtures with
independent metadata/content oracles. **Acceptance:** shrink around the minimum,
one allocation beyond it, partial tool failure, wrong filesystem identification,
space exhaustion, 512/4096-byte geometry and byte-identical rollback.

### AUD-011 — P2: FAT/exFAT resize and NTFS repair remain limited

**Known capability gaps.** Packaged tools refuse unsupported FAT/exFAT resize;
`ntfsfix` is a bounded repair aid, not proof of complete Windows filesystem
repair. These refusals are preferable to a misleading successful button.

**Change:** expose exact tool capabilities and repair limits; implement a
reviewed resizer only with provenance, metadata preservation and failure tests.
**Acceptance:** full FAT/exFAT directory and NTFS metadata fixtures, interrupted
resize and documented Windows-side validation. Do not equate a zero tool exit
with a complete repair of every corruption class.

## Runtime, throughput and host resource control

### AUD-012 — P1: managed chroot has isolation but no resource envelope

**Confirmed source gap.** `rescue.cpp:166–201,274–339` provides private namespaces,
reduced capabilities, a bounded console, process supervision and timeout cleanup.
It does not impose a per-session memory, PID or CPU budget. The 512 MiB `/tmp`
tmpfs also consumes the recovery environment's memory. A selected installed
program can exhaust RAM or processes before its timeout.

**Change:** use available cgroup controllers with a GUI memory reserve and
bounded job limits; add appropriate rlimit fallback and explicit unsupported
states when enforcement is absent. **Acceptance:** bounded allocation/fork stress,
timeout, cancel and GUI responsiveness without a global OOM. Kernel documentation
defines [memory and PID controls](https://docs.kernel.org/admin-guide/cgroup-v2.html);
controller availability must still be verified in the shipping kernel.

### AUD-013 — P1: long GUI jobs hold the management mutex

**Confirmed source bottleneck; input latency not measured.**
`device/ure-gui.cpp:469–470` locks all of `uremanager`. Filesystem execution at
`:606–608`, Btrfs send at `:653–655` and other synchronous jobs can retain that
lock for their whole duration. A second management status/cancel request cannot
obtain it. This does not prove that every frame stops rendering.

**Change:** an owned job executor, immutable reviewed inputs, short state locks,
bounded progress publication and cancellation at safe durable boundaries.
**Acceptance:** time status and cancel acknowledgement during large backup,
hashing, resize and rollback; report noninterruptible I/O honestly.

### AUD-014 — P1: detached maintenance has incomplete global lifetime coverage

**Source/lifetime concern; no use-after-free reproduced.** Scrub/balance uses a
detached worker at `ure-gui.cpp:637–642` and `maintenance_running` guards other
URE commands. This does not establish ownership of all stock unmount/reboot
actions or an orderly application shutdown.

**Change:** a joinable supervised worker tied to filesystem ownership and the
global job registry, with explicit terminal and cleanup states. **Acceptance:**
leave the page, switch root, unmount, request reboot, cancel and handle a worker
exception while maintenance is running. Keep controller cancellation available.

**Superseding source/host remediation, 4 October:** detached maintenance was
removed in AUD-013. The [runtime job registry and teardown contract](../docs/GUI-JOB-EXECUTION.md)
now retain shared activity ownership before queueing GUI work and exclusive
ownership through stock recovery/fastbootd lifecycle effects. The dependency-free
shipping header uses a compiled private registry path, exact descriptor identity
checks and bounded local registrations. A separate joinable controller captures
the original root, plan, journal and job; changing the GUI selection cannot
retarget rescue cancellation or Btrfs scrub/balance controls. Pause and errors
retain durable ownership until an explicit inactive-backend cleanup check passes.
Application teardown ends admission and joins controllers and supervisors before
disposing application resources.

Eighteen focused native tests passed in 90.61 seconds and the same eighteen
ASan/UBSan tests passed in 160.54 seconds against frozen input manifest
`4f1ee855ee61042e0b219fc42da1084d0499b4d6ba44a42f02fcd17811d91622`.
They cover independent-process exclusion, unsafe/replaced registry paths,
production lifecycle refusal, page/root changes, exact cancellation, pause,
backend exceptions, verified cleanup and existing management/display regressions.
The independently compiled English-only GUI publication source also passed the
actual rescue and Btrfs controller fixtures. Host namespace/resource/ioctl
stand-ins are explicit: no real kernel maintenance, fresh Android image,
combined guest, localization, visual or physical acceptance is claimed.

### AUD-015 — P1: directory enumeration is not bounded by one memory budget

**Confirmed allocation structure; worst-case RSS not measured.**
`tree.cpp:159–170` materializes up to 100,000 names in a sorted vector. Recursive
walking at `:355–370` retains ancestor vectors; the depth bound is 64, and the
global entry limit is applied after enumeration. A directory limit is not a
bound on total retained name bytes across the recursion stack.

**Change:** global byte/entry budgets before materialization, iterative traversal
and a disk-backed or paged deterministic listing for large directories.
**Acceptance:** wide/deep adversarial trees, very long names and RSS accounting;
refusal must release resources and preserve partial-journal recovery.

### AUD-016 — P2: hardlinks and sparse files cause avoidable data reads

**Confirmed repeated work.** `tree.cpp:359` hashes before hardlink de-duplication
at `:363`. Multiple links reread the same inode. `content_hash():49–55` reads
logical file contents including sparse zero ranges.

**Change:** a bounded inode/content cache tied to mount identity, size and change
checks; investigate extent-aware hashing while preserving the exact logical-byte
digest. **Acceptance:** many hardlinks, large sparse files, changed source and
same inode number on another filesystem. Measure bytes read, CPU and elapsed
time; do not silently change the manifest's integrity semantics.

### AUD-017 — P2: manifest page accounting repeatedly serializes prior records

**Confirmed repeated work.** `tree.cpp:368` serializes the current record array
to determine page space repeatedly. The 4 MiB/256-record bounds limit exposure,
but accumulated serialization within a page is unnecessary work.

**Change:** maintain exact encoded byte counts and serialize each record once;
avoid new duplicate large strings. **Acceptance:** byte-identical manifests,
correct comma/escaping accounting and unchanged page boundaries, with allocation
and CPU measurements for small-file workloads.

### AUD-018 — P2: a successful Btrfs send scans the stream three times

**Confirmed source work amplification.** `btrfs_backup.cpp:375,389,393` each runs
a full CRC/structure/SHA verification on the normal new-send path. Resume and
unknown file changes need their verification; same-inode publication under
ownership can avoid redundant scans.

**Change:** keep one verified seal attached to the owned file descriptor and
validated inode; recheck after interruption or loss of ownership. **Acceptance:**
count read bytes for normal/resumed publication, tamper between seal/publication
and checksum failures. Maintain fsync and no-replacement publication semantics.

### AUD-019 — P2: Btrfs command validation repeatedly allocates constant tables

**Confirmed hot-path allocation.** `btrfs_backup.cpp:306–307` constructs 22
vectors of required attributes inside every send-command iteration. A compact
constant table can provide the same validation without repeated heap allocation.

**Change:** static constexpr arrays/spans and unchanged attribute checks.
**Acceptance:** compare results for all command types, malformed TLVs and
incremental clone rules; measure allocations and command-validation CPU.

### AUD-020 — P2: monitor conversion and blocking KMS run on the drawing path

**Confirmed structure; device cost unmeasured.** `display-mirror.cpp:117–122`
uses blocking atomic commits; `:320–324` converts and commits from the update
path. `display-mirror-layout.cpp:46–68` clears the destination, rebuilds column
mapping and copies/converts pixels in CPU loops. Full QHD at approximately 30
updates/second would touch about 110.6 million destination pixels/second before
extra clearing and reads; that is arithmetic, not measured tablet bandwidth.

**Change:** separate worker and damage generations, cache mapping/letterbox
regions, then evaluate hardware plane scaling and event-driven commits. Keep
scanout buffer ownership correct until completion; changing a flag alone is
not a safe asynchronous design. **Acceptance:** hotplug, failed commits, mode
changes, p95 draw latency, CPU and frame drops with the actual dock. The kernel's
[KMS documentation](https://docs.kernel.org/gpu/drm-kms.html) describes atomic
state and asynchronous commit completion requirements.

### AUD-021 — P2: 75 Hz output selection does not mean 75 fps mirroring

**Confirmed cadence limitation.** `display-mirror.cpp:318` throttles updates by
34 ms, approximately 29.4 updates/second, independently of the selected output
mode. EDID/mode acceptance is separate from rendered frame cadence.

**Change:** label these two quantities correctly; use measured, configurable
cadence or dirty-frame updates with a CPU/thermal budget. **Acceptance:** actual
2560×1440@75 link, repeated static frames, input changes, fallback modes and
render latency. Advertising on the hub is not link-negotiation evidence.

### AUD-022 — P1: the host build budget leaves no reserve on a 16 GiB machine

**Confirmed policy issue, not a diagnosis of the owner's earlier OOM.**
`scripts/with-host-budget.sh:20–23` allows 14G high/16G max memory, a 12 GiB Go
heap target and 16 parallel jobs. A 16 GiB target computer also needs memory for
the desktop, agent, kernel and unrelated processes. Earlier small VM peaks do
not establish full-build safety.

**Change:** derive the job ceiling from available memory and ancestor cgroup
limits, reserve desktop/kernel memory, and adjust Soong and compile parallelism
separately. Keep `j16` as a maximum when the envelope permits it; measure ccache
hits instead of assuming they eliminate fresh-build memory pressure.
**Acceptance:** clean and warm builds with RSS/PSI/OOM receipts and a responsive
desktop. [cgroup memory documentation](https://docs.kernel.org/admin-guide/cgroup-v2.html)
distinguishes reclaim throttling at `memory.high` from the hard-limit behavior
of `memory.max`; neither is a substitute for system headroom.

### AUD-023 — P1: nested build isolation replaces disk `/tmp` with tmpfs

**Confirmed source contradiction.** The outer budget runner binds its disk
scratch directory to `/tmp`; `scripts/build-public.sh:29` creates a new tmpfs
over `/tmp` in the inner bubblewrap. Build temporary files can therefore consume
RAM despite the outer runner's stated disk-scratch policy.

**Change:** pass a dedicated disk-backed temporary directory through both
namespaces and constrain tmpfs use to small explicit runtime needs.
**Acceptance:** inspect the inner mount, write a bounded test temporary file,
and account its disk and memory effect before a clean build.

## Release, localization and acceptance quality

### AUD-024 — P1: compile attestation is weaker than payload attestation

**Confirmed gate gap; no historical misbuild established.**
`scripts/describe-prerelease.sh:254` sets `compile:true`. Existing source/marker,
payload extraction, dependency and VM receipts are useful but do not consume
an immutable build-completion receipt mapping all build inputs to the output
ELFs. Matching staged source at audit time does not prove that an older ELF was
rebuilt from it. `scripts/audit-recovery-image.sh:42–47` is not that proof.

**Change:** seal toolchain, configuration, patch/source inputs and output hashes
immediately after build; require the extracted shipping payload to match it.
**Acceptance:** change a header without rebuilding, reuse a stale ELF, change a
toolchain or source pin, and ensure publication refuses all mismatches.

### AUD-025 — P1: stronger release gates depend on candidate names

**Confirmed policy gap.** `describe-prerelease.sh:10–11` enables the special VM
requirements only for `ure-vm-review-alpha` and `ure-function-vm-alpha`. A new
candidate name does not automatically inherit that acceptance policy.

**Change:** explicit release classes with capability-driven required receipts;
names identify artifacts, not their safety requirements. **Acceptance:** publish
the same requested capability set under a new name with a missing receipt and
verify refusal. Keep sealed existing candidates immutable.

### AUD-026 — P1: the new localization inputs are absent from native receipts

**Confirmed draft integration gap.** `scripts/native-inputs.sh` explicitly lists
GUI files and walks native headers, but not the new GUI localization headers,
generator, font or generated language closure. Updating them can leave a native
receipt that does not describe all relevant reviewed inputs.

**Change:** bind generated-input provenance and dependency closure into build,
host, visual, payload and publication receipts. **Acceptance:** mutate each new
header/font/language input and verify receipt invalidation. Do not apply older
baseline passing receipts to the unbuilt draft.

### AUD-027 — P2: repeated packaging is not full build reproducibility

**Known, correctly disclosed limitation.** The manifest currently distinguishes
`package_repeat:true` from `binary_reproducibility:false`. Keep that distinction.

**Change:** two clean neutral-path builds with fixed input pins, environment,
timestamps and configuration; compare uncompressed payload and final artifacts.
**Acceptance:** explain any ELF/archive/image differences and publish the exact
comparison. Also validate the dependency/license and no-tablet-Python closure.

### AUD-028 — P2: GUI smoke receipts record requested framebuffer dimensions

**Confirmed evidence defect.** `tests/check-gui-vm.sh:107–134` passes GPU size
arguments, checks process/page status and records the requested width/height.
It does not independently measure the resulting framebuffer. The earlier
interactive trial selected 640×480 despite requested native dimensions; a
corrected private session verified 3200×2136 using fbdev and QMP. The smoke
receipt correctly says `visual_review:false`, which must not be promoted to
visual acceptance.

**Change:** record actual framebuffer and screenshot dimensions, required page,
resource/glyph errors, hit coordinates and observed scale. **Acceptance:** force
a mismatch and missing page; both must refuse. Capture portrait and landscape.

### AUD-029 — P1: language files do not establish usable multilingual rendering

**Confirmed packaging coverage gap.** `prepare-public-ramdisk.sh:82–90` copies
the same static Roboto font over every TTF resource alias. The staged font's
character map lacks Arabic, Hebrew, Indic, Thai and CJK blocks. The renderer
looks up one face at `truetype.cpp:362` and does not establish script shaping
or bidirectional layout. The new 15,628,896-byte CJK face is a draft input, not
a validated packaged solution for all 32 languages.

**Change:** licensed, source-pinned fallback coverage plus shaping/RTL where
required; bound caches and retain combining/script tables when subsetting.
**Acceptance:** all 32 actual language resources, missing-glyph detection,
Arabic/Hebrew order, Indic/Thai shaping, CJK, labels and destructive warnings at
every scale/orientation. Measure font loading RSS and image size after packaging.

### AUD-030 — P1: draft localization key generation uses freed JSON storage

**Reproduced draft defect, not a shipped regression.**
`src/localization/catalog.cpp:342–344` iterates
`parse(read(catalog_path))["strings"]` without keeping the owning JSON value
alive. Pinned Clang C++20 ASan reports `heap-use-after-free` for a one-entry
catalog. The current generated header contains only the `Extra` alias despite
the 2,664-entry custom catalog; no localization-completion claim is justified.

**Change:** keep the parsed owner alive for the full iteration and validate
generated key count/order/collisions. **Acceptance:** empty, one-entry and full
catalogs under sanitizers, exact key closure, and real callback/rendering builds.

### AUD-031 — P1: draft translation materialization needs stronger provenance

**Confirmed validation gap.** `catalog.cpp:377–387` imports `validated.json`
without rebinding its job hash, item source and target context to the current
catalog. Child-job merging checks a child receipt against its own job hash,
not a complete parent/child source correspondence. Placeholder/key checks are
helpful but cannot establish language identity or semantic accuracy, particularly
for destructive-operation warnings.

**Change:** regenerate expected jobs, reject stale/unexpected keys and locale
targets, bind source/context/parent identity, and review safety-critical wording
with competent speakers. **Acceptance:** stale translations after source edits,
foreign child jobs, unchanged masked tokens with wrong meaning, regional
Portuguese distinctions and language-switch plan invalidation rules.

### AUD-032 — P2: draft translation generation has excessive failure fan-out

**Confirmed retry structure and failed trial.** `translate-locales.sh:17–33`
retries a large request three times, then falls back to individual messages.
The current generation has not completed; a marker-validation failure can
multiply host network requests substantially. This optional host tool is not
a tablet runtime or build dependency. `ure-localization.hpp:46` also maps
`erase_recreate`, while actual callbacks store `recreate` at `ure-gui.cpp:856`.

**Change:** adaptive splitting, a global request/time budget, bounded retries,
validated caching/offline imports and exact machine-enum coverage. Restrict
translation to display text: the shared `gui_parse_text` parser also expands
action arguments and list values, so display hooks need regression tests even
where current arguments use literals. **Acceptance:** service failure, stale
cache, enum labels, opaque paths/hashes and unchanged CLI/plan machine values.

### AUD-033 — P1: Btrfs backup acceptance lacks shipping-kernel and restore gates

**Known capability gap.** The selected stock recovery kernel disables Btrfs.
Generic-guest snapshot/send/scrub/balance/rollback results do not enable it on
the tablet. Stream capture is not a completed receive/restore workflow.

**Change:** establish a matching, accepted Btrfs-capable kernel/module closure,
then implement controlled receive, parent-chain verification and boot integration.
**Acceptance:** restore full and incremental backups into separate filesystems,
compare data/metadata/subvolumes and rehearse interrupted receive. Keep physical
boot/ABI and generic guest results separate.

### AUD-034 — P1: Android, boot and hardware acceptance remains incomplete

**Validation and capability gaps.** The common preflight correctly reports that
the live writer is not ready. Installed Android FBE/KeyMint, Virtual A/B/super/OTA,
second Android isolation, UEFI/Aloha one-shot boot and physical fallback still
need their dedicated contracts. Kernel audits cannot by themselves establish
the exact root/DT/module ABI/signature chain. Synthetic Arch/Fedora rescue tests
do not prove installed package repair, initramfs or SELinux recovery.

**Change:** use the per-feature matrix below; expose unavailable capability
reasons and require exact-profile evidence. Before enabling device writes, also
establish read-only battery/charging, temperature and UFS-health observations,
with reviewed refusal/hold behavior when the device cannot safely finish.
These are proposed admission controls, not currently verified sensor support;
do not invent thresholds or calibration from another device. **Acceptance:** isolated functional
fixtures first, then separately authorized device tests with a fallback. Do not
reintroduce deferred BitLocker or SSH/network work as a requirement.

### AUD-035 — P1: checksums do not complete authenticated update delivery

**Known release limitation.** Current artifacts explicitly declare AVB `NONE`
and unsigned ZIPs; SHA-256 identifies bytes but does not authenticate an update
publisher. Signed updates are still a roadmap capability, not an accepted feature.

**Change:** source-pinned verification, managed trust roots, version/rollback
policy and authenticated manifest/payload binding. **Acceptance:** modified,
unsigned, wrong-key and older-version updates; signing-key rotation and offline
verification. Preserve the stock boot policy; do not imply AVB is enabled.

### AUD-036 — P2: the fault matrix needs more than SIGKILL and forced reset

**Test-gap recommendation.** Existing interruption and no-mutation tests are
valuable. They do not cover every EIO, short read/write, ENOSPC, readonly/remount,
USB-store removal, corrupted journal, failed fsync, sector geometry or concurrent
writer condition across every operation family.

**Change:** bounded native fault injection and regular-file guest fixtures at
each durable transition, including interrupted rollback. **Acceptance:** exact
terminal/uncertain state, independently verified target bytes, durable journal,
no ownership leak and a permitted recovery action. Use forced restart as the
tablet's realistic interruption model; do not require cutting physical power.

### AUD-037 — P2: visual and input acceptance needs a full interaction matrix

**Validation gap.** Existing renderer and callback tests cover important scale,
placement and Extra-menu fixes. They do not establish all-language touch/mouse
behavior at 50–100 percent, all page contents, monitor modes or long jobs.
Automatic brightness and rotation are correctly unavailable until a verified
recovery sensor stream exists; stock HAL descriptors and PMIC IIO links are
not that stream.

**Change:** test select/preview/apply/revert, descriptions and icons, long text,
footer/gesture separation and edge anchors, plus independent monitor fit.
**Acceptance:** every 5-percent scale, portrait/landscape, touch hitboxes,
keyboard focus/shortcuts, mouse drag/click, hotplug and native framebuffer capture.
When sensors become available, test fixed poses, lux, hysteresis, smoothing,
manual override and recovery service/firmware compatibility without exposing
private calibration. Keep HDMI and sensor claims model-specific.

## Reproduced audit checks

The [host-probe receipt](URE-AUDIT-HOST-PROBES-2026-10-04.json) records input,
compiler and relevant source hashes. Expected sanitizer failures document open
defects; they are not successful product acceptance tests. The host method uses
[AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html) to detect
invalid memory access; it does not establish physical device behavior.

| Check | Result | Limit |
|---|---|---|
| Exact upstream UTF-8 function extraction; `{0xf8,0x00}`; pinned Clang C++20 ASan/UBSan | Heap-buffer-overflow, one-byte read; exit 1 | Isolated host decoder, not whole-GUI/tablet execution |
| Draft full catalog tool; one-entry JSON; same compiler/instrumentation | Heap-use-after-free; exit 1 | Host-only draft generator, not shipped payload |
| Full 21,935,820-byte catalog probe with a 5-second deadline | Timed out; exit 124 | Inconclusive; not counted as a second reproduced failure |
| Baseline recovery header/AVB and reviewed Global/CN A/B profiles | All four profile/slot comparisons pass | Archived OEM geometry; live capacity unverified |
| Copy of profiles with recovery_b one sector smaller | Refused; exit 1, no receipt produced | Synthetic capacity boundary only |
| Staged packaged Roboto character-map inspection | Latin/Greek/Cyrillic coverage; required non-Latin blocks absent | Staged font inspected, not all-language screenshot acceptance |

The first sanitizer compile attempted the host GCC runtime, which could not
link its missing `libasan.so.8.0.0`. No result from that attempt is counted.
Repeating with the project's hash-verified Clang toolchain reproduced the two
defects. No host packages or tablet dependencies were installed.

The existing image is 104,857,600 bytes. Its v4 header has a zero-byte kernel,
a 38,812,491-byte compressed ramdisk and a 38,817,792-byte aligned payload.
Each reviewed recovery slot is 104,857,600 bytes, leaving 65,970,176 bytes after
the aligned payload and conservative 69,632-byte AVB reservation. This is not
a size guarantee for the unbuilt localization/font draft. The capacity checker
is also a draft; wiring it into publication and completing malformed-header
boundary tests remains necessary.

## Capability acceptance matrix

| Area | Existing evidence that may be retained | Still required |
|---|---|---|
| Partition planner and image application | GUI review, userdata-bounded filesystem/GPT jobs, regular-file resume/rollback and forced guest reset | Populated preservation, encrypted-userdata policy, installed-profile identity, common live writer and physical acceptance |
| Six-LUN stock return | Catalog/GPT fixtures, stock-namespace guest assertions and negative profiles | Actual six-LUN geometry, SKU/capacity profiles, full boot-layout restore, coordinated live journal and forced-restart recovery |
| Raw/Linux-home backups | Shipping CLI guest content/metadata and interrupted restore tests | Large sparse/hardlink/million-entry stress, external-store loss, populated real Linux-root restore and independent review |
| Filesystems | Five format/clean-repair guest workflows; supported empty shrink and original rollback | Populated/corrupted cases, supported FAT/exFAT resize, NTFS repair boundaries and failure injection |
| Btrfs | Native host/ioctl and generic guest snapshot/send/maintenance tests | Shipping kernel support, receive/restore, incremental parents, root/subvolume boot selection and physical ABI |
| Linux rescue | Synthetic Arch/Fedora namespace, selected writes, timeout and cleanup | Resource budgets, lifecycle cancellation, installed-distribution tools/dependencies, real initramfs/package/SELinux recovery |
| Kernel/boot audits | Bounded asset parsing and source/fixture consistency checks | Exact installed root UUID and fstab/BLS mapping, full executable dependency closure, DT compatibility, module ABI/signatures, accepted UEFI/fallback |
| Android | Stock profile and conservative trust refusal | FBE/KeyMint trust, slot/snapshot transitions, logical/super/OTA and second-install isolation |
| Windows/LUKS | Tool inventory and conditional capabilities | LUKS open/close/header workflows, WIM/ESD metadata restoration and ESP/BCD recovery; BitLocker remains deferred |
| ADB workflows | Packaged CLI and existing management commands | Documented shell-like inspection/job commands, safe return/exit codes, interruption, transfer resume and privacy-reviewed diagnostics; SSH/network remains deferred |
| Display/input | Generic renderer geometry, native-resolution private GUI and callback tests | Actual panel/dock 2K@75 link, touch/mouse/keyboard, every scale/language, disconnect/fallback and measured latency |
| Sensors | Reviewed stock descriptors/configuration and honest unavailable UI | Verified recovery SSC/service stream, mounted axes, units/timestamps, orientation/lux tests and per-model calibration ownership |
| Release | Exact payload receipts, dependency/privacy scans, repeat packaging and experimental disclosure | Closed source-to-ELF receipt, all-input invalidation, name-independent policies, authenticated updates and two clean builds |

Prior passing native/sanitizer and shipping-CLI guest checkpoints are recorded
in [the function review](URE-FUNCTION-VM-REVIEW.md) and
[the VM review](URE-VM-REVIEW.md). They were not rerun for this audit and do not
cover the new localization draft. Host/VM successes do not change physical
acceptance status.

## Implementation order and measurement plan

1. Close AUD-001 and correct the coverage claims; fix AUD-002/003 and AUD-030
   before expanding rendering or localization. Prove each with an independent
   negative test and unchanged-byte or sanitizer oracle.
2. Enforce a practical host memory envelope and disk temporary storage
   (AUD-022/023) before the next clean build. Integrate all input and build
   receipts (AUD-024–026) so subsequent results describe the actual candidate.
3. Finish transaction ownership, persistent installer backup and restart policy
   (AUD-004–009), then populated filesystem/failure cases. Keep the live writer
   unavailable until these and exact device-profile evidence are accepted.
4. Add owned asynchronous jobs and chroot/directory resource budgets; optimize
   repeated hash/serialization/Btrfs work with integrity-equivalence tests.
5. Finish language provenance, licensed rendering and the interaction matrix;
   profile the display path before changing scanout ownership or cadence.
6. Complete remaining capability contracts individually, preserving unavailable
   states and separate physical acceptance. Re-audit the resulting changes.

| Workload | Record | Acceptance direction |
|---|---|---|
| Clean/warm j16 host build | Wall time, compiler/Soong peak memory, PSI, ccache hit/miss, OOM events and temporary-storage mount | No system/global OOM; desktop reserve and responsiveness retained |
| Large/deep/wide home tree | Peak RSS, enumeration bytes, read bytes, SHA calls, allocations and elapsed time | Global memory bound; identical metadata/content closure |
| Btrfs full/incremental capture | Command count, allocations, stream bytes read, syncs and throughput | Fewer redundant reads/allocations with equal corruption/refusal coverage |
| GUI during backup/repair | p50/p95/p99 input/status/cancel latency and frame time | Bounded control response; cancellation only at documented safe points |
| 3200×2136 tablet / QHD monitor | CPU time, frame cadence, KMS wait, missed commits, hotplug and RSS | Measured latency/thermal budget with correct output-mode and buffer ownership |
| Every language and scale | Key and glyph closure, shaping/RTL, clipping, footer gaps and hit rectangles | Readable warnings and usable controls at 50–100 percent |
| Interrupted apply/rollback | Bytes before/after, durable state, journal checksums and ownership cleanup | Recoverable exact state or explicit refusal, never false “unchanged” success |

No measured speedup percentage is claimed. Initial latency/RSS targets should
be chosen after a baseline on the constrained host and the actual tablet;
generic TCG wall times are not tablet throughput benchmarks.

## AUD-015 superseding source/host remediation — 4 October 2026

The original directory-allocation finding remains preserved above. Current
source uses an iterative frontier and bounded deterministic anonymous sorted
runs, with aggregate entry admission before copying names. Four matching native
and sanitizer checks, the 100,000 maximum-length-name RSS fixture, 64-level
capture/restore and the actual tree CLI SIGKILL/resume checks passed. See
[the ordered remediation record](URE-P1-REMEDIATION-2026-10-04.md) and
[the enumeration resource contract](../docs/TREE-BACKUP.md#enumeration-resources)
for exact limits, final inputs, failed trials and residual scratch/runtime
boundaries. This resolves the reviewed source allocation structure; target,
combined VM and physical acceptance remain open.
