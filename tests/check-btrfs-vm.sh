#!/usr/bin/env bash
# Host-only: boot the native Android fixture with a generic virt kernel and one
# newly created regular-file disk. No host block node is accepted or attached.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
kernel=${1:?Pass the generic ARM64 virt kernel Image}
modules=${2:?Pass its matching module directory}
binary=${3:?Pass the optional Android uke-btrfs-vm-fixture ELF}
qemu=${UKE_QEMU_SYSTEM_AARCH64:-$(type -P qemu-system-aarch64 || true)}
payload="$component/src/upstream/orangefox-android16/out-public/target/product/uke/recovery/root"
[[ -f $kernel && ! -L $kernel && -d $modules && -f $binary && -x $qemu && -d $payload ]]
command -v mkfs.btrfs >/dev/null
mkdir -p "$component/build/btrfs-vm" "$component/reports/private"
job=$(mktemp -d "$component/build/btrfs-vm/job-XXXXXX")
root="$job/root"
mkdir "$root"
cp -a -- "$payload/." "$root/"
install -Dm755 "$binary" "$root/system/bin/uke-btrfs-vm-fixture"
for module in lib/crypto/libblake2b.ko lib/raid/xor/xor.ko lib/raid/raid6/raid6_pq.ko lib/zstd/zstd_compress.ko fs/btrfs/btrfs.ko; do
    install -Dm644 "$modules/kernel/$module" "$root/ure-vm-modules/$module"
done
mkdir -p "$root/mnt/btrfs" "$root/run" "$root/tmp" "$root/proc" "$root/sys" "$root/dev"
cat > "$root/ure-vm-init" <<'GUEST'
#!/system/bin/sh
export PATH=/system/bin
export LD_LIBRARY_PATH=/system/lib64:/vendor/lib64:/system/lib64/bootstrap
/system/bin/toybox mount -t proc proc /proc || exit 100
mount -t sysfs sysfs /sys || exit 101
mount -t devtmpfs devtmpfs /dev || exit 102
mount -t tmpfs tmpfs /run || exit 103
mount -t tmpfs tmpfs /tmp || exit 104
for module in lib/crypto/libblake2b.ko lib/raid/xor/xor.ko lib/raid/raid6/raid6_pq.ko lib/zstd/zstd_compress.ko fs/btrfs/btrfs.ko; do
    insmod "/ure-vm-modules/$module" || exit 105
done
mount -t btrfs -o subvolid=5 /dev/vda /mnt/btrfs || exit 106
/system/bin/uke-btrfs-vm-fixture /mnt/btrfs /run/ure-btrfs-vm
result=$?
echo "URE_VM_EXIT $result"
sync
umount /mnt/btrfs
echo o > /proc/sysrq-trigger
toybox reboot -p -f
exit "$result"
GUEST
chmod 755 "$root/ure-vm-init"
bash "$component/tests/check-payload.sh" "$root" > "$job/payload-gate.log"
truncate -s 512M "$job/disposable-btrfs.img"
[[ -f $job/disposable-btrfs.img && ! -L $job/disposable-btrfs.img ]]
mkfs.btrfs -q -f "$job/disposable-btrfs.img" > "$job/mkfs.log" 2>&1
(cd "$root" && find . -print0 | LC_ALL=C sort -z | cpio --null -o --format=newc --quiet | gzip -n -1) > "$job/initrd.cpio.gz"
timeout 240 "$qemu" -machine virt -cpu cortex-a72 -m 2048 -accel tcg \
    -display none -monitor none -serial stdio -no-reboot -nic none -no-user-config \
    -kernel "$kernel" -initrd "$job/initrd.cpio.gz" \
    -append 'console=ttyAMA0 rdinit=/ure-vm-init panic=1 ure_fixture=1' \
    -drive "if=none,id=fixture,format=raw,file=$job/disposable-btrfs.img" \
    -device virtio-blk-device,drive=fixture > "$job/console.log" 2>&1
rg -q '^URE_VM_EXIT 0\r?$' "$job/console.log"
awk '/^URE_BTRFS_VM_RESULT / { sub(/^URE_BTRFS_VM_RESULT /, ""); capture=1 } /^URE_VM_EXIT / { capture=0 } capture { sub(/\r$/, ""); print }' \
    "$job/console.log" > "$job/result.json"
jq -e '.passed==true and .physical_device==false and .validation_kind=="qemu-system-native-ioctl" and (.checks|length)>=14' "$job/result.json" >/dev/null
jq --arg kernel "$(sha256sum "$kernel" | cut -d' ' -f1)" --arg binary "$(sha256sum "$binary" | cut -d' ' -f1)" \
    --arg runner "$(sha256sum "${BASH_SOURCE[0]}" | cut -d' ' -f1)" \
    '. + {kernel_sha256:$kernel,fixture_elf_sha256:$binary,runner_sha256:$runner,shipping_kernel_test:false,tablet_hardware_test:false}' \
    "$job/result.json" > "$component/reports/private/btrfs-vm-verification.json"
printf '%s\n' 'Native Btrfs ioctl fixtures passed in the isolated generic-kernel ARM64 VM; shipping-kernel and tablet acceptance remain separate.'
