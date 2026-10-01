#!/usr/bin/env bash
# Offline artifact/header and staged payload inventory. Does not run target code.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$component/src/upstream/orangefox-android16/out-public"
product="$out/target/product/uke"
payload="$product/recovery/root"
candidate=${1:-prerelease}
[[ $candidate =~ ^[a-zA-Z0-9][a-zA-Z0-9._-]{0,63}$ ]]
destination="$component/artifacts/$candidate"
recovery="$destination/OrangeFox-uke-recovery.img"
temporary="$destination/OrangeFox-uke-fastboot-boot.img"
repeat_record="$component/reports/private/$candidate-first-package.sha256"
if [[ $candidate == prerelease ]]; then repeat_record="$component/reports/private/first-package.sha256"; fi
[[ -s $repeat_record ]] || {
    echo 'Run and record the package-repeat check before sealing the manifest' >&2; exit 1;
}
(cd -- "$destination" && sha256sum -c "$repeat_record" >/dev/null)
[[ -s $destination/EXTRACTED-RAMDISK-AUDIT.json && -s $component/reports/private/native-verification.json ]]
jq -e '.validation.cpp_storage_ownership_policy_fixtures and .validation.storage_image_backup_cli_fixtures and .validation.cpp_raw_restore_interruption_fixtures and .validation.raw_restore_cli_fixtures and .validation.cpp_host_stream_restore_fixtures and .validation.host_stream_restore_cli_and_duplex_transport_fixtures and .validation.cpp_stock_gpt_reconstruction_and_oem_xml_oracle and .validation.stock_gpt_cli_fixtures and .validation.cpp_partition_map_and_bounded_signatures and .validation.partition_map_cli_fixtures and (.validation.physical_device==false)' \
    "$component/reports/private/native-verification.json" >/dev/null
jq -e '.validation.cpp_tablet_display_density_and_actual_renderer_hooks and .validation.display_cli_settings_fixtures' \
    "$component/reports/private/native-verification.json" >/dev/null
jq -e '.validation.cpp_external_display_fake_drm_and_edid and .validation.cpp_hardware_keyboard_actual_routing and .validation.cpp_evdev_actual_hotplug' \
    "$component/reports/private/native-verification.json" >/dev/null
jq -e --arg image "$(sha256sum "$recovery" | cut -d' ' -f1)" '.recovery_image_sha256==$image and .validation.extracted_ramdisk and .validation.no_python_payload and .validation.elf_dependency_closure and .validation.qemu_user_fixtures' "$destination/EXTRACTED-RAMDISK-AUDIT.json" >/dev/null
jq -e '.validation.source_built_mirror_renderer_and_exports' "$destination/EXTRACTED-RAMDISK-AUDIT.json" >/dev/null
jq -e --arg runner "$(sha256sum "$component/tests/check-aarch64.sh" | cut -d' ' -f1)" --arg auditor "$(sha256sum "$component/scripts/audit-recovery-image.sh" | cut -d' ' -f1)" '.aarch64_runner_sha256==$runner and .auditor_sha256==$auditor' "$destination/EXTRACTED-RAMDISK-AUDIT.json" >/dev/null
cmp <(bash "$component/scripts/native-inputs.sh") "$component/reports/private/native-test-inputs.sha256"
cp -- "$component/reports/private/native-verification.json" "$destination/NATIVE-HOST-VERIFICATION.json"
cp -- "$component/reports/private/native-test-inputs.sha256" "$destination/NATIVE-TEST-INPUTS.sha256"
for archive in STOCK-GKI-SOURCE.tar.gz RECOVERY-UTILITY-SOURCES.tar.gz; do [[ -s $destination/$archive ]]; done
(cd "$component" && find .gitattributes src/device src/installer src/inventory configs patches manifests scripts tests -type f -print0 | sort -z | xargs -0 sha256sum) > "$destination/PROJECT-INPUTS.sha256"
[[ $(stat -c %s "$recovery") == 104857600 && $(stat -c %s "$temporary") == 100663296 ]]
kernel_bytes=$(od -An -tu4 -j8 -N4 "$temporary" | tr -d ' ')
ramdisk_bytes=$(od -An -tu4 -j12 -N4 "$recovery" | tr -d ' ')
[[ $kernel_bytes == 35432960 && $ramdisk_bytes -gt 0 ]]
kernel_hash=$(dd if="$temporary" bs=1M iflag=skip_bytes,count_bytes skip=4096 count="$kernel_bytes" status=none | sha256sum | cut -d' ' -f1)
[[ $kernel_hash == $(jq -r .kernel_sha256 "$component/manifests/stock-kernel-source.json") ]]
offset=$((4096+(kernel_bytes+4095)/4096*4096))
ramdisk_hash=$(dd if="$recovery" bs=1M iflag=skip_bytes,count_bytes skip=4096 count="$ramdisk_bytes" status=none | sha256sum | cut -d' ' -f1)
temporary_ramdisk_hash=$(dd if="$temporary" bs=1M iflag=skip_bytes,count_bytes skip="$offset" count="$ramdisk_bytes" status=none | sha256sum | cut -d' ' -f1)
[[ $ramdisk_hash == "$temporary_ramdisk_hash" ]]
[[ $(unzip -p "$destination/OrangeFox-uke-flashable.zip" recovery.img | sha256sum | cut -d' ' -f1) == $(sha256sum "$recovery" | cut -d' ' -f1) ]]
"$out/host/linux-x86/bin/avbtool" info_image --image "$recovery" > "$destination/RECOVERY-AVB.txt"
"$out/host/linux-x86/bin/avbtool" info_image --image "$temporary" > "$destination/TEMPORARY-BOOT-AVB.txt"
inventory="$destination/PAYLOAD-FILES.tsv"
printf 'path\tkind\tbytes\tsha256-or-target\n' > "$inventory"
while IFS= read -r -d '' file; do
    relative=${file#"$payload"/}
    if [[ -L $file ]]; then
        printf '%s\tsymlink\t-\t%s\n' "$relative" "$(readlink -- "$file")" >> "$inventory"
    else
        printf '%s\tfile\t%s\t%s\n' "$relative" "$(stat -c %s "$file")" "$(sha256sum "$file" | cut -d' ' -f1)" >> "$inventory"
    fi
done < <(find "$payload" \( -type f -o -type l \) -print0 | sort -z)
jq -n --arg commit "$(git -C "$component" rev-parse HEAD)" \
    --arg tree "$(git -C "$component" rev-parse HEAD^{tree})" \
    --argjson changed "$([[ -n $(git -C "$component" status --porcelain) ]] && echo true || echo false)" \
    --arg inputs "$(sha256sum "$destination/PROJECT-INPUTS.sha256" | cut -d' ' -f1)" \
    --slurpfile audit "$destination/EXTRACTED-RAMDISK-AUDIT.json" \
    --slurpfile fixtures "$destination/NATIVE-HOST-VERIFICATION.json" \
    --slurpfile tools "$destination/URE-TOOLS.json" \
    --arg mkbootimg "$(sha256sum "$out/host/linux-x86/bin/mkbootimg" | cut -d' ' -f1)" \
    --arg avbtool "$(sha256sum "$out/host/linux-x86/bin/avbtool" | cut -d' ' -f1)" \
    --arg mkbootimg_commit "$(git -C "$component/src/upstream/orangefox-android16/system/tools/mkbootimg" rev-parse HEAD)" \
    --arg avbtool_commit "$(git -C "$component/src/upstream/orangefox-android16/external/avb" rev-parse HEAD)" \
    --arg kernel "$kernel_hash" --arg ramdisk "$ramdisk_hash" --argjson ramdisk_bytes "$ramdisk_bytes" \
    '{schema_version:2,classification:"experimental-native-candidate",device:"uke",model_targets:["POCO Pad X1","Xiaomi Pad 7"],firmware_profile:"global-os3.0.303.0",firmware_version:"OS3.0.303.0.WOZMIXM",project_source:{base_commit:$commit,base_tree:$tree,worktree_changes:$changed,input_manifest:"PROJECT-INPUTS.sha256",input_manifest_sha256:$inputs},stock_kernel_sha256:$kernel,recovery_ramdisk:{bytes:$ramdisk_bytes,sha256:$ramdisk},host_tools:{mkbootimg:{source_commit:$mkbootimg_commit,executable_sha256:$mkbootimg},avbtool:{source_commit:$avbtool_commit,executable_sha256:$avbtool}},tools:$tools[0],ramdisk_audit:$audit[0],host_fixture_record:$fixtures[0],validation:{compile:true,header_sections:true,zip_integrity:true,static_installer:true,host_policy_fixtures:true,payload_privacy:true,no_python_payload:true,source_identification:true,package_repeat:true,binary_reproducibility:false,physical_device:false,gui_rendering:false,rollback_rehearsal:false,complete_roadmap:false},signatures:{avb:"NONE",zip:"unsigned",checksum:"SHA256SUMS"},source_snapshots:["STOCK-GKI-SOURCE.tar.gz","RECOVERY-UTILITY-SOURCES.tar.gz"]}' \
    > "$destination/ARTIFACT-MANIFEST.json"
# Hash every generated public release file once, including source snapshots.
(cd -- "$destination" && find . -maxdepth 1 -type f ! -name SHA256SUMS -printf '%f\0' | sort -z | xargs -0 sha256sum > SHA256SUMS)
echo 'Header sections, kernel identity, shared ramdisk and ZIP payload agree; release manifest generated.'
