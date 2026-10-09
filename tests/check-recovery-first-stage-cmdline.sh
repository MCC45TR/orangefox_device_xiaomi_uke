#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Host-only: exercise the actual AOSP recovery Make recipe and mkbootimg tool.
# The pinned upstream host boot-image builder is required to test its own header
# serialization; it is not a tablet payload. No device, donor script or module
# syscall is used. Header contents do not prove bootloader or kernel acceptance.
set -euo pipefail
task_component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
task_source="$task_component/src/upstream/orangefox-android16"
task_make="$task_source/build/make/core/Makefile"
task_board="$task_component/src/device/xiaomi/uke/BoardConfig.mk"
task_mkbootimg="$task_source/out-public/host/linux-x86/bin/mkbootimg"
task_expected='rdinit=/system/bin/init module_blacklist=charger_partition,ufs_ffu'
[[ -x $task_mkbootimg ]]
[[ $(git -C "$task_source/build/make" rev-parse HEAD) == a11b2e4720c651cd5e5fc92e92479f30196349a2 ]]
rg -F '$(if $(filter true, $(BOARD_EXCLUDE_KERNEL_FROM_RECOVERY_IMAGE)),, $(recovery_kernel)))' \
    "$task_make" > /dev/null
mkdir -p "$task_component/build/first-stage-cmdline" "$task_component/reports/private"
task_work=$(mktemp -d "$task_component/build/first-stage-cmdline/run-XXXXXXXX")

# Extract the real argument selection and recipe rather than reimplementing
# either. Unexpected upstream structure fails this bounded fixture.
awk '
  /^INTERNAL_RECOVERYIMAGE_ARGS := --ramdisk / { if (seen++) exit 1; copying=1 }
  copying { print }
  copying && /^ifndef BOARD_RECOVERY_MKBOOTIMG_ARGS$/ { ending=1 }
  copying && ending && /^endif$/ { completed++; copying=0 }
  END { if (seen!=1 || completed!=1 || copying) exit 2 }
' "$task_make" > "$task_work/recovery-args.mk"
awk '
  /^define build-recoveryimage-target$/ { if (seen++) exit 1; copying=1 }
  copying { print; if ($0=="endef") { completed++; copying=0 } }
  END { if (seen!=1 || completed!=1 || copying) exit 2 }
' "$task_make" > "$task_work/recovery-recipe.mk"
cp "$task_board" "$task_work/BoardConfig.mk"
printf '%s\n' 'host-only ramdisk fixture' > "$task_work/ramdisk"
cat > "$task_work/mkbootimg-arguments.sh" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\0' "$@" > "${URE_MKBOOTIMG_ARGUMENT_LOG:?}"
exec "${URE_MKBOOTIMG_REAL:?}" "$@"
SH
chmod 700 "$task_work/mkbootimg-arguments.sh"
cat > "$task_work/fixture.mk" <<'MAKE'
include $(BOARD_CONFIG)
# A late common argument must survive the recovery-only argument override.
BOARD_MKBOOTIMG_ARGS += --os_version 13.2.1 --os_patch_level 2026-10
BOARD_AVB_ENABLE := false
BOARD_USES_RECOVERY_AS_BOOT := false
BUILDING_VENDOR_BOOT_IMAGE := false
INTERNAL_KERNEL_CMDLINE := inherited_normal_boot_argument=1
recovery_ramdisk := $(RAMDISK)
recovery_kernel := $(RAMDISK)
include $(ARGS_MAKEFILE)
include $(RECIPE_MAKEFILE)
.PHONY: all
all: $(OUTPUT)
$(OUTPUT): $(RAMDISK)
	$(call build-recoveryimage-target,$@,$(if $(filter true,$(BOARD_EXCLUDE_KERNEL_FROM_RECOVERY_IMAGE)),,$(recovery_kernel)))
MAKE

build_image() {
    local task_config=$1 task_name=$2
    URE_MKBOOTIMG_ARGUMENT_LOG="$task_work/$task_name.arguments" \
    URE_MKBOOTIMG_REAL="$task_mkbootimg" \
    timeout 30 make --no-print-directory -s -f "$task_work/fixture.mk" \
        BOARD_CONFIG="$task_config" \
        MKBOOTIMG="$task_work/mkbootimg-arguments.sh" \
        ARGS_MAKEFILE="$task_work/recovery-args.mk" \
        RECIPE_MAKEFILE="$task_work/recovery-recipe.mk" \
        RAMDISK="$task_work/ramdisk" OUTPUT="$task_work/$task_name.img"
}
u32() { od -An -tu4 -j "$2" -N4 "$1" | tr -d '[:space:]'; }
verify_image() {
    local task_name=$1 task_image="$task_work/$1.img" task_argument
    local task_count=0 task_cmdline= task_next=false
    local -a task_arguments=()
    mapfile -d '' -t task_arguments < "$task_work/$task_name.arguments"
    for task_argument in "${task_arguments[@]}"; do
        if $task_next; then task_cmdline=$task_argument; task_next=false; fi
        if [[ $task_argument == --cmdline ]]; then
            task_count=$((task_count + 1)); task_next=true
        fi
    done
    [[ $(dd if="$task_image" bs=1 count=8 status=none) == 'ANDROID!' &&
       $(u32 "$task_image" 8) == 0 &&
       $(u32 "$task_image" 12) == "$(stat -c %s "$task_work/ramdisk")" &&
       $(u32 "$task_image" 40) == 4 &&
       $(u32 "$task_image" 16) == "$(((13 << 25) | (2 << 18) | (1 << 11) | (26 << 4) | 10))" ]] || {
        echo 'FAIL: kernel-less v4 header or common mkbootimg arguments changed'; return 1;
    }
    [[ $task_count == 1 && $task_next == false && $task_cmdline == "$task_expected" ]] || {
        echo 'FAIL: paired recovery safeguards absent from the actual mkbootimg arguments'; return 1;
    }
    [[ $(dd if="$task_image" bs=1 skip=44 count=1536 status=none | tr -d '\000') == "$task_expected" ]] || {
        echo 'FAIL: recovery safeguards were not serialized into the version-4 header'; return 1;
    }
}

build_image "$task_work/BoardConfig.mk" candidate
verify_image candidate
for task_mutation in missing-pair missing-rdinit missing-blacklist kernel-included; do
    cp "$task_work/BoardConfig.mk" "$task_work/$task_mutation.mk"
    case $task_mutation in
        missing-pair) task_value='$(BOARD_MKBOOTIMG_ARGS)';;
        missing-rdinit) task_value='$(BOARD_MKBOOTIMG_ARGS) --cmdline "module_blacklist=charger_partition,ufs_ffu"';;
        missing-blacklist) task_value='$(BOARD_MKBOOTIMG_ARGS) --cmdline "rdinit=/system/bin/init"';;
        kernel-included) task_value='$(BOARD_RECOVERY_MKBOOTIMG_ARGS)';;
    esac
    if [[ $task_mutation == kernel-included ]]; then
        printf '\nBOARD_EXCLUDE_KERNEL_FROM_RECOVERY_IMAGE := false\n' >> "$task_work/$task_mutation.mk"
    else
        printf '\nBOARD_RECOVERY_MKBOOTIMG_ARGS = %s\n' "$task_value" >> "$task_work/$task_mutation.mk"
    fi
    build_image "$task_work/$task_mutation.mk" "$task_mutation"
    if verify_image "$task_mutation" > "$task_work/$task_mutation.log" 2>&1; then
        echo "Recovery command-line control accepted $task_mutation" >&2; exit 1
    fi
    if [[ $task_mutation == kernel-included ]]; then
        rg -q '^FAIL: kernel-less v4 header or common mkbootimg arguments changed' "$task_work/$task_mutation.log"
    else
        rg -q '^FAIL: paired recovery safeguards absent' "$task_work/$task_mutation.log"
    fi
done
sha256sum "$task_make" "$task_board" "$task_mkbootimg" "${BASH_SOURCE[0]}" \
    "$task_work/recovery-args.mk" "$task_work/recovery-recipe.mk" > "$task_work/inputs.sha256"
sha256sum "$task_work/candidate.img" "$task_work/candidate.arguments" > "$task_work/outputs.sha256"
jq -n --arg inputs "$(sha256sum "$task_work/inputs.sha256" | cut -d' ' -f1)" \
    --arg outputs "$(sha256sum "$task_work/outputs.sha256" | cut -d' ' -f1)" \
    '{schema_version:1,passed:true,evidence_class:"host-aosp-recovery-header",
      inputs_sha256:$inputs,outputs_sha256:$outputs,header_version:4,kernel_in_image:false,
      recovery_only_arguments:true,common_arguments_preserved:true,late_common_arguments_preserved:true,
      actual_make_recipe:true,actual_mkbootimg_serialization:true,negative_controls_rejected:4,
      physical_device:false,bootloader_argument_honoring:false,kernel_enforcement:false}' \
    > "$task_component/reports/private/recovery-first-stage-cmdline-verification.json"
printf '%s\n' 'PASS: actual recovery Make recipe preserves paired safeguards and common arguments in a kernel-less v4 header; all four mutants rejected. Physical enforcement remains untested.'
