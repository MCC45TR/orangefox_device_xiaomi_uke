#!/usr/bin/env bash
# Verify reviewed module factoring without activating crypto or Android services.
set -euo pipefail
export LC_ALL=C LANG=C
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
tree="$component/src/upstream/orangefox-android16"
work=$(mktemp -d "$component/build/vold-module-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
git -C "$tree/system/vold" show 953de9608eb78380b3c4e39e801c2bc0af7dbddc:Android.bp > "$work/original.bp"
cp "$work/original.bp" "$work/Android.bp"
git -C "$work" apply "$component/patches/0079-compile-vold-guards-for-the-recovery-caller.patch"
bash "$component/scripts/prepare-reviewed-patches.sh" "$tree/system/vold" check vold
bash "$component/scripts/prepare-reviewed-patches.sh" "$tree/bootable/recovery" check recovery
cmp "$work/Android.bp" "$tree/system/vold/Android.bp"
"$tree/prebuilts/build-tools/linux-x86/bin/bpfmt" -o "$work/Android.bp" > "$work/parsed.bp"
module() {
    awk -v target="$2" '
        /^(cc_defaults|cc_library_static) \{$/ {inside=1;record=$0 ORS;selected=0;next}
        inside {record=record $0 ORS;if(index($0,"name: \"" target "\","))selected=1}
        inside && /^}$/ {if(selected){printf "%s",record;found++};inside=0}
        END {if(found!=1)exit 1}' "$1"
}
module "$work/original.bp" libvold | sed '1,2d' > "$work/original-properties"
module "$work/Android.bp" libvold_common_sources | sed '1,2d' > "$work/shared-properties"
cmp "$work/original-properties" "$work/shared-properties"
module "$work/Android.bp" libvold > "$work/normal-module"
module "$work/Android.bp" libvold_uke_recovery > "$work/recovery-module"
policy() {
    rg -qx '    defaults: \["libvold_common_sources"\],' "$1" &&
    rg -qx '    cflags: \["-D__ANDROID_RECOVERY__"\],' "$1" &&
    ! rg -q 'recovery:' "$1"
}
policy "$work/recovery-module"
! rg -q '__ANDROID_RECOVERY__|recovery:' "$work/normal-module"
rg -qx 'LOCAL_STATIC_LIBRARIES \+= libfoxui libvold_uke_recovery libuke-recovery liblzma' "$tree/bootable/recovery/Android.mk"
! rg -q '(^|[[:space:]])libvold([[:space:]]|$)' "$tree/bootable/recovery/Android.mk"
sed '/cflags: \["-D__ANDROID_RECOVERY__"\],/d' "$work/recovery-module" > "$work/no-macro"
! policy "$work/no-macro"
printf '%s\n' 'Recovery vold module parsing, unchanged platform properties, caller and missing-macro control passed. Actual generated output requires the separate post-build gate.'
