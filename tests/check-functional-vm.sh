#!/usr/bin/env bash
# Production ARM64 CLI/tools, real guest syscalls and newly created file disks.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
kernel=$(realpath -- "${1:?Pass a generic ARM64 virt kernel}")
group=${2:?core/filesystems/rescue/btrfs}
[[ $group == core || $group == filesystems || $group == rescue || $group == btrfs ]]
qemu=${UKE_QEMU_SYSTEM_AARCH64:?Pass the reviewed emulator}
payload="$component/src/upstream/orangefox-android16/out-public/target/product/uke/recovery/root"
guest="$component/tests/vm/functional-init.sh"
[[ -f $kernel && ! -L $kernel && -x $qemu && -x $payload/system/bin/uke-recoveryctl ]]
runner_before=$(sha256sum "${BASH_SOURCE[0]}" | cut -d' ' -f1)
guest_before=$(sha256sum "$guest" | cut -d' ' -f1)
kernel_before=$(sha256sum "$kernel" | cut -d' ' -f1)
for command in mke2fs cpio gzip jq sha256sum timeout; do command -v "$command" >/dev/null; done
mkdir -p "$component/build/functional-vm" "$component/reports/private"
job=$(mktemp -d "$component/build/functional-vm/job-XXXXXX")
root="$job/root"
mkdir "$root"
payload_inputs() {
    (cd "$payload"; find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum
     find . -type l -printf '%p\t%l\n' | LC_ALL=C sort)
}
payload_inputs > "$job/payload-inputs.sha256"
cp -a -- "$payload/." "$root/"
install -m755 "$guest" "$root/ure-functional-init"
printf '%s\n' "$group" > "$root/ure-function-group"
mkdir -p "$root"/{media,proc,sys,dev,run,tmp}
# Format, repair and resize retain separate complete rollback journals.
# The 512 MiB F2FS case must fit all three plus staging and safety margins.
media_capacity=6G
guest_timeout=1800
if [[ $group == filesystems ]]; then media_capacity=12G; guest_timeout=3600; fi
truncate -s "$media_capacity" "$job/media.img"
[[ -f $job/media.img && ! -L $job/media.img ]]
mke2fs -q -F -t ext4 -O '^orphan_file,^metadata_csum_seed' "$job/media.img" > "$job/mkfs.log" 2>&1
drives=(-drive "if=none,id=media,format=raw,file=$job/media.img,cache=none" -device virtio-blk-device,drive=media,serial=ure-functional-media)
modules_before=''
strip_before=''
if [[ $group == btrfs ]]; then
    modules=$(realpath -- "${3:?Pass matching 7.2.8 module tree}")
    version=${4:-7.2.8}
    [[ $version == 7.2.8 ]]
    module_files=(lib/crypto/libblake2b.ko lib/raid/xor/xor.ko lib/raid/raid6/raid6_pq.ko lib/zstd/zstd_compress.ko fs/btrfs/btrfs.ko)
    module_inputs() { (cd "$modules/lib/modules/$version/kernel"; sha256sum "${module_files[@]}"); }
    module_inputs > "$job/module-inputs.sha256"
    modules_before=$(sha256sum "$job/module-inputs.sha256" | cut -d' ' -f1)
    strip_tool="$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/llvm-strip"
    strip_before=$(sha256sum "$strip_tool" | cut -d' ' -f1)
    [[ $strip_before == d2a3191ad2228bb60c35e18466615cb53ed7263c2e73134624349f0367cb1f88 ]]
    printf '' > "$root/ure-function-modules"
    for module in "${module_files[@]}"; do
        install -Dm644 "$modules/lib/modules/$version/kernel/$module" "$root/ure-test-modules/$module"
        # Generic-kernel DWARF includes local build paths. Strip debug only in
        # the disposable copy; keep loadable symbols and the input tree intact.
        "$strip_tool" --strip-debug "$root/ure-test-modules/$module"
        printf '/ure-test-modules/%s\n' "$module" >> "$root/ure-function-modules"
    done
    truncate -s 512M "$job/btrfs.img"
    [[ -f $job/btrfs.img && ! -L $job/btrfs.img ]]
    mkfs.btrfs -q -f "$job/btrfs.img" > "$job/btrfs-mkfs.log" 2>&1
    drives+=(-drive "if=none,id=btrfs,format=raw,file=$job/btrfs.img,cache=none" -device virtio-blk-device,drive=btrfs,serial=ure-functional-btrfs)
fi
bash "$component/tests/check-payload.sh" "$root" > "$job/payload-gate.log"
(cd "$root" && find . -print0 | LC_ALL=C sort -z | cpio --null -o --format=newc --owner=0:0 --quiet | gzip -n -1) > "$job/initrd.cpio.gz"
printf '%s\n' "$job" > "$component/build/functional-vm/latest-job"
printf 'Functional VM group %s, private job: %s\n' "$group" "$job"
timeout "$guest_timeout" "$qemu" -machine virt -cpu cortex-a72 -smp 2 -m 2048 -accel tcg \
    -display none -monitor none -serial stdio -no-reboot -nic none -no-user-config \
    -kernel "$kernel" -initrd "$job/initrd.cpio.gz" \
    -append 'console=ttyAMA0 rdinit=/ure-functional-init panic=1 ure_function_fixture=1' \
    "${drives[@]}" > "$job/console.log" 2>&1
rg -q "^URE_FUNCTION_EXIT $group 0\r?$" "$job/console.log"
if rg -q 'URE_FUNCTION_FAILURE|Scudo ERROR|Fatal signal|Kernel panic' "$job/console.log"; then exit 1; fi
mkdir "$job/records"
while IFS= read -r name; do
    [[ $name =~ ^[a-z][a-z0-9-]+$ ]]
    awk -v begin="URE_FUNCTION_JSON_BEGIN $name" -v end="URE_FUNCTION_JSON_END $name" \
        '{sub(/\r$/,"")} $0==end {copying=0} copying {print} $0==begin {copying=1}' "$job/console.log" > "$job/records/$name.json"
    jq -e '.schema==1 and (.result=="ok" or .result=="error" or .result=="operation-error")' "$job/records/$name.json" >/dev/null
done < <(sed -n 's/^URE_FUNCTION_JSON_BEGIN \([a-z0-9-]*\)\r\?$/\1/p' "$job/console.log")
assert_json() {
    if ! jq -e "$2" "$job/records/$1.json" >/dev/null; then
        printf 'Functional JSON assertion failed: %s: %s\n' "$1" "$2" >&2
        return 1
    fi
}
assert_ok() { assert_json "$1" '.result=="ok"'; }
assert_error() { assert_json "$1" ".result==\"error\" and .error.code==\"$2\""; }
case "$group" in
core)
    assert_json capabilities '.data.active_scope.remote_transport=="adb-only" and .data.active_scope.bitlocker==false and .data.active_scope.network_rescue==false'
    assert_json platform-admission '.data.format=="ure-platform-capabilities" and .data.read_only and (.data.live_action_allowed|not) and
        (.data.credential_use_allowed|not) and (.data.mapper_creation_allowed|not) and (.data.encrypted_mount_allowed|not) and
        (.data.physical_test_record|not) and (.data.features|length)==11 and .data.health.decision=="hold"'
    assert_json platform-untrusted '(.data.current_system_root|not) and (.data.blockers|index("platform-observation-root-untrusted"))!=null'
    for operation in fbe-open slot-set snapshot-merge super-apply ota-install second-install; do
        assert_error "platform-$operation" platform-action-unavailable
    done
    assert_error platform-import fixture-only-command
    assert_error block-path-refused invalid-image
    assert_error unrelated-options invalid-options
    assert_json arch-detect '.data.distribution.ID=="arch" and .data.recovery_kernel_is_installed_kernel==false'
    assert_json fedora-detect '.data.distribution.ID=="fedora" and .data.recovery_kernel_is_installed_kernel==false'
    assert_ok files-list; assert_ok files-search
    assert_error traversal-refused invalid-path
    assert_error editor-wrong-confirm confirmation-required
    assert_json editor-execute '.data.state=="COMMITTED"'
    assert_json editor-inspect '.data.current_state=="PAYLOAD_VERIFIED" and .data.backup_verified'
    assert_json editor-rollback '.data.state=="ROLLED_BACK"'
    for percent in 50 55 60 65 70 75 80 85 90 95 100; do
        assert_json "scale-save-$percent" ".data.scale_percent==$percent and .data.mounts_performed==false"
        assert_json "scale-load-$percent" ".data.scale_percent==$percent"
    done
    assert_error scale-invalid invalid-scale
    for sector in 512 4096; do
        assert_json "raw-$sector-capture" '.data.state=="COMPLETE" and .data.verified'
        assert_error "raw-$sector-hole" invalid-backup
        assert_json "raw-$sector-partial" '.data.state=="PARTIAL" and .data.next_chunk==1'
        assert_json "raw-$sector-resume" '.data.state=="COMPLETE" and .data.verified'
        assert_json "raw-$sector-verify" '.data.state=="COMPLETE" and .data.verified'
        assert_error "raw-$sector-wrong-confirm" confirmation-required
        assert_json "raw-$sector-restore" '.data.state=="COMMITTED" and .data.verified'
        assert_json "raw-$sector-inspect" '.data.classification=="TARGET" and .data.backup_verified'
        assert_json "raw-$sector-rollback" '.data.state=="ROLLED_BACK" and .data.verified'
    done
    assert_error home-wrong-confirm confirmation-required
    assert_json home-capture '.data.state=="COMPLETE" and .data.verified'
    assert_json home-verify '.data.verified'
    assert_json home-restore '.data.state=="RESTORED" and .data.verified and .data.existing_files_overwritten==false'
    assert_error home-before-capture path-unavailable
    assert_error home-existing-target existing-target
    assert_error home-stale-source stale-source
    assert_error home-corrupt-pages invalid-json
    assert_json home-independent-verify '.data.verified'
    assert_json home-interrupted '.data.state=="CAPTURING" and .data.verified==false'
    assert_json home-resumed '.data.state=="COMPLETE" and .data.verified'
    assert_json home-resumed-verify '.data.verified'
    ;;
filesystems)
    assert_json filesystem-capabilities 'any(.data.filesystems[]; .filesystem=="exfat" and .offline_resize_available==false) and any(.data.filesystems[]; .filesystem=="vfat" and .offline_resize_available==false)'
    for type in ext4 exfat ntfs vfat f2fs; do
        assert_error "fs-$type-wrong-confirm" confirmation-required
        assert_json "fs-$type-format" '.data.state=="COMPLETE"'
        assert_json "fs-$type-inspect" ".data.type==\"$type\""
        assert_json "fs-$type-journal" '.data.application.classification=="TARGET"'
        assert_json "fs-$type-repair" '.data.state=="COMPLETE"'
        assert_json "fs-$type-repair-rollback" '.data.state=="ROLLED_BACK"'
        assert_json "fs-$type-rollback" '.data.state=="ROLLED_BACK"'
        case "$type" in
        ext4|ntfs|f2fs)
            assert_json "fs-$type-resize" '.data.state=="COMPLETE"'
            assert_json "fs-$type-resize-rollback" '.data.state=="ROLLED_BACK"';;
        exfat) assert_error fs-exfat-resize-unavailable resize-unavailable;;
        vfat) assert_error fs-vfat-resize-unavailable missing-tool;;
        esac
    done
    assert_error fs-ext4-live-loop busy-target
    ;;
rescue)
    for name in readonly-plan fedora-plan; do
        family=arch
        [[ $name != fedora-plan ]] || family=fedora
        assert_json "rescue-$name" ".data.distribution_family==\"$family\" and .data.connections[0].method==\"selected-esp\" and .data.raw_block_devices_exposed==false"
    done
    for name in readonly write fedora; do
        assert_json "rescue-$name" '.data.state=="COMPLETE" and .data.namespace_worker_reaped and .data.session_mounts_released and .data.descendants_bound_to_pid_namespace and .data.cleanup_pending==false'
    done
    assert_json rescue-timeout '.result=="operation-error" and .data.state=="TIMED_OUT" and .data.successful==false and .data.namespace_worker_reaped and .data.session_mounts_released and .data.cleanup_pending==false'
    assert_error rescue-stale stale-rescue-plan
    assert_error rescue-missing-distribution-tool missing-rescue-tool
    ;;
btrfs)
    assert_error btrfs-snapshot-wrong-confirm confirmation-required
    assert_error btrfs-corrupt-stream btrfs-stream-corrupt
    for name in create rollback scrub balance shrink grow; do assert_json "btrfs-$name-execute" '.data.state=="COMPLETE"'; done
    for name in snapshot second full-capture incremental-capture; do assert_json "btrfs-$name" '.data.progress.state=="COMPLETE"'; done
    for name in full-verify incremental-verify; do assert_json "btrfs-$name" '.data.data_verified'; done
    assert_ok btrfs-stream-check
    for name in subvolumes usage device-stats scrub-status balance-status; do assert_ok "btrfs-$name"; done
    assert_json btrfs-scrub-execute '.data.scrub_progress.uncorrectable_errors==0'
    ;;
esac
payload_inputs > "$job/payload-inputs-after.sha256"
cmp "$job/payload-inputs.sha256" "$job/payload-inputs-after.sha256"
cmp "$payload/system/bin/uke-recoveryctl" "$root/system/bin/uke-recoveryctl"
cmp "$guest" "$root/ure-functional-init"
[[ $runner_before == $(sha256sum "${BASH_SOURCE[0]}" | cut -d' ' -f1) &&
   $guest_before == $(sha256sum "$guest" | cut -d' ' -f1) &&
   $kernel_before == $(sha256sum "$kernel" | cut -d' ' -f1) ]]
if [[ $group == btrfs ]]; then
    module_inputs > "$job/module-inputs-after.sha256"
    cmp "$job/module-inputs.sha256" "$job/module-inputs-after.sha256"
    [[ $strip_before == $(sha256sum "$strip_tool" | cut -d' ' -f1) ]]
fi
checks=$(sed -n 's/^URE_FUNCTION_CHECK \([a-z0-9-]*\)\r\?$/\1/p' "$job/console.log" | jq -R . | jq -s .)
records=$(find "$job/records" -name '*.json' -type f | wc -l)
jq -n --arg group "$group" --arg runner "$(sha256sum "${BASH_SOURCE[0]}" | cut -d' ' -f1)" \
    --arg guest "$(sha256sum "$guest" | cut -d' ' -f1)" --arg kernel "$(sha256sum "$kernel" | cut -d' ' -f1)" \
    --arg cli "$(sha256sum "$payload/system/bin/uke-recoveryctl" | cut -d' ' -f1)" --arg console "$(sha256sum "$job/console.log" | cut -d' ' -f1)" \
    --arg initrd "$(sha256sum "$job/initrd.cpio.gz" | cut -d' ' -f1)" --arg userspace "$(sha256sum "$job/payload-inputs.sha256" | cut -d' ' -f1)" \
    --arg modules "$modules_before" --arg strip "$strip_before" \
    --argjson checks "$checks" --argjson records "$records" \
    '{schema_version:1,passed:true,validation_kind:"qemu-system-shipping-cli-functions",group:$group,runner_sha256:$runner,guest_script_sha256:$guest,kernel_sha256:$kernel,native_cli_sha256:$cli,console_sha256:$console,guest_initrd_sha256:$initrd,payload_inputs_sha256:$userspace,generic_module_inputs_sha256:(if $modules=="" then null else $modules end),module_strip_tool_sha256:(if $strip=="" then null else $strip end),generic_module_debug_stripped:($group=="btrfs"),checked_json_records:$records,checks:$checks,physical_device:false,shipping_kernel_test:false,modified_shipping_cli:false,real_distribution_installation:false,host_block_attachment:false,nic:false,complete_feature_acceptance:false}' \
    > "$component/reports/private/functional-$group-vm-verification.json"
printf 'Functional ARM64 guest group %s passed %s JSON records and %s data/metadata checks.\n' "$group" "$records" "$(jq length <<<"$checks")"
