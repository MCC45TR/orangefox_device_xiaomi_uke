#!/usr/bin/env bash
# Post-build evidence gate: inspect generated compiler flags, archive edge and real object.
set -euo pipefail
export LC_ALL=C LANG=C
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
tree="$component/src/upstream/orangefox-android16"
output=${1:-"$tree/out-public"}
work=$(mktemp -d "$component/build/vold-variant-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
mapfile -t ninja_files < <(find "$output/soong" -maxdepth 1 -type f -name 'build.twrp_uke.*.ninja' -print | sort)
((${#ninja_files[@]} > 0))
awk '/^m.libvold_uke_recovery_android_arm64_armv8-a_static.cFlags1 = /' "${ninja_files[@]}" > "$work/recovery-flags"
awk '/^m.libvold_android_arm64_armv8-a_static.cFlags1 = /' "${ninja_files[@]}" > "$work/normal-flags"
[[ $(wc -l < "$work/recovery-flags") == 1 && $(wc -l < "$work/normal-flags") == 1 ]]
rg -q -- '(^| )-D__ANDROID_RECOVERY__( |$)' "$work/recovery-flags"
! rg -q -- '-D__ANDROID_RECOVERY__' "$work/normal-flags"
for source in Keystore Decrypt; do
    awk -v suffix="libvold_uke_recovery/android_arm64_armv8-a_static/obj/system/vold/$source.o:" '
        index($0,suffix) {copy=1}
        copy {print}
        copy && /^$/ {copy=0}' "${ninja_files[@]}" > "$work/$source-edge"
    rg -qF 'cFlags = ${m.libvold_uke_recovery_android_arm64_armv8-a_static.cFlags1}' "$work/$source-edge"
done
awk '/^build .*EXECUTABLES\/recovery_intermediates\/LINKED\/recovery:/ {
    for(i=1;i<=NF;i++)if($i~/libvold.*\.a$/)print $i
}' "$output/build-twrp_uke.ninja" > "$work/recovery-link"
[[ $(wc -l < "$work/recovery-link") == 1 ]]
rg -q 'STATIC_LIBRARIES/libvold_uke_recovery_intermediates/libvold_uke_recovery.a$' "$work/recovery-link"
archive="$output/soong/.intermediates/system/vold/libvold_uke_recovery/android_arm64_armv8-a_static/libvold_uke_recovery.a"
object="${archive%/*}/obj/system/vold/Keystore.o"
[[ -s $archive && -s $object ]]
"$tree/prebuilts/clang/host/linux-x86/clang-r547379/bin/llvm-nm" -u "$object" > "$work/Keystore-undefined.txt"
rg -q ' AServiceManager_checkService$' "$work/Keystore-undefined.txt"
printf '%s\n' 'Generated recovery-library flags contain the macro, normal libvold flags do not; Keystore/Decrypt compile with those flags, recovery links only the private archive, and the real Keystore object references non-waiting service lookup. No Binder/TEE or decryption acceptance.'
