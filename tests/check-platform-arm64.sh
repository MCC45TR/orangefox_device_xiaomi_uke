#!/usr/bin/env bash
# Compile current sources against pinned Android/Bionic headers without
# modifying the Android output tree or claiming a linked recovery candidate.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
[[ $# == 0 ]]
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" arm64 bash "$component/tests/check-platform-arm64.sh"
fi
cd "$component"
tree="$component/src/upstream/orangefox-android16"
compiler="$tree/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
[[ $(sha256sum "$compiler" | cut -d' ' -f1) == 55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f ]]
readelf="$tree/prebuilts/clang/host/linux-x86/clang-r547379/bin/llvm-readelf"
record=$(mktemp -d "$component/build/platform-arm64-evidence-XXXXXXXX")
bash scripts/native-inputs.sh > "$record/inputs-before.sha256"
flags=(-target aarch64-linux-android10000 -nostdlibinc -std=c++20 -fexceptions -fno-rtti -fPIC
    -D__ANDROID_RECOVERY__ -D__ANDROID_RAMDISK__ -DANDROID_STRICT
    -D__LIBC_API__=10000 -D__LIBM_API__=10000 -D__LIBDL_API__=10000
    -Wall -Wextra -Werror
    "-ffile-prefix-map=$component=/workspace/recovery" "-fdebug-prefix-map=$component=/workspace/recovery"
    "-I$component/src/device/xiaomi/uke/recoveryctl/libuke"
    "-I$tree/external/jsoncpp/include"
    "-I$tree/prebuilts/clang/host/linux-x86/clang-r547379/android_libc++/platform/aarch64/include/c++/v1"
    "-I$tree/prebuilts/clang/host/linux-x86/clang-r547379/include/c++/v1"
    -isystem "$tree/bionic/libc/include"
    -isystem "$tree/bionic/libc/kernel/uapi/asm-arm64"
    -isystem "$tree/bionic/libc/kernel/uapi"
    -isystem "$tree/bionic/libc/kernel/android/scsi"
    -isystem "$tree/bionic/libc/kernel/android/uapi")
printf '%s\n' "${flags[@]}" > "$record/flags.txt"
units=(platform management preflight diagnostics)
# Discover and hash the exact include set before compilation. No generated
# command text is evaluated, and unexpected depfile tokens refuse admission.
dep_headers() {
    awk '{for(i=1;i<=NF;i++)if($i!="\\" && $i!~/:$/)print $i}' "$@" | LC_ALL=C sort -u
}
hash_headers() {
    while IFS= read -r header; do
        [[ $header =~ ^/[A-Za-z0-9_./+-]+$ && -f $header && ! -L $header ]]
        sha256sum -- "$header"
    done < "$record/headers.list"
}
for unit in "${units[@]}"; do
    "$compiler" "${flags[@]}" -M -MF "$record/$unit.pre.d" -MT "$record/$unit.o" \
        "$component/src/device/xiaomi/uke/recoveryctl/libuke/$unit.cpp" > "$record/$unit.pre.log" 2>&1
done
dep_headers "$record"/*.pre.d > "$record/headers.list"
hash_headers > "$record/headers-before.sha256"
for unit in platform management preflight diagnostics; do
    "$compiler" "${flags[@]}" -MD -MF "$record/$unit.d" -c \
        "$component/src/device/xiaomi/uke/recoveryctl/libuke/$unit.cpp" -o "$record/$unit.o" \
        > "$record/$unit.log" 2>&1
    "$readelf" -h "$record/$unit.o" > "$record/$unit.elf.txt"
    rg -q 'Machine:.*AArch64' "$record/$unit.elf.txt"
done
dep_headers "$record"/{platform,management,preflight,diagnostics}.d > "$record/headers-compiled.list"
cmp "$record/headers.list" "$record/headers-compiled.list"
hash_headers > "$record/headers-after.sha256"
cmp "$record/headers-before.sha256" "$record/headers-after.sha256"
# Recompiling immediately after the first pass detects substitutions while
# preserving a separately hashed complete header input set.
for unit in platform management preflight diagnostics; do
    "$compiler" "${flags[@]}" -c "$component/src/device/xiaomi/uke/recoveryctl/libuke/$unit.cpp" \
        -o "$record/$unit.repeat.o" > "$record/$unit.repeat.log" 2>&1
    cmp "$record/$unit.o" "$record/$unit.repeat.o"
done
hash_headers > "$record/headers-repeat.sha256"
cmp "$record/headers-after.sha256" "$record/headers-repeat.sha256"
bash scripts/native-inputs.sh > "$record/inputs-after.sha256"
cmp "$record/inputs-before.sha256" "$record/inputs-after.sha256"
sha256sum "$record"/{platform,management,preflight,diagnostics}.o > "$record/objects.sha256"
jq -n --arg inputs "$(sha256sum "$record/inputs-before.sha256" | cut -d' ' -f1)" \
    --arg compiler "$(sha256sum "$compiler" | cut -d' ' -f1)" \
    --arg headers "$(sha256sum "$record/headers-after.sha256" | cut -d' ' -f1)" \
    --arg flags "$(sha256sum "$record/flags.txt" | cut -d' ' -f1)" \
    '{schema_version:1,evidence_scope:"focused-current-android-arm64-compilation",target:"aarch64-linux-android10000",
      source_inputs_sha256:$inputs,compiler_sha256:$compiler,header_inputs_sha256:$headers,compile_flags_sha256:$flags,
      translation_units:["platform","management","preflight","diagnostics"],
      validation:{current_bionic_platform_compilation:true,repeat_objects_identical:true,
        source_and_header_repeat_unchanged:true,linked_recovery:false,complete_android_build:false,
        extracted_image:false,combined_vm:false,physical_device:false,live_writer:false}}' > "$record/verification.json"
printf 'Current Android/Bionic ARM64 compilation passed for four units; linked image and device acceptance remain separate. Evidence: %s\n' "$record"
