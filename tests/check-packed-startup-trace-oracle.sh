#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Tiny host-only text controls for the exact shared syscall-text oracle.
# This script does not run a target ELF, QEMU, a build or a device command.
set -euo pipefail
export LC_ALL=C LANG=C
umask 077
[[ $# == 1 && -d $1 && ! -L $1 ]] || {
    printf 'Usage: check-packed-startup-trace-oracle.sh PRIVATE_OUTPUT_PARENT\n' >&2
    exit 1
}
runner=$(realpath -e -- "${BASH_SOURCE[0]}")
oracle="$(dirname -- "$runner")/packed-startup-trace-lib.sh"
[[ -f $oracle && ! -L $oracle && $(realpath -e -- "$oracle") == "$oracle" ]]
source "$oracle"
pattern=$(ure_packed_startup_boot_lookup_regex)
output=$(realpath -e -- "$1")
[[ $(stat -c '%u' "$output") == "$UID" ]]
mode=$(stat -c '%a' "$output")
[[ $mode =~ ^[0-7]{3,4}$ ]] && (( (8#$mode & 077) == 0 ))
work=$(mktemp -d "$output/oracle-XXXXXXXX")
mkdir -m700 -- "$work/cases"
expected=299
completed=0
ready=false
: > "$work/cases.jsonl"
digest() { sha256sum -- "$1" | cut -d' ' -f1; }
snapshot() { sha256sum -- "$runner" "$oracle"; }
finish() {
    local status=$? unchanged=false
    trap - EXIT
    set +e
    if [[ $ready == true ]] && snapshot > "$work/inputs-after.sha256" &&
        cmp -s -- "$work/inputs-before.sha256" "$work/inputs-after.sha256"; then
        unchanged=true
    else
        status=1
    fi
    if ! jq -n --argjson status "$status" --argjson expected "$expected" \
        --arg runner "$(digest "$runner")" --arg oracle "$(digest "$oracle")" \
        --arg before "$(digest "$work/inputs-before.sha256")" \
        --arg after "$(digest "$work/inputs-after.sha256")" --argjson unchanged "$unchanged" \
        --slurpfile cases "$work/cases.jsonl" \
        '{schema_version:1,evidence_class:"host-only-packed-startup-syscall-text-oracle-controls",
          passed:($status==0 and $unchanged and ($cases|length)==$expected and all($cases[];.passed)),
          runner_exit_status:$status,expected_cases:$expected,completed_cases:($cases|length),
          rejected_lookup_controls:([$cases[]|select(.expected_boot_lookup)]|length),
          permitted_normal_controls:([$cases[]|select(.expected_boot_lookup|not)]|length),
          reproduced_legacy_false_negatives:([$cases[]|select(.expected_boot_lookup and (.legacy_boot_lookup_seen|not))]|length),
          legacy_oracle_model_original_runner_sha256:"a9fffdacba32ba8ec890fc06b687e44b374aab079253f1bf65e482a254b8d5af",
          runner_sha256:$runner,boot_lookup_oracle_sha256:$oracle,
          input_manifests:{before_sha256:$before,after_sha256:$after,unchanged:$unchanged},
          cases:$cases,validation:{host_text_fixtures:true,target_elf_execution:false,qemu:false,
            production_integration:false,production_boot_service_environment:false,physical_device:false,
            hardware:false,tablet_writes:false,complete_hal_safety:false},
          scope:"Deterministic syscall-text oracle controls only; no emulated loader, compiled target, production init environment or hardware acceptance"}' \
        > "$work/verification.json"; then
        status=1
    fi
    if ! jq -e '.passed==true and .rejected_lookup_controls==228 and .permitted_normal_controls==71 and
        .reproduced_legacy_false_negatives>0' "$work/verification.json" >/dev/null 2>&1; then
        status=1
    fi
    printf 'Host text-oracle controls retained privately: %s\n' "$work"
    exit "$status"
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
snapshot > "$work/inputs-before.sha256"
ready=true

# Preserve the frozen production oracle as a text model for regression proof.
# The actual original script is retained separately and is never executed here.
legacy_path_call='(open|openat|openat2|access|faccessat|faccessat2|stat|lstat|newfstatat|statx|readlink|readlinkat)\('
legacy_vendor_impl='"(/payload)?/(vendor|odm)/([^"/]*/)*(hw(/|")|bootctrl[^"/]*"|android\.hardware\.boot[^"/]*-impl[^"/]*")'
legacy_relative_impl='"(bootctrl[^"/]*|android\.hardware\.boot[^"/]*-impl[^"/]*)"'
legacy_pattern="$legacy_path_call.*($legacy_vendor_impl|$legacy_relative_impl)"
control() {
    local label=$1 trace=$2 expected_lookup=$3 status=0 legacy_status=0
    local actual=false legacy_seen=false passed=true case_dir
    completed=$((completed+1))
    printf -v case_dir '%s/cases/%04d' "$work" "$completed"
    mkdir -m700 -- "$case_dir"
    printf '%s\n' "$trace" > "$case_dir/qemu.strace.text-fixture"
    rg -n -- "$pattern" "$case_dir/qemu.strace.text-fixture" > "$case_dir/lookup-matches" || status=$?
    case "$status" in 0) actual=true;; 1) actual=false;; *) passed=false;; esac
    [[ $actual == "$expected_lookup" ]] || passed=false
    rg -n -- "$legacy_pattern" "$case_dir/qemu.strace.text-fixture" > "$case_dir/legacy-matches" || legacy_status=$?
    case "$legacy_status" in 0) legacy_seen=true;; 1) legacy_seen=false;; *) passed=false;; esac
    jq -cn --arg label "$label" --arg text "$trace" --arg trace "$(digest "$case_dir/qemu.strace.text-fixture")" \
        --argjson expected "$expected_lookup" --argjson actual "$actual" --argjson legacy "$legacy_seen" \
        --argjson status "$status" --argjson passed "$passed" \
        '{case:$label,syscall_text:$text,trace_fixture_sha256:$trace,
          expected_boot_lookup:$expected,boot_lookup_seen:$actual,oracle_exit_status:$status,
          legacy_boot_lookup_seen:$legacy,passed:$passed}' >> "$work/cases.jsonl"
    [[ $passed == true ]] || { printf 'Text-oracle control failed: %s\n' "$label" >&2; return 1; }
}
text_for_path() {
    local call=$1 path=$2
    case "$call" in
        open) printf '2 open("%s",O_RDONLY|O_CLOEXEC) = -1 errno=2 (No such file or directory)' "$path";;
        openat) printf '2 openat(-100,"%s",O_RDONLY|O_CLOEXEC) = -1 errno=2 (No such file or directory)' "$path";;
        openat2) printf '2 openat2(-100,"%s",0x00007fff0000,24) = -1 errno=2 (No such file or directory)' "$path";;
        access) printf '2 access("%s",R_OK) = -1 errno=2 (No such file or directory)' "$path";;
        faccessat|faccessat2) printf '2 %s(-100,"%s",R_OK,0) = -1 errno=2 (No such file or directory)' "$call" "$path";;
        stat|lstat) printf '2 %s("%s",0x00007fff0000) = -1 errno=2 (No such file or directory)' "$call" "$path";;
        newfstatat) printf '2 newfstatat(-100,"%s",0x00007fff0000,0) = -1 errno=2 (No such file or directory)' "$path";;
        statx) printf '2 statx(-100,"%s",0,STATX_BASIC_STATS,0x00007fff0000) = -1 errno=2 (No such file or directory)' "$path";;
        readlink) printf '2 readlink("%s",0x00007fff0000,256) = -1 errno=2 (No such file or directory)' "$path";;
        readlinkat) printf '2 readlinkat(-100,"%s",0x00007fff0000,256) = -1 errno=2 (No such file or directory)' "$path";;
        *) return 1;;
    esac
}
prefixes=('' /vendor/lib64/hw/ /odm/lib64/hw/ /system/lib64/hw/ /system_ext/lib64/hw/
    /system/vendor/lib64/hw/ /payload/vendor/lib64/hw/ /payload/system/lib64/hw/
    vendor/lib64/hw/ ./system_ext/lib64/hw/ ../odm/lib64/hw/ lib64/hw/ /system/lib64/
    'directory with spaces/')
basenames=(bootctrl.default.so bootctrl.sm7675.so android.hardware.boot@1.0-impl.so android.hardware.boot@1.0-impl-1.2.so)
calls=(openat access newfstatat)
for prefix in "${prefixes[@]}"; do
    for basename in "${basenames[@]}"; do
        for call in "${calls[@]}"; do
            control "implementation-$call-${prefix}${basename}" "$(text_for_path "$call" "${prefix}${basename}")" true
        done
    done
done
directories=(hw lib64/hw ./vendor/lib64/hw ../odm/lib64/hw /system/lib64/hw /system_ext/lib64/hw
    /vendor/lib64/hw /odm/lib64/hw /system/vendor/lib64/hw /payload/system/lib64/hw
    /payload/system_ext/lib64/hw /payload/vendor/lib64/hw /payload/odm/lib64/hw
    'directory with spaces/hw' /vendor/lib64/vndk-sp/hw /system/lib/hw)
for directory in "${directories[@]}"; do
    for call in "${calls[@]}"; do
        control "hal-directory-$call-$directory" "$(text_for_path "$call" "$directory")" true
    done
done
normal_paths=(/system/lib64/libboot_control_client.so /payload/system/lib64/libboot_control_client.so
    /system/lib64/android.hardware.boot@1.0.so /system/lib64/android.hardware.boot@1.1.so
    /system/lib64/android.hardware.boot@1.2.so /system/lib64/android.hardware.boot-V1-ndk.so
    /system/lib64/libhardware.so /system/lib64/libhidlbase.so /system/lib64/libbinder_ndk.so
    /payload/system/lib64/libc.so /system/bin/linker64 /system/bin/android.hardware.boot@1.0-service
    /system/etc/init/android.hardware.boot@1.2-service.rc /system/etc/ld.config.txt
    /dev/__properties__ /proc/self/exe libboot_control_client.so /system/lib64/notbootctrl.default.so)
for path in "${normal_paths[@]}"; do
    for call in "${calls[@]}"; do
        control "normal-dependency-$call-$path" "$(text_for_path "$call" "$path")" false
    done
done
all_calls=(open openat openat2 access faccessat faccessat2 stat lstat newfstatat statx readlink readlinkat)
for call in "${all_calls[@]}"; do
    control "syscall-$call-forbidden" "$(text_for_path "$call" /system/lib64/hw/bootctrl.default.so)" true
    control "syscall-$call-normal" "$(text_for_path "$call" /payload/system/lib64/libboot_control_client.so)" false
done
control target-stderr-is-not-a-lookup '2 write(2,"bootctrl.default.so diagnostic only",38) = 38' false
control argv-text-is-not-a-lookup '2 execve("/system/bin/bootctl",{"bootctrl.default.so"},0) = 0' false
control library-suffix-is-not-an-implementation '2 openat(-100,"/system/lib64/android.hardware.boot@1.0-service",O_RDONLY) = 5' false
control unrelated-basename-prefix '2 openat(-100,"/system/lib64/otherbootctrl.default.so",O_RDONLY) = 5' false
control arbitrary-hw-substring '2 openat(-100,"/system/lib64/libhwutility.so",O_RDONLY) = 5' false
[[ $completed == "$expected" ]]
printf 'PASS: %s host-only syscall-text controls. No target or QEMU execution.\n' "$completed"
