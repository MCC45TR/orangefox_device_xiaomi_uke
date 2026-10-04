# Ordered P1 remediation — 4 October 2026

AUD-001 was completed first in source, native/sanitizer fixtures, ARM64 packaging
and the focused generic guest. The new candidate remains unsealed until broader
matching receipts exist. P1 remediation follows the original report order;
combined VM acceptance follows the source changes, and P2 follows afterward.

| Finding | Current result | Remaining acceptance |
|---|---|---|
| AUD-002 | Bounded UTF-8 scalar decoding; exhaustive exact-source native and sanitizer controls passed | Fresh target and combined guest |
| AUD-003 | Checked raster/parser budgets, bounded global caches, font ownership and clipping; production native and sanitizer controls passed | Fresh target and combined visual guest |
| AUD-004 | Next: durable installer boundary | Source and fixture remediation pending |
| AUD-005 | Common transaction ownership | Pending |
| AUD-006 | Exact installed model/profile/geometry admission | Pending; physical evidence unavailable |
| AUD-007 | Prefix versus complete boot-layout restoration | Pending |
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
| AUD-034 | Android, boot and hardware capability reasons | Pending; donor comparison in progress |
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
