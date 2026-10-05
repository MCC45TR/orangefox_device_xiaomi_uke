#!/usr/bin/env bash
# Host-only adapters for a generic mainline VM. Never install these in a release.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
tree="$component/src/upstream/orangefox-android16"
bash "$component/scripts/build-evidence.sh" verify
destination="$component/build/gui-vm"
mkdir -p "$destination"
clang="$tree/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
crt="$tree/out-public/soong/.intermediates/bionic/libc"
libc="$tree/out-public/target/product/uke/recovery/root/system/lib64/libc.so"
for helper in gui-ashmem gui-properties lp-dump; do
    "$clang" --target=aarch64-linux-android10000 -O2 -fPIC -fno-exceptions -fno-rtti \
        -Wall -Wextra -Werror -c "$component/tests/vm/$helper.cpp" -o "$destination/$helper.o"
done
"$clang" --target=aarch64-linux-android10000 -pie -nostdlib -Wl,--no-undefined \
    -Wl,-dynamic-linker,/system/bin/linker64 \
    "$crt/crtbegin_dynamic/android_arm64_armv8-a/crtbegin_dynamic.o" "$destination/gui-properties.o" \
    "$libc" "$crt/crtend_android/android_arm64_armv8-a/crtend_android.o" -o "$destination/uke-vm-properties"
"$clang" --target=aarch64-linux-android10000 -pie -nostdlib -Wl,--no-undefined -Wl,--allow-shlib-undefined \
    -Wl,-dynamic-linker,/system/bin/linker64 \
    "$crt/crtbegin_dynamic/android_arm64_armv8-a/crtbegin_dynamic.o" "$destination/lp-dump.o" \
    "${libc%/libc.so}/liblpdump.so" "$libc" \
    "$crt/crtend_android/android_arm64_armv8-a/crtend_android.o" -o "$destination/uke-vm-lpdump"
properties="$tree/system/core/property_service"
"${CXX:-c++}" -std=c++20 -O2 -include algorithm -include cstring \
    -I"$properties/libpropertyinfoserializer/include" -I"$properties/libpropertyinfoparser/include" \
    -I"$tree/system/libbase/include" \
    "$component/tests/vm/gui-property-info.cpp" \
    "$properties/libpropertyinfoserializer/property_info_serializer.cpp" \
    "$properties/libpropertyinfoserializer/trie_builder.cpp" \
    "$properties/libpropertyinfoserializer/trie_serializer.cpp" \
    "$properties/libpropertyinfoparser/property_info_parser.cpp" \
    "$tree/system/libbase/strings.cpp" "$tree/system/libbase/stringprintf.cpp" \
    -o "$destination/uke-vm-property-info"
"$destination/uke-vm-property-info" "$destination/property_info"
# Derive the link recipe from the current build, not a stale logged invocation.
ninja="$tree/out-public/build-twrp_uke.ninja"
rule=$(awk '/^build .*EXECUTABLES\/recovery_intermediates\/LINKED\/recovery: /{print $3;exit}' "$ninja")
[[ $rule =~ ^rule[0-9]+$ ]]
awk -v rule="$rule" '$0=="rule "rule {found=1;next} found && /^ command = /{sub(/^ command = /, "");print;exit}' "$ninja" \
    | sed 's| -o /mnt/out-public/target/product/uke/obj/EXECUTABLES/recovery_intermediates/LINKED/recovery | /tmp/ure-vm/gui-ashmem.o -Wl,--defsym=ashmem_create_region=__wrap_ashmem_create_region -o /tmp/ure-vm/recovery-vm |' \
    > "$destination/relink-current.sh"
rg -q -- '--defsym=ashmem_create_region=__wrap_ashmem_create_region' "$destination/relink-current.sh"
bwrap --ro-bind / / --tmpfs /tmp --ro-bind "$tree" /mnt \
    --bind "$destination" /tmp/ure-vm --chdir /mnt bash /tmp/ure-vm/relink-current.sh \
    > "$destination/relink.log" 2>&1
"${clang%/clang++}/llvm-strip" --strip-debug "$destination/recovery-vm"
"${clang%/clang++}/llvm-nm" -D "$destination/recovery-vm" | rg -q ' T ashmem_create_region$'
sha256sum "$destination/recovery-vm" "$destination/uke-vm-properties" "$destination/property_info" \
    > "$destination/adapters.sha256"
echo 'VM-only recovery relink and property fixtures prepared; shipping recovery is unchanged.'
