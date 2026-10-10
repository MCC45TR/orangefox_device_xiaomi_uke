#!/usr/bin/env bash
# Compare the shipping input profile with an extracted, source-bound ramdisk.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source "$component/scripts/touch-payload-init.sh"
root=$(realpath -- "${1:?Pass the extracted recovery root}")
report=${2:?Pass a private JSON report path}
[[ -d $root && ! -L $root ]]
init_script=$(touch_payload_init "$root")
work=$(mktemp -d "$component/build/touch-payload-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
"${CXX:-g++}" -std=c++20 -Wall -Wextra -Werror \
    -I "$component/src/device/xiaomi/uke/touch" \
    "$component/tests/touch-session/image-pins.cpp" -o "$work/pins"
"$work/pins" > "$work/pins.tsv"
count=0
while IFS=$'\t' read -r path digest bytes; do
    [[ $path == /* && $digest =~ ^[a-f0-9]{64}$ && $bytes =~ ^[0-9]+$ ]]
    candidate="$root$path"
    if [[ $path == /linkerconfig/ld.config.txt ]]; then
        # Init copies this reviewed config into the RAM-only runtime location.
        # The packaged placeholder is intentionally empty; this is not a claim
        # that init performed the copy in a cold-boot hardware test.
        [[ -f $candidate && ! -L $candidate && $(stat -c %s "$candidate") == 0 ]]
        candidate="$root/system/etc/ld.config.txt"
    fi
    [[ -f $candidate && ! -L $candidate && $(stat -c %s "$candidate") == "$bytes" ]]
    [[ $(sha256sum "$candidate" | cut -d ' ' -f 1) == "$digest" ]] || {
        printf 'Touch runtime provider changed: %s\n' "$path" >&2; exit 1;
    }
    ((count+=1))
done < "$work/pins.tsv"
[[ $count == 18 ]]
helper="$root/system/bin/uke-touch-supervisor"
[[ -f $helper && ! -L $helper && -x $helper ]]
readelf -h "$helper" | rg 'Machine:.*AArch64' >/dev/null
strings "$helper" | rg -F '/odm/bin/hw/vendor.xiaomi.hw.touchfeature-service' >/dev/null
strings "$helper" | rg -F '6.1.175-android14-11-ga3b9c44908dd-ab13320413' >/dev/null
[[ ! -e $root/odm/bin/hw/vendor.xiaomi.hw.touchfeature-service ]]
jq -cn --arg helper "$(sha256sum "$helper" | cut -d ' ' -f 1)" \
    --arg init_script "$(sha256sum "$init_script" | cut -d ' ' -f 1)" \
    --arg profile "$(sha256sum "$work/pins.tsv" | cut -d ' ' -f 1)" \
    '{schema_version:1,evidence_class:"package",supervisor_sha256:$helper,
      image_profile_sha256:$profile,image_provider_count:18,image_providers_match:true,
      runtime_config_copy_reviewed:true,runtime_init_script:"/system/etc/init/hw/init.rc",
      runtime_init_script_sha256:$init_script,proprietary_service_packaged:false,
      physical_cold_start_accepted:false,storage_acceptance:false}' > "$report"
printf 'Packed touch supervisor and 18 runtime provider pins match; cold boot remains separate.\n'
