#!/usr/bin/env bash
# Validate the minimal profile startup; host parser only, no init actions run.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
tree="$component/src/upstream/orangefox-android16"
device="$component/src/device/xiaomi/uke"
script="$device/recovery/root/init.recovery.qcom.rc"
verifier="$tree/out-public/host/linux-x86/bin/host_init_verifier"
[[ -x $verifier ]]
"$verifier" "$script"
# Imports precede the generic on-init action; this stage must not race its reset.
rg -q '^on early-fs && property:ro.boot.usbcontroller=a600000.dwc3$' "$script"
rg -q '^    setprop sys.usb.configfs 0$' "$tree/bootable/recovery/etc/init.rc"
rg -q '^import /init.recovery.\$\{ro.hardware\}.rc$' "$tree/bootable/recovery/etc/init.rc"
rg -q '^TW_EXCLUDE_DEFAULT_USB_INIT := true$' "$device/BoardConfig.mk"
rg -q '^BOARD_VENDOR_SEPOLICY_DIRS \+= \$\(DEVICE_PATH\)/sepolicy$' "$device/BoardConfig.mk"
# Accept RAM aliases, exact USB controls and one bounded native clock service.
# No shell command, inherited service or storage writer is admitted here.
startup_policy() { awk '
  BEGIN {
    clock[1]="    class core"; clock[2]="    user root"; clock[3]="    group root";
    clock[4]="    capabilities SYS_ADMIN SYS_TIME DAC_READ_SEARCH";
    clock[5]="    seclabel u:r:recovery:s0"; clock[6]="    disabled"; clock[7]="    oneshot";
    boot[1]="    wait /dev/block/platform/soc/1d84000.ufshc/by-name 2";
    boot[2]="    symlink /dev/block/platform/soc/1d84000.ufshc /dev/block/bootdevice";
  }
  boot_section { if ($0!=boot[boot_section]) { failed=1; exit 1 }; if (++boot_section==3) boot_section=0; next }
  $0=="on early-fs && property:ro.boot.bootdevice=1d84000.ufshc && property:ro.boot.boot_devices=soc/1d84000.ufshc" {
    if (++boot_actions!=1) { failed=1; exit 1 }; boot_section=1; next
  }
  section { if ($0!=clock[section]) { failed=1; exit 1 }; if (++section==8) section=0; next }
  /^service uke-clock-sync \/system\/bin\/uke-clock-sync$/ { if (++services!=1) { failed=1; exit 1 }; section=1; next }
  /^    start uke-clock-sync$/ { if (++starts!=1) { failed=1; exit 1 }; next }
  /^[[:space:]]*#/ || /^[[:space:]]*$/ || /^on / { next }
  $1=="setprop" && (($2=="sys.usb.configfs" && $3=="1") || ($2=="sys.usb.controller" && $3=="a600000.dwc3")) { next }
  $1=="wait" && ($2=="/sys/bus/platform/devices/a600000.ssusb/mode" || $2=="/sys/class/udc/a600000.dwc3") && $3=="2" { next }
  $1=="write" && $2=="/sys/bus/platform/devices/a600000.ssusb/mode" && $3=="peripheral" { next }
  { failed=1; exit 1 }
  END { if (failed || section || boot_section || boot_actions!=1 || services!=1 || starts!=1) exit 1 }
' "$1"; }
startup_policy "$script"
work=$(mktemp -d "$component/build/startup-policy-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
for mutation in extra-capability shell-exec duplicate-service wrong-controller unbounded-wait directory-alias; do
    case $mutation in
        extra-capability) sed 's/SYS_ADMIN SYS_TIME DAC_READ_SEARCH/SYS_ADMIN SYS_TIME DAC_READ_SEARCH SYS_RAWIO/' "$script" > "$work/$mutation" ;;
        shell-exec) sed 's@/system/bin/uke-clock-sync@/system/bin/sh -c reboot@' "$script" > "$work/$mutation" ;;
        duplicate-service) cat "$script" "$script" > "$work/$mutation" ;;
        wrong-controller) sed 's/property:ro.boot.bootdevice=1d84000.ufshc/property:ro.boot.bootdevice=other.ufshc/' "$script" > "$work/$mutation" ;;
        unbounded-wait) sed 's@/dev/block/platform/soc/1d84000.ufshc/by-name 2@/dev/block/platform/soc/1d84000.ufshc/by-name@' "$script" > "$work/$mutation" ;;
        directory-alias) sed 's@    symlink /dev/block/platform/soc/1d84000.ufshc /dev/block/bootdevice@    mkdir /dev/block/bootdevice 0755 root root@' "$script" > "$work/$mutation" ;;
    esac
    if startup_policy "$work/$mutation"; then echo "Unexpected startup policy admission: $mutation" >&2; exit 1; fi
done
# Model init symlink() with host files; every fstab alias must retain identity.
controller="$work/dev/block/platform/soc/1d84000.ufshc"
alias="$work/dev/block/bootdevice"
mkdir -p "$controller/by-name"
ln -s "$controller/by-name" "$work/dev/block/by-name"
ln -sT "$controller" "$alias"
[[ $(readlink "$alias") == "$controller" ]]
for partition in userdata metadata persist misc boot_a boot_b vendor_boot_a vendor_boot_b dtbo_a dtbo_b vbmeta_a vbmeta_b; do
    touch "$controller/by-name/$partition"
    [[ $(stat -Lc '%d:%i' "$work/dev/block/by-name/$partition") == $(stat -Lc '%d:%i' "$alias/by-name/$partition") ]]
done
unlink "$alias"
mkdir "$alias"
touch "$alias/preserve"
if ln -sT "$controller" "$alias" 2>/dev/null; then echo 'Existing boot-device directory replaced' >&2; exit 1; fi
[[ ! -L $alias && -f $alias/preserve ]]
rg -q '^type sysfs_uke_usb_role, fs_type, sysfs_type;$' "$device/sepolicy/uke_usb_role.te"
rg -q '^genfscon sysfs /devices/platform/soc/a600000.ssusb/mode u:object_r:sysfs_uke_usb_role:s0$' \
    "$device/sepolicy/genfs_contexts"
echo 'USB/alias and isolated UTC service init passes the actual host parser and bounded startup policy.'
