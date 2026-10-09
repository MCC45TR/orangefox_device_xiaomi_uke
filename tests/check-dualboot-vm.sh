#!/usr/bin/env bash
# Host-only: the packed ARM64 CLI, generic virt kernel and disposable regular
# images. A shipping ownership refusal is measured, never called execution.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
kernel=${1:?Pass the approved generic ARM64 virt kernel Image}
payload=${2:?Pass the fresh recovery payload root}
image=${3:?Pass the sealed recovery image matching that payload}
qemu=${UKE_QEMU_SYSTEM_AARCH64:-$(type -P qemu-system-aarch64 || true)}
fixture="$component/build/ure-host/uke-dualboot-tests"
[[ -f $kernel && ! -L $kernel && -f $image && ! -L $image && -d $payload && -x $qemu && -x $fixture ]]
[[ -f $payload/system/bin/uke-recoveryctl && ! -L $payload/system/bin/uke-recoveryctl ]]
for command in mke2fs cpio gzip jq sha256sum timeout readelf bwrap rg; do command -v "$command" >/dev/null; done
readelf -h "$payload/system/bin/uke-recoveryctl" | rg -q 'Machine:.*AArch64'
mkdir -p "$component/build/dualboot-vm" "$component/reports/private"
job=$(mktemp -d "$component/build/dualboot-vm/job-XXXXXX")
printf '%s\n' "$job" > "$component/build/dualboot-vm/latest-job"
bash "$component/scripts/build-evidence.sh" verify > "$job/build-evidence-gate.log" 2>&1
bash "$component/scripts/build-evidence.sh" payload "$image" "$payload" "$job/compile-evidence.json" > "$job/packed-payload-gate.log" 2>&1
mkdir "$job/root" "$job/media"
cp -a -- "$payload/." "$job/root/"
cmp -- "$payload/system/bin/uke-recoveryctl" "$job/root/system/bin/uke-recoveryctl"
"$fixture" --fixture-compatible "$job/media/disk.img"
"$fixture" --request > "$job/media/request.json"
jq -e '.format=="uke-dualboot-request" and .linux_enabled and .windows_enabled and .separate_linux_boot and
    .userdata_policy=="recreate" and .userdata_filesystem=="ext4" and .linux.filesystem=="ext4" and .linux_boot.size=="64"' "$job/media/request.json" >/dev/null
sha256sum "$job/media/disk.img" | cut -d' ' -f1 > "$job/media/original.sha256"
truncate -s 4G "$job/disposable-media.img"
[[ -f $job/disposable-media.img && ! -L $job/disposable-media.img ]]
mke2fs -q -F -t ext4 -O '^encrypt,^orphan_file' -d "$job/media" "$job/disposable-media.img" > "$job/mkfs.log" 2>&1
mkdir -p "$job/root/media" "$job/root/run" "$job/root/tmp" "$job/root/proc" "$job/root/sys" "$job/root/dev"
cat > "$job/root/ure-dualboot-vm-init" <<'GUEST'
#!/system/bin/sh
export PATH=/system/bin
export LD_LIBRARY_PATH=/system/lib64:/vendor/lib64:/system/lib64/bootstrap
export MKE2FS_CONFIG=/system/etc/mke2fs.conf
# This host-domain setting does not enable the shipping Android owner backend.
export URE_OPERATION_COORDINATOR=/media/operation-coordinator
/system/bin/toybox mount -t proc proc /proc || exit 100
mount -t sysfs sysfs /sys || exit 101
mount -t devtmpfs devtmpfs /dev || exit 102
mount -t tmpfs tmpfs /run || exit 103
mount -t tmpfs tmpfs /tmp || exit 104
mount -t ext4 /dev/vda /media || exit 105
ctl=/system/bin/uke-recoveryctl
fail() {
    echo "URE_DUALBOOT_VM_FAILURE $1"
    for record in /media/apply.json /media/refused-hash.json /media/refused-policy.json /media/after-reboot.json /media/resumed.json /media/rolled-back.json /media/job/state.json; do
        if [ -f "$record" ]; then echo "$record"; cat "$record"; fi
    done
    sync
    umount /media
    echo o > /proc/sysrq-trigger
    exit "$1"
}
original_unchanged() {
    expected=$(cat /media/original.sha256)
    actual=$(sha256sum /media/disk.img | awk '{ print $1 }')
    [ "$actual" = "$expected" ] || fail 106
}
finish() {
    echo 'URE_DUALBOOT_VM_EXIT 0'
    sync
    umount /media || exit 107
    echo o > /proc/sysrq-trigger
    toybox reboot -p -f
}
if [ ! -f /media/plan.json ]; then
    printf '5\n1\next4\next4\n64 MiB\n64 MiB\n64 MiB\n64 MiB\n' |
        "$ctl" dualboot setup --image /media/disk.img --sector-size 4096 --profile vm-fixture --output /media/shell-plan.json > /media/shell-preview.txt || fail 108
    grep -q 'linux_boot' /media/shell-preview.txt || fail 109
    grep -q 'No application journal was selected' /media/shell-preview.txt || fail 110
    "$ctl" dualboot plan /media/request.json --image /media/disk.img --sector-size 4096 --profile vm-fixture --output /media/plan.json > /media/planned.json || fail 111
    "$ctl" dualboot preview /media/plan.json > /media/preview-before.json || fail 112
    grep -q 'linux_boot' /media/preview-before.json || fail 113
    grep -q 'Operations to review' /media/preview-before.json || fail 114
    grep -q 'ERASE USERDATA' /media/preview-before.json || fail 115
    awk '/^    "text" :/ { print }' /media/preview-before.json > /media/preview-text.json
    [ -s /media/preview-text.json ] || fail 116
    original_unchanged
    echo 'URE_DUALBOOT_SHELL_AND_JSON_PREVIEW 0'
    hash=$(awk -F '"' '/^  "plan_sha256"/ { print $4 }' /media/plan.json)
    [ ${#hash} = 64 ] || fail 117
    if "$ctl" dualboot execute /media/plan.json --image /media/disk.img --sector-size 4096 --journal /media/refused-hash --confirm wrong --data-policy 'ERASE USERDATA' > /media/refused-hash.json; then fail 118; fi
    grep -q '"code" : "confirmation-required"' /media/refused-hash.json || fail 119
    if "$ctl" dualboot execute /media/plan.json --image /media/disk.img --sector-size 4096 --journal /media/refused-policy --confirm "$hash" --data-policy wrong > /media/refused-policy.json; then fail 120; fi
    grep -q '"code" : "confirmation-required"' /media/refused-policy.json || fail 121
    [ ! -e /media/refused-hash ] && [ ! -e /media/refused-policy ] || fail 122
    original_unchanged
    echo 'URE_DUALBOOT_EXACT_CONSENT_REFUSED 0'
    "$ctl" dualboot execute /media/plan.json --image /media/disk.img --sector-size 4096 --journal /media/job --confirm "$hash" --data-policy 'ERASE USERDATA' > /media/apply.json &
    writer=$!
    while kill -0 "$writer" 2>/dev/null; do
        if grep -q '^  "state" : "APPLYING"' /media/job/state.json 2>/dev/null &&
           grep -Eq '^  "last_verified_chunk" : ([3-9]|[1-9][0-9]+)' /media/job/state.json 2>/dev/null; then
            kill -STOP "$writer" || fail 123
            echo 'URE_DUALBOOT_FORCED_REBOOT 1'
            # Exercise the writer's own durable boundaries; no sync/unmount.
            echo b > /proc/sysrq-trigger
            exit 124
        fi
        sleep 0.02
    done
    if wait "$writer"; then echo 'URE_DUALBOOT_UNEXPECTED_COMPLETE 1'; fail 125; fi
    # The current shipping persistent owner is deliberately unaccepted. This
    # is a measured safe refusal, not an execution/restore support result.
    grep -q '"code" : "ownership-unavailable"' /media/apply.json || fail 126
    [ ! -e /media/job ] || fail 127
    original_unchanged
    "$ctl" dualboot preview /media/plan.json > /media/preview-refused.json || fail 128
    awk '/^    "text" :/ { print }' /media/preview-refused.json > /media/preview-text-refused.json
    cmp /media/preview-text.json /media/preview-text-refused.json || fail 129
    echo 'URE_DUALBOOT_SHIPPING_OWNERSHIP_REFUSED 0'
    echo 'URE_DUALBOOT_REFUSAL_COMPLETE_IMAGE_UNCHANGED 0'
    finish
    exit 0
fi
inner_hash=$(awk -F '"' '/^  "plan_sha256"/ { print $4 }' /media/job/plan.json)
[ ${#inner_hash} = 64 ] || fail 130
cmp /media/plan.json /media/job/dualboot-plan.json || fail 131
"$ctl" dualboot preview /media/job/dualboot-plan.json > /media/preview-reopened.json || fail 132
awk '/^    "text" :/ { print }' /media/preview-reopened.json > /media/preview-text-reopened.json
cmp /media/preview-text.json /media/preview-text-reopened.json || fail 133
echo 'URE_DUALBOOT_SAVED_WRAPPER_PREVIEW 0'
"$ctl" partition job-inspect /media/job --image /media/disk.img --sector-size 4096 > /media/after-reboot.json || fail 134
grep -q '^    "classification" : "PARTIAL_EXPECTED_WRITE"' /media/after-reboot.json || fail 135
echo 'URE_DUALBOOT_CROSS_BOOT_INSPECT 0'
"$ctl" partition job-resume /media/job --image /media/disk.img --sector-size 4096 --confirm "$inner_hash" > /media/resumed.json || fail 136
grep -q '^    "state" : "COMMITTED"' /media/resumed.json || fail 137
grep -q '^    "protected_ranges_verified" : true' /media/resumed.json || fail 138
echo 'URE_DUALBOOT_CROSS_BOOT_RESUME 0'
"$ctl" partition job-inspect /media/job --image /media/disk.img --sector-size 4096 > /media/committed.json || fail 139
grep -q '^    "classification" : "TARGET"' /media/committed.json || fail 140
"$ctl" partition job-rollback /media/job --image /media/disk.img --sector-size 4096 --confirm "$inner_hash" > /media/rolled-back.json || fail 141
grep -q '^    "state" : "ROLLED_BACK"' /media/rolled-back.json || fail 142
original_unchanged
cmp /media/plan.json /media/job/dualboot-plan.json || fail 143
echo 'URE_DUALBOOT_COMPLETE_BYTE_ROLLBACK 0'
finish
GUEST
chmod 755 "$job/root/ure-dualboot-vm-init"
bash "$component/tests/check-payload.sh" "$job/root" > "$job/payload-gate.log"
(cd "$job/root" && find . -print0 | LC_ALL=C sort -z | cpio --null -o --format=newc --quiet | gzip -n -1) > "$job/initrd.cpio.gz"
boot() {
    local number=$1
    timeout 600 "$qemu" -machine virt -cpu cortex-a72 -smp 2 -m 2048 -accel tcg \
        -display none -monitor none -serial stdio -no-reboot -nic none -no-user-config \
        -kernel "$kernel" -initrd "$job/initrd.cpio.gz" \
        -append 'console=ttyAMA0 rdinit=/ure-dualboot-vm-init panic=1 ure_fixture=1' \
        -drive "if=none,id=fixture,format=raw,file=$job/disposable-media.img,cache=none" \
        -device virtio-blk-device,drive=fixture > "$job/boot-$number.log" 2>&1
}
boot 1
for marker in URE_DUALBOOT_SHELL_AND_JSON_PREVIEW URE_DUALBOOT_EXACT_CONSENT_REFUSED; do
    rg -q "^$marker 0\r?$" "$job/boot-1.log"
done
positive=false
if rg -q '^URE_DUALBOOT_SHIPPING_OWNERSHIP_REFUSED 0\r?$' "$job/boot-1.log"; then
    for marker in URE_DUALBOOT_REFUSAL_COMPLETE_IMAGE_UNCHANGED URE_DUALBOOT_VM_EXIT; do
        rg -q "^$marker 0\r?$" "$job/boot-1.log"
    done
    ! rg -q '^URE_DUALBOOT_FORCED_REBOOT' "$job/boot-1.log"
else
    rg -q '^URE_DUALBOOT_FORCED_REBOOT 1\r?$' "$job/boot-1.log"
    boot 2
    for marker in URE_DUALBOOT_SAVED_WRAPPER_PREVIEW URE_DUALBOOT_CROSS_BOOT_INSPECT URE_DUALBOOT_CROSS_BOOT_RESUME URE_DUALBOOT_COMPLETE_BYTE_ROLLBACK URE_DUALBOOT_VM_EXIT; do
        rg -q "^$marker 0\r?$" "$job/boot-2.log"
    done
    positive=true
fi
jq -n --arg kernel "$(sha256sum "$kernel" | cut -d' ' -f1)" \
    --arg image "$(sha256sum "$image" | cut -d' ' -f1)" \
    --arg binary "$(sha256sum "$payload/system/bin/uke-recoveryctl" | cut -d' ' -f1)" \
    --arg runner "$(sha256sum "${BASH_SOURCE[0]}" | cut -d' ' -f1)" \
    --arg fixture "$(sha256sum "$fixture" | cut -d' ' -f1)" \
    --arg evidence "$(sha256sum "$job/compile-evidence.json" | cut -d' ' -f1)" \
    --arg version "$("$qemu" --version | head -n 1)" --argjson positive "$positive" \
    '{schema_version:1,passed:true,validation_kind:"qemu-system-packed-dualboot-image-route",
      kernel_sha256:$kernel,recovery_image_sha256:$image,native_cli_sha256:$binary,runner_sha256:$runner,
      fixture_sha256:$fixture,packed_payload_evidence_sha256:$evidence,qemu_version:$version,
      positive_execute:$positive,functional_feature_acceptance:$positive,
      checks:(["interactive-five-role-shell-preview","JSON-five-role-plan-preview","exact-hash-and-data-policy-refused","preview-and-refusal-original-image-unchanged"]+
        if $positive then ["saved-approved-wrapper-preview","actual-guest-emergency-reboot","persistent-journal-reopen","inner-job-exact-consent-resume","protected-ranges-verified","complete-original-image-rollback"]
        else ["shipping-ownership-unavailable-refused","complete-image-unchanged-after-owner-refusal","saved-preview-unchanged-after-owner-refusal"] end),
      refusal_code:(if $positive then null else "ownership-unavailable" end),
      interruption:(if $positive then "guest-sysrq-emergency-reboot" else null end),
      shipping_kernel_test:false,physical_device:false,tablet_hardware_test:false,ufs_controller_test:false,
      host_block_attachment:false,network:false,encrypted_userdata:false,live_device_execute:false}' > "$job/verification.json"
cp -- "$job/verification.json" "$component/reports/private/dualboot-vm-verification.json"
if $positive; then
    printf '%s\n' 'Packed ARM64 dualboot image route passed forced guest reboot, exact journal resume and complete rollback; generic kernel and disposable images only.'
else
    printf '%s\n' 'Packed ARM64 dualboot previews and exact-consent refusals passed; shipping execution refused ownership-unavailable with the complete image unchanged. Positive execution is unaccepted.'
fi
