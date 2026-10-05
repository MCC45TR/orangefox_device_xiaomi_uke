# Ordered P1 remediation — 4 October 2026

AUD-001 was completed first in source, native/sanitizer fixtures, ARM64 packaging
and the focused generic guest. The new candidate remains unsealed until broader
matching receipts exist. P1 remediation follows the original report order;
combined VM acceptance follows the source changes, and P2 follows afterward.

| Finding | Current result | Remaining acceptance |
|---|---|---|
| AUD-002 | Bounded UTF-8 scalar decoding; exhaustive exact-source native and sanitizer controls passed | Fresh target and combined guest |
| AUD-003 | Checked raster/parser budgets, bounded global caches, font ownership and clipping; production native and sanitizer controls passed | Fresh target and combined visual guest |
| AUD-004 | Volatile writer removed; persistent regular-image installer, atomic first records, interrupted preparation recovery, exact fallback/plan/path binding and checked terminal fsync; five native and five sanitizer controls passed | Fresh target/combined guest; live installation explicitly unavailable pending physical fallback and durability admission |
| AUD-005 | Common process-independent host ownership, explicit compound delegation, durable exact retirement, retained tree/stream recovery and shared lifecycle/control callbacks; native/sanitizer and CLI controls passed | Fresh target/combined guest; Android coordinator and physical durability unaccepted; rescue aggregate resources and GUI worker ownership follow in AUD-012–014 |
| AUD-006 | Bounded exact model/SKU/firmware and six-LUN declaration comparator; unit-bound original GUID backups, complete A/B boot declarations, stable profile blockers and early live preflight refusal; three native and three sanitizer controls plus JSON CLI checks passed | Fresh target/combined guest; no live profile accepted because the OS2 stock inventory lacks full physical GPT/geometry and boot/fallback evidence |
| AUD-007 | Explicit exact-profile whole boot programming; default preserved tails, separate full-layout results, complete DTBO-tail mirrors, legacy/offline recovery and actual host SIGKILL; focused native/sanitizer, CLI and independent catalog controls passed | Fresh target/combined guest; installed Android boot trust, unit geometry and physical durability remain unaccepted |
| AUD-009 | Dedicated image/live capability contract, stable trust/geometry/ownership/fallback blockers, earlier refusal before request/target access, actual GUI review and explicit front recreation/original rollback; five native/five sanitizer controls and both CLI sets passed | Native physical writer and encrypted userdata migration remain unimplemented/unaccepted; fresh target and combined guest pending |
| AUD-010 | Populated/fragmented/damaged ext4 and populated F2FS/NTFS/FAT oracles; inspectable space refusal, exact persisted size and volume identity guards; four native/four sanitizer controls and six-format CLI sets passed | Fresh target/combined guest, arbitrary damage and encrypted-data acceptance separate |
| AUD-012 | Compiled aggregate cgroup-v2 envelope, GUI/ancestor headroom, blocked-worker admission, read-only proc and supervisor protection, exact-owner cancellation and conservative cleanup; eight native/eight sanitizer controls plus real host stress and namespace CLI sets passed | Fresh target/combined guest, GUI latency (AUD-013), protected negative OOM inheritance and external forced-restart recovery remain separate; shipping backend/physical acceptance unaccepted |
| AUD-013 | Owned worker, frozen session inputs, short GUI locks, bounded single completion, stale-view refusal and truthful advisory stop; nine native/nine sanitizer controls and exact reviewed source-stack controls passed | Fresh target, rendered-frame/combined visual guest and physical acceptance remain separate; global lifetime and exact backend GUI control follow in AUD-014 |
| AUD-014 | Retained runtime lifecycle leases, exact rescue/Btrfs controllers, pause/error cleanup and joined application teardown; eighteen native/eighteen sanitizer controls passed | Fresh target, combined guest, rendered/locale and physical acceptance remain separate; registry is volatile cooperating ownership |
| AUD-015 | Iterative frontier, global pre-copy entry/memory admission and anonymous deterministic sorted runs; four native/four sanitizer controls plus actual tree CLI interruption/resume passed | Fresh target/combined guest and physical acceptance remain separate; accounted listing budget excludes bounded metadata/index/runtime overhead and RAM-backed scratch remains volatile |
| AUD-022 | RAM/ancestor headroom reserve, post-lock unit readback, separate compiler/Soong scheduling, reviewed host stacks and measured cold/warm cache/Go controls passed | Full clean/warm Android RSS/PSI/OOM and desktop response remain pending after AUD-023/024; small fixtures do not diagnose the earlier task shutdowns |
| AUD-023 | Kernel-resolved private disk identity/space/inode admission across both namespaces; actual 64 MiB write/cache accounting and RAM/substitution/readonly refusals passed | Full clean/warm recovery build and output capacity/immutable closure remain pending; free-space snapshots are not allocation guarantees |
| AUD-024 | Fresh isolated output, complete source-content/pin/tool snapshots, readonly completion receipts, successful-service acknowledgment and atomic output publication; actual compiler/interruption/substitution controls and production stale-output refusal passed | Fresh complete Android build, extracted-payload audit and combined guest pending; local evidence is not authenticated updates or a hermetic OS closure |
| AUD-025 | Explicit class/capability policy, mandatory shipped-feature coverage, matching complete native/sanitizer catalogs, bound package repeats and six sealed-output guards; renamed-candidate/missing-record/metadata and historical-alpha controls passed | Fresh complete native/sanitizer/image/package and applicable combined guest receipts pending; policy preflight is not build acceptance and physical classes remain unavailable |
| AUD-026 | All-language/draft/font/license input closure, six pinned dependencies and source/shipping/reviewed-overlay identities; inventory/mutation and stale-record controls passed | Fresh complete catalogs/build/image/GUI/combined guest pending; inventory flags do not establish rendering or translation acceptance |
| AUD-029 | Licensed original script/CJK assets, bounded OpenType/bidi layout, contextual logical-prefix fit, original byte clusters and source-over marks; 32-language corpus, 616 scale/orientation draws, interleaved fonts, deep allocation failure, corrected upstream FreeType guard and halt-on-error runtime negative passed in three native/three sanitizer tests | Fresh complete named catalogs, Android target, image/capacity/package and shipping/combined GUI pending; Unicode 18 bidi, translation semantics and physical acceptance remain unavailable |
| AUD-030 | Retained JSON owner, strict schema/unique keys, deterministic UTF-8 headers, atomic refusal preservation; five native/five sanitizer controls and empty/one/non-BMP/current 3,189-entry header/lookup compilation passed | Full 49-test catalogs and shipping target/GUI/VM pending; translation provenance/semantics remain AUD-031 |
| AUD-031 | Actual GUI language changes invalidate stale completions, plans, journal actions and captured Btrfs control reviews; four native/four sanitizer callback controls and reviewed-change-only compilation passed | Source/context/parent provenance and competent semantic review pending; full 50-test catalogs, target/GUI/VM separate |
| AUD-033 | Btrfs receive/restore and shipping-kernel admission | Pending; physical kernel acceptance separate |
| AUD-034 | Android, boot and hardware capability reasons; pinned donor USB/touch readiness and module/service closure compared | Implementation pending; source comparison is not hardware acceptance |
| AUD-035 | Authenticated manifest/payload and rollback policy | Pending; production trust remains unconfigured |

## AUD-024: bind completion to the actual build and service

New builds use fresh private output, readonly source mounts and a cleared,
recorded environment. All 399 locked Android project revisions are checked;
content inventories include staged, ignored and untracked files, modes,
symlink targets and source timestamps. Selected installed host tools and their
direct ELF libraries are hashed separately. Before/after differences, incomplete
jobs and reused output refuse sealing. Readonly receipts bind the actual image,
staged payload, installed products and generated configuration/build graph.

An atomic host directory exchange retains the complete previous output. The
worker records publication only after successful command status and unchanged
OOM counters. The outer controller acknowledges completion only after
systemd-run itself returns success. Package and manifest drivers recompute
evidence; image audits additionally compare the complete extracted payload.
Manifest schema 3 derives compile success from the accepted receipt. Historical
sealed candidates and the public alpha remain unchanged.

Final frozen source-input manifest SHA-256:
`5cb2b95a823a9886b5d3a649be13275e80d1c1b7e968ac0a42de71162d9fe637`.
Actual pinned-Clang ARM64 fixture compilation, a changed-header rebuild with a
different ELF hash, retained previous output, 40 atomic exchanges, child SIGKILL
and compiler failure passed. Negative controls reject changed content/time,
actual ignored headers, modes, revisions, toolchains, ELFs, escaping links,
corrupted receipts, missing acknowledgment, OOM/class mismatches and false or
nonzero service-controller results. Fixture evidence cannot establish an
Android recovery build. Production verification, image audit and packaging
refused the existing unreceipted output before destination publication, with
unchanged recovery-image bytes.

Two actual source inventories produced identical indexes across 811,108 files.
The final bounded-sort run used 66,816 KiB maximum command RSS in 2:43.84; its
service peak was 12,248,391,680 charged bytes, including 12,066,119,680 bytes of
file cache at the final snapshot. Memory-high throttling occurred 100,665 times;
max/OOM/kill counters were zero. Final host memory PSI avg10 was zero, while
avg60/avg300 were 0.22/0.16. The preceding run had different cache/sort conditions,
so timing differences are not attributed solely to the sort limit. Inventories
cost disk I/O and charged cache even when process RSS is bounded.

Failed resealing and corruption-oracle trials remain private and are explained
in dated parent lessons. These controls are source/reference-host evidence:
they do not compile a new complete image, authenticate updates, establish binary
reproducibility, diagnose earlier application shutdowns, or accept a tablet.
See [the completion contract](../docs/BUILD-COMPLETION.md).

## AUD-025: artifact names cannot weaken release requirements

New packages require an explicit `experimental`, `vm-reviewed` or
`function-reviewed` request. All ten shipped capability domains must appear
exactly once. The reviewed policy derives five, ten or fourteen required
receipts respectively. Unknown classes/features, omissions, duplicates, extra
request fields, changed requirements/digests and physical-acceptance requests
refuse. A candidate retains its normalized policy; changing it requires a new
directory. No device-validated class is configured.

Manifest validation retains the exact source/image/ELF/runner/scope predicates.
Native and sanitizer receipts now bind complete sorted CTest names and counts
to the current configured catalog instead of historical hardcoded numbers.
Package-repeat evidence binds two packages from the same accepted build and
policy; it does not establish two clean builds. Required receipt digests and
the normalized policy are included in manifest schema 3.

Final frozen source-input manifest SHA-256:
`a491b60e1f93332e7a3f0584edcf24ef94015ab482f51436467eac4ef753c5d9`.
Three differently named directories with the same capabilities refused the
same missing functional receipt without changing their file contents. All
fourteen missing/indirect receipt controls passed, as did malformed JSON,
tampered policy and catalog/source/CLI/compiler/sanitizer metadata refusals.
The actual configured catalog has 47 names; this enumeration is not a fresh
47-test execution receipt. Presence and metadata fixtures cannot accept an
Android build or a VM result.

Package, manifest, source-archive, repeat, image-audit and completion-export
entry points refused the locally available historical sealed alpha. Complete
before/after file hashes matched. Unmodified copied entry points also passed
isolated sealed fixtures, and a lower-class request refused an already bound
candidate before build/export effects. The archive path's previously missing
seal check is corrected. [The release policy](../docs/RELEASE-POLICY.md)
documents the future fresh-build/testing workflow. No new complete image,
package repeat, sanitizer matrix, combined guest or tablet acceptance is claimed.

These are engineering results, not claims that every capability is implemented
or accepted on a tablet. Unaccepted device workflows must stay explicitly
unavailable. Donor reports and currently used third-party implementations are
evidence to compare, not permission to execute donor scripts, adopt their
security policy, borrow partition geometry or modify a connected device.

For AUD-003, native checks passed in 2.29 seconds and pinned Clang
ASan/UBSan/leak checks in 14.98 seconds for both text executables. The test builds
the pinned FreeType dependency, exercises actual production functions and includes
the compressed-font parser path. A single fresh candidate review identified
preview enlargement, width-zero input, punctuation wrapping, parser expansion,
pre-raster admission, font generation and rotated texture-origin corrections;
each was reconciled with a focused regression. Historical failures remain in
private build logs and dated project lessons. Full VM and release results are
not inferred from these focused checks.

AUD-004's final focused native set passed 5/5 in 70.39 seconds, and pinned
Clang ASan/UBSan/leak checks passed 5/5 in 99.97 seconds. The full 100 MiB
installer model passed in 58.54 and 62.92 seconds respectively. Its controls
include both fixture slots, actual creator/writer SIGKILL, persistent mirrors,
source-independent resume/rollback, short writes/EINTR, failed and repeatedly
failed target fsync, foreign raw journals, replaced wrapper paths and unchanged
inactive-image byte oracles. The initial sanitizer trial exceeded 300 seconds;
buffer comparison removed a per-byte classification bottleneck while preserving
complete hashing and mixed-byte inspection. Existing raw/stream/file-transaction
regressions passed again. These receipts cover source/host fixtures only.

AUD-005's complete CTest sets passed 36/36 native in 271.98 seconds and
36/36 under pinned Clang ASan/UBSan/leak checks in 602.57 seconds. Subsequent
CLI integration exposed a parsed numeric-type mismatch in rescue synchronization,
a sector-width fixture mismatch and a stale call to the removed installer writer.
The corrected affected native/sanitizer rescue C++ and namespace controls passed,
as did all six filesystem CLI workflows, foreign derived-plan rejection, the
remaining CLI scripts, 111 production entry guards and exact reviewed patch-stack
checks. Complete regression totals and subsequent affected receipts remain
separate; a fresh complete release receipt is not inferred from an interrupted
script followed by targeted checks. Every device effect in lifecycle fixtures
was mocked; no host block device or connected tablet was accessed.

AUD-007's three-control sets passed native in 58.90 seconds and pinned Clang
ASan/UBSan/leak checks in 156.66 seconds. Native and instrumented CLI checks
passed, as did the independent five-image programming catalog. Subsequent
explicit missing/end-shifted-footer and truncation fixture controls passed
native in 2.42 seconds and instrumented in 2.79 seconds; only that test source
changed between the frozen input manifests.
The final matching implementation and the existing stock/GUI controls did not
change. A successful prefix write can report a noncanonical preserved tail;
whole programming requires the complete compiled layout before commit. Both
keep boot-ready and Android-compatibility results false. These focused receipts
do not constitute a fresh target, complete release or combined VM acceptance.

AUD-009's five-control sets passed native in 147.69 seconds and under pinned
Clang ASan/UBSan/leak checks in 334.41 seconds. Both native/instrumented
capability and layout CLI controls passed; exact frozen inputs remained
unchanged. The front-recreation fixture verifies original-file loss, independent
filesystem signatures and complete original-image rollback. Unaccepted live
compound routes refuse with stable reasons before request/root/target access.
This closes the ambiguous scope reporting and late admission behavior, not the
native device-writer or Android encryption implementation gap. Those capabilities
remain unavailable pending exact unit, trust, ownership and durability evidence.

AUD-010's new two-control sets passed native in 132.06 seconds and under
pinned Clang ASan/UBSan/leak checks in 152.11 seconds. The unchanged compound
partition and transaction regression sets then passed 2/2 native in 131.05
seconds and 2/2 instrumented in 295.37 seconds. All six original format/check/
resize-where-supported/rollback CLI workflows passed on both binaries. The
frozen input manifest remained byte-identical after both regression runs:
`4618981a374dd15cd723ea91f850b25faffc5434f23cfd82fffe43258273c18f`.
These are separate focused source/host receipts, not a fresh complete release
or combined guest. The tests exposed and corrected missing state after space
refusal and successful F2FS no-op resize; independent original bytes, persisted
geometry, contents and metadata now bound the acceptance claim. Preserved
fixture failures and numeric/readback corrections are recorded in dated lessons.

AUD-013's nine focused controls passed native in 72.26 seconds and under pinned
Clang ASan/UBSan/leak checks in 125.45 seconds. Both unchanged-source comparisons
match `5229d440ae19d7df0a5207f6beaa7ac4a60910fd7bb1a671c364bfe6b60ffd93`.
Actual callbacks use a frozen owned session and bounded once-only GUI publication.
Real 512 MiB hash/backup and 320 MiB ext4 resize/rollback controls verified data
and continued status sampling. Native maximum status latency was 1 ms; the
instrumented maximum was 28 ms. Stop acknowledgements were at most 1 ms and
explicitly advisory. They do not prove that native I/O, kernel maintenance or
cleanup stopped. Changed selections and view epochs refuse stale publication;
UTF-8 preview boundaries, persisted applied scale, six-LUN display projection,
normal scale/mirror/graph and exact reviewed source-stack controls also passed.
The detached worker is removed, but AUD-014's global lifecycle, orderly teardown
and separate exact-owner GUI backend controllers remain unfinished. Fresh target,
rendered-frame/combined guest, full release and physical acceptance remain open.
See `docs/GUI-JOB-EXECUTION.md` and the dated parent engineering lessons.

AUD-015 replaces recursive ancestor name vectors with an iterative frontier and
sorted 1,024-name runs. The shared limits are 2 MiB of accounted listing/frontier
storage, 512 MiB of live anonymous scratch and one million entries admitted
before name copying; per-directory and path-depth limits remain 100,000 and 64.
The backup store retains a 16 MiB free-space reserve. Bytewise sibling ordering,
opaque filenames and existing namespace hashes remain compatible. Older sealed
plans without the new informational enumeration fields remain readable.

The final frozen input manifest is
`30e2670dee369bf077754600df74492c25336eb944f5c55a10003fad787dc4f3`.
Four focused native checks passed in 57.20 seconds; matching leak-enabled pinned
Clang ASan/UBSan checks passed in 148.26 seconds. The actual CLI capture/verify/
restore and observed SIGKILL/resume suite passed separately. The wide fixture
contains 100,000 actual children with 255-byte names: accounted working peak is
425,056 bytes and scratch peak 51,400,000 bytes. Isolated native maximum RSS grew
from 5,484 to 6,144 KiB; sanitizer maximum RSS grew from 14,640 to 19,308 KiB.
These are measurements for enumeration, not a whole-backup RSS guarantee.
The 64-directory / 1,088-entry fixture completed actual plan/capture/restore,
with 972,356 accounted bytes and 268,131 scratch bytes at peak.

The same 32 MiB RSS-growth assertion first failed under sanitizers. Successful
per-name guards were constructing error strings on every check; failure-only
construction removed allocator/quarantine churn without relaxing the assertion.
Earlier compile and deep-count/namespace-oracle failures are preserved. The
final namespace oracle independently reopens and compares the actual source
inode and names rather than relying on a reused listing cursor. Allocation,
ENOSPC/reserve, short-write/EINTR, global entry exhaustion, 100,001-child refusal
and observed child SIGKILL checks verify descriptor/accounting cleanup. Existing
CLI interruption tests retain durable capture recovery.

Preferred scratch is anonymous `O_TMPFILE`; the immediate-unlink fallback has a
creation/unlink forced-restart gap that may leave an unreferenced private
`enumeration-*.tmp`. RAM-backed stores charge scratch to RAM and are volatile.
Metadata pages, hardlink indexes, allocator/runtime overhead and page cache are
separate from the 2 MiB budget. No fresh Android package, combined guest, live
storage writer, physical backup or whole-process low-memory acceptance is claimed.

AUD-022 now calculates admission from MemAvailable and ancestor high/max/current
accounting, retaining at least 4 GiB or 25% of effective shared capacity. A
16 GiB fully available policy snapshot leaves 12 GiB for the job, admits at most
13 normal or eight sanitizer compile workers, and separates six Soong runtime
processors / 6 GiB soft heap from that compile ceiling. j16 remains available
when a larger envelope permits it. The owned service recomputes after its lock,
verifies its exact cgroup, tightens and reads back memory limits, and refuses
nested admission. Checked OOM grouping and Nice 5 isolate failure/scheduling of
that new job; no existing desktop unit is changed.

The reviewed host stacks preserve GOMEMLIMIT/GOGC/GOMAXPROCS, remove Blueprint's
runtime concurrency reset, bound bootstrap/compiler scheduling, and propagate
limits into microfactory compiler children. Exact upstream pins are
Soong `6dc77879464584ef3f178cae622134ed0bf19e1e` and
Blueprint `dcb14f2e146f40cf1f212efb220e9aa1f3cfc280`;
Go 1.23.4 executable SHA-256 is
`c4859c0d97fe48a45d348c8ceba892a5c2ca1d7f3e429cf3ea4f2c0dae5cc406`.
These are existing upstream host dependencies, not tablet runtimes.

Final source-input manifest SHA-256 is
`5a86c844d48e4aed6e602bac77fbbb9eafed8ad9874f40ce54150fb4b80a9088`.
Snapshot/refusal controls, actual service readback and nonzero-command refusal,
real cold/warm C++ object identity/cache-hit checks, pinned Blueprint package
compilation/tests, actual RunBlueprint policy retention and unknown-edit
preservation passed. Existing recovery reviewed-stack controls also passed.
The small cache job reports 45,113,344 cgroup peak bytes / 61,156 KiB command RSS
in 0.37 seconds; the isolated Go package job reports 332,845,056 cgroup peak
bytes / 188,044 KiB command RSS in 18.04 seconds. Both report zero OOM events and
zero avg10/avg60/avg300 host memory PSI at the final snapshot. RSS and cgroup
charging are distinct measurements, not interchangeable whole-build estimates.

Failed trials preserved an awk printf parse error, an incorrect nested high-limit
oracle and incorrect ccache counter names; assertions were corrected against
actual inputs/statistics without relaxing reserve or cache identity. Scheduling
allowances are heuristics, external allocations may change pressure, and small
jobs do not establish full clean recovery build safety or desktop response.
AUD-023 still leaves inner Android temporary storage on tmpfs, and AUD-024 still
requires immutable build-completion closure. No full Android build or VM/tablet
acceptance was performed for this host policy checkpoint.

## AUD-023: preserve the disk scratch through Android isolation

The source changes remove the inner tmpfs overlay and reuse one production
Android namespace helper for both compilation and its host test. The outer
service, post-lock worker and inner command verify the same private directory
device/inode, filesystem magic/ID, owner/mode and receipt. TMPDIR/TMP/TEMP use
that directory. Conservative admission requires 8 GiB for Android or 2 GiB for
native/sanitizer jobs plus 131,072 free inodes on fixed-count filesystems.
Known Btrfs zero counts are dynamic, with a real small create/write/filesystem
sync/unlink check. Unsupported/RAM/network/overlay filesystems, bad accounting,
low capacity, altered proof, readonly mounts and directory substitution refuse.

Frozen native-input manifest SHA-256:
`55bc8d47c4c96c9a80388530f46f4ff7c5728198a0bf2ceaff8b31abb612abcf`.
The actual nested Btrfs fixture allocated a 67,108,864-byte random file; charged
file cache and filesystem available-space drop were each 67,117,056 bytes at
the measured snapshots. Anonymous memory changed by -4,096 bytes and shmem by
zero. The owned service recorded 73,277,440 peak charged bytes, 4,664 KiB command
RSS, 0.52 seconds and zero OOM counters. Disk scratch still consumes charged
cache; unrelated filesystem work can affect aggregate free-space snapshots.
Actual tmpfs, different disk and readonly substitution controls all refused
before their sentinel commands. Existing host reserve/cache and exact pinned
Blueprint compile/runtime controls passed again without input changes.

The first trial's `findmnt --first-only` reported a shadowed old tmpfs although
the resolved path's statfs and directory identity were Btrfs. That conservative
false refusal is retained privately. Kernel statfs magic now determines actual
placement; the receipt retains all target mount entries as diagnostics rather
than treating the first entry as identity. The corrected bounded fixture also
synchronizes before filesystem availability accounting. Source/host AUD-023
supersedes the temporary-storage limitation at the earlier AUD-022 checkpoint.
No full Android image, combined VM or tablet acceptance is claimed. AUD-024
must still prevent stale build/output evidence before the complete build.
