#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Host-only bounded compile and actual-loader fixture. No Android build, device,
# kernel module insertion or active-source mutation is performed.
set -euo pipefail
task_component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
task_candidate="$task_component/tests/ure/module-policy"
task_source="$task_component/src/upstream/orangefox-android16"
task_compiler="$task_source/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
task_libs="$task_source/out-public/host/linux-x86/lib64"
mkdir -p "$task_component/build/module-policy-tests" "$task_component/reports/private"
task_result=$(mktemp -d "$task_component/build/module-policy-tests/run-XXXXXXXX")
mkdir -p "$task_result/stock-fixture" "$task_result/synthetic-fixture"
[[ $(git -C "$task_source/system/core" rev-parse HEAD) == \
    1efa79514b2f520c20a837c9216ff6b6e7e0dda3 ]]
rg -q 'ClangDefaultVersion[[:space:]]*= "clang-r547379"' \
    "$task_source/build/soong/cc/config/global.go"

# Reconstruct the candidate and removed-guard mutant from the exact source pin.
# Neither the staged checkout nor any kernel state is modified.
mkdir -p "$task_result/reconstructed/libmodprobe"
git -C "$task_source/system/core" show HEAD:libmodprobe/libmodprobe.cpp > \
    "$task_result/unpatched-libmodprobe.cpp"
cp "$task_result/unpatched-libmodprobe.cpp" \
    "$task_result/reconstructed/libmodprobe/libmodprobe.cpp"
patch --batch --fuzz=0 -p1 -d "$task_result/reconstructed" < \
    "$task_component/patches/0040-recovery-block-automatic-storage-modules.patch"
cmp "$task_result/reconstructed/libmodprobe/libmodprobe.cpp" \
    "$task_source/system/core/libmodprobe/libmodprobe.cpp"

# Recreate the stock graph using ordinary files below this candidate folder.
# Only /lib/modules/ path prefixes change; module names and dependencies remain.
awk -v base="$task_result/stock-fixture/" '{gsub("/lib/modules/", base); print}' \
    "$task_candidate/stock/modules.dep" > "$task_result/stock-fixture/modules.dep"
cp "$task_candidate/stock/modules.load.recovery" \
    "$task_result/stock-fixture/modules.load.recovery"
cp "$task_candidate/stock/modules.blocklist" \
    "$task_result/stock-fixture/modules.blocklist"
cp "$task_candidate/stock/modules.alias" "$task_result/stock-fixture/modules.alias"
cp "$task_candidate/stock/modules.softdep" "$task_result/stock-fixture/modules.softdep"
cp "$task_candidate/stock/modules.options" "$task_result/stock-fixture/modules.options"
awk '{for (i=1; i<=NF; ++i) {value=$i; sub(/:$/, "", value); print value}}' \
    "$task_result/stock-fixture/modules.dep" | sort -u | while IFS= read -r module; do
    [[ "$module" == "$task_result/stock-fixture/"*.ko ]] || exit 1
    printf '%s\n' 'host fixture, never a kernel object' > "$module"
done

compile_loader() {
    local task_loader_source=$1 task_output=$2
    timeout 45 "$task_compiler" -std=c++17 -stdlib=libc++ -pthread -O0 \
    -I"$task_source/system/libbase/include" \
    -I"$task_source/external/fmtlib/include" \
    -I"$task_source/system/core/libmodprobe/include" \
    "$task_loader_source" \
    "$task_source/system/core/libmodprobe/exthandler.cpp" \
    "$task_source/system/core/libmodprobe/libmodprobe_ext.cpp" \
    "$task_candidate/loader.cpp" \
    -L"$task_libs" -Wl,-rpath,"$task_libs" -Wl,--wrap=access -Wl,--wrap=syscall \
    -Wl,--wrap=fork -Wl,--wrap=execv \
    -lbase -o "$task_output"
}

compile_loader "$task_source/system/core/libmodprobe/libmodprobe.cpp" \
    "$task_result/libmodprobe-recovery-denial"

timeout 30 "$task_result/libmodprobe-recovery-denial" \
    "$task_result/synthetic-fixture" "$task_result/stock-fixture"

compile_loader "$task_result/unpatched-libmodprobe.cpp" \
    "$task_result/libmodprobe-removed-denial"
set +e
timeout 30 "$task_result/libmodprobe-removed-denial" \
    "$task_result/synthetic-fixture" "$task_result/stock-fixture" > \
    "$task_result/removed-guard.log" 2>&1
task_mutant_status=$?
set -e
[[ $task_mutant_status == 1 ]]
rg -q '^FAIL: canonical denial absent$' "$task_result/removed-guard.log"
printf '%s\n' 'PASS: removed-denial mutant rejected by the same actual-loader test.'

sha256sum "$task_compiler" "$task_libs/libbase.so" "$task_libs/liblog.so" \
    "$task_libs/libc++.so" \
    "$task_component/patches/0040-recovery-block-automatic-storage-modules.patch" \
    "$task_source/system/core/libmodprobe/libmodprobe.cpp" \
    "$task_source/system/core/libmodprobe/libmodprobe_ext.cpp" \
    "$task_source/system/core/libmodprobe/exthandler.cpp" \
    "$task_candidate/loader.cpp" \
    "$task_candidate/stock/modules.dep" \
    "$task_candidate/stock/modules.load.recovery" > "$task_result/inputs.sha256"
sha256sum "$task_result/libmodprobe-recovery-denial" \
    "$task_result/libmodprobe-removed-denial" > "$task_result/binaries.sha256"
jq -n --arg runner "$(sha256sum "${BASH_SOURCE[0]}"|cut -d' ' -f1)" \
    --arg inputs "$(sha256sum "$task_result/inputs.sha256"|cut -d' ' -f1)" \
    '{schema_version:1,passed:true,evidence_class:"host-actual-libmodprobe-policy",runner_sha256:$runner,inputs_sha256:$inputs,
      actual_complete_translation_units:3,removed_guard_rejected:true,validation:{direct_and_alias_paths:true,
      hard_and_soft_dependencies:true,sequential_and_parallel_loaders:true,oem_blocklist_preserved:true,
      caller_bypass_refused:true,stock_graph:true,normal_boot_control:true,module_syscalls_mocked:true,
      external_handler_calls:0,physical_device:false,target_runtime:false}}' \
    > "$task_component/reports/private/recovery-module-policy-verification.json"
