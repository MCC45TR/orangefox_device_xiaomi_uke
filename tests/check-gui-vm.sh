#!/usr/bin/env bash
# Host-only graphical smoke runner: generic virt kernel, private ramdisk overlay,
# VM-only memory/property adapters and two newly created regular-file disks.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
kernel=$(realpath -- "${1:?Pass the generic ARM64 virt kernel Image}")
modules=$(realpath -- "${2:?Pass its installed lib/modules root}")
version=${3:?Pass the matching module release}
page=${4:-ure_display}
[[ $page =~ ^ure_[a-z_]+$ || $page == filemanagerlist || $page == settings || $page == advanced ]]
[[ $version =~ ^[a-zA-Z0-9._+-]+$ ]]
width=${UKE_GUI_VM_WIDTH:-2560}
height=${UKE_GUI_VM_HEIGHT:-1600}
scale=${UKE_GUI_VM_SCALE:-75}
seconds=${UKE_GUI_VM_SECONDS:-240}
[[ $width =~ ^[1-9][0-9]{2,3}$ && $height =~ ^[1-9][0-9]{2,3}$ && $scale =~ ^[0-9]{2,3}$ && $seconds =~ ^[0-9]{2,3}$ ]]
((width>=1080 && width<=3840 && height>=1080 && height<=3840 && scale>=50 && scale<=100 && seconds>=90 && seconds<=600))
qemu=${UKE_QEMU_SYSTEM_AARCH64:-$(type -P qemu-system-aarch64 || true)}
[[ -f $kernel && ! -L $kernel && -d $modules/lib/modules/$version && -x $qemu ]]
payload="$component/src/upstream/orangefox-android16/out-public/target/product/uke/recovery/root"
adapters="$component/build/gui-vm"
for helper in recovery-vm uke-vm-properties property_info; do [[ -s $adapters/$helper ]]; done
rg -q -- "<page name=\"$page\">" "$payload/sbin/maintainer.xml" "$payload/twres/pages/"*.xml
mkdir -p "$component/build/gui-vm" "$component/reports/private"
job=$(mktemp -d "$component/build/gui-vm/job-XXXXXX")
root="$job/root"
mkdir "$root"
cp -a "$payload/." "$root/"
install -m755 "$adapters/recovery-vm" "$root/system/bin/recovery-vm"
install -m755 "$adapters/uke-vm-properties" "$root/system/bin/uke-vm-properties"
install -m644 "$adapters/property_info" "$root/ure-vm-property-info"
mkdir -p "$root/run" "$root/tmp" "$root/proc" "$root/sys" "$root/dev" "$root/data" "$root/cache"
: > "$root/ure-vm-module-order"
while read -r command path rest; do
    [[ $command == insmod && -f $path && ! -L $path ]]
    relative=${path#"$modules/lib/modules/$version/"}
    [[ $relative != "$path" && $relative != *..* ]]
    install -Dm644 "$path" "$root/ure-vm-modules/$relative"
    printf '/ure-vm-modules/%s\n' "$relative" >> "$root/ure-vm-module-order"
done < <(modprobe --show-depends --set-version "$version" --dirname "$modules" virtio_gpu;
         modprobe --show-depends --set-version "$version" --dirname "$modules" virtio_input)
cat > "$root/system/etc/twrp.fstab" <<'FSTAB'
/data ext4 /dev/vda flags=display=Data;storage;settingsstorage;backup=1
/cache ext4 /dev/vdb flags=display=Cache;backup=1
FSTAB
# The test overlay chooses a page and disables screen timeout. Product XML is
# preserved, apart from these documented changes inside the disposable copy.
sed -i "s|<action function=\"page\">filemanagerlist</action>|<action function=\"page\">$page</action>|" "$root/twres/pages/main.xml"
sed -i "s|<page name=\"$page\">|&<action><action function=\"set\">tw_screen_timeout_secs=0</action></action>|" "$root/sbin/maintainer.xml" "$root/twres/pages/"*.xml
# A private RAM-only settings fixture drives the actual native settings loader.
# CPIO ownership is normalized below; no Android or host settings are mounted.
install -d -m700 "$root/mnt/uke-settings"
jq -n --argjson scale "$scale" '{schema:1,format:"ure-display-settings",scale_percent:$scale,uniform_density:true}' > "$root/mnt/uke-settings/display.json"
chmod 600 "$root/mnt/uke-settings/display.json"
printf '%s\n' "$((seconds/2))" > "$root/ure-vm-attempts"
cat > "$root/ure-gui-vm-init" <<'GUEST'
#!/system/bin/sh
export PATH=/system/bin
export ANDROID_ROOT=/system ANDROID_DATA=/data ANDROID_STORAGE=/storage EXTERNAL_STORAGE=/sdcard
export LD_LIBRARY_PATH=/system/lib64:/vendor/lib64:/system/lib64/bootstrap
/system/bin/toybox mount -t proc proc /proc || exit 101
mount -t sysfs sysfs /sys || exit 102
mount -t devtmpfs devtmpfs /dev || exit 103
mount -t tmpfs tmpfs /run || exit 104
mount -t tmpfs tmpfs /tmp || exit 105
while read -r module; do insmod "$module" || exit 106; done < /ure-vm-module-order
mkdir -p /dev/graphics
ln -s /dev/fb0 /dev/graphics/fb0
mount -t ext4 /dev/vda /data || exit 107
mount -t ext4 /dev/vdb /cache || exit 108
mkdir -p /dev/__properties__
cp /ure-vm-property-info /dev/__properties__/property_info
chmod 444 /dev/__properties__/property_info
/system/bin/uke-vm-properties || exit 109
/system/bin/recovery-vm &
recovery_pid=$!
echo "URE_GUI_PROCESS $recovery_pid"
result=0
for attempt in $(seq 1 "$(cat /ure-vm-attempts)"); do
    if ! kill -0 "$recovery_pid" 2>/dev/null; then result=1; break; fi
    if [ "$attempt" = 10 ] || [ "$attempt" = 30 ] || [ "$attempt" = 60 ]; then
        echo URE_GUI_LOG_BEGIN; cat /tmp/recovery.log; echo URE_GUI_LOG_END
    fi
    sleep 2
done
if kill -0 "$recovery_pid" 2>/dev/null; then kill -TERM "$recovery_pid"; wait "$recovery_pid"; fi
echo URE_GUI_FINAL_LOG_BEGIN
cat /tmp/recovery.log
echo URE_GUI_FINAL_LOG_END
echo "URE_GUI_EXIT $result"
sync
umount /data
umount /cache
echo o > /proc/sysrq-trigger
toybox reboot -p -f
while :; do sleep 1; done
GUEST
chmod 755 "$root/ure-gui-vm-init"
for disk in data cache; do
    truncate -s 128M "$job/$disk.img"
    [[ -f $job/$disk.img && ! -L $job/$disk.img ]]
    mke2fs -q -t ext4 -F -O '^orphan_file,^metadata_csum_seed' "$job/$disk.img"
done
(cd "$root" && find . -print0 | LC_ALL=C sort -z | cpio --null -o --format=newc --owner=0:0 --quiet | gzip -n -1) > "$job/initrd.cpio.gz"
printf '%s\n' "$job" > "$adapters/latest-job"
printf 'GUI VM private job: %s\n' "$job"
timeout "$((seconds+120))" "$qemu" -machine virt -cpu cortex-a72 -m 2048 -accel tcg \
    -display none -monitor none -serial stdio -no-reboot -nic none -no-user-config \
    -global virtio-mmio.force-legacy=false -kernel "$kernel" -initrd "$job/initrd.cpio.gz" \
    -append 'console=ttyAMA0 rdinit=/ure-gui-vm-init panic=1 ure_fixture=1' \
    -drive "if=none,id=data,format=raw,file=$job/data.img" -device virtio-blk-device,drive=data \
    -drive "if=none,id=cache,format=raw,file=$job/cache.img" -device virtio-blk-device,drive=cache \
    -device "virtio-gpu-device,xres=$width,yres=$height" -device virtio-keyboard-device -device virtio-mouse-device \
    -qmp "unix:$job/qmp.sock,server=on,wait=off" > "$job/console.log" 2>&1
rg -q '^URE_GUI_EXIT 0\r?$' "$job/console.log"
rg -q "Set page: '$page'" "$job/console.log"
if rg -q 'Scudo ERROR|Fatal signal|Kernel panic' "$job/console.log"; then exit 1; fi
jq -n --arg kernel "$(sha256sum "$kernel" | cut -d' ' -f1)" \
    --arg runner "$(sha256sum "${BASH_SOURCE[0]}" | cut -d' ' -f1)" \
    --arg shipping "$(sha256sum "$payload/system/bin/recovery" | cut -d' ' -f1)" \
    --arg adapted "$(sha256sum "$root/system/bin/recovery-vm" | cut -d' ' -f1)" \
    --arg log "$(sha256sum "$job/console.log" | cut -d' ' -f1)" --arg page "$page" \
    --arg properties "$(sha256sum "$root/system/bin/uke-vm-properties" | cut -d' ' -f1)" \
    --arg trie "$(sha256sum "$root/ure-vm-property-info" | cut -d' ' -f1)" \
    --arg theme "$(sha256sum "$root/sbin/maintainer.xml" | cut -d' ' -f1)" \
    --argjson width "$width" --argjson height "$height" --argjson scale "$scale" \
    --arg settings "$(sha256sum "$root/mnt/uke-settings/display.json" | cut -d' ' -f1)" \
    '{schema_version:1,validation_kind:"qemu-system-adapted-orangefox-gui",process_smoke_passed:true,page:$page,
      framebuffer:{width:$width,height:$height},initial_scale_percent:$scale,settings_fixture_sha256:$settings,
      kernel_sha256:$kernel,runner_sha256:$runner,shipping_recovery_sha256:$shipping,adapted_recovery_sha256:$adapted,
      property_helper_sha256:$properties,property_trie_sha256:$trie,overlay_theme_sha256:$theme,console_sha256:$log,
      adapters:{memfd_code_cache:true,synthetic_property_area:true,disposable_fstab:true,page_redirect:true},
      nic:false,host_block_attachment:false,physical_device:false,shipping_kernel_test:false,
      unmodified_shipping_gui_test:false,visual_review:false,complete_feature_acceptance:false}' \
    > "$job/verification.json"
echo 'Adapted graphical VM smoke completed; inspect screenshots separately before claiming visual acceptance.'
