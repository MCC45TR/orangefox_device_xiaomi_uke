# Recovery function tests in a disposable ARM64 guest

`tests/check-functional-vm.sh` runs the production recovery CLI and packaged
filesystem tools in a generic Linux 7.2.8 QEMU guest. The CLI is copied unchanged;
there is no GUI relink or mocked syscall boundary. The guest init script uses
the packaged POSIX shell and Toybox. Neither Python nor an installed host OS
repair is involved.

Each invocation creates a new regular-file ext4 disk under the ignored build
directory. The Btrfs group adds one new regular-file Btrfs disk and matching
generic-kernel modules. No host block device, USB passthrough or NIC is attached.
The Btrfs modules are debug-stripped only in the disposable guest copy using
the pinned Android LLVM 20 host tool. This removes local DWARF build paths without
altering the original module tree or the production recovery payload. Btrfs
receipts bind the original module manifest and strip-tool hashes, and compare
both again after execution.
The guest checks its generic-virt identity and fixture command line before
operations. Image writes and chroot commands affect only those disposable disks.
Disks are selected by unique synthetic virtio serial tags, not `/dev/vda` or
`/dev/vdb` enumeration order. Bootstrap failures are trapped before mounting the
fixture media.
Shell/runtime setup errors shut down the test guest and cannot produce a pass.

| Group | Real operations and independent checks |
|---|---|
| `core` | Contained file browsing and traversal refusal; transactional editing and exact rollback; eleven private scale settings round trips; 512/4096 raw backup, missing-tail resume, internal-gap refusal, binary export, restore and source-independent rollback; home content/metadata restore, corrupt/stale/existing-target refusal and observed SIGKILL/resume |
| `filesystems` | Packaged format/repair, independent check, image inode/capacity preservation and exact complete rollback for ext4/exFAT/NTFS/FAT/F2FS; available resize workflows, unavailable-resize refusal and active-loop alias refusal before writes |
| `rescue` | Actual namespace/chroot execution, selected ESP binding, read-only enforcement, explicit writable session, descendant reaping, timeout, mount cleanup, changed-profile refusal and absent distribution-tool refusal |
| `btrfs` | Production CLI subvolume/snapshot/full and incremental send, offline stream checks, corrupt-stream refusal, retained-original rollback, inventory, scrub, bounded balance and shrink/grow through real guest ioctls |

Guest JSON records are delimited in the console and checked by host `jq` against
specific results and refusal codes. Byte comparisons, complete image hashes and
metadata comparisons run independently inside the guest. A pass needs the final
group exit marker, every required JSON assertion and no panic, Scudo abort or
fatal signal. Receipts bind runner, guest script, kernel, production CLI, complete
userspace-input manifest, initrd and console hashes. Source inputs must remain
unchanged throughout the run.

Use the host resource wrapper so large fixtures use disk-backed scratch and
heavy jobs remain serialized. Its configured j16/16 GiB budget applies to host
jobs; each generic guest uses 2 GiB. Pass the reviewed QEMU wrapper through an
explicit `env` command inside the resource wrapper. For example:

```sh
bash scripts/with-host-budget.sh function-core env \
  UKE_QEMU_SYSTEM_AARCH64="$PWD/build/qemu-runtime/qemu-system-aarch64-wrapper" \
  bash tests/check-functional-vm.sh \
  build/gui-vm/kernel/arch/arm64/boot/Image core
```

Replace `core` with `filesystems` or `rescue`. The Btrfs invocation also takes
`build/gui-vm/modules` and `7.2.8`. The filesystem group uses a 12 GiB sparse
file disk and a bounded 60-minute emulator timeout to retain separate complete
format/repair/resize journals for the 512 MiB F2FS case. Other groups use a
6 GiB sparse file disk and a 30-minute timeout. Guest RAM stays at 2 GiB.
Raw logs, test disks and synthetic root contents remain private.

## Boundaries

The rescue roots contain the packaged native shell/libraries and synthetic
Arch/Fedora identities. This proves session mechanics and distribution dispatch,
not an installed distribution's package database, initramfs or SELinux repair.
No fake package executable substitutes for those missing tools.

Filesystem repair fixtures start with clean newly formatted filesystems. They
test the packaged repair invocation, read-only independent post-check and exact
transaction rollback, not recovery of arbitrary damaged filesystems. NTFS repair
continues to use the limited `ntfsfix` adapter; this is not Windows `chkdsk`.
Resize fixtures are newly formatted empty filesystems. Their geometry,
independent checker and exact image rollback are verified; resize of populated
filesystems and preservation of moved user data remain separate required tests.
The active-loop refusal case selects the unused generic guest `/dev/loop0`
explicitly because Android Toybox's automatic lookup uses a `/dev/block` alias
not created by generic devtmpfs.

The generic kernel supplies Btrfs support that the shipping kernel has not yet
accepted. FAT offline resize depends on a packaged resizer; exFAT resize remains
unimplemented. Refusing an unavailable operation is tested independently of a
successful supported operation. Device-specific UFS/GPT/FBE/KeyMint behavior,
sensor streams, HDMI negotiation and physical boot remain separate requirements.
