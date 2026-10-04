# Native recovery write-refusal tests

The source policy is described in [RECOVERY-WRITE-POLICY.md](RECOVERY-WRITE-POLICY.md).
The host gate compiles complete production entry functions with counted fake
hardware callbacks and compares independent regular-file target bytes. It also
removes the Format Data guard deliberately; both the entry census and compiled
behavior must detect that regression.

```sh
bash tests/check-write-gate.sh
bash tests/run-native.sh
bash tests/check-sanitizers.sh
```

## Generic guest

`tests/check-write-gate-vm.sh` exercises the actual OrangeFox structured FIFO
commands in a headless generic ARM64 guest. It does not use a host block device,
network interface, tablet, credential or firmware image as its writable target.
Two newly created 128 MiB ext4 media files are attached **writable** to QEMU;
the guest mounts them `ro,noload`. Their complete hashes must remain unchanged
after recovery exits. This arrangement can detect writes even if a command
returns an error after attempting them.

Prepare the current build's VM-only recovery adapter first. The adapter replaces
ashmem code-cache allocation with memfd, supplies synthetic properties and uses
a disposable fstab. It does not replace the write policy or its native sinks.
The test uses the previously built graphics-enabled generic 7.2.8 kernel with
SHA-256 `0dafe914751929b011f6a3d8d84e47c51452de4b604d123714b64dd2546e8a80`
and matching virtio GPU/input modules. A kernel with the same release string but
without those modules is not a substitute. The matching emulator module
directory must expose `virtio-gpu-device`.

```sh
bash tests/prepare-gui-vm.sh
QEMU_MODULE_DIR="$PWD/build/gui-vm/qemu/root/usr/lib64/qemu" \
UKE_QEMU_SYSTEM_AARCH64="$PWD/build/qemu-runtime/qemu-system-aarch64-wrapper" \
  bash tests/check-write-gate-vm.sh \
    "$PWD/build/gui-vm/kernel/arch/arm64/boot/Image" \
    "$PWD/build/gui-vm/modules" 7.2.8
```

The sequence covers status inspection, missing confirmation, confirmed Format
Data, repair, resize, filesystem change, wipe, package install, sideload, an ORS
command that would create a sentinel, MTP enable, a writable-mount preference,
ORS format/wipe/mkdir/slot/backup commands, a caller-owned ORS file, recovery
reflash, Format Data again and status after refusal (21 results). The source ORS
file must survive a refusal. The preference is accepted as a UI
setting; the second format must still fail. The sentinel must not exist and
both complete media hashes must match the pre-boot copies.

Run heavy build, filesystem-fixture, sanitizer and guest jobs serially through
`scripts/with-host-budget.sh`. When invoking the guest through that wrapper,
pass both emulator variables explicitly with `env` inside its command. Outer
shell exports are not an attested guest configuration.

Private logs and a hash-bound receipt are retained under ignored build/report
storage. A failed launch, timeout, missing result or kernel panic produces no
success receipt. `ure-write-gate-alpha` sealing requires its own current-source
native, sanitizer, extracted-image and write-gate guest receipts.

These are native-path refusal tests, not full GUI/visual acceptance, encrypted
userdata acceptance, physical fastboot USB testing or a boot result for the
shipping kernel. Root ADB/terminal commands remain outside the managed-native
policy boundary.
