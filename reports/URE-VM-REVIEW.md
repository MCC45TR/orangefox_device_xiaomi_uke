# OrangeFox VM review and correction checkpoint — 3 October 2026

The boot-manager checkpoint was committed and pushed before this review.
This review found defects in DRM cleanup, literal theme defaults, viewport
geometry, complete HID report handling, initial menu selection and interrupted
backup publication. Project
patches and regression tests compile the actual upstream functions; final
acceptance is bound to exact source and executable identities.

This is **host/build/package/emulation evidence**. The complete roadmap is not
finished. A generic VM cannot establish either commercial model's firmware,
KeyMint/TEE, UFS, USB-C video, Android slot or physical boot behavior.

## Scope and isolation

The partition and Btrfs guests use Linux 7.2.8 on QEMU's generic ARM64 `virt`
machine. Every attached disk was created as a new regular file under the ignored
build directory. No host block device, USB device or NIC was attached. The
partition guest actually uses `sysrq` emergency reboot during `APPLYING`, then
boots again from its persistent test image. This tests forced guest restart;
it is not an electrical-power-loss or UFS-controller trial.

The GUI guest uses QEMU 11.1.2, TCG, modern virtio MMIO, virtio GPU, keyboard and
mouse, with a separate Linux 7.2.8 build and matching modules. Its scanout is
2560 × 1600 or 1600 × 2560; the product's fixed rotation produces portrait or
landscape logical screenshots respectively. This exercises both initial layouts,
not a physical orientation sensor or a live display-mode switch.
This is not a 75 Hz HDMI or MST test.

Generic mainline lacks the Android ashmem compatibility expected by this
OrangeFox build. The GUI executable is therefore **relinked for the VM only**
with a memfd code-cache adapter; a synthetic property area, disposable fstab,
fbdev path link and copied theme startup redirect are also used. The shipping
executable, shipping stock kernel and product theme are not replaced by these
adapters. `tests/prepare-gui-vm.sh` and `tests/check-gui-vm.sh` keep this distinction
explicit. The runner's process-smoke result does not automatically claim visual
or complete feature acceptance.

## Coverage matrix

| Area | Evidence in this review | Limit / remaining work |
|---|---|---|
| Transaction, identity, stale plans, backup and rollback | All native/CLI gates, including real host SIGKILL and exact original-byte comparisons | Live tablet block writes and UFS persistence remain unaccepted |
| GPT/layout and combined userdata/filesystem job | Native units/advanced-mode/ownership/graph tests; 10 generic-guest checks, emergency reboot, resume and full image rollback | Encrypted userdata preservation, front placement, real capacity and stock layouts require independent device acceptance |
| Six-LUN stock jobs, sparse/logical ranges and boot programming | Native, actual GUI callback, independent OEM catalog and extracted ARM64 CLI fixtures; observed 121-label namespace and eight logical allocations checked on six read-only generic-guest disks | Physical GPT ranges/GUIDs and firmware bytes are synthetic or absent; no six-LUN guest reset or actual Pad 7 / POCO Pad X1 stock restore was accepted |
| Filesystem management | ext4/F2FS/FAT/exFAT/NTFS staged tools, policy and negative fixtures; actual ext4 guest journal | exFAT resizing and comprehensive Windows-side NTFS repair remain limited; generic ext4 is not encrypted Android userdata |
| Btrfs | 14 native-ioctl guest checks: subvolume, readonly snapshots, full/incremental send, lineage/CRC/SHA checks, corrupt stream refusal, retained-original rollback, scrub, filtered balance and resize | Shipping stock kernel lacks accepted Btrfs support; receive/restore and boot integration are not completed by send tests |
| Linux/home backup | Native tree metadata/content/journal tests, actual CLI and SIGKILL resume | Real distribution installations and end-to-end boot recovery remain separate |
| Linux rescue and boot audit | Distribution-dispatch/chroot cleanup, package/initramfs/SELinux command policy, codec and executable-closure fixtures | Real Arch/Fedora repairs, installed root/DT/module ABI/signature closure and GUI cancellation remain open |
| OS one-shot routing | Android/Linux/Windows decisions, exact EFI/loader/default binding, ownership, single consumption, retirement, correlated fixture acknowledgements, unknown/fallback and actual callback/CLI tests | Private regular-file stores only; Uke/Aloha backend, trusted OS receipts, EFI runtime writes and actual loader execution remain unaccepted |
| Display and input | Actual renderer-function oracle across 60 described-row layouts, density/reload and explicit Apply tests, independent DRM/EDID fixtures, keyboard/hotplug tests; separate adapted GUI review | No exact shipping Android GUI boot, touch panel, dock, secondary independent CRTC, HDMI resolution/75 Hz, MST or physical hotplug acceptance |
| Files/editor/sessions | Native containment, metadata/editing/search and transaction fixtures, existing management callbacks | Comprehensive multiple-root/session lifecycle and all installed fstab/crypttab/BLS checks remain unfinished |
| Encryption and Windows deployment | Existing discovery and operation refusals reviewed | LUKS/header lifecycle and WIM/ESD/ESP/BCD recovery are not completed; BitLocker is deferred by owner |
| ADB and distribution | Existing backup/restore duplex/mock transport, extracted tools and installer gates; source pins, recursive payload/privacy audits and repeated packaging | Additional ADB terminal/session commands and signed updater/complete binary-reproducible CI remain open; SSH/network work is deferred by owner |

The initial review ran 23 CTests and the actual CLI gates, including pinned
address/undefined/leak instrumentation. The preview adds a 24th CTest; current
unchanged-input results are recorded below. `vptr` remains excluded
because of the previously documented pinned host runtime limitation. The DRM
allocation oracle additionally runs with poisoned automatic storage and pinned
instrumentation; it is separate from the preview CTest.

## Corrections and failed trials

1. DRM framebuffer planes: the old arrays left three unused handles, pitches and
   offsets uninitialized. An independent oracle compiling the actual allocation
   function reproduced refusal before correction. All unused entries now start
   at zero. Successful mapping and allocation-failure cleanup are checked.
2. DRM failure cleanup: resource enumeration had already been freed before three
   later error paths freed it again. The first actual GUI trial aborted with
   Scudo's invalid-chunk error. The corrected paths reach fbdev fallback without
   that abort. Standard topology defaults to one primary mixer unless the
   connector advertises its vendor topology; physical vendor behavior remains
   untested.
3. GUI defaults: `PageSet::LoadVariables` treats a hyphen in any default as
   subtraction. The actual boot page displayed model and firmware as `0` despite
   `poco-pad-x1` and `global-os3.0.303.0` XML defaults. The correction preserves
   URE-prefixed selections as literal strings, retaining existing theme geometry
   arithmetic. Regression tests execute the actual XML loader with model,
   firmware, hyphenated path, JSON, signed number, empty value, reload selection
   and original addition/subtraction cases. The extended test failed before the
   patch and passed after it.
4. VM harness setup: legacy virtio MMIO prevented probing; missing
   `EXTERNAL_STORAGE` crashed before main; the initial proc mount needed an
   absolute toybox path. A property trie and the platform libc exports were
   needed for the property fixture. `LD_PRELOAD` and `--wrap` did not replace the
   executable's ashmem symbol as called by shared code; the VM-only relink uses
   an explicit symbol alias. A stale logged link command also lacked liblzma;
   the public helper derives its recipe from the current generated build.
5. Test harness corrections: the extended actual XML loader needed the upstream
   `string` alias and RapidXML error-handler implementation in its host
   stand-ins. Earlier compile/link failures and an accidentally run stale
   executable are retained in private failed-trial logs; those are not pass
   evidence. The final baseline failure and corrected test were freshly built.
6. Responsive layout: stock variables fixed status, battery, trailing toolbar,
   content/console widths and navigation centers to a 1080-wide phone canvas.
   File search backgrounds/fields, credits tabs and right-side gesture strips
   also contained literal widths/positions. Explicit anchors now use the full
   logical viewport while compact controls keep uniform density. The actual
   stock-variable oracle covers five framebuffer shapes and seven percentages;
   the freshly compiled previous adapter fails with `Status bar retained the
   phone right edge` (log SHA-256
   `cb79b8cbcf1e9c82edb6095bbb58630b813c1345fedb3fcba28af6f78ec18a45`).
   Two baseline harness attempts failed before execution: the first assumed an
   installed jsoncpp include; the second copied a function opening into its
   prefix. Neither is acceptance evidence. Loading the real stock vars also
   reset the host fixture's initial lock value; the test now establishes that
   value immediately before reload and simulates the stock reset during reload.
7. Host memory pressure: overlapping build/test jobs left about 12 GiB of image
   fixtures on the host's RAM-backed `/tmp`. Kernel logs recorded global OOM
   termination of two Soong processes at about 18–19 GiB anonymous RSS. Those
   interrupted runs are not acceptance evidence. After preserving small private
   diagnostics, only the identified abandoned project fixtures were removed.
   `scripts/with-host-budget.sh` now serializes heavy jobs, runs them in a separate
   user-service cgroup (14 GiB high / 16 GiB maximum / 512 MiB swap maximum / 16
   CPUs), sets the pinned Go 1.23.4 runtime's soft heap limit to 12 GiB, and binds
   a disk-backed private scratch directory as `/tmp`. Native/sanitizer builds
   default to 16 jobs in the wrapper. A probe confirmed the initial 8 GiB cgroup maximum and disk-backed
   temporary filesystem. These controls apply only to host tooling; they do not
   alter the tablet payload, host-wide VM settings or unrelated applications.
   A further source/child-environment check found that the primary builder uses
   `env -i`, dropping the outer Go variables, and Blueprint resets GOMAXPROCS to
   runtime.NumCPU. Patch 0013 forwards only GOMEMLIMIT/GOGC in this host launcher;
   CPU affinity now bounds the process's visible CPUs as well as its quota.
   The first constrained but unpropagated run was deliberately stopped. It
   produced no cgroup OOM kill and is not a successful build record. The final
   wrapper probe reported a disk-backed /tmp, 12GiB heap limit, GOGC 40 and 16
   visible CPUs. Soong's reviewed host source is included in the source snapshot.

## Log interpretation

All final host, sanitizer, build, extracted-payload and VM logs were checked for
failures. Negative fixtures intentionally exercise refusal paths. In the GUI
VM, linker `/proc/self/fd` warnings precede mounting proc; resetprop errors reflect
an absent property service; Android vendor fstab, system_root, persist, brightness,
thermal, app-list and MTP-driver warnings reflect deliberately absent Android
hardware/runtime. Keymaster's upstream fallback log is not proof of KeyMint
trust or permission to unwrap/mount Android data.

`drmModeAddFB2 ret=-2` persists in the generic GUI guest: this virtio driver accepts
XRGB/ARGB, while this product requests XBGR. This format difference is distinct
from the independently reproduced uninitialized-array bug. The observed guest
GUI uses fbdev fallback. The synthetic flash-suspend message at QEMU poweroff is
not evidence about the tablet's UFS controller.

The `indeterminate033` image message ends the stock animation discovery loop:
`AnimationResource` loads numbered frames until the first absent frame. It does
not establish a broken added icon. The final two GUI logs contain no Scudo
abort, fatal signal or kernel panic and end with `URE_GUI_EXIT 0` and powerdown.

Raw logs and screenshots remain private. Candidate checksums, source identity,
exact executable comparisons and sanitized verification records are attached to
`artifacts/ure-vm-review-alpha`. Previously sealed candidates are unchanged.
Shipping GUI rendering, device boot, signed release and full roadmap acceptance
remain false.

## Optimization work to measure next

Host resource isolation is already implemented: use the budget wrapper for each
heavy build, VM or test command, rather than overlapping unrestricted jobs. It
requires the host's user systemd instance, cgroup v2 and bubblewrap. An unavailable
limiter fails explicitly; the wrapper does not silently run without its limits.
Interrupted scratch directories stay under the ignored build directory for
review, so no abandoned image fixture continues consuming RAM-backed `/tmp`.

- Cache a verified inventory/immutable source hash within one reviewed operation,
  and invalidate it on identity, range, mount, slot, snapshot or artifact changes.
  Keep the mandatory write-time checks, fsync and readback intact.
- Measure GUI reload time and allocations, then reuse unchanged fonts/resources
  where ownership is clear. Coalesce status updates and avoid full redraws when
  only one label changes; retain input and mirror frame correctness.
- Benchmark backup chunk size, compression level and bounded read/hash/compress
  pipelining on disposable images. Record throughput, peak memory and resume cost
  before selecting defaults; do not trade durable checkpoints for speed.
- Keep inventory scans bounded and cancelable, with explicit long-job state.
  Measure large trees and many subvolumes instead of extrapolating tiny fixtures.
- Benchmark optimized host fixture builds while explicitly retaining assertions.
  The current native CMake build has no optimization flag; filesystem transactions
  spend substantial time hashing original and rollback images. An optimized
  fixture mode must preserve every refusal/assertion and be identified separately
  from instrumented debug checks. Do not enable a Release mode that silently
  removes the assert-based DRM and input oracles.
- Reuse pinned build outputs and dependency caches, split independent CI gates
  into bounded jobs, and distinguish repeated packaging from a clean, fully
  reproducible binary build.

The owner subsequently requested 16 build jobs with a 16 GiB RAM budget and
compiler caching. The final host policy uses that hard memory cap, a 14 GiB high
threshold, 12GiB Go soft limit, 16 allowed CPUs and two separate 10 GB C++ caches.
The first j16 trial hit the 512-task cgroup limit (`newosproc`, 476 Go OS threads),
not an observed global OOM. TasksMax is now 2048. The lower-budget and failed-task
runs are excluded from acceptance. The public build and native/sanitizer entry
points automatically use isolation; the service owns the serialization lock,
and command completion and memory/task metrics are recorded separately.

The build before the backup-publication correction completed in 2 minutes
45 seconds. Its
cgroup recorded memory.peak 15,033,892,864 bytes, 2,804 high-threshold events,
zero memory.max events, zero OOM/kill events and pids.peak 1,057. This is a
successful incremental build on the reviewed host, not a claim that a complete
16 GiB host can spare that entire budget. The Android cache's cumulative counters
were 13,447 cacheable calls, 4,887 hits and 8,560 misses, with 0.8 GB stored under
the 10 GB cap; those counters include earlier attempts rather than timing one
clean build. That intermediate shipping recovery SHA-256 was
`b7e6ce0ccf8b9347e9f6923c51a878c63b8b290ae4daedce1e5a7fe981cd0fa5`.
It is excluded from final acceptance after the publication correction below.

## Additional findings from final visual review

- Complete HID reports exposed phantom touchscreen releases after mouse/key
  SYN_REPORT packets. Patch 0014 classifies paired absolute-position capabilities
  and preserves relative/key edges plus dropped-queue cancellation. The native
  oracle includes repeated button reports and real touch down/up with interleaved
  mouse sync, failed capability probes and incomplete axis pairs.
- The gesture indicator now occupies the center of the reserved footer; its
  home-swipe region covers that footer. Optional back/home/console targets occupy
  three evenly spaced cells with disjoint bounds. The pre-correction oracle
  failed with `Navigation footer touch targets overlap` (log SHA-256
  `c048d4232c59c340cbb984e103a5d7e9c213ce2db0fbf55f6cd696cead989f8a`).
- Declaring a vertical center did not override DataManager's immutable constants.
  A failed adapted screenshot retained y=960 on a 2560-high canvas. The initial
  mutable-only host stand-in missed this. The corrected stand-in models constant
  precedence/write refusal; direct placement now consults explicit viewport
  names before stored values. Standard, customizable and restore-default splash
  templates retain full-screen centers. The corrected-boundary baseline failed
  with `Splash logo is not centered vertically` (log SHA-256
  `4afa5480f16589f8e58d658b751efda9a3c06fc79b967b04e9a3c428ffce9100`).
- Action-only menu rows with empty backing values were all selected, causing page
  focus to scroll the URE menu to its last entries. Patch 0015 preserves action
  menus' position and visibility changes while maintaining stored selection
  scrolling in bound lists. The actual constructor statements and full
  focus/variable-change functions are included in the oracle. Its freshly built
  baseline failed with `Action-only menu items inherit an empty selected value`
  (log SHA-256
  `39813eccadbbddd9fbf4f2e342e3b834a41ff1c7201c173b595244229af0ac44`).

The intermediate portrait runs explored scale changes, menu navigation and
keyboard folder navigation. They are excluded from final visual acceptance:
some capture filenames overstated the percentage actually shown, and an earlier
splash was vertically misplaced. Observed labels and final source identities
take precedence over filenames. Short host-generated presses
were not reliable under TCG; the QMP helper uses a one-second held press and
checks protocol responses. This is test timing, not a tablet-latency measurement.
The first list-state harness compilation failed on its baseline-only unused
parameter; it is excluded from acceptance. Project warnings remain fatal.

The subsequent full sanitizer run exposed an interrupted backup-publication
defect: SIGKILL between linkat and unlinkat left a verified published chunk with
two links, correctly rejected by the strict manifest verifier. A host-only
linkat wrapper freezes that boundary in a freshly built old implementation and
deterministically reproduces the refusal (log SHA-256
`77fafe2bc7f52fdd079358c6786bb1d1d418ea720e23f6ea4fa3875dced63e9e`).
Backup and raw-restore mirror chunks now use renameat2/RENAME_NOREPLACE and
directory fsync. Foreign hardlinks remain refused. The corrected instrumented
probe passes SIGKILL/resume. The graphical trial using the earlier shipping ELF
was deliberately stopped when this source correction became necessary; it
confirmed splash centering but is not final process or visual acceptance.

## Intermediate source build

The build containing the atomic-publication correction completed in 2 minutes
54 seconds with 16 jobs. Its cgroup recorded memory.peak 15,035,535,360 bytes,
3,995 high-threshold events, zero memory.max or OOM/kill events and pids.peak
1,046. Cumulative Android compiler-cache counters at this checkpoint were 13,449
cacheable calls, 4,888 hits and 8,561 misses, with 0.8 GB stored under the 10 GB
cap. These are cumulative cache counters and an incremental build result.

| Executable | SHA-256 |
|---|---|
| Shipping recovery | `341aa2adb4e88df7c5a321e76814ff205b8dc8c21d6b3a22fc0effd34d3ef114` |
| Shipping native CLI | `9199d27343d258480f97898ae9bf7593370206ba4eee17dcc64d07383891ac5b` |
| VM-adapted recovery | `32e53eee6e5f42176e433cb10407112a5c058b117d1f6b28297c50302ced88d1` |
| Btrfs native fixture | `a33b0fc9c48a53873142cf23d116589a503d91bce7115265003ddff3dace8053` |

The Btrfs guest record remains bound to its identical fixture executable and
runner. The changed native CLI passed a renewed partition guest-reset run: all
ten checks passed across an actual emergency restart, source-independent resume
and byte-exact complete rollback. All 23 native CTests and the full CLI gate
also passed, with unchanged inputs identified by SHA-256
`3e0354b4ea8460febc050bf5e732b5e7b29483c9e5edacc586ce814ecf10a805`.
This checkpoint precedes the described Extra menus and explicit Apply design;
its source identity and graphical executable are superseded below.

## Described menus and deliberate scale application

The Extra footer tab now opens six categories with smaller explanations under
every one of the 360 added list entries. Storage and diagnostics retain separate
inspection and maintenance groups. The 23 icons are pinned Lucide 0.563.0
rasters with source/pixel hashes, theme tint and original ISC/MIT license notices.
The stock four-cell workshop remains four cells while the footer uses five.

Compact (50%), Balanced (75%) and Large (100%) presets, custom input, reset and
load update a selection only. The fixed bottom Apply control validates and
queues the existing render-thread reload; save uses the applied size. Native
tests compile the actual callbacks and check deferred reset/load, one explicit
reload and invalid-selection refusal. The actual row-renderer oracle covers
60 layouts, scaled icon/text separation, bounded text, arrow margins, smaller
description fonts and the legacy font fallback.

Failed trials are retained privately. An undefined button font left Apply
blank, and the added pages omitted the gesture template. The final theme uses
the declared Secondary font and explicit gesture templates. Direct arithmetic
in placement attributes was not evaluated, so the fixed button and scroll
reserve now use declared theme variables. A missing fifth navigation variable
also failed the real stock-variable oracle before correction.

## Observed stock namespace

STOCK-ADB-20261003-01 establishes 121 labels and indices across six disk groups,
and eight active super-relative allocations. The guest checks those names,
indices, parent groups, read-only policy and logical starts/ends using the
shipping native CLI and liblpdump. Four malformed fixture variants are refused
before disk creation. Physical GPT geometry and GUIDs are synthetic; firmware
and encrypted userdata contents are absent. The library entry adapter is VM
only, because the normal lpdump client needs an Android Binder service missing
from this generic guest. Initial mount/service/zero-size/text-end assumptions
failed before the corrected namespace trial passed.

The observed POCO Pad X1 is on OS2.0.205.0.VOZMIXM / Android 15. This recovery
still uses the Global OS3.0.303.0.WOZMIXM / Android 16 build profile. The inventory
does not establish flashing or stock-restoration compatibility for that device,
and does not replace an independent Xiaomi Pad 7 profile.

## Earlier interface build, superseded by the scale preview checkpoint

The final interface build completed in 2 minutes 47 seconds with 16 jobs.
Its cgroup recorded memory.peak 15,033,954,304 bytes, 4,107 high-threshold events,
zero memory.max or OOM/kill events and pids.peak 1,058. These are incremental
build metrics on the reviewed host under a 16 GiB job cap, not a promise that
an entire 16 GiB host can dedicate that amount while running other applications.
Cumulative Android compiler-cache counters are 13,487 cacheable calls,
4,889 hits and 8,598 misses; these include earlier attempts.

| Executable | SHA-256 |
|---|---|
| Shipping recovery | `5c1ddb38c0a503495bd4d5d062359df245d7a4106a7471b9cf49b3fc63ccaa3b` |
| Shipping native CLI | `9199d27343d258480f97898ae9bf7593370206ba4eee17dcc64d07383891ac5b` |
| Final VM-adapted recovery | `be9fc825ebf8380cc43600dd28e6e8d058ee7a01b1b9e27eb4b622bcc3d8dcac` |
| Btrfs native fixture | `a33b0fc9c48a53873142cf23d116589a503d91bce7115265003ddff3dace8053` |

All 23 native CTests and the full CLI gates passed with source inputs identified
by SHA-256 `f0d13bc809a296641e3996d4b6628eac350c298ac2bec8c6ca988dfbfa9192a3`.
The Btrfs and emergency-restart records remain bound to their identical fixture
and shipping CLI. The new namespace record binds its fixture, generator,
runner, shipping library and VM entry/property helpers separately.
The matching final pinned sanitizer run passed all 23 CTests in 421.89 seconds,
plus the actual DRM allocation oracle with poisoned storage. The manually
reviewed graphical receipt is required before sealing. Repeated packaging
produced identical image and ZIP bytes; the final
extracted ramdisk passed recursive privacy, no-Python, ELF closure and ARM64
CLI checks. Complete roadmap and physical acceptance remain false.

## Scale preview checkpoint

The five presets are Very small (55%), Small (65%), Medium (75%), Large (85%)
and Very large (95%). Custom scale provides eleven five-point selections from
50 through 100, with a size preview before explicit Apply. The actual widget's
605 density/applied/selected combinations pass bounded drawing, small-target
warnings and font-reference ownership. Menu heights inside scroll containers
follow their row counts. All 389 added entries have smaller descriptions.

Monitor image size is independent of tablet density and applies together with
resolution and refresh rate. The actual sink passes 44 rotation/size conversion
cases, invalid-request refusal and redraw after an idle size-only request. The
first regression failed because packed scale bits incorrectly disabled the
conservative automatic mode preference. Mode policy now checks only the three
mode fields, and the applied mode reports its actual image size. No actual dock
scanout has been tested.

The inactive Extra icon was missing on stock Files because those pages loaded
before the maintainer image include. Both Extra glyphs now belong to shared
early resources. The preview patch's initial malformed hunk was refused; repeated
preparation also exposed changed context in older monitor/density patches.
Preparation still reconstructs and compares the complete reviewed stack through
an isolated index. The corrected repeated preparation passes. The intermediate
native run was explicitly stopped before its completion receipt; it cannot seal
this checkpoint.

The latest shipping build passes in 3 minutes with 16 jobs. Its cgroup records memory.peak
15,033,548,800 bytes, 4,341 high-threshold events, zero memory.max or OOM/kill
events and pids.peak 1,052. Cumulative Android cache counters are 13,539 cacheable
calls, 4,889 hits and 8,650 misses, with 0.9 GB under the build's 10 GB limit.

| Current executable | SHA-256 |
|---|---|
| Shipping recovery | `2493670a7e06170d0bec52acdafa08e6a274ea72086ae233277cf45aafd78727` |
| Shipping native CLI | `9199d27343d258480f97898ae9bf7593370206ba4eee17dcc64d07383891ac5b` |
| VM-adapted recovery | `5c82f4df107a6ec764807e33e38a1056d1822e882739f9946d39bbbbbc3b97bf` |

All 24 native CTests and the full CLI gates pass against input-manifest SHA-256
`090bb8ff5a03921516dd0d6ad6292200af4a4f64bfc8940473ceded8d3f57eb3`.
The storage CLI, Btrfs fixture and namespace helper/library identities remain
unchanged, so their separately bound guest records remain applicable. The two
images and installer ZIP repeat byte-for-byte. Fresh extracted ARM64 CLI,
dependency closure and recursive privacy/Python audits pass. The matching pinned
sanitizer run passes all 24 CTests in 431.41 seconds and the additional poisoned
DRM-allocation oracle. The final graphical review below supplies the separately
bound visual receipt for this checkpoint.

The newer calibration audit supplies SSC board-axis candidates and identifies
the front STK3BCx non-wakeup ALS as Android's automatic-brightness source. It does
not supply a verified recovery stream or a matching OS3 service/firmware closure.
The availability page therefore reports automatic rotation and brightness
unavailable and links to manual screen controls. No calibration data, generic
sysfs collection, sensor activation or speculative DT connection is installed.
The source finding that reading `fsm_re25_show()` initiates calibration/save
reinforces the existing diagnostic allowlist. See [sensor readiness](../docs/SENSOR-READINESS.md).

## Final adapted graphical review

The final portrait job `WX3RGO` and landscape job `U8P6aw` both finish cleanly.
Nine portrait and eleven landscape screenshots were manually inspected and
hashed. In both orientations, actual applied labels confirm 50%, 75% and 100%.
Selection changes the preview while the current size stays unchanged until
Apply. All eleven custom choices, the warning at 70% or below, described menus,
scaled icon gaps and the fixed Apply/gesture regions were reviewed. The footer
keeps its full viewport width and centered gesture indicator; Extra has both
its selected glyph and its unselected Files glyph. Mouse navigation works in
both guests. F6, Down and Enter change the rendered Files path from `/sdcard`
to `/`, separately confirming keyboard navigation. The landscape monitor page
shows independent resolution, refresh and image-size choices with explicit
Apply; it correctly reports that the guest has no usable mirror connector.

The sanitized visual receipt SHA-256 is
`db145b786469942ef9697e7979ba4c88ff42f587636a571a3b572392a017a9cd`.
It binds the shipping/adapted recovery, runner, controller, property helpers,
kernel, console logs and original screenshot identities. The console hashes
are `24e2905bda8dd1de037e52940da888baf8dba655c78f4fd6f56f334a2c20ebab`
and `427301725d1de23d9e0fc04d88da6e17e680be636f4263db018c8b0045ad70c0`.
Original screenshots and logs remain private. The receipt is copied into the
candidate as `ADAPTED-GUI-VM-VERIFICATION.json`; it leaves physical, shipping
kernel, unmodified shipping GUI and complete-feature acceptance false.

The first seal attempt correctly refused a package-repeat receipt from the
earlier pre-preview images. That record was retained privately. The already
recorded final package pair, independently compared before sealing, matches
fastboot image `fdb1b7e00ec6f66d30040b82a78e6e45893b8b7fbe8e92bb5911638fc7a80389`,
recovery image `77f7cbf86aaa97a62fb0bfbb3781eb11dffbf9430709ac4fc3e00e92fc0b6c38`
and ZIP `52d67a7065f826c72f4616047bf158edae688352af1b4b2b6831f41b852fcf79`.
The final candidate uses that matching repeat receipt; no identity gate was
relaxed to accept older package bytes.

Two launch-environment mistakes occurred before these final jobs. The isolated
systemd service did not inherit QEMU variables placed outside the wrapper, and
the packaged QEMU executable needed its matching module directory to expose
virtio GPU/HID devices. Passing an explicit `env` command inside the wrapper and
the matching `QEMU_MODULE_DIR` corrected the launch. Earlier jobs that refused
before boot are not passes. An interrupted portrait guest `3jIMxN` terminated
with SIGTERM and has no clean completion receipt; it is also excluded. An
immediate landscape capture still displayed 100% after queuing Apply 75%; the
accepted later capture visibly confirms Current 75%. QMP acknowledgement and
filenames are not evidence that an asynchronous guest reload has completed.

This review completes this source/build/package/emulation checkpoint. The
remaining roadmap items and physical boundaries are listed in the coverage
matrix; neither the namespace fixture nor the sensor audit authorizes live
tablet storage writes or calibration changes.
