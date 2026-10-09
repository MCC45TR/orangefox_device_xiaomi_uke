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
  }
  section { if ($0!=clock[section]) { failed=1; exit 1 }; if (++section==8) section=0; next }
  /^service uke-clock-sync \/system\/bin\/uke-clock-sync$/ { if (++services!=1) { failed=1; exit 1 }; section=1; next }
  /^    start uke-clock-sync$/ { if (++starts!=1) { failed=1; exit 1 }; next }
  /^[[:space:]]*#/ || /^[[:space:]]*$/ || /^on / { next }
  $1=="setprop" && (($2=="sys.usb.configfs" && $3=="1") || ($2=="sys.usb.controller" && $3=="a600000.dwc3")) { next }
  $1=="wait" && ($2=="/sys/bus/platform/devices/a600000.ssusb/mode" || $2=="/sys/class/udc/a600000.dwc3") && $3=="2" { next }
  $1=="write" && $2=="/sys/bus/platform/devices/a600000.ssusb/mode" && $3=="peripheral" { next }
  $1=="mkdir" && $2=="/dev/block/bootdevice" && $3=="0755" && $4=="root" && $5=="root" { next }
  $1=="symlink" && $2=="/dev/block/by-name" && $3=="/dev/block/bootdevice/by-name" { next }
  { failed=1; exit 1 }
  END { if (failed || section || services!=1 || starts!=1) exit 1 }
' "$1"; }
startup_policy "$script"
work=$(mktemp -d "$component/build/startup-policy-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
for mutation in extra-capability shell-exec duplicate-service; do
    case $mutation in
        extra-capability) sed 's/SYS_ADMIN SYS_TIME DAC_READ_SEARCH/SYS_ADMIN SYS_TIME DAC_READ_SEARCH SYS_RAWIO/' "$script" > "$work/$mutation" ;;
        shell-exec) sed 's@/system/bin/uke-clock-sync@/system/bin/sh -c reboot@' "$script" > "$work/$mutation" ;;
        duplicate-service) cat "$script" "$script" > "$work/$mutation" ;;
    esac
    if startup_policy "$work/$mutation"; then echo "Unexpected startup policy admission: $mutation" >&2; exit 1; fi
done
rg -q '^type sysfs_uke_usb_role, fs_type, sysfs_type;$' "$device/sepolicy/uke_usb_role.te"
rg -q '^genfscon sysfs /devices/platform/soc/a600000.ssusb/mode u:object_r:sysfs_uke_usb_role:s0$' \
    "$device/sepolicy/genfs_contexts"
echo 'USB/alias and isolated UTC service init passes the actual host parser and bounded startup policy.'
