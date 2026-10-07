#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Compile the pinned reader and utility; never open a host/tablet block device.
set -euo pipefail
export LC_ALL=C LANG=C
umask 077
ulimit -c 0
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" native bash "${BASH_SOURCE[0]}" "$@"
fi
[[ $# == 0 || $# == 1 ]]
cd "$component"
tree="$component/src/upstream/orangefox-android16"
core="$tree/system/core"
library="$core/fs_mgr/liblp"
compiler="$tree/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
libs="$tree/out-public/host/linux-x86/lib64"
[[ $(git -C "$core" rev-parse HEAD) == 1efa79514b2f520c20a837c9216ff6b6e7e0dda3 ]]
result=$(mktemp -d "$component/build/lp-record-reader-XXXXXXXX")
mkdir -m700 "$result/reconstructed" "$result/reconstructed/fs_mgr" "$result/reconstructed/fs_mgr/liblp" "$result/controls" "$result/mutant-controls"
phase=admission
finish() {
    local status=$?
    trap - EXIT
    jq -n --arg phase "$phase" --argjson status "$status" \
        '{schema_version:1,phase:$phase,exit_status:$status,complete:($status==0 and $phase=="complete"),host_only:true,tablet_writes:false}' > "$result/outcome.json"
    printf 'LP reader controls retained privately: %s\n' "$result"
    exit "$status"
}
trap finish EXIT
patch_file="$component/patches/0044-read-extra-logical-metadata-records.patch"
inputs=("${BASH_SOURCE[0]}" "$component/tests/ure/lp-record-reader.cpp" "$patch_file" "$compiler" "$libs/libbase.so" "$libs/liblog.so" "$libs/libc++.so"
    /usr/lib64/libcrypto.so.3 /usr/include/openssl/sha.h
    "$tree/system/extras/ext4_utils/include/ext4_utils/ext4_utils.h"
    "$library/reader.cpp" "$library/reader.h" "$library/utility.cpp" "$library/utility.h"
    "$library/include/liblp/liblp.h" "$library/include/liblp/partition_opener.h" "$library/include/liblp/metadata_format.h")
sha256sum "${inputs[@]}" > "$result/inputs.before.sha256"
git -C "$core" show HEAD:fs_mgr/liblp/reader.cpp > "$result/reader.original.cpp"
cp "$result/reader.original.cpp" "$result/reconstructed/fs_mgr/liblp/reader.cpp"
patch --batch --fuzz=0 -p1 -d "$result/reconstructed" < "$patch_file"
active_integrated=false
if cmp -s "$library/reader.cpp" "$result/reconstructed/fs_mgr/liblp/reader.cpp"; then
    active_integrated=true
else
    cmp "$library/reader.cpp" "$result/reader.original.cpp"
fi
phase=compile
compile() {
    timeout 60s "$compiler" -std=c++17 -stdlib=libc++ -O1 -ffunction-sections -fdata-sections -pthread \
        -Wno-deprecated-declarations -I"$tree/system/libbase/include" -I"$tree/external/fmtlib/include" \
        -I"$tree/system/extras/ext4_utils/include" -I"$library/include" -I"$library" \
        "$1" "$library/utility.cpp" "$component/tests/ure/lp-record-reader.cpp" \
        -L"$libs" -Wl,-rpath,"$libs" -Wl,--gc-sections -lbase -llog -lcrypto -o "$2"
}
compile "$result/reconstructed/fs_mgr/liblp/reader.cpp" "$result/reader-controls"
compile "$result/reader.original.cpp" "$result/reader-removed-guard"
sandbox=(bwrap --unshare-all --cap-drop ALL --ro-bind /usr /usr --ro-bind /lib /lib --ro-bind /lib64 /lib64
    --ro-bind "$libs" /android-libs --proc /proc --remount-ro /proc --dev /dev --remount-ro /dev
    --clearenv --setenv LC_ALL C --setenv LD_LIBRARY_PATH /android-libs)
phase=controls
timeout 30s "${sandbox[@]}" --ro-bind "$result/reader-controls" /reader --bind "$result/controls" /cases --remount-ro / \
    /reader /cases > "$result/controls.stdout" 2> "$result/controls.stderr"
[[ $(rg -c '^PASS: ' "$result/controls.stdout") == 15 ]]
rg -q '^PASS: 14 liblp host controls;' "$result/controls.stdout"
phase=removed-guard-control
status=0
timeout 30s "${sandbox[@]}" --ro-bind "$result/reader-removed-guard" /reader --bind "$result/mutant-controls" /cases --remount-ro / \
    /reader /cases > "$result/mutant.stdout" 2> "$result/mutant.stderr" || status=$?
[[ $status == 134 && $(rg -c '^PASS: ordinary-record-' "$result/mutant.stdout") == 2 ]]
rg -q 'slot_number == 0|Check failed' "$result/mutant.stderr"
copied_metadata=false
if [[ $# == 1 ]]; then
    metadata=$(realpath -e -- "$1")
    [[ $metadata == "$component/reports/private/"* || $metadata == "$component/build/"* ]]
    [[ -f $metadata && ! -L $metadata && $(stat -c %s "$metadata") == 1048576 ]]
    sha256sum "$metadata" > "$result/copied-metadata.before.sha256"
    mkdir -m700 "$result/copied-controls"
    phase=copied-metadata-controls
    timeout 30s "${sandbox[@]}" --ro-bind "$result/reader-controls" /reader --ro-bind "$metadata" /metadata \
        --bind "$result/copied-controls" /cases --remount-ro / /reader /cases /metadata \
        > "$result/copied.stdout" 2> "$result/copied.stderr"
    [[ $(rg -c '^PASS: copied metadata record ' "$result/copied.stdout") == 3 ]]
    sha256sum "$metadata" > "$result/copied-metadata.after.sha256"
    cmp "$result/copied-metadata.before.sha256" "$result/copied-metadata.after.sha256"
    copied_metadata=true
fi
phase=input-stability
sha256sum "${inputs[@]}" > "$result/inputs.after.sha256"
cmp "$result/inputs.before.sha256" "$result/inputs.after.sha256"
sha256sum "$result/reader-controls" "$result/reader-removed-guard" > "$result/executables.sha256"
jq -n --arg inputs "$(sha256sum "$result/inputs.before.sha256" | cut -d' ' -f1)" \
    --arg patch "$(sha256sum "$patch_file" | cut -d' ' -f1)" --argjson integrated "$active_integrated" --argjson copied "$copied_metadata" \
    '{schema_version:1,passed:true,evidence_class:"host-actual-liblp-reader-and-utility",synthetic_controls:14,
      removed_guard_abort_reproduced:true,active_source_integrated:$integrated,copied_metadata_tested:$copied,inputs_sha256:$inputs,patch_sha256:$patch,
      input_hashes_unchanged:true,host_device_opening:false,physical_helpers_unavailable:true,tablet_writes:false,
      host_fixture_files_created:true,target_metadata_writes:false,physical_boot_accepted:false,ota_mutation_accepted:false}' > "$result/verification.json"
phase=complete
