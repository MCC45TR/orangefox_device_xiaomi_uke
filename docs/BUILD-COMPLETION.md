# Recovery build completion evidence

Every new recovery build starts with a fresh private output directory. Existing
outputs stay available until the new job is accepted; they are never adopted as
fresh build evidence. Compiler cache is shared separately with content checks.
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

Compilation sees readonly sources, a fresh writable output mounted at the
neutral `/mnt/out-public` path, and the admitted disk scratch. The launcher
clears inherited environment settings and supplies fixed locale, timezone,
home, user, build date/number, cache and admitted Go runtime settings. Upstream
host Python cannot write bytecode into the readonly source tree. Source and
tool snapshots are repeated after the successful command; any difference
prevents sealing. Failed or interrupted jobs retain diagnostics and cannot be
resealed or reused as a new build.

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
acknowledgment, OOM metadata and wrong evidence class. Those synthetic outputs
cannot be accepted as an Android recovery build.

Actual project-pin and source-tree index controls verify the current host inputs;
they do not compile a fresh complete recovery image. Clean/warm image builds,
resource/desktop-response measurements, extracted-image audits and combined VM
functional/visual acceptance still follow the ordered P1 source changes. Own-device
boot, UFS durability and tablet feature acceptance remain separate.
