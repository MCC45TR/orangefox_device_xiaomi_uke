#!/usr/bin/env bash
# An uninstalled ARM64 fixture on newly created VM media; never attach a host disk.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
[[ $# == 4 ]] || { echo 'Usage: check-deferred-userdata-vm.sh GENERIC_KERNEL DM_MODULE FIXTURE_ELF SEALED_RECOVERY_IMAGE' >&2; exit 2; }
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" vm /usr/bin/env \
        "UKE_QEMU_SYSTEM_AARCH64=${UKE_QEMU_SYSTEM_AARCH64:-$(type -P qemu-system-aarch64 || true)}" bash "$0" "$@"
fi
kernel=$1 module=$2 binary=$3 image=$4
qemu=${UKE_QEMU_SYSTEM_AARCH64:-$(type -P qemu-system-aarch64 || true)}
output="$component/src/upstream/orangefox-android16/out-public"
payload="$output/target/product/uke/recovery/root"
accepted="$output/soong/.intermediates/device/xiaomi/uke/recoveryctl/uke-deferred-userdata-vm-fixture/android_recovery_arm64_armv8-a/uke-deferred-userdata-vm-fixture"
for file in "$kernel" "$module" "$binary" "$image" "$accepted"; do [[ -f $file && ! -L $file ]]; done
[[ -d $payload && -x $qemu ]]
cmp -- "$binary" "$accepted"
readelf -h "$binary" | rg -q 'Machine:.*AArch64'
mkdir -p "$component/build/deferred-userdata-vm" "$component/reports/private"
job=$(mktemp -d "$component/build/deferred-userdata-vm/job-XXXXXXXX")
printf '%s\n' "$job" > "$component/build/deferred-userdata-vm/latest-job"
inputs=("$kernel" "$module" "$binary" "$image" "${BASH_SOURCE[0]}"
    "$component/src/device/xiaomi/uke/recoveryctl/vm-deferred-userdata-fixture.cpp"
    "$component/tests/vm/strip-debug-module.sh")
sha256sum -- "${inputs[@]}" > "$job/inputs-before.sha256"
bash "$component/scripts/build-evidence.sh" verify > "$job/build-evidence.log" 2>&1
bash "$component/scripts/build-evidence.sh" payload "$image" "$payload" "$job/compile-evidence.json" > "$job/payload-evidence.log" 2>&1
mkdir "$job/root"
cp -a -- "$payload/." "$job/root/"
install -Dm755 "$binary" "$job/root/system/bin/ure-deferred-fixture"
bash "$component/tests/vm/strip-debug-module.sh" "$component" "$module" "$job/root/ure-vm-modules/dm-mod.ko" "$job"
mkdir -p "$job/root/run" "$job/root/tmp" "$job/root/proc" "$job/root/sys" "$job/root/dev"
cat > "$job/root/ure-deferred-init" <<'GUEST'
#!/system/bin/sh
export PATH=/system/bin
export LD_LIBRARY_PATH=/system/lib64:/vendor/lib64:/system/lib64/bootstrap
toybox mount -t proc proc /proc || exit 100
mount -t sysfs sysfs /sys || exit 101
mount -t devtmpfs devtmpfs /dev || exit 102
mount -t tmpfs tmpfs /run || exit 103
mount -t tmpfs tmpfs /tmp || exit 104
insmod /ure-vm-modules/dm-mod.ko || exit 105
/system/bin/ure-deferred-fixture
result=$?
echo "URE_DEFERRED_VM_EXIT $result"
sync
echo o > /proc/sysrq-trigger
toybox reboot -p -f
exit "$result"
GUEST
chmod 755 "$job/root/ure-deferred-init"
bash "$component/tests/check-payload.sh" "$job/root" > "$job/payload-gate.log"
truncate -s 528M "$job/disposable-media.img"
[[ -f $job/disposable-media.img && ! -L $job/disposable-media.img ]]
(cd "$job/root" && find . -print0 | LC_ALL=C sort -z | cpio --null -o --format=newc --quiet | gzip -n -1) > "$job/initrd.cpio.gz"
timeout 240 "$qemu" -machine virt -cpu cortex-a72 -smp 2 -m 2048 -accel tcg \
    -display none -monitor none -serial stdio -no-reboot -nic none -no-user-config \
    -kernel "$kernel" -initrd "$job/initrd.cpio.gz" \
    -append 'console=ttyAMA0 rdinit=/ure-deferred-init panic=1 ure_userdata_fixture=1' \
    -drive "if=none,id=fixture,format=raw,file=$job/disposable-media.img,cache=none" \
    -device virtio-blk-device,drive=fixture,logical_block_size=4096,physical_block_size=4096 > "$job/console.log" 2>&1
rg -q '^URE_DEFERRED_USERDATA_VM 0\r?$' "$job/console.log"
rg -q '^URE_DEFERRED_VM_EXIT 0\r?$' "$job/console.log"
sha256sum -- "${inputs[@]}" > "$job/inputs-after.sha256"
cmp "$job/inputs-before.sha256" "$job/inputs-after.sha256"
bash "$component/scripts/build-evidence.sh" verify > "$job/build-evidence-after.log" 2>&1
jq -n --arg inputs "$(sha256sum "$job/inputs-after.sha256" | cut -d' ' -f1)" \
    --arg image "$(sha256sum "$image" | cut -d' ' -f1)" --arg elf "$(sha256sum "$binary" | cut -d' ' -f1)" \
    --arg kernel "$(sha256sum "$kernel" | cut -d' ' -f1)" --arg module "$(sha256sum "$module" | cut -d' ' -f1)" \
    --arg console "$(sha256sum "$job/console.log" | cut -d' ' -f1)" \
    '{schema_version:1,passed:true,evidence_class:"generic-arm64-vm",source_inputs_sha256:$inputs,
      recovery_image_sha256:$image,fixture_elf_sha256:$elf,kernel_sha256:$kernel,dm_module_sha256:$module,console_sha256:$console,
      erased_bytes:16384,bounded_view_bytes:536870912,neighbor_sentinels_unchanged:true,
      shipping_kernel_test:false,tablet_hardware_test:false,host_block_attachment:false,
      userdata_filesystem_created:false,interrupted_live_transaction_test:false}' > "$job/verification.json"
cp -- "$job/verification.json" "$component/reports/private/deferred-userdata-vm-verification.json"
echo 'Bounded userdata header erase passed on disposable generic ARM64 VM media.'
