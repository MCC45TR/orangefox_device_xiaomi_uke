# Pinned host tools

Run `scripts/prepare-build-tree.sh global-os3.0.303.0` after changing project
device sources. The public build compares every staged device source and the
linked GUI against the project copy before compiling; a stale copy is rejected.
Preparation also verifies the selected firmware archive and reviewed patch stack.

Android's official `repo` client is required to resolve and synchronize the multi-repository OrangeFox Android 16 manifest. It is an upstream Python program and runs **only on the development host**. The project-owned download, verification and report tools are Bash. No `repo` executable, Python interpreter, Python script or Python runtime library is packaged for the tablet.

The host client is archived as `android-repo` in the workspace source catalog at the peeled `v2.67` commit `d27d6829a84f488b7253ea693dcc429076c33914`. Its Git bundle and offline restore are verified separately. The synchronized Android source checkout is a build input, not a set of independent offline archives. Its 399 exact project revisions are recorded in `manifests/orangefox-android16-uke.lock.xml`; projects carrying an upstream `clone-depth` remain shallow by design and the lock records reproducibility at the checked-out commits rather than claiming complete history.

`unpack_bootimg` and `mkbootimg` are AOSP host tools for inspecting and constructing boot images. Their archived AOSP sources are pinned in the workspace. They are also excluded from tablet payloads. Packaging uses `mkbootimg` and `avbtool` compiled by the locked Android tree, not an arbitrary system package; their source commits and executable hashes are recorded with the artifact manifest. Their embedded upstream Python build machinery is host-only. A future build container still needs to pin the full host OS/package closure.

The same pinned host avbtool info_image command is used to inspect the OEM DTBO
footer and embedded vbmeta during programming-layout research. It is read-only
and host-only; the target checks use native C++ and whole-partition hashes.
The Bash catalog recipe and independent C++ fixtures reconstruct the reviewed
AOSP capacity-adjusted layout without executing fastboot or OEM scripts.

`scripts/build-public.sh` builds at `/mnt` inside an unprivileged Bubblewrap namespace with neutral build-user/host names. `FOX_BUILD_BASH=1` is exported before lunch because OrangeFox's vendor script reads a captured shell environment; a make-only flag does not prevent it from copying its prebuilt Bash. The source-built Bash avoids the vendor binary's unrelated home-directory locale/debugger prefixes. Each new build uses a fresh output, readonly sources, cleared environment and separate compiler cache. The [build-completion contract](BUILD-COMPLETION.md) binds before/after input content to actual image/payload/tool outputs and requires owned-service acceptance before atomically replacing `out-public`. Failed jobs and previous outputs remain preserved; older unrecorded outputs cannot acquire a retrospective completion receipt.

`scripts/package-prerelease.sh` constructs the separate temporary-boot candidate and a project-owned slot-safe ZIP. `scripts/archive-release-sources.sh` supplies the stock GKI commit snapshot and recovery/GPL utility sources, including recursive Magisk utility dependencies. These are source archives, not tablet payloads. They do not constitute offline archives of all 399 Android repositories or prove binary reproducibility.

The host-only `prepare-public-ramdisk.sh` callback stages the compiled Bash explicitly, including after an incremental build left a vendor prebuilt behind. It omits generic FRP, AVB-disable and verity/encryption-edit addon recipes because they have no Uke-specific target/fallback guard. It accepts only the neutral build's exact generated ramdisk path, is not copied into the tablet ramdisk, and does not access a block device. The stock kernel's upstream `/root/initrd` literal is a runtime initrd relocation path in `init/do_mounts_initrd.c`, not a personal build path; it remains unmodified.

The existing public alpha uses aliases of the licensed AOSP static Roboto font
and does not supply distinct font families. The [new multilingual source
checkpoint](MULTILINGUAL-TEXT.md) adds six unchanged, license-identified Noto
fallback assets, exact notices and bounded native shaping/bidi libraries. It does
not retrospectively rebuild or accept that alpha. Recovery source snapshots
exclude unaccepted local font drafts and unrelated vendor prebuilt/addon archives;
accepted Roboto/Noto originals and their notices are retained.

The nested ZIP audit found a private build prefix in a legacy addon updater. All generic bundled addon ZIPs are omitted from the alpha, with their optional UI actions unsupported. The shipped script sources are included directly in the source snapshot; the upstream binary installer directory is excluded. The project installation ZIP is audited separately and contains only its C++ helper, recovery image, POSIX wrapper, hash and documentation.

The native candidate adds reviewed host adapters for wimlib 1.14.5 and Dropbear
2025.89. Their original sources are read from immutable reference snapshots;
only the active build tree receives project configuration. NTFS resizing uses
a reviewed Android module patch against the locked ntfs-3g source. Exact pins,
licenses and build limitations are in `src/device/xiaomi/uke/ure-tools.lock.json`.
No upstream reference configure/build script is executed. Cryptsetup remains a
reference. Native Btrfs management uses kernel UAPI without btrfs-progs, but
Btrfs filesystem support is absent from the preserved stock kernel. Boot asset
decoding uses pinned zlib, Zstd and public-domain LZMA SDK sources; their
original notices are retained in the source snapshot.

`UKE_BUILD_VM_FIXTURE=1 scripts/build-public.sh 6` also builds the optional
uninstalled `uke-btrfs-vm-fixture` Soong target. `tests/check-btrfs-vm.sh` boots it
with a separately identified generic ARM64 virt kernel, matching modules and a
newly created regular-file Btrfs disk. Host QEMU is separate from tablet
contents. Guest startup/tests are POSIX shell/C++; neither a Python program
nor generic-kernel modules are added to the recovery product.

An incremental build exposed a callback failure when AOSP's absolute `/bin`
symlink was treated as a host directory. The callback now verifies that link
without following it, and a reviewed OrangeFox patch propagates callback
failures to the build. ELF closure checks also require the lpdump snapshot/binder
libraries and the Bionic bootstrap-loader alias. The vendor `ps` command path
uses source-built Toybox; an unused generic KeyMint helper is omitted from this
FBE-disabled profile. Final compressed-ramdisk audits verify these changes.

Packaging accepts a separate candidate directory name. Sealed candidate
directories refuse changes and the existing public alpha is preserved.
Every new package also requires an explicit class/capability request under the
[release policy](RELEASE-POLICY.md). Candidate names never select its native,
sanitizer or generic-VM requirements. Package repeats bind the same accepted
build and policy; complete native/sanitizer catalogs replace historical counts.
New manifest schema 3 records the base Git commit separately from changed
working-tree sources, includes exact input-file checksums and derives compile
evidence from the matching accepted build completion and extracted payload.
Source snapshots contain pristine upstream files plus the reviewed patches,
project adapters, tests and original licenses needed to reconstruct this build.

## Localization catalog checks

`uke-locale-catalog` is a C++20 host-only `BUILD_TESTING` target. It compiles the
pinned JsonCpp implementation and links the host libxml2/OpenSSL development
libraries. The focused Bash [key producer](LOCALIZATION-KEY-GENERATION.md) uses
the existing resource envelope and compiler cache, without a translator request
or Python. Its deliberately invalid owner-lifetime probe is a sanitizer negative
oracle and must never be included in a tablet payload or treated as a passing test.
