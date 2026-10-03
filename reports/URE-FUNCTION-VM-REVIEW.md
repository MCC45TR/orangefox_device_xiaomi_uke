# Production recovery CLI function-test review

Date: 2026-10-03. Scope: OrangeFox on a disposable generic ARM64 Linux guest.

This checkpoint adds function execution to the earlier native and adapted GUI
review. It uses the unchanged production `uke-recoveryctl` and the packaged
filesystem tools. Guest syscalls, loop ownership checks, process/mount
namespaces and Btrfs ioctls run against newly created file disks. No host block
device, USB device or NIC is attached. Device-specific physical acceptance
remains separate.

All four groups passed against one frozen runner/script/runtime identity:
172 JSON results and 29 independent data/metadata/cleanup checks. Each guest
and its host assertions finished successfully. Earlier failed or superseded
trials remain private and are summarized separately; a guest exit marker alone
is insufficient. This completes the declared generic-guest checkpoint, not the
whole feature roadmap or either tablet's physical acceptance.

## Runtime and source identity

| Input | SHA-256 |
|---|---|
| Shipping recovery CLI | `9199d27343d258480f97898ae9bf7593370206ba4eee17dcc64d07383891ac5b` |
| Complete shipping userspace file/link manifest | `004fb6ab9ed77ec623869ce7f2dc7c4f3310fc392a3c72083bb187dcf64854c0` |
| Generic Linux 7.2.8 kernel | `0dafe914751929b011f6a3d8d84e47c51452de4b604d123714b64dd2546e8a80` |
| Existing native source/test manifest | `090bb8ff5a03921516dd0d6ad6292200af4a4f64bfc8940473ceded8d3f57eb3` |
| Final function host runner | `bac830fbec8df548647e37a54fed6edec5cac2fbd08a47b526397d75ce9064d6` |
| Final function guest script | `9c8c45053bdac0b404e6e2342d70a5969803a433714486ab47d2eb7c652b6dba` |

Production runtime inputs are unchanged from the reviewed UI/build checkpoint.
The function runner freezes its own source, guest init script, kernel and
complete shipping payload before execution, then compares them after all
assertions. Sanitized receipts also bind the guest initrd and console. Raw
logs, synthetic roots and images remain private.

Final core/rescue/Btrfs host resource peaks are 678,129,664 / 598,142,976 /
601,329,664 bytes. Their cgroups record zero memory-max, OOM and OOM-kill events.
These are this host's resource-isolated function jobs, not clean-build peaks or
a physical 16 GiB computer acceptance result. Each guest is configured for 2 GiB
RAM; sparse fixture disk capacity is independent of resident memory.

## Function coverage

| Group | Required result | Acceptance |
|---|---|---|
| Core | 73 JSON assertions and six independent byte/metadata checks | Passed |
| Filesystems | 58 JSON assertions and 16 independent byte/geometry/refusal checks | Passed |
| Managed rescue | 11 JSON assertions and four independent namespace/cleanup checks | Passed |
| Btrfs | 30 JSON assertions and three independent content/ioctl checks | Passed |

The accepted Btrfs receipt SHA-256 is
`a76f9eb1096397c00b4c94e33377377e0a7ee9f6bfd15d76e8f0474659303261`;
its console SHA-256 is
`3beb8769406486146b061924039ec0d2c833fade8dfdc8680927b76214bdf1c0`.
The original five-module manifest SHA-256 is
`2facc755ce97d6692a494011b945dee7006e11b873f265ec035a8e9e2cf4491f`.
Only the disposable module copies are debug-stripped with pinned host-tool
SHA-256 `d2a3191ad2228bb60c35e18466615cb53ed7263c2e73134624349f0367cb1f88`.

The accepted rescue receipt SHA-256 is
`50eb83bd26a42468b2ecae0626b88d9dd00ad1e745b420b85e06fbb3706ac8aa`;
its console SHA-256 is
`5f745812f2bc33db26c0143488ec1fa98bf18a17f347343358df69fcf9a31495`.
Nested readonly/write/timeout/Fedora console logs were read from the powered-off
fixture disk. Read-only write errors are expected test stimuli. Linker-config
warnings reflect packaged Android shell/libraries in synthetic distribution
roots; they are recorded as fixture limitations, not installed-distribution
acceptance. Timeout cleanup reports unsuccessful `TIMED_OUT` with the worker
reaped, mounts released and no cleanup pending.

The final-source core receipt SHA-256 is
`c1a604aecbec113d7856f12c9eb2f1aac1c3334b504968d6f196ce9b93b05b51`;
its console SHA-256 is
`0a8aca8e633ee9bca7ca030ed62c3ad2c76036ffa08cff037add45c6a8e0a025`.
Expected negative records include confirmation, internal gap, invalid scale,
traversal, unavailable pre-capture state, existing target, stale source and
corrupted JSON refusals. Every refusal is paired with the applicable independent
no-side-effect assertion.

The accepted filesystem receipt SHA-256 is
`c6b28c3709b5f3c0121a4740532380c032bfede3930aba7018b1bcade0351c8d`;
its console SHA-256 is
`eed12b8fc6e5c1490be3bbb57a16563dc0a740f12e4e16ebd4a26f0719b075a2`.
All five format and clean-repair results include successful independent
read-only post-checks. Supported resize and complete original-image rollback
passed for ext4/NTFS/F2FS; unavailable exFAT/FAT resize and a live-loop alias
were refused with the exact expected codes and no source mutation. The final
guest powered down after 2,138 seconds. Host resource peak was 2,406,281,216
bytes, with zero memory-high/max, OOM or kill events.

Observed spans between each format-plan completion and its final original-image
rollback completion are below. They include repair, resize/refusal, shell hash
checks and journaling in TCG; they are not isolated formatter measurements or
tablet throughput benchmarks.

| Empty filesystem fixture | Image capacity | Span, seconds |
|---|---|---|
| ext4 | 64 MiB | 190.594 |
| exFAT | 64 MiB | 121.636 |
| NTFS | 64 MiB | 186.948 |
| FAT32 | 128 MiB | 238.128 |
| F2FS | 512 MiB | 1,341.871 |

Core covers contained browsing/search, traversal refusal, file edit commit and
exact rollback, all eleven private 50–100% scale setting round trips, two raw
sector-size workflows and Linux home backup/restore. Raw capture resumes a
missing tail, refuses internal holes, exports the stored binary chunk, restores
the requested complete image hash and rolls back independently after its source
backup is relocated. Home comparisons include content, hardlinks, symlinks,
FIFO, permissions, xattr, sparse allocation and opaque names. The interruption
case observes a published blob while capture is still running, sends SIGKILL,
then requires verified resume.

Filesystem cases invoke the packaged formatter and repair tools on staging
images. Production execution requires a separate read-only checker before
applying the prepared bytes. Full hashes, image inode and physical container
capacity are compared independently after rollback. Repair starts with clean
newly formatted fixtures; it does not prove recovery from arbitrary corruption.
NTFS repair remains the limited `ntfsfix` workflow. exFAT resize is unimplemented
and FAT resize lacks its packaged resizer; the harness requires explicit refusal
without creating a plan or altering source bytes. An attached loop alias must
also cause refusal before journal creation or source mutation.
Resize uses newly formatted empty fixtures: geometry and exact original-image
rollback are tested, while populated filesystem resize/data movement remains a
separate requirement before live-storage acceptance.

Managed rescue uses synthetic Arch/Fedora identities with the packaged native
shell and libraries. It executes the real namespace/chroot implementation and
binds the selected fixture ESP. Read-only source/ESP writes must fail, private
runtime files must not persist, child processes must be reaped and the outer
mount table must match its baseline. Explicit writable sessions and timeout
cleanup are checked separately. A changed distribution identity invalidates its
plan; absent package tools remain unavailable. There is no fake rpm/pacman
executable or package database.

Btrfs uses matching generic-kernel modules and the production CLI, extending
the earlier non-shipping native ioctl fixture. Full and incremental streams
must pass offline checks, corruption must be rejected and rollback must retain
both original and selected snapshot contents. Inventory, scrub, bounded balance
and 384/512 MiB resize operate through real guest ioctls. Receive/restore and
shipping-kernel support remain unaccepted.

## Failed trials and corrections

| Trial | Finding and correction | Private console/log SHA-256 |
|---|---|---|
| Payload gate before boot | A literal synthetic home path triggered the privacy gate. The fixture now composes a relative home name; the scanner was not weakened. | `f4e9b5d4bb7e2e1008ebaf9f7c4dbdd05b04b8b3d7358b9496be8c4e6352f158` |
| Core shell helper | mksh's existing `hash` alias conflicted with the helper name. Rename to `plan_digest`. | `6033a2fcaab76e0ffdd64de32fb6b28724536590f94433814dbdf52baa33fa6e` |
| Raw partial assumption | Deleting one middle chunk correctly yielded `invalid-backup`. Test internal-gap refusal separately from deleting the entire tail for resume. | `f448e04c75f3296bc57bbfb245f35a254b9e5e237ccf7dbe099cc11d87318a8b` |
| Home FIFO mode | The private umask produced 0600; restore preserved it. Explicit source `chmod 640` establishes the intended fixture. | `a2e8154df3b732e3769de33cf6c77c505339003eea41a9a9ff3e60cb7b440d7c` |
| Rescue shell bootstrap | Missing temporary directory broke a here-document and PID 1 exited. Set private `TMPDIR` and trap unexpected shell exits. | `751e523b1ad308ce0f154c150c302294d30e23b8b281c6eb2f0a83ae5ca0602a` |
| Core host oracle | Guest operations completed, but the host expected `incomplete-tree` before any state record existed. Require the actual `path-unavailable` refusal and no output target. | `01d3cb99a98451af2139c0c22d1f4c0deb8850c414603bbf8c2f7e850318a8e2` |
| Generic loop node | Toybox auto-discovery returned an absent Android alias. Select and validate the fixture-owned `/dev/loop0` exposed by generic devtmpfs. | `030afd42f6b92f7a8cdf87d3cf77d0a7aaa6d2b964364da51b1336b30547edcf` |
| Filesystem fixture capacity review | The run completed ext4/exFAT/NTFS checks, then was deliberately stopped after source review showed that three retained F2FS journals and their staging margins exceeded the 6 GiB fixture budget. It has no clean group receipt. The new fixture uses a 12 GiB sparse disk and a 60-minute TCG deadline, retaining the 2 GiB RAM limit and all safety checks. | `988ec54624fa2ac1c6e755553a6067cb9c5c24598f2cc9b35208ef0283cab112` |
| Generic module privacy | Local DWARF paths in the newly built generic modules were rejected before boot. The pinned Android LLVM strip tool now removes debug only from disposable copies; source modules and production recovery remain unchanged. Original module and strip-tool hashes are bound before and after the test. | `8887ae58f68f649962369088449c1e1b5960298b36ac8db382aa8e766710fef8` |
| Two-disk enumeration | The second virtio disk became `vda`, so a name-based ext4 mount selected Btrfs and failed before any CLI operation. Bind unique synthetic serial tags and resolve each disk through sysfs. Move the shell failure trap ahead of all mounts. | `fc9e18649e31f580c0a28ac8313341fdff755acdd83c460a6b651188d8c5cdd6` |

No production ELF was changed to make these fixture corrections pass. The
runner reports a failed JSON record and its predicate, powers down on unexpected
guest shell failure and withholds the acceptance receipt if any required check
fails. Historical passing core/rescue records are renewed after the guest script
changes rather than being transferred to a new source identity.

Accepted logs contain no panic, fatal signal or Scudo abort. Early linker
realpath warnings occur before the first proc mount. Nested rescue linker-config
warnings come from synthetic Android-library roots. Generic pflash notices at
powerdown report state 20: Linux 7.2.8's CFI enum defines this as `FL_SHUTDOWN`,
which its reset handler sets before shutdown. This source comparison explains
the generic-device notice; it does not describe a UFS write or tablet firmware
event. Each accepted guest ends with its success marker, synchronization,
fixture unmount and clean powerdown. The host still checks the emulator exit and
all later failure notices before writing a receipt. Expected negative
CLI results are matched to exact refusal codes rather than ignored.

## Candidate closure

`artifacts/ure-function-vm-alpha` preserves all four accepted function receipts
alongside the separately bound native/sanitizer, partition restart, Btrfs fixture,
adapted GUI and stock-namespace records. Source snapshots retain their original
licenses; the function guide and reviewed summary are included. The earlier
sealed `ure-vm-review-alpha` candidate is preserved.

Both images and the installer ZIP were packaged twice and compared byte for
byte. Their SHA-256 values remain:

| Artifact | SHA-256 |
|---|---|
| Fastboot boot image | `fdb1b7e00ec6f66d30040b82a78e6e45893b8b7fbe8e92bb5911638fc7a80389` |
| Recovery image | `77f7cbf86aaa97a62fb0bfbb3781eb11dffbf9430709ac4fc3e00e92fc0b6c38` |
| Installer ZIP | `52d67a7065f826c72f4616047bf158edae688352af1b4b2b6831f41b852fcf79` |

A fresh audit extracted the actual compressed ramdisk, checked recursive
privacy/no-Python and ELF dependency closure, compared shipped/source-built
tools and passed the ARM64 CLI fixtures. Reusing this production build is
supported by the unchanged native input manifest and exact ELF/payload hashes;
no clean binary-reproducibility or physical flag is inferred from packaging.
The extracted-audit and source-archive host log SHA-256 values are
`5732c068bbf36cc382635b83861c6c58405c41181f4a36424103718e5dd34cf9`
and `034265b5ec2ae101b5ae5211db541b9616ba3a106f62b9fec7f6ab39c65437f2`.
The parent workspace's nineteen host checks also passed and explicitly reported
that no hardware tests were performed.

A negative seal trial changed only a temporary core receipt's guest-script
hash. The release gate refused it with status 1 before creating the artifact
manifest. Its trace SHA-256 is
`56a84fbf49bd3242b81755e7fd47ac6e593b154864f4cccc9a1bfddc2f5b0234`.
The original accepted receipt was restored byte for byte, and the positive seal
then passed with all four exact source/runtime records. The final seal follows
the focused source/evidence commits and verifies the complete candidate checksum
set. Commercial-device, shipping-GUI, full-roadmap and clean-binary acceptance
stay false.

## Remaining validation and optimization

The detailed roadmap coverage matrix and prior visual/build evidence remain in
[the VM review](URE-VM-REVIEW.md). Current function evidence does not accept live
UFS/GPT stock restore, encrypted userdata/KeyMint, physical forced reboot,
installed distribution package/initramfs/SELinux repair, Btrfs receive/boot
integration, LUKS/WIM/ESP/BCD recovery, HDMI negotiation, QHD75 output or sensor
streams. BitLocker and SSH/network work remain deferred by the owner; ADB stays
the authorized remote transport.

The existing j16/16 GiB host wrapper, disk-backed scratch, serialized heavy jobs
and verified ccache remain enabled. Each function guest uses 2 GiB. The next
optimization work should measure staging/hash/copy costs per operation, cache
immutable build/test inputs by their complete hashes and bound inventory scans
and idle redraw. Durable journals, verification and rollback must remain intact.
Performance changes require fresh correctness receipts rather than reuse by
feature name. The larger F2FS span and retained-copy disk budget make bounded
read/hash/copy pipelining and immutable-copy reuse useful benchmark candidates.
Such changes must preserve every write-time ownership check, fsync/readback and
source-independent rollback; no shortcut was enabled to accelerate this test.

See [the reproducible function-test guide](../docs/FUNCTIONAL-VM-TESTS.md) and
[the dated lessons](https://github.com/MCC45TR/uke-linux/blob/codex/ure-roadmap-integration/docs/lessons/2026-10-03-RECOVERY-FUNCTION-TESTS.md).
