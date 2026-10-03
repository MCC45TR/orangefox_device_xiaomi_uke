#!/usr/bin/env bash
# Observed names and logical allocations; synthetic physical geometry only.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
kernel=$(realpath -- "${1:?Pass a generic ARM64 virt kernel}")
qemu=${UKE_QEMU_SYSTEM_AARCH64:?Pass the reviewed QEMU executable}
payload="$component/src/upstream/orangefox-android16/out-public/target/product/uke/recovery/root"
fixture="$component/tests/fixtures/stock-adb-2026-10-03.json"
[[ -f $kernel && ! -L $kernel && -x $qemu ]]
mkdir -p "$component/build/stock-layout-vm" "$component/reports/private"
job=$(mktemp -d "$component/build/stock-layout-vm/job-XXXXXX")
mkdir "$job/disks" "$job/root"
cp -a "$payload/." "$job/root/"
target() { timeout 30 qemu-aarch64 -L "$payload" -E "LD_LIBRARY_PATH=$payload/system/lib64:$payload/vendor/lib64" "$payload/system/bin/$1" "${@:2}"; }
args=(--metadata-size 65536 --metadata-slots 3 --device super:11274289152:1048576:0
      --group qti_dynamic_partitions_a:11263803392 --group qti_dynamic_partitions_b:11263803392 --output "$job/super-metadata.img")
while read -r name bytes; do args+=(--partition "$name:readonly:$bytes:qti_dynamic_partitions_a"); done < <(jq -r '.logical_active_extents[]|"\(.name) \(.size_bytes)"' "$fixture")
while read -r name; do args+=(--partition "$name:readonly:0:qti_dynamic_partitions_b"); done < <(jq -r '.logical_active_extents[].name|sub("_a$";"_b")' "$fixture")
target lpmake "${args[@]}" > "$job/lpmake.log" 2>&1
c++ -std=c++20 -O2 -Wall -Wextra -Werror -I "$component/src/upstream/orangefox-android16/external/jsoncpp/include" \
    "$component/tests/vm/stock-layout.cpp" "$component/build/ure-host/libuke-jsoncpp.a" -o "$job/stock-layout"
for mutation in incomplete duplicate invalid-index invalid-extent; do
    case "$mutation" in
        incomplete) filter='.physical_partition_labels |= .[1:]';;
        duplicate) filter='.physical_partition_labels[1] = .physical_partition_labels[0]';;
        invalid-index) filter='.physical_partition_labels[0].device="/dev/block/sdd128"';;
        invalid-extent) filter='.logical_active_extents[0].start_sector=18446744073709500000';;
    esac
    jq "$filter" "$fixture" > "$job/$mutation.json"
    mkdir "$job/$mutation"
    if "$job/stock-layout" "$job/$mutation.json" "$job/$mutation" > "$job/$mutation.log" 2>&1; then
        echo 'Malformed namespace evidence was accepted' >&2; exit 1
    fi
    [[ -z $(ls -A "$job/$mutation") ]]
done
"$job/stock-layout" "$fixture" "$job/disks" "$job/super-metadata.img"
mkdir -p "$job/root/proc" "$job/root/sys" "$job/root/dev" "$job/root/run" "$job/root/tmp"
install -m755 "$component/build/gui-vm/uke-vm-properties" "$job/root/system/bin/uke-vm-properties"
install -m755 "$component/build/gui-vm/uke-vm-lpdump" "$job/root/system/bin/uke-vm-lpdump"
install -m644 "$component/build/gui-vm/property_info" "$job/root/ure-vm-property-info"
cat > "$job/root/ure-stock-namespace-init" <<'GUEST'
#!/system/bin/sh
export PATH=/system/bin
export LD_LIBRARY_PATH=/system/lib64:/vendor/lib64:/system/lib64/bootstrap
/system/bin/toybox mount -t proc proc /proc || exit 100
mount -t sysfs sysfs /sys || exit 101
mount -t devtmpfs devtmpfs /dev || exit 102
mount -t tmpfs tmpfs /run || exit 103
mount -t tmpfs tmpfs /tmp || exit 104
mkdir -p /dev/__properties__ /dev/block/by-name
cp /ure-vm-property-info /dev/__properties__/property_info
chmod 444 /dev/__properties__/property_info
uke-vm-properties || exit 105
for entry in /sys/class/block/vd*; do
    [ -f "$entry/partition" ] || continue
    label=$(sed -n 's/^PARTNAME=//p' "$entry/uevent")
    [ -n "$label" ] || exit 106
    ln -s "/dev/${entry##*/}" "/dev/block/by-name/$label" || exit 107
done
echo URE_STOCK_LABELS_READY
toybox timeout -s KILL 45 uke-recoveryctl storage graph > /run/graph.json || exit 108
echo URE_STOCK_GRAPH_READY
toybox timeout -s KILL 45 uke-vm-lpdump -s 0 -j /dev/block/by-name/super > /run/logical.json || exit 109
echo URE_STOCK_LOGICAL_READY
toybox timeout -s KILL 45 uke-vm-lpdump -s 0 /dev/block/by-name/super > /run/extents.txt || exit 110
echo URE_STOCK_GRAPH_BEGIN; cat /run/graph.json; echo URE_STOCK_GRAPH_END
echo URE_STOCK_LOGICAL_BEGIN; cat /run/logical.json; echo URE_STOCK_LOGICAL_END
echo URE_STOCK_EXTENTS_BEGIN; cat /run/extents.txt; echo URE_STOCK_EXTENTS_END
echo 'URE_STOCK_NAMESPACE_EXIT 0'
echo o > /proc/sysrq-trigger
toybox reboot -p -f
GUEST
chmod 755 "$job/root/ure-stock-namespace-init"
(cd "$job/root" && find . -print0 | LC_ALL=C sort -z | cpio --null -o --format=newc --owner=0:0 --quiet | gzip -n -1) > "$job/initrd.cpio.gz"
drives=()
for disk in a b c d e f; do
    image="$job/disks/sd$disk.img"; [[ -f $image && ! -L $image ]]
    drives+=(-drive "if=none,id=stock$disk,format=raw,file=$image,readonly=on" -device "virtio-blk-device,drive=stock$disk")
done
printf '%s\n' "$job" > "$component/build/stock-layout-vm/latest-job"
timeout 180 "$qemu" -machine virt -cpu cortex-a72 -smp 2 -m 2048 -accel tcg \
    -display none -monitor none -serial stdio -no-reboot -nic none -no-user-config \
    -kernel "$kernel" -initrd "$job/initrd.cpio.gz" \
    -append 'console=ttyAMA0 rdinit=/ure-stock-namespace-init panic=1 ure_fixture=1' "${drives[@]}" > "$job/console.log" 2>&1
rg -q '^URE_STOCK_NAMESPACE_EXIT 0\r?$' "$job/console.log"
for record in GRAPH LOGICAL EXTENTS; do
    awk -v begin="URE_STOCK_${record}_BEGIN" -v end="URE_STOCK_${record}_END" '{sub(/\r$/,"")} $0==end {copying=0} copying {print} $0==begin {copying=1}' "$job/console.log" > "$job/${record,,}.json"
done
# Validate each observed index and that every label group has one unique parent.
jq -e --slurpfile observed "$fixture" '
    .data as $graph | .data | .read_only and ([.objects[]|select(.partition)]|length)==121 and
    ([.objects[]|select(.partition)|.label]|sort)==([$observed[0].physical_partition_labels[].name]|sort) and
    all(.objects[]|select(.partition); .read_only_state and .write_policy=="PROTECTED_OR_UNCLASSIFIED") and
    ([.objects[]|select(.partition)|.parent_lun_name]|unique|length)==6 and
    all($observed[0].physical_partition_labels[]; . as $expected |
        ($expected.device|capture("sd(?<disk>[a-f])(?<index>[0-9]+)$").index|tonumber) as $index |
        any($graph.objects[]; .label==$expected.name and .partition_index==$index)) and
    all(["sda","sdb","sdc","sdd","sde","sdf"][]; . as $disk |
        [$observed[0].physical_partition_labels[]|select(.device|startswith("/dev/block/"+$disk))|.name] as $names |
        ([$graph.objects[]|select(.label as $label|$names|index($label))|.parent_lun_name]|unique|length)==1)
    ' "$job/graph.json" >/dev/null
jq -e --slurpfile observed "$fixture" '
    ([.partitions[]|select(((.size//0)|tonumber)>0)|{name:(.name|sub("_a$";"")),size:(.size|tonumber)}]|sort_by(.name))==
    ([$observed[0].logical.partitions[]|{name,size:(.size|tonumber)}]|sort_by(.name)) and
    (.super_device.total_size|tonumber)==11274289152 and (.super_device.used_size|tonumber)==7936512000
    ' "$job/logical.json" >/dev/null
while read -r name start end; do
    rg -q "${name}" "$job/extents.json"
    # AOSP prints the logical partition's start/end followed by physical start;
    # its layout summary uses exclusive super-relative sector ends.
    rg -q "^super: ${start} \.\. ${end}: ${name} " "$job/extents.json"
done < <(jq -r '.logical_active_extents[]|"\(.name) \(.start_sector) \(.end_sector_exclusive)"' "$fixture")
jq -n --arg runner "$(sha256sum "${BASH_SOURCE[0]}" | cut -d' ' -f1)" \
    --arg generator "$(sha256sum "$component/tests/vm/stock-layout.cpp" | cut -d' ' -f1)" \
    --arg fixture "$(sha256sum "$fixture" | cut -d' ' -f1)" --arg kernel "$(sha256sum "$kernel" | cut -d' ' -f1)" \
    --arg binary "$(sha256sum "$payload/system/bin/uke-recoveryctl" | cut -d' ' -f1)" \
    --arg library "$(sha256sum "$payload/system/lib64/liblpdump.so" | cut -d' ' -f1)" \
    --arg adapter "$(sha256sum "$component/build/gui-vm/uke-vm-lpdump" | cut -d' ' -f1)" \
    --arg properties "$(sha256sum "$component/build/gui-vm/uke-vm-properties" | cut -d' ' -f1)" \
    '{schema_version:1,passed:true,validation_kind:"generic-vm-stock-namespace",source_record:"STOCK-ADB-20261003-01",
      fixture_sha256:$fixture,generator_sha256:$generator,runner_sha256:$runner,kernel_sha256:$kernel,native_cli_sha256:$binary,
      metadata_library_sha256:$library,vm_entry_adapter_sha256:$adapter,property_adapter_sha256:$properties,
      malformed_input_refusals:["incomplete","duplicate","invalid-index","invalid-extent"],
      observed_partition_labels:121,observed_disk_groups:6,active_logical_extents:8,read_only_attachments:true,
      physical_geometry:"synthetic",guid_identity:"synthetic",firmware_contents:false,encrypted_userdata:false,
      ufs_lu_numbers_known:false,metadata_library_entry:"VM-only direct library adapter; Binder client untested",
      shipping_kernel_test:false,physical_device:false,stock_restore_acceptance:false,nic:false,host_block_attachment:false}' \
    > "$component/reports/private/stock-namespace-vm-verification.json"
echo 'Observed stock namespace and logical allocations passed on six read-only synthetic VM disks; no physical restore or encryption acceptance.'
