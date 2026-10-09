#!/usr/bin/env bash
# Host-only isolated ARM64 guest, synthetic ATS and regular-file disks only.
set -euo pipefail
umask 077
export LC_ALL=C LANG=C
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" clock-vm bash "$component/tests/check-clock-mount-vm.sh" "$@"
fi
scratch="$component/src/device/xiaomi/uke/clock-sync"
tree="$component/src/upstream/orangefox-android16"
toolchain="$tree/prebuilts/clang/host/linux-x86/clang-r547379"
payload=${UKE_CLOCK_VM_PAYLOAD:-"$tree/out-public/target/product/uke/recovery/root"}
kernel=${UKE_CLOCK_VM_KERNEL:-"$component/build/gui-vm/kernel/arch/arm64/boot/Image"}
qemu=${UKE_CLOCK_VM_QEMU:-"$component/build/qemu-runtime/qemu-system-aarch64-wrapper"}
crt="$tree/out-public/soong/.intermediates/bionic/libc"
[[ $(sha256sum "$toolchain/bin/clang++" | cut -d' ' -f1) == 55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f ]]
result=$(mktemp -d "$component/build/clock-mount-vm-XXXXXXXX")
mkdir -p "$component/reports/private"
printf '%s\n' "$result" > "$component/reports/private/clock-mount-vm-latest"
sha256sum "$scratch/"{clock-sync.cpp,clock-mount-policy.hpp} "$component/tests/check-clock-mount-vm.sh" \
    "$component/src/device/xiaomi/uke/"{ure-clock.hpp,ure-telemetry.hpp} \
    "$toolchain/bin/clang++" "$kernel" "$qemu" > "$result/inputs.sha256"
cp "$scratch/clock-mount-policy.hpp" "$result/"
# The generic virt kernel provides virtio disks. Only this fixture changes sdf
# to vdf; the production Uke identity, units and all containment code stay fixed.
sed 's@sdf7@vdf7@g; s@class/block/sdf/slaves@class/block/vdf/slaves@g' \
    "$scratch/clock-sync.cpp" > "$result/clock-fixture.cpp"
flags=(--target=aarch64-linux-android10000 -nostdlibinc -std=c++20 -fPIC -O1 -Wall -Wextra -Werror
    -D__ANDROID_RECOVERY__ -DANDROID_STRICT -D__LIBC_API__=10000 -D__LIBM_API__=10000 -D__LIBDL_API__=10000
    "-ffile-prefix-map=$component=/workspace/recovery" "-fdebug-prefix-map=$component=/workspace/recovery"
    -isystem "$toolchain/android_libc++/platform/aarch64/include/c++/v1" -isystem "$toolchain/include/c++/v1"
    -isystem "$tree/bionic/libc/include" -isystem "$tree/bionic/libc/kernel/uapi/asm-arm64"
    -isystem "$tree/bionic/libc/kernel/uapi" -isystem "$tree/bionic/libc/kernel/android/scsi"
    -isystem "$tree/bionic/libc/kernel/android/uapi" -I "$component/src/device/xiaomi/uke")
timeout 60 "$toolchain/bin/clang++" "${flags[@]}" -MD -MF "$result/clock-fixture.d" \
    -c "$result/clock-fixture.cpp" -o "$result/clock-fixture.o"
timeout 60 "$toolchain/bin/clang++" --target=aarch64-linux-android10000 -pie -nostdlib -Wl,--no-undefined \
    -Wl,-dynamic-linker,/system/bin/linker64 "$crt/crtbegin_dynamic/android_arm64_armv8-a/crtbegin_dynamic.o" \
    "$result/clock-fixture.o" "$payload/system/lib64/libc++.so" "$payload/system/lib64/libc.so" \
    "$payload/system/lib64/libm.so" "$payload/system/lib64/libdl.so" \
    "$crt/crtend_android/android_arm64_armv8-a/crtend_android.o" -o "$result/clock-fixture"
readelf -h -d "$result/clock-fixture" > "$result/elf.txt"
rg -q 'Machine:.*AArch64' "$result/elf.txt"
mkdir -p "$result/root/system/bin" "$result/root/system/lib64" "$result/seed/time"
for name in toybox linker64 sh; do cp -L "$payload/system/bin/$name" "$result/root/system/bin/"; done
for name in mount mkdir cat mknod cmp date; do ln -s toybox "$result/root/system/bin/$name"; done
cp "$result/clock-fixture" "$result/root/system/bin/clock-fixture"
# Close the actual ELF runtime, including Toybox's crypto/SELinux dependencies.
# A minimal guest cannot assume the shell's libc-only closure covers applets.
declare -A copied=()
queue=("$result/clock-fixture" "$payload/system/bin/toybox" "$payload/system/bin/sh")
: > "$result/runtime-inputs.sha256"
for ((index=0;index<${#queue[@]};++index)); do
    input=${queue[index]}
    [[ -f $input && ! -L $input && ${#queue[@]} -le 128 ]]
    sha256sum "$input" >> "$result/runtime-inputs.sha256"
    readelf -d "$input" > "$result/runtime-dynamic.txt"
    while IFS= read -r name; do
        [[ $name =~ ^[a-zA-Z0-9+._-]+$ ]]
        [[ ! ${copied[$name]:-} ]] || continue
        copied[$name]=1
        source="$payload/system/lib64/$name"
        [[ -f $source && ! -L $source ]]
        cp "$source" "$result/root/system/lib64/"
        queue+=("$source")
    done < <(sed -n 's/^.*(NEEDED).*Shared library: \[\([^]]*\)\].*$/\1/p' "$result/runtime-dynamic.txt")
done
# 3,600,000 milliseconds, little-endian synthetic ATS_TOD offset.
printf '\x80\xee\x36\x00\x00\x00\x00\x00' > "$result/seed/time/ats_2"
truncate -s 32M "$result/persist-filesystem.img"
mke2fs -q -F -t ext4 -b 4096 -O '^orphan_file' -d "$result/seed" "$result/persist-filesystem.img" > "$result/mkfs.log" 2>&1
for number in 0 1 2 3 4 5; do truncate -s 64M "$result/disk-$number.img"; done
cat > "$result/layout.txt" <<'LAYOUT'
label: gpt
unit: sectors
start=256, size=256, name="fixture1"
start=512, size=256, name="fixture2"
start=768, size=256, name="fixture3"
start=1024, size=256, name="fixture4"
start=1280, size=256, name="fixture5"
start=1536, size=256, name="fixture6"
start=6656, size=8192, name="persist"
LAYOUT
sfdisk --sector-size 4096 --no-reread --no-tell-kernel "$result/disk-5.img" < "$result/layout.txt" > "$result/gpt.log" 2>&1
dd if="$result/persist-filesystem.img" of="$result/disk-5.img" bs=4096 seek=6656 conv=notrunc status=none
sha256sum "$result/disk-"*.img > "$result/disks-before.sha256"
mkdir -p "$result/root/"{dev,proc,sys,tmp}
cat > "$result/root/clock-init" <<'GUEST'
#!/system/bin/sh
export PATH=/system/bin
export LD_LIBRARY_PATH=/system/lib64
/system/bin/toybox mount -t proc proc /proc || exit 100
toybox mount -t sysfs sysfs /sys || exit 101
toybox mount -t devtmpfs devtmpfs /dev || exit 102
mkdir -p /dev/block
identity=$(cat /sys/class/block/vdf7/dev) || exit 103
major=${identity%:*}
minor=${identity#*:}
mknod /dev/block/vdf7 b "$major" "$minor" || exit 104
mount -t tmpfs tmpfs /tmp || exit 105
echo original-parent-namespace > /tmp/parent-marker
cat /proc/self/mountinfo > /namespace-before
clock-fixture
status=$?
echo "URE_CLOCK_HELPER_EXIT $status"
[ "$status" = 0 ] || { echo o > /proc/sysrq-trigger; exit 106; }
[ "$(cat /tmp/parent-marker)" = original-parent-namespace ] || exit 107
[ ! -e /tmp/uke-clock-persist ] || exit 108
cat /proc/self/mountinfo > /namespace-after
cmp /namespace-before /namespace-after || exit 109
counter=$(cat /sys/class/rtc/rtc0/since_epoch) || exit 110
now=$(date -u +%s) || exit 111
delta=$((now-counter))
[ "$delta" -ge 3598 ] && [ "$delta" -le 3602 ] || { echo "URE_CLOCK_DELTA_FAILURE $delta"; exit 112; }
echo 'URE_CLOCK_PRIVATE_NAMESPACE_EXIT 0'
echo o > /proc/sysrq-trigger
GUEST
chmod 755 "$result/root/clock-init"
(cd "$result/root" && find . -print0 | LC_ALL=C sort -z | cpio --null -o --format=newc --quiet | gzip -n -1) > "$result/initrd.cpio.gz"
args=()
for number in 0 1 2 3 4 5; do
    [[ -f $result/disk-$number.img && ! -L $result/disk-$number.img ]]
    args+=(-drive "if=none,id=disk$number,file=$result/disk-$number.img,format=raw,cache=writeback"
        -device "virtio-blk-pci,drive=disk$number,logical_block_size=4096,physical_block_size=4096,addr=$((number+2)),romfile=")
done
status=0
timeout 120 "$qemu" -machine virt -cpu cortex-a72 -smp 1 -m 512 -accel tcg \
    -kernel "$kernel" -initrd "$result/initrd.cpio.gz" -append 'console=ttyAMA0 rdinit=/clock-init panic=-1' \
    "${args[@]}" -nic none -display none -serial stdio -monitor none -no-reboot > "$result/guest.log" 2>&1 || status=$?
printf '%s\n' "$status" > "$result/qemu-exit-status"
[[ $status == 0 ]]
rg -q '^URE_CLOCK_HELPER_EXIT 0' "$result/guest.log"
rg -q '^URE_CLOCK_PRIVATE_NAMESPACE_EXIT 0' "$result/guest.log"
sha256sum --check --quiet "$result/disks-before.sha256"
sha256sum --check --quiet "$result/inputs.sha256"
sha256sum --check --quiet "$result/runtime-inputs.sha256"
jq -n '{evidence_class:"generic-arm64-private-namespace",passed:true,synthetic_ats:true,
    virtual_block_alias_adapter:true,parent_mount_namespace_unchanged:true,all_six_images_unchanged:true,
    guest_clock_corrected:true,host_clock_modified:false,physical_device:false,shipping_binary_accepted:false}' > "$result/verification.json"
printf '%s\n' 'PASS: actual ARM64 mount namespace and clock correction; parent namespace and all synthetic disks unchanged.'
