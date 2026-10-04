# Linux rescue and installed boot audit

Select an already mounted Linux root and, when present, its mounted ESP in the
Linux manager. The native library reads contained os-release aliases and chooses
an Arch, Fedora, Debian, Alpine or generic adapter from ID/ID_LIKE. It never
substitutes the recovery's installed kernel or distribution for the selected OS.

Long management actions now use the [owned GUI job executor](GUI-JOB-EXECUTION.md).
Queue admission and backend completion are separate. Job status remains
reachable, and an advisory stop request never claims that native I/O or cleanup
has already stopped.

## Managed chroot

The session recreates the mount/PID/network/IPC/UTS namespaces, isolates mount
propagation and binds the selected root. Separate fstab entries for `/boot`,
`/boot/efi`, `/efi`, `/home`, `/usr` and `/var` are reused when mounted. A selected
ESP is bound automatically. Unmounted supported Linux/ESP partitions can be
resolved only by exact UUID/PARTUUID through the validated Storage Graph.
Ambiguous, encrypted or OEM sources fail. Only reviewed ext4/Btrfs/FAT mounts
and Btrfs subvolume selectors are interpreted; fstab helper commands are not run.

The root/ESP default to recursively read-only mounts. The session exposes
private read-only proc/sys, minimal character devices, private devpts/run/tmp
and an isolated network. Raw partition nodes and mount capabilities are not
given to the installed command. A native ELF64 command must match the recovery
architecture; its executable hash, selected root, ESP, fstab and scripts are
revalidated before execution. Installed runtime dependencies still need
distribution-specific acceptance.

| Action | Adapter and write policy |
| --- | --- |
| shell | installed native Bash, no profile/rc; GUI supplies an explicit bounded command; CLI can use interactive stdin |
| package-check | Arch `pacman -Dk`, Fedora `rpmdb --verifydb`, Debian `dpkg --audit` |
| package-repair | writable Fedora `rpmdb --rebuilddb`; Arch/Debian repairs need a separate reviewed package workflow |
| module-index | writable installed `depmod -a` for an exact module release |
| initramfs-rebuild | writable Fedora dracut or Arch mkinitcpio through verified Bash, for an exact installed release |
| selinux-relabel | writable Fedora restorecon on `/etc`, `/usr`, `/var` |

Initramfs scripts and hook trees referencing Python/PyPy or unreviewed aliases
are rejected. The existing output is backed up before rebuilding. This
conservative policy may reject an unused optional hook. No project Python
program or runtime is shipped, invoked in tests, or used for these adapters.
The installed shell is an explicit administration action; writable commands
and package scripts do not become atomic raw-image transactions. Keep verified
root/boot backups before enabling writes.

Example read-only request:

```json
{"schema":1,"action":"shell","write":false,"network":false,
 "timeout_seconds":300,"shell_input":"printf 'Selected Linux root\n'\n",
 "resources":{"memory_mib":1024,"jobs":2,"pids":128}}
```

```sh
uke-recoveryctl linux rescue-plan request.json --root MOUNTED_ROOT \
  --esp MOUNTED_ESP --output rescue-plan.json
uke-recoveryctl linux rescue-execute rescue-plan.json --root MOUNTED_ROOT \
  --esp MOUNTED_ESP --journal rescue-journal --confirm REVIEWED_PLAN_SHA256
uke-recoveryctl linux rescue-inspect rescue-journal
uke-recoveryctl linux rescue-cancel rescue-journal --confirm REVIEWED_PLAN_SHA256
uke-recoveryctl linux rescue-capabilities
```

The sealed request includes aggregate resource limits. Defaults are 1 GiB RAM,
two CPU-equivalent jobs and 128 tasks; reviewed ranges are 128–2048 MiB,
1–4 jobs and 16–256 tasks. `/tmp` is limited to one quarter of that RAM budget;
run/dev tmpfs and descendants also count toward the group. Memory high pressure
starts at three quarters, anonymous swap is disabled and OOM grouping is enabled.
Admission checks current system and ancestor headroom with an additional 512 MiB
GUI/kernel reserve. This admission reserve does not control unrelated processes.
MAKEFLAGS and CMake parallelism inherit the selected job limit.

The compiled backend requires the real cgroup-v2 memory, pids and CPU controllers,
an owned private domain and usable whole-group termination. It configures and
reads back the controls before publishing any session journal, then attaches a
blocked worker before it may create a namespace, mount or descendant. A caller
cannot choose a controller path/backend or accept an unbounded fallback.
Per-process file/core limits and fair scheduling supplement aggregate controls;
UID-zero RLIMIT_NPROC is not treated as equivalent enforcement. Missing required
kernel support/delegation stops execution with `rescue-resource-unavailable`;
insufficient reserve uses `rescue-resource-headroom`. The read-only capabilities
probe reports current admission limitations separately from live/physical proof.
See the [kernel cgroup-v2 contract](https://docs.kernel.org/admin-guide/cgroup-v2.html).

Timeout is bounded to 1–7200 seconds. Owned pidfds and the resource group permit
termination without trusting a saved PID. A cancellation requires the sealed
plan hash and exact retained owner; its acknowledgement means the request is
durable, not that cleanup is complete. The supervisor kills remaining descendants,
verifies group emptiness/removal, releases session mounts and checks required
filesystem sync before retiring ownership. Failed verification retains the owner
and pending cleanup for inspection; it never reports installed-content validation.
After an external forced restart, unresolved resource/namespace journal recovery
is not accepted as an automatic owner-release path. Do not delete ownership
records to bypass this boundary. Noninteractive console capture is private and
bounded to 2 MiB; the GUI shows a bounded preview. The GUI executor/cancel control
is addressed separately in AUD-013. Network-enabled rescue is unavailable.

Supervisor controller descriptors are closed in children. Namespace init is
nondumpable before starting the installed payload, whose capabilities exclude
ptrace/mount/resource administration. Protected negative OOM adjustment is reset
to zero; an already positive score stays positive. The payload's proc mount is
read-only, preventing it from restoring an inherited OOM exemption. These controls prevent the
payload from borrowing supervisor descriptors through its private `/proc/1`.

Host integration fixtures use a newly delegated user scope, with a separate
outside supervisor and a 1 GiB outer limit. Only that fresh scope is changed.
`tests/check-rescue.sh` exercises actual namespace mounts, selected ESP,
read-only/writable sessions, descriptor protection, stale plans, timeout and
owner-bound cancellation. `tests/with-rescue-cgroup.sh` with the native
`uke-rescue-resource-stress` executable exercises real memory/PID/CPU controls
and descendant termination. The stress oracle distinguishes a local OOM from
memory-high throttling followed by deadline termination. A responsive outside
supervisor does not prove GUI rendering responsiveness. Fedora package dispatch
uses a native test executable; real installed databases/initramfs, the shipping
kernel, fresh ARM64 packaging, combined VM and tablet acceptance remain separate.

## Installed kernel/boot audit

`linux audit --root MOUNTED_ROOT [--esp MOUNTED_ESP] [--output REPORT]` is read-only.
It checks indexed ELF module headers, architecture, vermagic, dependency paths,
kernel headers, initramfs newc/CRC archives, BLS assets, root selectors, FDT
structure and PE32+ UKI payloads. Gzip members, XZ and Zstd are decoded with
bounded input, output and decoder memory. Early cpio followed by a compressed
archive is supported. ESP BLS entries cannot borrow files from an unrelated root
boot directory.

All indexed modules within the limits are inspected. Reports keep 256 binary
records per release and every detected failure; a sample limit does not stop
validation. Userspace/module/kernel architecture, UKI uname/module release,
embedded initramfs releases and os-release are compared. Missing or invalid
assets produce findings; an empty inventory is not described as consistent.

The audit does not prove root UUID/PARTUUID block equivalence, Uke DT hardware
compatibility, kernel symbol ABI, Secure Boot signatures, module loading or a
successful boot. Initramfs module-directory releases are inspected, but every
embedded executable/module is not yet audited as a complete runtime closure.
Synthetic unit assets prove parser/mismatch detection, not kernel execution.
Installed boot selection and one-shot boot still require an accepted Uke backend.
The separate [boot-routing contract](BOOT-ROUTING.md) provides registered EFI
inventory, exact request review, consumed private-fixture attempts, retired-plan
replay refusal and inspected fallback/history. It does not execute an EFI image
or turn these installed-boot audit findings into routing acceptance.

Primary interface references: [Arch chroot](https://man.archlinux.org/man/arch-chroot.8)
and [Boot Loader Specification](https://uapi-group.org/specifications/specs/boot_loader_specification/).
