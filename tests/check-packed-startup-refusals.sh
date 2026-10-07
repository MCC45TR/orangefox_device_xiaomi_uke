#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Execute the caller-authenticated extracted AArch64 ELFs, without patching or
# substituting them. The image auditor owns host-budget admission and payload
# authentication. These short startup fixtures are not a complete HAL audit.
set -euo pipefail
export LC_ALL=C LANG=C
umask 077

component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
if [[ -n ${UKE_RECOVERY_SOURCE_COMPONENT:-} ]]; then
    component=$(cd -- "$UKE_RECOVERY_SOURCE_COMPONENT" && pwd -P)
fi
[[ ( $# == 1 || $# == 2 ) && -d $1 && ! -L $1 ]] || {
    printf 'Usage: check-packed-startup-refusals.sh AUTHENTICATED_EXTRACTED_RAMDISK [PRIVATE_REPORT_JSON]\n' >&2
    exit 1
}
payload=$(realpath -e -- "$1")
runner=$(realpath -e -- "${BASH_SOURCE[0]}")
for tool in bwrap qemu-aarch64 timeout readelf sha256sum jq rg find sort head od tr cmp truncate dd stat realpath mktemp mkdir basename dirname cut wc ln; do
    command -v "$tool" >/dev/null
done
qemu=$(realpath -e -- "$(command -v qemu-aarch64)")
bubblewrap=$(realpath -e -- "$(command -v bwrap)")
deadline=$(realpath -e -- "$(command -v timeout)")
[[ $qemu == /usr/* && -f $qemu && -x $qemu ]]
[[ -d $component/src/device/xiaomi/uke && -d $component/build && ! -L $component/build ]]
report=${2:-}
private_report_mutable() {
    local requested=$1 resolved lexical parent name mode
    resolved=$(realpath -m -- "$requested") || return 1
    lexical=$(realpath -ms -- "$requested") || return 1
    [[ $resolved == "$lexical" ]] || return 1
    [[ $resolved == "$component/build/"* || $resolved == "$component/reports/private/"* ]] || return 1
    parent=$(dirname -- "$resolved")
    name=$(basename -- "$resolved")
    [[ $name =~ ^[a-zA-Z0-9][a-zA-Z0-9._-]*\.json$ && -d $parent && ! -L $parent &&
        $(stat -c '%u' "$parent") == "$UID" && ! -e $resolved && ! -L $resolved ]] || return 1
    mode=$(stat -c '%a' "$parent") || return 1
    [[ $mode =~ ^[0-7]{3,4}$ ]] && (( (8#$mode & 077) == 0 ))
}
if [[ -n $report ]]; then
    private_report_mutable "$report" || {
        printf 'The optional report must be a new JSON file in an owner-only private build/report directory, without symlink ancestry.\n' >&2
        exit 1
    }
    report=$(realpath -m -- "$report")
fi
work=$(mktemp -d "$component/build/packed-startup-refusals-XXXXXXXX")
mkdir -m700 -- "$work/fixtures" "$work/cases"
: > "$work/cases.jsonl"
expected_cases=26
fixtures_ready=false
elfs_ready=false
inputs_ready=false
fixture_before=''

digest() { sha256sum -- "$1" | cut -d' ' -f1; }
digest_if_present() { if [[ -f $1 ]]; then digest "$1"; else printf ''; fi; }

snapshot_elfs() {
    local label=$1
    (
        cd -- "$payload" || exit 1
        find . -type f -print0 | sort -z > "$work/elfs.$label.members.nul" || exit 1
        while IFS= read -r -d '' file; do
            magic=$(head -c4 -- "$file" | od -An -tx1 | tr -d ' \n') || exit 1
            if [[ $magic == 7f454c46 ]]; then
                sha256sum -- "$file" || exit 1
            fi
        done < "$work/elfs.$label.members.nul"
    ) > "$work/elfs.$label.sha256"
}

snapshot_inputs() {
    sha256sum -- "$runner" "$qemu" "$bubblewrap" "$deadline" "$work/fixtures/sentinel.img"
}

finish() {
    local status=$? elfs_same=false inputs_same=false fixture_same=false missing_absent=false
    local fixture_after='' result_written=false
    trap - EXIT
    set +e
    # Keep every diagnostic, including incomplete runs. Never remove a caller's
    # extracted payload, and never infer a pass from an interrupted run.
    if [[ $elfs_ready == true ]] && snapshot_elfs after &&
        cmp -s -- "$work/elfs.before.sha256" "$work/elfs.after.sha256"; then
        elfs_same=true
    else
        status=1
    fi
    if [[ $inputs_ready == true ]] && snapshot_inputs > "$work/inputs.after.sha256" &&
        cmp -s -- "$work/inputs.before.sha256" "$work/inputs.after.sha256"; then
        inputs_same=true
    else
        status=1
    fi
    if [[ $fixtures_ready == true && -f $work/fixtures/sentinel.img && ! -L $work/fixtures/sentinel.img ]]; then
        fixture_after=$(digest "$work/fixtures/sentinel.img")
        [[ $fixture_after != "$fixture_before" ]] || fixture_same=true
    fi
    if [[ ! -e $work/fixtures/missing.img && ! -L $work/fixtures/missing.img ]]; then
        missing_absent=true
    fi
    [[ $fixture_same == true && $missing_absent == true ]] || status=1
    if jq -n --argjson status "$status" --argjson expected "$expected_cases" \
        --arg runner "$(digest "$runner")" --arg qemu "$(digest "$qemu")" \
        --arg elfs_before "$(digest_if_present "$work/elfs.before.sha256")" \
        --arg elfs_after "$(digest_if_present "$work/elfs.after.sha256")" \
        --arg inputs_before "$(digest_if_present "$work/inputs.before.sha256")" \
        --arg inputs_after "$(digest_if_present "$work/inputs.after.sha256")" \
        --arg fixture_before "$fixture_before" --arg fixture_after "$fixture_after" \
        --argjson elfs_same "$elfs_same" --argjson inputs_same "$inputs_same" \
        --argjson fixture_same "$fixture_same" --argjson missing_absent "$missing_absent" \
        --slurpfile cases "$work/cases.jsonl" \
        '{schema_version:1,evidence_class:"packed-aarch64-qemu-user-startup-refusals",
          passed:($status==0 and ($cases|length)==$expected and all($cases[];.passed)),
          runner_exit_status:$status,expected_cases:$expected,completed_cases:($cases|length),
          runner_sha256:$runner,qemu_sha256:$qemu,
          payload_provenance:"Caller-authenticated extracted ramdisk; no staging fallback",
          unmodified_packed_binaries:$elfs_same,runner_and_fixture_inputs_unchanged:$inputs_same,
          elf_manifests:{before:"elfs.before.sha256",after:"elfs.after.sha256",
            before_sha256:$elfs_before,after_sha256:$elfs_after},
          input_manifests:{before:"inputs.before.sha256",after:"inputs.after.sha256",
            before_sha256:$inputs_before,after_sha256:$inputs_after},
          fixture:{regular_image_sha256_before:$fixture_before,regular_image_sha256_after:$fixture_after,
            regular_image_unchanged:$fixture_same,missing_image_still_absent:$missing_absent},
          isolation:{read_only_host_runtime:true,read_only_payload:true,read_only_input_fixture:true,
            synthetic_dev:true,host_dev_exposed:false,host_sys_exposed:false,network_namespace_isolated:true,
            trace_and_diagnostics_separate:true},
          cases:$cases,validation:{fixture:true,emulation:true,physical:false,physical_device:false,hardware:false,
            complete_hal_safety:false,tablet_writes:false},
          scope:"Startup and boot-client factory refusal only; no physical acceptance or complete HAL safety claim"}' \
        > "$work/result.json"; then
        result_written=true
    else
        status=1
    fi
    if [[ $result_written != true ]] || ! jq -e '.passed == true' "$work/result.json" >/dev/null 2>&1; then
        status=1
    fi
    if [[ -n $report && $result_written == true ]]; then
        # A new hard link atomically binds the caller's exact receipt path to
        # this completed result, without replacing any file or following a
        # destination symlink. Both paths are inside the owning component.
        # Persist failure receipts as well; callers still require exit zero.
        if ! private_report_mutable "$report" || ! ln -- "$work/result.json" "$report"; then
            printf 'Cannot bind the packed startup result to its requested private report path.\n' >&2
            status=1
        fi
    fi
    printf 'Packed startup refusal diagnostics retained privately: %s\n' "$work" >&2
    exit "$status"
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

required=(
    system/bin/android.hardware.boot@1.0-service
    system/bin/android.hardware.boot@1.1-service
    system/bin/android.hardware.boot@1.2-service
    system/bin/bootctl
    system/bin/uke-recovery-install
    system/bin/linker64
    system/lib64/libboot_control_client.so
)
for relative in "${required[@]}"; do
    [[ -f $payload/$relative && ! -L $payload/$relative &&
        $(realpath -e -- "$payload/$relative") == "$payload/$relative" ]]
    readelf -h -- "$payload/$relative" > "$work/$(basename -- "$relative").elf-header"
    rg -q 'Class:[[:space:]]+ELF64' "$work/$(basename -- "$relative").elf-header"
    rg -q 'Machine:[[:space:]]+AArch64' "$work/$(basename -- "$relative").elf-header"
done
[[ -d $payload/dev && ! -L $payload/dev && $(realpath -e -- "$payload/dev") == "$payload/dev" ]]
# Mask the extracted dev directory as well as /dev. Refuse device nodes at
# other payload paths before exposing any payload file to QEMU.
find "$payload" \( -type b -o -type c \) -print > "$work/payload-device-nodes.txt"
[[ ! -s $work/payload-device-nodes.txt ]]

truncate -s 104857600 "$work/fixtures/sentinel.img"
printf 'Synthetic installer refusal fixture; never a flashable image.\n' | \
    dd of="$work/fixtures/sentinel.img" conv=notrunc status=none
printf 'Synthetic end marker.\n' | \
    dd of="$work/fixtures/sentinel.img" bs=1 seek=104857500 conv=notrunc status=none
[[ $(stat -c %s "$work/fixtures/sentinel.img") == 104857600 ]]
fixture_before=$(digest "$work/fixtures/sentinel.img")
fixtures_ready=true
snapshot_elfs before
[[ -s $work/elfs.before.sha256 ]]
elfs_ready=true
snapshot_inputs > "$work/inputs.before.sha256"
inputs_ready=true
"$qemu" --version > "$work/qemu-version.txt"
"$bubblewrap" --version > "$work/bwrap-version.txt"

# All host paths are read-only inside a fresh namespace. The only output
# channels are inherited standard streams and fd 3 for QEMU's trace. There is
# no writable diagnostics mount and no binding of the host's /dev or /sys.
sandbox=(
    "$bubblewrap" --unshare-user --unshare-ipc --unshare-pid --unshare-net --unshare-uts
    --die-with-parent --new-session --cap-drop ALL
    --ro-bind /usr /usr --ro-bind /lib /lib --ro-bind /lib64 /lib64
    --ro-bind "$payload" /payload --ro-bind "$work/fixtures" /fixtures
    --proc /proc --remount-ro /proc
    --dev /dev --remount-ro /dev --dev /payload/dev --remount-ro /payload/dev
    --dir /sys --dir /tmp --dir /nonexistent
    --clearenv --setenv PATH /usr/bin --setenv LC_ALL C --setenv LANG C
    --setenv HOME /nonexistent --chdir /payload --remount-ro /
)
open_call='(open|openat|openat2)\('
path_call='(open|openat|openat2|access|faccessat|faccessat2|stat|lstat|newfstatat|statx|readlink|readlinkat)\('
binder_path='"((/payload)?/dev/)?(binder|hwbinder|vndbinder|binderfs)(/|")'
block_path='"(/payload)?/dev/(block(/|")|sd[a-z][0-9]*"|mmcblk[^"/]*"|nvme[^"/]*"|dm-[0-9]+")'
vendor_impl='"(/payload)?/(vendor|odm)/([^"/]*/)*(hw(/|")|bootctrl[^"/]*"|android\.hardware\.boot[^"/]*-impl[^"/]*")'
relative_impl='"(bootctrl[^"/]*|android\.hardware\.boot[^"/]*-impl[^"/]*)"'
input_path='"((/payload)?/fixtures/)?(sentinel|missing)\.img"'

trace_absent() {
    local pattern=$1 trace=$2 matches=$3 status=0
    rg -n -- "$pattern" "$trace" > "$matches" || status=$?
    # A malformed expression or an unreadable trace is also a failed check.
    [[ $status == 1 ]]
}

run_case() {
    local label=$1 relative=$2 expected=$3 diagnostic=$4
    shift 4
    local case_dir="$work/cases/$label" status=0 passed=true
    local exit_seen=false code_seen=false binder_absent=true block_absent=true vendor_absent=true input_absent=true
    mkdir -m700 -- "$case_dir"
    # -D and a dedicated fd prevent QEMU strace lines from being interleaved
    # with the target's stderr, which must independently contain its refusal.
    "$deadline" --signal=TERM --kill-after=2s 10s "${sandbox[@]}" \
        "$qemu" -strace -D /proc/self/fd/3 -L /payload \
        -E 'LD_LIBRARY_PATH=/payload/system/lib64:/payload/vendor/lib64' \
        "/payload/$relative" "$@" \
        > "$case_dir/stdout" 2> "$case_dir/stderr" 3> "$case_dir/qemu.strace" || status=$?
    [[ $status == "$expected" ]] || passed=false
    if rg -q "(exit|exit_group)\\($expected\\)" "$case_dir/qemu.strace"; then exit_seen=true; else passed=false; fi
    if rg -Fq -- "$diagnostic" "$case_dir/stderr"; then code_seen=true; else passed=false; fi
    if ! trace_absent "$open_call.*$binder_path" "$case_dir/qemu.strace" "$case_dir/binder-open-matches"; then
        binder_absent=false; passed=false
    fi
    if ! trace_absent "$open_call.*$block_path" "$case_dir/qemu.strace" "$case_dir/block-open-matches"; then
        block_absent=false; passed=false
    fi
    if ! trace_absent "$path_call.*($vendor_impl|$relative_impl)" "$case_dir/qemu.strace" "$case_dir/boot-implementation-matches"; then
        vendor_absent=false; passed=false
    fi
    if ! trace_absent "$open_call.*$input_path" "$case_dir/qemu.strace" "$case_dir/fixture-open-matches"; then
        input_absent=false; passed=false
    fi
    jq -cn --arg label "$label" --arg executable "$relative" --arg elf "$(digest "$payload/$relative")" \
        --argjson expected "$expected" --argjson actual "$status" --arg diagnostic "$diagnostic" \
        --argjson passed "$passed" --argjson exit_seen "$exit_seen" --argjson code_seen "$code_seen" \
        --argjson binder_absent "$binder_absent" --argjson block_absent "$block_absent" \
        --argjson vendor_absent "$vendor_absent" --argjson input_absent "$input_absent" \
        --arg stdout "$(digest "$case_dir/stdout")" --arg stderr "$(digest "$case_dir/stderr")" \
        --arg trace "$(digest "$case_dir/qemu.strace")" --args \
        '{case:$label,executable:$executable,elf_sha256:$elf,arguments:$ARGS.positional,
          expected_exit_status:$expected,exit_status:$actual,expected_stderr_diagnostic:$diagnostic,
          expected_target_exit_in_trace:$exit_seen,expected_stderr_diagnostic_seen:$code_seen,
          no_binder_open_attempt:$binder_absent,no_block_path_open_attempt:$block_absent,
          no_vendor_odm_boot_implementation_access:$vendor_absent,no_fixture_input_open_attempt:$input_absent,
          stdout_sha256:$stdout,stderr_sha256:$stderr,qemu_strace_sha256:$trace,passed:$passed}' \
        -- "$@" >> "$work/cases.jsonl"
    if [[ $passed != true ]]; then
        printf 'Packed startup refusal failed: %s (expected exit %s, observed %s).\n' "$label" "$expected" "$status" >&2
        return 1
    fi
    printf 'PASS packed startup refusal: %s\n' "$label"
}

for version in 1.0 1.1 1.2; do
    run_case "boot-service-$version" "system/bin/android.hardware.boot@$version-service" 1 ure-legacy-write-unavailable
done
for verb in hal-info get-number-slots get-current-slot get-active-boot-slot get-snapshot-merge-status; do
    run_case "bootctl-$verb" system/bin/bootctl 70 ure-legacy-write-unavailable "$verb"
done
for slot in 0 1; do
    for verb in is-slot-bootable is-slot-marked-successful get-suffix; do
        run_case "bootctl-$verb-$slot" system/bin/bootctl 70 ure-legacy-write-unavailable "$verb" "$slot"
    done
done
run_case bootctl-mark-boot-successful system/bin/bootctl 70 ure-legacy-write-unavailable mark-boot-successful
for slot in 0 1; do
    for verb in set-active-boot-slot set-slot-as-unbootable; do
        run_case "bootctl-$verb-$slot" system/bin/bootctl 70 ure-legacy-write-unavailable "$verb" "$slot"
    done
done
for merge in none unknown snapshotted merging cancelled; do
    run_case "bootctl-set-snapshot-merge-status-$merge" system/bin/bootctl 70 ure-legacy-write-unavailable set-snapshot-merge-status "$merge"
done
run_case installer-valid-regular-input system/bin/uke-recovery-install 1 \
    'uke-recovery-install: installer-durability-unavailable:' install /fixtures/sentinel.img "$fixture_before"
run_case installer-missing-input-invalid-hash system/bin/uke-recovery-install 1 \
    'uke-recovery-install: installer-durability-unavailable:' install /fixtures/missing.img invalid-hash
[[ $(wc -l < "$work/cases.jsonl") == "$expected_cases" ]]
printf 'All %s packed startup refusal cases passed in QEMU user-mode fixtures; no physical or hardware acceptance.\n' "$expected_cases"
