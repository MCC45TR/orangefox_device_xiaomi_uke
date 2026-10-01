# Pinned host tools

Android's official `repo` client is required to resolve and synchronize the multi-repository OrangeFox Android 16 manifest. It is an upstream Python program and runs **only on the development host**. The project-owned download, verification and report tools are Bash. No `repo` executable, Python interpreter, Python script or Python runtime library is packaged for the tablet.

The host client is archived as `android-repo` in the workspace source catalog at the peeled `v2.67` commit `d27d6829a84f488b7253ea693dcc429076c33914`. Its Git bundle and offline restore are verified separately. The synchronized Android source checkout is a build input, not a set of independent offline archives. Its 399 exact project revisions are recorded in `manifests/orangefox-android16-uke.lock.xml`; projects carrying an upstream `clone-depth` remain shallow by design and the lock records reproducibility at the checked-out commits rather than claiming complete history.

`unpack_bootimg` and `mkbootimg` are AOSP host tools for inspecting and constructing boot images. Their archived AOSP sources are pinned in the workspace. They are also excluded from tablet payloads. Packaging uses `mkbootimg` and `avbtool` compiled by the locked Android tree, not an arbitrary system package; their source commits and executable hashes are recorded with the artifact manifest. Their embedded upstream Python build machinery is host-only. A future build container still needs to pin the full host OS/package closure.

`scripts/build-public.sh` builds at `/mnt` inside an unprivileged Bubblewrap namespace with neutral build-user/host names. `FOX_BUILD_BASH=1` is exported before lunch because OrangeFox's vendor script reads a captured shell environment; a make-only flag does not prevent it from copying its prebuilt Bash. The source-built Bash avoids the vendor binary's unrelated home-directory locale/debugger prefixes. `out-public` must start empty for the first build; incremental rebuilds afterward use the same namespace.

`scripts/package-prerelease.sh` constructs the separate temporary-boot candidate and a project-owned slot-safe ZIP. `scripts/archive-release-sources.sh` supplies the stock GKI commit snapshot and recovery/GPL utility sources, including recursive Magisk utility dependencies. These are source archives, not tablet payloads. They do not constitute offline archives of all 399 Android repositories or prove binary reproducibility.

The host-only `prepare-public-ramdisk.sh` callback stages the compiled Bash explicitly, including after an incremental build left a vendor prebuilt behind. It omits generic FRP, AVB-disable and verity/encryption-edit addon recipes because they have no Uke-specific target/fallback guard. It accepts only the neutral build's exact generated ramdisk path, is not copied into the tablet ramdisk, and does not access a block device. The stock kernel's upstream `/root/initrd` literal is a runtime initrd relocation path in `init/do_mounts_initrd.c`, not a personal build path; it remains unmodified.

Font assets with unestablished redistribution terms are replaced in the generated ramdisk by aliases of `external/roboto-fonts/RobotoStatic-Regular.ttf` at commit `c50938f329a44707b06b336166c95ec2aa49c331`, with its Apache-2.0 notice retained. Distinct font-family selection is therefore not supported in this alpha. Recovery source snapshots exclude the original font binaries and unrelated vendor prebuilt/addon archives; the pinned repositories remain the acquisition references.

The nested ZIP audit found a private build prefix in a legacy addon updater. All generic bundled addon ZIPs are omitted from the alpha, with their optional UI actions unsupported. The shipped script sources are included directly in the source snapshot; the upstream binary installer directory is excluded. The project installation ZIP is audited separately and contains only its C++ helper, recovery image, POSIX wrapper, hash and documentation.

The native candidate adds reviewed host adapters for wimlib 1.14.5 and Dropbear
2025.89. Their original sources are read from immutable reference snapshots;
only the active build tree receives project configuration. NTFS resizing uses
a reviewed Android module patch against the locked ntfs-3g source. Exact pins,
licenses and build limitations are in `src/device/xiaomi/uke/ure-tools.lock.json`.
No upstream reference configure/build script is executed. Cryptsetup remains a
reference, and Btrfs is absent from the candidate userspace and stock kernel.

An incremental build exposed a callback failure when AOSP's absolute `/bin`
symlink was treated as a host directory. The callback now verifies that link
without following it, and a reviewed OrangeFox patch propagates callback
failures to the build. ELF closure checks also require the lpdump snapshot/binder
libraries and the Bionic bootstrap-loader alias. The vendor `ps` command path
uses source-built Toybox; an unused generic KeyMint helper is omitted from this
FBE-disabled profile. Final compressed-ramdisk audits verify these changes.

Packaging accepts a separate candidate directory name. The existing public
alpha is preserved. Manifest schema 2 records the base Git commit separately
from changed working-tree sources and includes exact input-file checksums.
Source snapshots contain pristine upstream files plus the reviewed patches,
project adapters, tests and original licenses needed to reconstruct this build.
