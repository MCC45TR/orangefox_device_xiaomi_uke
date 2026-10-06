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
# Accept only RAM aliases and exact peripheral/configfs controls. A donated
# mount_all, exec, firmware mount or block writer must fail this boundary.
awk '
  /^[[:space:]]*#/ || /^[[:space:]]*$/ || /^on / { next }
  $1=="setprop" && (($2=="sys.usb.configfs" && $3=="1") || ($2=="sys.usb.controller" && $3=="a600000.dwc3")) { next }
  $1=="wait" && ($2=="/sys/bus/platform/devices/a600000.ssusb/mode" || $2=="/sys/class/udc/a600000.dwc3") && $3=="2" { next }
  $1=="write" && $2=="/sys/bus/platform/devices/a600000.ssusb/mode" && $3=="peripheral" { next }
  $1=="mkdir" && $2=="/dev/block/bootdevice" && $3=="0755" && $4=="root" && $5=="root" { next }
  $1=="symlink" && $2=="/dev/block/by-name" && $3=="/dev/block/bootdevice/by-name" { next }
  { exit 1 }
' "$script"
rg -q '^type sysfs_uke_usb_role, fs_type, sysfs_type;$' "$device/sepolicy/uke_usb_role.te"
rg -q '^genfscon sysfs /devices/platform/soc/a600000.ssusb/mode u:object_r:sysfs_uke_usb_role:s0$' \
    "$device/sepolicy/genfs_contexts"
echo 'Minimal USB/alias init passes the actual host parser and pre-fs physical-write boundary.'
