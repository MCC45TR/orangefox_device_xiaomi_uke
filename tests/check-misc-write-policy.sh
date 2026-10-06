#!/usr/bin/env bash
# Full-source host regressions. Every mutation stays in private regular fixtures.
set -Eeuo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
candidate="$component/tests/ure/misc-policy"
[[ $# == 0 ]]
tree="$component/src/upstream/orangefox-android16"
bcb="$tree/bootable/recovery/bootloader_message/bootloader_message.cpp"
control="$tree/hardware/interfaces/boot/1.1/default/boot_control/libboot_control.cpp"
hal="$tree/hardware/interfaces/boot/1.1/default/BootControl.cpp"
args="$tree/bootable/recovery/install/get_args.cpp"
policy="$tree/device/xiaomi/uke/recoveryctl/libuke/recovery_write_policy.hpp"
for source in "$bcb" "$control" "$hal" "$args" "$policy"; do [[ -f $source && ! -L $source ]]; done
cmp "$component/src/device/xiaomi/uke/recoveryctl/libuke/recovery_write_policy.hpp" "$policy"
mkdir -p "$component/build/misc-policy-tests" "$component/reports/private"
run=$(mktemp -d "$component/build/misc-policy-tests/run-XXXXXXXX")
compiler=${CXX:-/usr/bin/c++}
flags=(-std=c++20 -O0 -Wall -Wextra -Werror -Wno-sign-compare
    -I"$candidate/mocks" -I"$tree/bootable/recovery/bootloader_message/include"
    -I"$tree/hardware/interfaces/boot/1.1/default/boot_control/include"
    -I"$tree/hardware/interfaces/boot/1.1/default"
    -I"$tree/device/xiaomi/uke/recoveryctl/libuke")
inputs() {
    local file
    for file in bootable/recovery/bootloader_message/bootloader_message.cpp \
        hardware/interfaces/boot/1.1/default/boot_control/libboot_control.cpp \
        hardware/interfaces/boot/1.1/default/BootControl.cpp bootable/recovery/install/get_args.cpp \
        bootable/recovery/bootloader_message/include/bootloader_message/bootloader_message.h \
        hardware/interfaces/boot/1.1/default/boot_control/include/libboot_control/libboot_control.h \
        hardware/interfaces/boot/1.1/default/boot_control/include/private/boot_control_definition.h \
        hardware/interfaces/boot/1.1/default/BootControl.h device/xiaomi/uke/recoveryctl/libuke/recovery_write_policy.hpp; do
        printf '%s\t%s\n' "$file" "$(sha256sum "$tree/$file"|cut -d ' ' -f1)"
    done
}
inputs > "$run/production-inputs.before.tsv"
compile() { timeout 45 "$compiler" "${flags[@]}" -c "$1" -o "$2"; }
link() { timeout 30 "$compiler" "$run/test.o" "$1" "$2" "$run/hal.o" "$run/args.o" -Wl,--wrap=open -Wl,--wrap=open64 -o "$3"; }
compile "$candidate/misc_policy.cpp" "$run/test.o"
compile "$bcb" "$run/bcb.o"
compile "$control" "$run/control.o"
compile "$hal" "$run/hal.o"
compile "$args" "$run/args.o"
link "$run/bcb.o" "$run/control.o" "$run/original"
fixture=$(mktemp -d "$run/fixture-XXXXXXXX")
timeout 20 "$run/original" "$fixture" all | tee "$run/original.log"
# Preserve each complete production TU and remove only its six-line guard.
remove_guard() {
    local source=$1 destination=$2
    [[ $(rg -c 'const auto decision = ure::legacy_write_decision' "$source") == 1 ]]
    awk '/const auto decision = ure::legacy_write_decision/ {drop=1; next}
        drop {if($0 ~ /^  }$/) drop=0; next}
        {print} END {if(drop) exit 1}' "$source" > "$destination"
    [[ $(( $(wc -l < "$source") - $(wc -l < "$destination") )) == 6 ]]
}
mutants=0
reject() {
    local name=$1 binary=$2 suite=$3 expected=$4 status
    fixture=$(mktemp -d "$run/fixture-XXXXXXXX")
    if timeout 20 "$binary" "$fixture" "$suite" > "$run/$name.log" 2>&1; then
        printf 'Removed-policy mutant accepted: %s\n' "$name" >&2; exit 1
    else status=$?; fi
    [[ $status == 1 ]] || { cat "$run/$name.log" >&2; exit 1; }
    rg "$expected" "$run/$name.log" >/dev/null || { cat "$run/$name.log" >&2; exit 1; }
    mutants=$((mutants+1)); printf 'PASS rejected mutant %s\n' "$name"
}
remove_guard "$bcb" "$run/bcb-no-guard.cpp"
compile "$run/bcb-no-guard.cpp" "$run/bcb-no-guard.o"
link "$run/bcb-no-guard.o" "$run/control.o" "$run/bcb-mutant"
reject bcb-guard-removed "$run/bcb-mutant" bcb 'bcb-direct-partition returned success'
reject get-args-unguarded-BCB "$run/bcb-mutant" get-args 'get-args-empty performed a writable open|changed whole fixture bytes'
remove_guard "$control" "$run/control-no-guard.cpp"
compile "$run/control-no-guard.cpp" "$run/control-no-guard.o"
link "$run/bcb.o" "$run/control-no-guard.o" "$run/control-mutant"
reject boot-control-guard-removed "$run/control-mutant" hal 'invalid-CRC repeated Init returned success|performed a writable open|boot-control-direct-save returned success'
# Restore the exact old initialization defect in a complete private TU. The
# real save guard remains present, so repeated Init rather than a write catches it.
awk '{print} /^  misc_device_ = device;$/ {print "  initialized_ = true;"}' "$control" > "$run/control-early-initialized.cpp"
compile "$run/control-early-initialized.cpp" "$run/control-early-initialized.o"
link "$run/bcb.o" "$run/control-early-initialized.o" "$run/early-init-mutant"
reject early-initialized-invalid-CRC "$run/early-init-mutant" invalid-crc 'invalid-CRC repeated Init returned success'
reject early-initialized-invalid-VAB "$run/early-init-mutant" invalid-vab 'invalid-VAB repeated Init returned success'
# Ignoring a refused CRC repair must also fail; leave both actual guards intact.
awk '/    if \(!UpdateAndSaveBootloaderControl\(device.c_str\(\), &boot_ctrl\)\) \{/ {print "    UpdateAndSaveBootloaderControl(device.c_str(), &boot_ctrl);"; drop=1; next}
    drop {if($0 ~ /^    }$/) drop=0; next} {print} END {if(drop) exit 1}' "$control" > "$run/control-ignored-repair.cpp"
compile "$run/control-ignored-repair.cpp" "$run/control-ignored-repair.o"
link "$run/bcb.o" "$run/control-ignored-repair.o" "$run/ignored-repair-mutant"
reject ignored-refused-CRC-repair "$run/ignored-repair-mutant" invalid-crc 'invalid-CRC repeated Init returned success'
inputs > "$run/production-inputs.after.tsv"
cmp "$run/production-inputs.before.tsv" "$run/production-inputs.after.tsv"
cp "$run/production-inputs.before.tsv" "$component/reports/private/misc-write-policy-inputs.tsv"
cases=$(sed -n 's/^RESULT cases=\([0-9]*\).*/\1/p' "$run/original.log")
[[ $cases =~ ^[1-9][0-9]*$ && $mutants == 6 ]]
jq -n --argjson cases "$cases" --argjson mutants "$mutants" \
    --arg input_sha256 "$(sha256sum "$component/reports/private/misc-write-policy-inputs.tsv"|cut -d ' ' -f1)" \
    --arg runner_sha256 "$(sha256sum "${BASH_SOURCE[0]}"|cut -d ' ' -f1)" \
    '{schema_version:1,evidence_class:"host-full-production-misc-policy-regression",date:"2026-10-06",passed:true,
      actual_complete_translation_units:4,cases:$cases,rejected_mutants:$mutants,production_inputs_sha256:$input_sha256,runner_sha256:$runner_sha256,
      validation:{zero_writable_opens:true,whole_regular_fixture_unchanged:true,actual_BCB_writers:true,actual_all_system_message_writers:true,
        actual_get_args_preserves_modes_and_CLI_precedence:true,repeated_invalid_CRC_Init_refuses:true,repeated_invalid_VAB_Init_refuses:true,
        valid_CRC_and_VAB_read_queries:true,actual_HAL_wrapper:true,removed_guard_and_initialization_mutants_rejected:true,
        source_before_after_equal:true,physical_device:false,shipping_kernel_test:false,target_runtime:false,live_block_attachment:false}}' > "$component/reports/private/misc-write-policy-verification.json"
printf 'Full production TU regressions: %s cases, %s rejected mutants; host regular fixtures only.\n' "$cases" "$mutants"
