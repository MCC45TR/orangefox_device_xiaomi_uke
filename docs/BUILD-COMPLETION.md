# Recovery build completion evidence

Every new recovery build owns a separate private output directory. It starts
empty by default. An explicit `--cache-seed job-ID` may copy intermediates from
an earlier unsealed project job using mandatory filesystem reflinks. Existing
outputs stay available until the new job is accepted. Seeded output is recorded
as `fresh_output=false` and `cache_seeded=true`; the producer is never adopted
as successful build evidence. Compiler cache remains separate.
The preserved public alpha and its historical manifest are unchanged.

## What the receipt binds

Before compilation, the host captures project sources, headers, configuration,
patches, scripts, tests, assets and optional localization inputs. Production
receipts also carry the [localization inventory](LOCALIZATION-EVIDENCE.md),
which binds supported languages, themes, fonts/notices and exact dependency
pins to native and GUI resource evidence. Inventory is separate from glyph,
translation and font-license acceptance. The Android
tree is indexed by actual file content, including staged, ignored and untracked
sources and prebuilts. File/symlink modes, targets and source timestamps are
recorded; source timestamps can affect compiler macros. Fixed Git revisions and
tree identities for all 399 locked projects are checked against the complete
project list. VCS storage and the three previous output directories are excluded
from file-content indexing. An input link into excluded storage refuses; an
external regular file is hashed with its metadata, and an unreviewed external
directory link refuses. Ambiguous control-character names and special source
objects also refuse.

The selected installed host executables and their direct ELF runtime libraries
are hashed; host OS/package versions are recorded separately. Host Python is
used only by upstream AOSP tools, as documented in [host tools](HOST-TOOLS.md).
This is **not a complete hermetic host OS closure**. It does not establish
binary reproducibility or defend against a compromised host account.

Compilation sees readonly sources, its owned writable output mounted at the
neutral `/mnt/out-public` path, and the admitted disk scratch. The launcher
clears inherited environment settings and supplies fixed locale, timezone,
home, user, build date/number, cache and admitted Go runtime settings. Upstream
host Python cannot write bytecode into the readonly source tree. Source and
tool snapshots are repeated after the successful command; any difference
prevents sealing. Failed or interrupted jobs retain diagnostics and cannot be
resealed. Their intermediates may supply an explicitly bound cache to a new job.

Cache admission checks producer ownership, output identity, source lock and
revision list, host tools/runtime, lunch, build targets and firmware profiles.
It indexes every cached file by content and records modes, timestamps and
symlink targets before and after copying; producer changes reject the seed.
Copied content and layout must match. Installed recovery, root and partition
payloads, final images, packaging timestamps and stale build identity are
removed from the new copy; Soong and Make object intermediates are retained.
Ninja must reinstall and repack those outputs. The producer is preserved, and
the new receipt binds its cache provenance alongside the new before/after
source evidence. Cache reuse does not establish binary reproducibility.

The sealed receipt maps these inputs to the actual recovery image, complete
staged recovery payload, installed product files, generated Soong configuration
and build graph, host packaging tools and the optional uninstalled VM fixture.
Critical recovery/fastboot/native-helper ELF headers must be ARM64. Complete
payload ELF dependencies, scripts, privacy and no-Python checks remain the
separate extracted-image audit. Object intermediates and compiler caches are
not shipped-product attestations.

## Completion and publication

The sequence is prepared, compiled, then service-completed. Receipt files become
readonly and their content index is checked. The owned service checks command
status and OOM-event deltas before accepting publication; a compiled receipt
alone is insufficient. A small host C++ helper uses Linux directory rename
operations to publish a new path or exchange the previous and new output
directories atomically. It refuses indirect, unowned, writable-by-others,
cross-filesystem or overwrite paths. Parent-directory sync failures leave
completion unaccepted. The exchanged old output is retained with the job.

The outer controller adds final service acknowledgment only after systemd-run
returns success and the command status matches. A worker's publication marker
alone cannot establish successful service exit. Acknowledgment binds the exact
sealed receipt, admitted resource policy and scratch policy. A forced host restart
or lost controller between publication and acknowledgment
can leave a new output present but unaccepted; verification fails without the
matching acknowledgment. There is no automatic recovery or claim of physical
power-loss durability at this checkpoint.

Package creation and manifest sealing recompute source/tool/output evidence.
Image auditing also matches the extracted payload, file modes and symlink
targets to the sealed payload index, and binds the image digest. Changed sources
without a rebuild, replaced tools/ELFs, missing acknowledgment, altered receipts
and different images refuse. New manifest schema 3 derives its compile result
from accepted completion evidence. Sealed candidate directories refuse mutation.
The public summary omits private host directory identity and raw diagnostics.
Exports and audit reports refuse output paths in sealed candidate directories.
The [explicit release policy](RELEASE-POLICY.md) additionally binds the requested
class/capabilities, complete native/sanitizer catalogs and applicable VM receipts.
These local content checks are separate from authenticated updates and a
production trust root, which remain AUD-035 work.

```sh
bash scripts/prepare-build-tree.sh global-os3.0.303.0
bash scripts/build-public.sh 16
# Optional: seed a new job from an explicitly selected unsealed project job.
bash scripts/build-public.sh 16 --cache-seed job-XXXXXXXXXXXX
bash scripts/build-evidence.sh verify
bash scripts/build-evidence.sh export artifacts/NEW-CANDIDATE/BUILD-COMPLETION.json
```

Create the new candidate directory before exporting a standalone summary. The
normal package/manifest workflow manages its own destination. Verification runs
through host resource admission automatically when needed. Full content
indexing intentionally costs disk I/O; ccache does not substitute for it.
Both source-inventory sorts have a 64 MiB buffer limit and one worker; spill
files use the admitted disk scratch. Charged file cache remains a separate cost.
Immutable source snapshots or a fully pinned build container are possible future
optimizations, requiring their own equivalent closure checks.

## Host controls and remaining validation

`tests/check-build-evidence.sh` uses the pinned Clang to compile real small
ARM64 executables in a separate host-fixture evidence class. It verifies a
changed-header rebuild with a different ELF hash, preserved prior output,
repeated atomic exchanges, actual SIGKILL and compiler failure. Negative cases
cover unrebuilt headers/timestamps, real ignored inputs, modes, revisions,
toolchain/ELF substitution, escaping links, receipt corruption, missing service
acknowledgment, OOM metadata and wrong evidence class. Cache controls exercise
real reflinks, consumer isolation, installed-payload invalidation, a rebuilt
receipt, and rejected foreign lunch, indirect producer, corrupted input index,
completed producer and stale sealing attempts. Those synthetic outputs
cannot be accepted as an Android recovery build.

Actual project-pin and source-tree index controls verify the current host inputs;
they do not compile a fresh complete recovery image. Clean/warm image builds,
resource/desktop-response measurements, extracted-image audits and combined VM
functional/visual acceptance still follow the ordered P1 source changes. Own-device
boot, UFS durability and tablet feature acceptance remain separate.
