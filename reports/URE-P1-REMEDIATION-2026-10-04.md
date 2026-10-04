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
| AUD-009 | Explicit image/live repartition capabilities | Pending |
| AUD-010 | Populated, fragmented and damaged filesystem oracles | Pending |
| AUD-012 | Aggregate rescue resource envelope | Pending |
| AUD-013 | Owned GUI executor and immutable inputs | Pending |
| AUD-014 | Joinable maintenance and global lifetime | Pending |
| AUD-015 | Global directory-memory admission | Pending |
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
