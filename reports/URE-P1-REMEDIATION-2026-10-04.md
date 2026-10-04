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
| AUD-022 | Host reserve and separate parallelism | Pending |
| AUD-023 | Nested disk temporary storage | Pending |
| AUD-024 | Immutable build-completion evidence | Pending |
| AUD-025 | Explicit release classes and capability-driven receipts | Pending |
| AUD-026 | Complete localization input closure | Pending |
| AUD-029 | Bounded all-language fallback, shaping and RTL | Pending |
| AUD-030 | Localization generator owner lifetime | Pending |
| AUD-031 | Translation context and provenance | Pending; competent semantic review separate |
| AUD-033 | Btrfs receive/restore and shipping-kernel admission | Pending; physical kernel acceptance separate |
| AUD-034 | Android, boot and hardware capability reasons; pinned donor USB/touch readiness and module/service closure compared | Implementation pending; source comparison is not hardware acceptance |
| AUD-035 | Authenticated manifest/payload and rollback policy | Pending; production trust remains unconfigured |

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
