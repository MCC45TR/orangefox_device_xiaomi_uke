#!/usr/bin/env bash
# Offline artifact/header and staged payload inventory. Does not run target code.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$component/src/upstream/orangefox-android16/out-public"
product="$out/target/product/uke"
payload="$product/recovery/root"
destination="$component/artifacts/prerelease"
recovery="$destination/OrangeFox-uke-recovery.img"
temporary="$destination/OrangeFox-uke-fastboot-boot.img"
[[ -s $component/reports/private/first-package.sha256 ]] || {
    echo 'Run and record the package-repeat check before sealing the manifest' >&2; exit 1;
}
sha256sum -c "$component/reports/private/first-package.sha256" >/dev/null
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
    --arg mkbootimg "$(sha256sum "$out/host/linux-x86/bin/mkbootimg" | cut -d' ' -f1)" \
    --arg avbtool "$(sha256sum "$out/host/linux-x86/bin/avbtool" | cut -d' ' -f1)" \
    --arg mkbootimg_commit "$(git -C "$component/src/upstream/orangefox-android16/system/tools/mkbootimg" rev-parse HEAD)" \
    --arg avbtool_commit "$(git -C "$component/src/upstream/orangefox-android16/external/avb" rev-parse HEAD)" \
    --arg kernel "$kernel_hash" --arg ramdisk "$ramdisk_hash" --argjson ramdisk_bytes "$ramdisk_bytes" \
    '{schema_version:1,classification:"experimental-untested-prerelease",device:"uke",model_targets:["POCO Pad X1","Xiaomi Pad 7"],firmware_profile:"global-os3.0.303.0",project_commit:$commit,project_tree:$tree,stock_kernel_sha256:$kernel,recovery_ramdisk:{bytes:$ramdisk_bytes,sha256:$ramdisk},host_tools:{mkbootimg:{source_commit:$mkbootimg_commit,executable_sha256:$mkbootimg},avbtool:{source_commit:$avbtool_commit,executable_sha256:$avbtool}},validation:{compile:true,header_sections:true,zip_integrity:true,static_installer:true,host_policy_fixtures:true,payload_privacy:true,no_python_payload:true,source_identification:true,package_repeat:true,binary_reproducibility:false,physical_device:false,rollback_rehearsal:false},signatures:{avb:"NONE",zip:"unsigned",checksum:"SHA256SUMS"},source_snapshots:["STOCK-GKI-SOURCE.tar.gz","RECOVERY-UTILITY-SOURCES.tar.gz"]}' \
    > "$destination/ARTIFACT-MANIFEST.json"
# Hash every generated public release file once, including source snapshots.
(cd -- "$destination" && find . -maxdepth 1 -type f ! -name SHA256SUMS -printf '%f\0' | sort -z | xargs -0 sha256sum > SHA256SUMS)
echo 'Header sections, kernel identity, shared ramdisk and ZIP payload agree; release manifest generated.'
