#!/usr/bin/env bash
# Host-only: native recovery userspace, disposable ext4 media, two generic ARM64
# boots and an actual guest emergency reboot. Never attach a host block node.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
kernel=${1:?Pass a generic ARM64 virt kernel Image}
payload=${2:-"$component/src/upstream/orangefox-android16/out-public/target/product/uke/recovery/root"}
qemu=${UKE_QEMU_SYSTEM_AARCH64:-$(type -P qemu-system-aarch64 || true)}
fixture="$component/build/ure-host/uke-partition-job-tests"
[[ -f $kernel && ! -L $kernel && -d $payload && -x $qemu && -x $fixture ]]
for command in mke2fs cpio gzip jq sha256sum timeout; do command -v "$command" >/dev/null; done
mkdir -p "$component/build/partition-vm" "$component/reports/private"
job=$(mktemp -d "$component/build/partition-vm/job-XXXXXX")
mkdir "$job/root" "$job/media"
cp -a -- "$payload/." "$job/root/"
"$fixture" --fixture-compatible "$job/media/disk.img"
"$fixture" --request > "$job/media/request.json"
sha256sum "$job/media/disk.img" | cut -d' ' -f1 > "$job/media/original.sha256"
truncate -s 64M "$job/media/unsupported-ext4.img"
mke2fs -q -F -t ext4 -O '^encrypt,orphan_file' "$job/media/unsupported-ext4.img" > "$job/unsupported-mkfs.log" 2>&1
sha256sum "$job/media/unsupported-ext4.img" | cut -d' ' -f1 > "$job/media/unsupported.sha256"
printf '%s\n' '{"schema":1,"action":"resize","filesystem":"ext4","target_bytes":33554432}' > "$job/media/unsupported-request.json"
truncate -s 4G "$job/disposable-media.img"
[[ -f $job/disposable-media.img && ! -L $job/disposable-media.img ]]
mke2fs -q -F -t ext4 -O '^encrypt' -d "$job/media" "$job/disposable-media.img" > "$job/mkfs.log" 2>&1
mkdir -p "$job/root/media" "$job/root/run" "$job/root/tmp" "$job/root/proc" "$job/root/sys" "$job/root/dev"
cat > "$job/root/ure-partition-vm-init" <<'GUEST'
#!/system/bin/sh
export PATH=/system/bin
export LD_LIBRARY_PATH=/system/lib64:/vendor/lib64:/system/lib64/bootstrap
export MKE2FS_CONFIG=/system/etc/mke2fs.conf
/system/bin/toybox mount -t proc proc /proc || exit 100
mount -t sysfs sysfs /sys || exit 101
mount -t devtmpfs devtmpfs /dev || exit 102
mount -t tmpfs tmpfs /run || exit 103
mount -t tmpfs tmpfs /tmp || exit 104
mount -t ext4 /dev/vda /media || exit 105
ctl=/system/bin/uke-recoveryctl
fail() {
    echo "URE_PARTITION_VM_FAILURE $1"
    for record in /media/apply.json /media/after-reboot.json /media/resumed.json /media/rolled-back.json /media/job/state.json /media/job/stage-userdata/state.json /media/unsupported-job/state.json; do
        if [ -f "$record" ]; then echo "$record"; cat "$record"; fi
    done
    sync
    umount /media
    echo o > /proc/sysrq-trigger
    exit "$1"
}
if [ ! -f /media/plan.json ]; then
    "$ctl" filesystem plan /media/unsupported-request.json --image /media/unsupported-ext4.img --profile vm-fixture --output /media/unsupported-plan.json > /media/unsupported-planned.json || fail 121
    hash=$(awk -F '"' '/^  "plan_sha256"/ { print $4 }' /media/unsupported-plan.json)
    if "$ctl" filesystem execute /media/unsupported-plan.json --image /media/unsupported-ext4.img --journal /media/unsupported-job --confirm "$hash" > /media/unsupported-result.json; then fail 122; fi
    grep -q '^  "state" : "FAILED_SAFE"' /media/unsupported-job/state.json || fail 123
    grep -q 'FEATURE_C12' /media/unsupported-job/state.json || fail 124
    expected=$(cat /media/unsupported.sha256)
    actual=$(sha256sum /media/unsupported-ext4.img | awk '{ print $1 }')
    [ "$actual" = "$expected" ] || fail 125
    echo 'URE_PARTITION_UNSUPPORTED_FEATURE_REFUSED 0'
    rm -rf /media/unsupported-job /media/unsupported-ext4.img
    "$ctl" partition job-plan /media/request.json --image /media/disk.img --sector-size 4096 --profile vm-fixture --output /media/plan.json > /media/planned.json || exit 106
    hash=$(awk -F '"' '/^  "plan_sha256"/ { print $4 }' /media/plan.json)
    [ ${#hash} = 64 ] || exit 107
    "$ctl" partition job-execute /media/plan.json --image /media/disk.img --sector-size 4096 --journal /media/job --confirm "$hash" > /media/apply.json &
    writer=$!
    while kill -0 "$writer" 2>/dev/null; do
        if grep -q '^  "state" : "APPLYING"' /media/job/state.json 2>/dev/null &&
           grep -Eq '^  "last_verified_chunk" : ([3-9]|[1-9][0-9]+)' /media/job/state.json 2>/dev/null; then
            kill -STOP "$writer" || exit 108
            echo 'URE_PARTITION_FORCED_REBOOT 1'
            # No global sync/unmount here: the transaction's own durable
            # boundaries must survive a reset during the application phase.
            echo b > /proc/sysrq-trigger
            exit 109
        fi
        sleep 0.02
    done
    wait "$writer" || fail 126
    echo 'URE_PARTITION_UNEXPECTED_COMPLETE 1'
    fail 110
fi
hash=$(awk -F '"' '/^  "plan_sha256"/ { print $4 }' /media/plan.json)
"$ctl" partition job-inspect /media/job --image /media/disk.img --sector-size 4096 > /media/after-reboot.json || fail 111
grep -q '^    "classification" : "PARTIAL_EXPECTED_WRITE"' /media/after-reboot.json || fail 112
echo 'URE_PARTITION_CROSS_BOOT_INSPECT 0'
"$ctl" partition job-resume /media/job --image /media/disk.img --sector-size 4096 --confirm "$hash" > /media/resumed.json || fail 113
grep -q '^    "state" : "COMMITTED"' /media/resumed.json || fail 114
echo 'URE_PARTITION_CROSS_BOOT_RESUME 0'
"$ctl" partition job-inspect /media/job --image /media/disk.img --sector-size 4096 > /media/committed.json || fail 115
grep -q '^    "classification" : "TARGET"' /media/committed.json || fail 116
"$ctl" partition job-rollback /media/job --image /media/disk.img --sector-size 4096 --confirm "$hash" > /media/rolled-back.json || fail 117
grep -q '^    "state" : "ROLLED_BACK"' /media/rolled-back.json || fail 118
expected=$(cat /media/original.sha256)
actual=$(sha256sum /media/disk.img | awk '{ print $1 }')
[ "$actual" = "$expected" ] || fail 119
echo 'URE_PARTITION_COMPLETE_BYTE_ROLLBACK 0'
echo 'URE_PARTITION_VM_EXIT 0'
sync
umount /media || exit 120
echo o > /proc/sysrq-trigger
toybox reboot -p -f
GUEST
chmod 755 "$job/root/ure-partition-vm-init"
bash "$component/tests/check-payload.sh" "$job/root" > "$job/payload-gate.log"
(cd "$job/root" && find . -print0 | LC_ALL=C sort -z | cpio --null -o --format=newc --quiet | gzip -n -1) > "$job/initrd.cpio.gz"
boot() {
    local number=$1
    timeout 600 "$qemu" -machine virt -cpu cortex-a72 -smp 2 -m 2048 -accel tcg \
        -display none -monitor none -serial stdio -no-reboot -nic none -no-user-config \
        -kernel "$kernel" -initrd "$job/initrd.cpio.gz" \
        -append 'console=ttyAMA0 rdinit=/ure-partition-vm-init panic=1 ure_fixture=1' \
        -drive "if=none,id=fixture,format=raw,file=$job/disposable-media.img,cache=none" \
        -device virtio-blk-device,drive=fixture > "$job/boot-$number.log" 2>&1
}
boot 1
rg -q '^URE_PARTITION_UNSUPPORTED_FEATURE_REFUSED 0\r?$' "$job/boot-1.log"
rg -q '^URE_PARTITION_FORCED_REBOOT 1\r?$' "$job/boot-1.log"
boot 2
for marker in URE_PARTITION_CROSS_BOOT_INSPECT URE_PARTITION_CROSS_BOOT_RESUME URE_PARTITION_COMPLETE_BYTE_ROLLBACK URE_PARTITION_VM_EXIT; do
    rg -q "^$marker 0\r?$" "$job/boot-2.log"
done
jq -n --arg kernel "$(sha256sum "$kernel" | cut -d' ' -f1)" \
    --arg binary "$(sha256sum "$payload/system/bin/uke-recoveryctl" | cut -d' ' -f1)" \
    --arg runner "$(sha256sum "${BASH_SOURCE[0]}" | cut -d' ' -f1)" \
    --arg version "$("$qemu" --version | head -n 1)" \
    '{schema_version:1,passed:true,validation_kind:"qemu-system-native-partition-job",kernel_sha256:$kernel,native_cli_sha256:$binary,runner_sha256:$runner,qemu_version:$version,
      checks:["unsupported-ext4-feature-refused","unsupported-feature-original-bytes-unchanged","native-filesystem-tools-and-final-capacity-checks","complete-before-after-journal","actual-guest-emergency-reboot","persistent-ext4-journal-reopen","cross-boot-inode-binding","partial-write-readback","source-independent-resume-and-GPT-verification","complete-original-image-rollback"],
      interruption:"guest-sysrq-emergency-reboot",shipping_kernel_test:false,physical_device:false,tablet_hardware_test:false,ufs_controller_test:false}' \
    > "$component/reports/private/partition-vm-verification.json"
printf '%s\n' 'Native ARM64 partition job passed guest forced reboot, persistent journal inspection/resume and exact complete rollback; generic virt kernel and disposable media only.'
