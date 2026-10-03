# Linux rescue and installed boot audit

Select an already mounted Linux root and, when present, its mounted ESP in the
Linux manager. The native library reads contained os-release aliases and chooses
an Arch, Fedora, Debian, Alpine or generic adapter from ID/ID_LIKE. It never
substitutes the recovery's installed kernel or distribution for the selected OS.

## Managed chroot

The session recreates the mount/PID/network/IPC/UTS namespaces, isolates mount
propagation and binds the selected root. Separate fstab entries for `/boot`,
`/boot/efi`, `/efi`, `/home`, `/usr` and `/var` are reused when mounted. A selected
ESP is bound automatically. Unmounted supported Linux/ESP partitions can be
resolved only by exact UUID/PARTUUID through the validated Storage Graph.
Ambiguous, encrypted or OEM sources fail. Only reviewed ext4/Btrfs/FAT mounts
and Btrfs subvolume selectors are interpreted; fstab helper commands are not run.

The root/ESP default to recursively read-only mounts. The session exposes
private proc, read-only sys, minimal character devices, private devpts/run/tmp
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
 "timeout_seconds":300,"shell_input":"printf 'Selected Linux root\n'\n"}
```

```sh
uke-recoveryctl linux rescue-plan request.json --root MOUNTED_ROOT \
  --esp MOUNTED_ESP --output rescue-plan.json
uke-recoveryctl linux rescue-execute rescue-plan.json --root MOUNTED_ROOT \
  --esp MOUNTED_ESP --journal rescue-journal --confirm REVIEWED_PLAN_SHA256
uke-recoveryctl linux rescue-inspect rescue-journal
```

Timeout is bounded to 1–7200 seconds. The namespace's supervisor reaps all child
processes; an owned pidfd permits timeout termination without trusting a saved
PID. Cleanup is reported as verified only after supervisor termination. Failed
cleanup retains the mount anchor and a pending state for inspection. Noninteractive
console capture is private and bounded to 2 MiB; the GUI shows a bounded preview.
An explicit GUI cancel control for chroot is still pending; timeout supplies the
current termination boundary. Network-enabled rescue is unavailable.

Host integration fixtures exercise actual namespace mounts, read-only and
writable sessions, selected ESP binding, stale-plan rejection, descendant
cleanup and timeout. Fedora package dispatch uses a native test executable;
this is not evidence of a real installed Fedora database or initramfs repair.

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
