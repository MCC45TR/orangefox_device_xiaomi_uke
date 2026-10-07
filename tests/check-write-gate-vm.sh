#!/usr/bin/env bash
# Generic guest only: native OrangeFox RPC, read-only filesystem mounts and
# writable QEMU attachments to newly created regular files. No host devices.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ -n ${UKE_RECOVERY_SOURCE_COMPONENT:-} ]]; then
    component=$(realpath -- "$UKE_RECOVERY_SOURCE_COMPONENT")
fi
kernel=$(realpath -- "${1:?Pass the pinned generic ARM64 virt kernel Image}")
modules=$(realpath -- "${2:?Pass its matching module installation root}")
version=${3:-7.2.8}
[[ $version == 7.2.8 && -f $kernel && ! -L $kernel && -d $modules/lib/modules/$version ]]
[[ $(sha256sum "$kernel" | cut -d' ' -f1) == 0dafe914751929b011f6a3d8d84e47c51452de4b604d123714b64dd2546e8a80 ]]
qemu=${UKE_QEMU_SYSTEM_AARCH64:-$(type -P qemu-system-aarch64 || true)}
[[ -x $qemu ]]
payload="$component/src/upstream/orangefox-android16/out-public/target/product/uke/recovery/root"
adapters="$component/build/gui-vm"
for file in recovery-vm uke-vm-properties property_info; do [[ -s $adapters/$file ]]; done
strings "$payload/system/bin/recovery" | rg -F 'URE_STORAGE_WRITE_BLOCKED' >/dev/null
mkdir -p "$component/build/write-gate-vm" "$component/reports/private"
job=$(mktemp -d "$component/build/write-gate-vm/job-XXXXXX")
root="$job/root"
mkdir "$root"
cp -a "$payload/." "$root/"
install -m755 "$adapters/recovery-vm" "$root/system/bin/recovery-vm"
install -m755 "$adapters/uke-vm-properties" "$root/system/bin/uke-vm-properties"
install -m644 "$adapters/property_info" "$root/ure-vm-property-info"
mkdir -p "$root/run" "$root/tmp" "$root/proc" "$root/sys" "$root/dev" "$root/data" "$root/metadata"
: > "$root/ure-vm-module-order"
modprobe --show-depends --set-version "$version" --dirname "$modules" virtio_gpu > "$job/module-dependencies"
modprobe --show-depends --set-version "$version" --dirname "$modules" virtio_input >> "$job/module-dependencies"
while read -r command path rest; do
    [[ $command == insmod && -f $path && ! -L $path ]]
    relative=${path#"$modules/lib/modules/$version/"}
    [[ $relative != "$path" && $relative != *..* ]]
    install -Dm644 "$path" "$root/ure-vm-modules/$relative"
    printf '/ure-vm-modules/%s\n' "$relative" >> "$root/ure-vm-module-order"
done < "$job/module-dependencies"
(cd "$root" && find ure-vm-modules -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum) > "$job/module-inputs.sha256"
cat > "$root/system/etc/twrp.fstab" <<'FSTAB'
/data ext4 /dev/ure-vm-data flags=display=Data;storage;settingsstorage;backup=1;readonly
/metadata ext4 /dev/ure-vm-metadata flags=display=Metadata;backup=1;readonly
/misc emmc /dev/ure-vm-misc flags=backup=0;readonly
FSTAB
cat > "$root/system/etc/recovery.fstab" <<'FSTAB'
/dev/ure-vm-data /data ext4 ro,noload defaults
/dev/ure-vm-metadata /metadata ext4 ro,noload defaults
/dev/ure-vm-misc /misc emmc defaults defaults
FSTAB
cat > "$root/ure-write-gate-init" <<'GUEST'
#!/system/bin/sh
export PATH=/system/bin
export ANDROID_ROOT=/system ANDROID_DATA=/data ANDROID_STORAGE=/storage EXTERNAL_STORAGE=/sdcard
export LD_LIBRARY_PATH=/system/lib64:/vendor/lib64:/system/lib64/bootstrap
/system/bin/toybox mount -t proc proc /proc || exit 101
mount -t sysfs sysfs /sys || exit 102
mount -t devtmpfs devtmpfs /dev || exit 103
mount -t tmpfs tmpfs /run || exit 104
mount -t tmpfs tmpfs /tmp || exit 105
fixture_disk() {
    wanted=$1
    expected_sectors=$2
    selected_disk=''
    for serial_file in /sys/class/block/vd*/serial; do
        [ -r "$serial_file" ] || continue
        [ "$(cat "$serial_file")" = "$wanted" ] || continue
        [ -z "$selected_disk" ] || return 1
        disk_path=${serial_file%/serial}
        [ "$(cat "$disk_path/size")" = "$expected_sectors" ] || return 1
        selected_disk="/dev/${disk_path##*/}"
    done
    [ -n "$selected_disk" ] && [ -b "$selected_disk" ] || return 1
    printf '%s\n' "$selected_disk"
}
data_disk=$(fixture_disk ure-gate-data 262144) || exit 120
metadata_disk=$(fixture_disk ure-gate-metadata 262144) || exit 121
misc_disk=$(fixture_disk ure-gate-misc 8192) || exit 122
[ "$data_disk" != "$metadata_disk" ] && [ "$data_disk" != "$misc_disk" ] && [ "$metadata_disk" != "$misc_disk" ] || exit 123
ln -s "$data_disk" /dev/ure-vm-data || exit 124
ln -s "$metadata_disk" /dev/ure-vm-metadata || exit 125
ln -s "$misc_disk" /dev/ure-vm-misc || exit 126
echo "URE_GATE_FIXTURE_DATA $data_disk"
echo "URE_GATE_FIXTURE_METADATA $metadata_disk"
echo "URE_GATE_FIXTURE_MISC $misc_disk"
while read -r module; do insmod "$module" || exit 106; done < /ure-vm-module-order
mkdir -p /dev/graphics
ln -s /dev/fb0 /dev/graphics/fb0
# No journal replay or mount-time fixture write can mask the gate result.
mount -t ext4 -o ro,noload /dev/ure-vm-data /data || exit 107
mount -t ext4 -o ro,noload /dev/ure-vm-metadata /metadata || exit 108
mkdir -p /dev/__properties__
cp /ure-vm-property-info /dev/__properties__/property_info
chmod 444 /dev/__properties__/property_info
uke-vm-properties || exit 109
recovery-vm &
recovery_pid=$!
for attempt in $(seq 1 60); do
    [ -p /system/bin/foxin ] && [ -p /system/bin/foxout ] && break
    kill -0 "$recovery_pid" || { cat /tmp/recovery.log; exit 110; }
    sleep 1
done
[ -p /system/bin/foxin ] && [ -p /system/bin/foxout ] || { cat /tmp/recovery.log; exit 111; }
rpc() {
    name=$1
    request=$2
    toybox timeout -s KILL 30 sh -c 'cat /system/bin/foxout' > "/run/$name.rpc" &
    reader=$!
    toybox timeout -s KILL 30 sh -c 'printf "%s\n" "$1" > /system/bin/foxin' sh "$request" || exit 112
    wait "$reader" || { cat /tmp/recovery.log; exit 113; }
    echo "URE_GATE_RPC_${name}_BEGIN"
    cat "/run/$name.rpc"
    echo "URE_GATE_RPC_${name}_END"
}
rpc status '{"v":1,"op":"status","args":{}}'
rpc confirm '{"v":1,"op":"format_data","args":{"confirm":false}}'
rpc format '{"v":1,"op":"format_data","args":{"confirm":true}}'
rpc repair '{"v":1,"op":"partition","args":{"path":"/data","action":"repair"}}'
rpc resize '{"v":1,"op":"partition","args":{"path":"/data","action":"resize"}}'
rpc changefs '{"v":1,"op":"partition","args":{"path":"/data","action":"change-fs","fs":"f2fs"}}'
rpc wipe '{"v":1,"op":"partition","args":{"path":"/data","action":"wipe"}}'
rpc flash '{"v":1,"op":"flash","args":{"zips":["/run/not-a-package.zip"]}}'
rpc sideload '{"v":1,"op":"sideload","args":{}}'
rpc script '{"v":1,"op":"ors-cmd","args":{"raw":["cmd","touch","/run/ors-bypass"]}}'
[ ! -e /run/ors-bypass ] || exit 114
rpc orsformat '{"v":1,"op":"ors-cmd","args":{"raw":["format","data"]}}'
rpc orswipe '{"v":1,"op":"ors-cmd","args":{"raw":["wipe","data"]}}'
rpc orsmkdir '{"v":1,"op":"ors-cmd","args":{"raw":["mkdir","/run/ors-mkdir"]}}'
[ ! -e /run/ors-mkdir ] || exit 118
rpc orsslot '{"v":1,"op":"ors-cmd","args":{"raw":["set_active","B"]}}'
rpc orsbackup '{"v":1,"op":"ors-cmd","args":{"raw":["backup","D"]}}'
printf '%s\n' 'mkdir /run/ors-file-bypass' > /run/original.ors
rpc orsfile '{"v":1,"op":"ors","args":{"path":"/run/original.ors"}}'
[ -f /run/original.ors ] && [ ! -e /run/ors-file-bypass ] || exit 119
rpc reflash '{"v":1,"op":"reflash","args":{}}'
rpc mtp '{"v":1,"op":"mtp","args":{"action":"enable"}}'
rpc preference '{"v":1,"op":"internal","args":{"action":"set","name":"tw_mount_system_ro","value":"0"}}'
rpc formatagain '{"v":1,"op":"format_data","args":{"confirm":true}}'
rpc finalstatus '{"v":1,"op":"status","args":{}}'
echo URE_GATE_RECOVERY_LOG_BEGIN
cat /tmp/recovery.log
echo URE_GATE_RECOVERY_LOG_END
kill -0 "$recovery_pid" || exit 115
kill -TERM "$recovery_pid"
wait "$recovery_pid"
if grep -q ' /data ' /proc/mounts; then umount /data || exit 116; fi
if grep -q ' /metadata ' /proc/mounts; then umount /metadata || exit 117; fi
echo 'URE_WRITE_GATE_VM_EXIT 0'
echo o > /proc/sysrq-trigger
toybox reboot -p -f
while :; do sleep 1; done
GUEST
chmod 755 "$root/ure-write-gate-init"
for disk in data metadata; do
    truncate -s 128M "$job/$disk.img"
    [[ -f $job/$disk.img && ! -L $job/$disk.img ]]
    mke2fs -q -t ext4 -F -O '^orphan_file,^metadata_csum_seed' "$job/$disk.img"
done
truncate -s 4M "$job/misc.img"
[[ -f $job/misc.img && ! -L $job/misc.img ]]
printf 'boot-recovery\0' | dd of="$job/misc.img" conv=notrunc status=none
(cd "$job" && sha256sum data.img metadata.img misc.img) > "$job/media-before.sha256"
(cd "$root" && find . -print0 | LC_ALL=C sort -z | cpio --null -o --format=newc --owner=0:0 --quiet | gzip -n -1) > "$job/initrd.cpio.gz"
printf '%s\n' "$job" > "$component/build/write-gate-vm/latest-job"
timeout 540 "$qemu" -machine virt -cpu cortex-a72 -smp 2 -m 2048 -accel tcg \
    -display none -monitor none -serial stdio -no-reboot -nic none -no-user-config \
    -global virtio-mmio.force-legacy=false -kernel "$kernel" -initrd "$job/initrd.cpio.gz" \
    -append 'console=ttyAMA0 rdinit=/ure-write-gate-init panic=1 ure_fixture=1' \
    -drive "if=none,id=data,format=raw,file=$job/data.img" -device virtio-blk-device,drive=data,serial=ure-gate-data \
    -drive "if=none,id=metadata,format=raw,file=$job/metadata.img" -device virtio-blk-device,drive=metadata,serial=ure-gate-metadata \
    -drive "if=none,id=misc,format=raw,file=$job/misc.img" -device virtio-blk-device,drive=misc,serial=ure-gate-misc \
    -device 'virtio-gpu-device,xres=3200,yres=2136' -device virtio-keyboard-device \
    > "$job/console.log" 2>&1
rg -q '^URE_WRITE_GATE_VM_EXIT 0\r?$' "$job/console.log"
if rg -q 'Scudo ERROR|Fatal signal|Kernel panic' "$job/console.log"; then exit 1; fi
(cd "$job" && sha256sum -c media-before.sha256) > "$job/media-unchanged.log"
for operation in status confirm format repair resize changefs wipe flash sideload script orsformat orswipe orsmkdir orsslot orsbackup orsfile reflash mtp preference formatagain finalstatus; do
    awk -v begin="URE_GATE_RPC_${operation}_BEGIN" -v end="URE_GATE_RPC_${operation}_END" \
        '{sub(/\r$/,"")} $0==end {copying=0} copying {print} $0==begin {copying=1}' "$job/console.log" > "$job/$operation.jsonl"
    expected=1
    case $operation in status|preference|finalstatus) expected=0;; confirm) expected=2;; esac
    jq -se --argjson expected "$expected" '([.[]|select(.event=="result")]|length)==1 and
        all(.[]|select(.event=="result"); .code==$expected)' "$job/$operation.jsonl" >/dev/null
done
rg -q 'URE_STORAGE_WRITE_BLOCKED operation=format code=ure-legacy-write-unavailable' "$job/console.log"
jq -n --arg runner "$(sha256sum "${BASH_SOURCE[0]}" | cut -d' ' -f1)" \
    --arg kernel "$(sha256sum "$kernel" | cut -d' ' -f1)" \
    --arg shipping "$(sha256sum "$payload/system/bin/recovery" | cut -d' ' -f1)" \
    --arg adapted "$(sha256sum "$adapters/recovery-vm" | cut -d' ' -f1)" \
    --arg log "$(sha256sum "$job/console.log" | cut -d' ' -f1)" \
    --arg media "$(sha256sum "$job/media-before.sha256" | cut -d' ' -f1)" \
    --arg modules "$(sha256sum "$job/module-inputs.sha256" | cut -d' ' -f1)" \
    '{schema_version:1,passed:true,validation_kind:"qemu-system-adapted-orangefox-native-write-gate",
      runner_sha256:$runner,kernel_sha256:$kernel,shipping_recovery_sha256:$shipping,adapted_recovery_sha256:$adapted,
      console_sha256:$log,fixture_media_hashes_sha256:$media,module_inputs_sha256:$modules,rpc_result_count:21,
      checks:["status-remains-available","confirmation-is-not-authorization","format-data-refused","repair-refused",
        "resize-refused","filesystem-change-refused","wipe-refused","package-install-refused","sideload-refused",
        "ors-script-refused-without-sentinel","ors-format-failure-preserved","ors-wipe-refused","ors-mkdir-refused",
        "ors-slot-mutation-refused","ors-backup-command-refused","ors-source-preserved","recovery-reflash-refused",
        "mtp-write-service-refused","preference-cannot-authorize-format",
        "inspection-after-refusal",
        "userdata-metadata-and-misc-complete-hashes-unchanged"],
      adapters:{memfd_code_cache:true,synthetic_property_area:true,disposable_fstab:true},
      writable_qemu_attachments:true,guest_filesystem_mounts_read_only:true,geometry:"synthetic",
      fixture_disk_binding:"unique virtio serial and exact sector count; guest aliases",
      startup_BCB_diagnostics_observed:false,startup_misc_mutation_attempt_observed:false,
      nic:false,host_block_attachment:false,physical_device:false,shipping_kernel_test:false,
      unmodified_shipping_gui_test:false,fastboot_usb_hardware_test:false,visual_acceptance:false,complete_feature_acceptance:false}' \
    > "$component/reports/private/write-gate-vm-verification.json"
echo 'Native recovery write refusals passed in a generic guest; all three entire disposable media images remain unchanged.'
