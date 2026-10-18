#!/usr/bin/env bash
# Assemble a private, unqualified v2 experiment; never install or publish it.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" native bash "${BASH_SOURCE[0]}" "$@"
fi
[[ $# == 2 && $2 =~ ^[a-zA-Z0-9][a-zA-Z0-9._-]{0,63}$ ]] || {
    echo 'Usage: prepare-independent-recovery.sh PINNED_RECOVERY_IMAGE NEW_PRIVATE_NAME' >&2; exit 2;
}
[[ -f $1 && ! -L $1 ]] || { echo 'A regular recovery image is required.' >&2; exit 1; }
recovery=$(realpath -- "$1")
destination="$component/reports/private/independent-recovery/$2"
[[ ! -e $destination && ! -L $destination ]] || { echo 'Private destination already exists.' >&2; exit 1; }
product="$component/src/upstream/orangefox-android16/out-public"
kernel="$component/build/stock-global/boot/kernel"
vendor="$component/build/stock-global/vendor_boot"
stock="$component/referances/firmware/global/derived/uke_global_images_OS3.0.303.0.WOZMIXM_16.0/images"
catalog="$component/manifests/stock-payloads-global.json"
lz4="$product/host/linux-x86/bin/lz4"
mkboot="$product/host/linux-x86/bin/mkbootimg"
avb="$product/host/linux-x86/bin/avbtool"
for tool in jq od dd sha256sum stat gzip cpio bwrap strings readelf truncate rg; do command -v "$tool" >/dev/null; done
pin() {
    [[ -f $1 && ! -L $1 && $(sha256sum "$1" | cut -d' ' -f1) == "$2" ]] || {
        printf 'Pinned input rejected: %s\n' "${1##*/}" >&2; exit 1;
    }
}
pin "$recovery" 67ffc810ee43cf7028129a29e2950bf08da3a55f028043c1f29163313c15c7fc
pin "$kernel" 97b2c53022e8c0bd0b279c4c592a196126a6eb0a93647c2957b39dcd5f2dc1c9
pin "$vendor/vendor_ramdisk00" b2fd70d7001656d729eeb58a785d9804bf696f3fcfe8f8e5e67a0071949483b3
pin "$vendor/dtb" 9ed69a768852348a9af1e32e94ebbfcfb439b95ff8aac6fd319326fe7ca72454
pin "$vendor/bootconfig" 9cd2e774b149c87ac3e7939841475bd9b78a560a36d91682ba84d3afcd1d1310
pin "$stock/vendor_boot.img" c2811677d6aa07753b615747c4f2dba110dd4519cf52ff3a89e41e00a5b02bcc
pin "$stock/dtbo.img" 044aae9d9a144e9a05b91d2785a2ff4504c78caa11f8c22781839ba2f6c76490
for name in vendor_boot dtbo; do
    jq -e --arg name "$name.img" --arg digest "$(sha256sum "$stock/$name.img" | cut -d' ' -f1)" \
        '.firmware_profile=="global-os3.0.303.0" and any(.payloads[]; .filename==$name and .source_sha256==$digest)' "$catalog" >/dev/null
done
for tool in "$lz4" "$mkboot" "$avb"; do [[ -f $tool && ! -L $tool && -x $tool ]]; done
u32() { od -An -tu4 --endian=little -j"$2" -N4 "$1" | tr -d ' \n'; }
be32() { od -An -tu4 --endian=big -j"$2" -N4 "$1" | tr -d ' \n'; }
[[ $(stat -c %s "$recovery") == 104857600 && $(u32 "$recovery" 8) == 0 && $(u32 "$recovery" 40) == 4 ]]
ramdisk_bytes=$(u32 "$recovery" 12)
[[ $ramdisk_bytes -gt 0 && $ramdisk_bytes -le 65000000 ]]
dtbo_bytes=$(be32 "$stock/dtbo.img" 4)
[[ $(be32 "$stock/dtbo.img" 0) == 3619138334 && $dtbo_bytes == 592007 &&
   $(be32 "$stock/dtbo.img" 8) == 32 && $(be32 "$stock/dtbo.img" 12) == 32 &&
   $(be32 "$stock/dtbo.img" 16) == 1 && $(be32 "$stock/dtbo.img" 20) == 32 &&
   $(( $(be32 "$stock/dtbo.img" 32) + $(be32 "$stock/dtbo.img" 36) )) == "$dtbo_bytes" ]]
mkdir -p -- "${destination%/*}"
[[ ! -L ${destination%/*} ]]
mkdir -- "$destination"
inputs=("$recovery" "$kernel" "$vendor/vendor_ramdisk00" "$vendor/dtb" "$vendor/bootconfig" \
    "$stock/vendor_boot.img" "$stock/dtbo.img" "$catalog" "$lz4" "$mkboot" "$avb" "${BASH_SOURCE[0]}")
sha256sum "${inputs[@]}" > "$destination/INPUTS-BEFORE.sha256"
trap 'status=$?; if ((status)); then printf "Private prototype incomplete; see retained evidence. No device was accessed.\n" >&2; fi' EXIT
dd if="$recovery" of="$destination/recovery.lz4" bs=1M iflag=skip_bytes,count_bytes skip=4096 count="$ramdisk_bytes" status=none
"$lz4" -dc "$vendor/vendor_ramdisk00" > "$destination/vendor.cpio"
"$lz4" -dc "$destination/recovery.lz4" > "$destination/recovery.cpio"
mkdir "$destination/root"
for name in vendor recovery; do
    archive="$destination/$name.cpio"
    [[ $(stat -c %s "$archive") -le 268435456 ]]
    cpio -it --quiet < "$archive" > "$destination/$name.entries"
    [[ $(wc -l < "$destination/$name.entries") -le 20000 ]]
    while IFS= read -r entry; do
        case "$entry" in /*|../*|*/../*|*/..|*\\*|*$'\r'*) echo 'Unsafe CPIO member rejected.' >&2; exit 1;; esac
    done < "$destination/$name.entries"
    LC_ALL=C cpio -itv --quiet < "$archive" | awk 'substr($0,1,1)!~/[-dl]/ {exit 1}'
    # Separate extraction also checks the second archive, which GNU cpio would skip after the first trailer.
    bwrap --unshare-all --die-with-parent --ro-bind / / --dev /dev --tmpfs /tmp \
        --bind "$destination/root" /mnt --chdir /mnt \
        cpio -idmu --quiet --no-absolute-filenames < "$archive"
done
root="$destination/root"
[[ $(readlink "$root/init") == /system/bin/init && -f $root/system/bin/init && ! -L $root/system/bin/init ]]
pin "$root/lib/modules/modules.blocklist" 97df133c683035085dfbedea04d34a4771211466acd7b51449572a4a0dc88b06
for blocked in charger_partition ufs_ffu; do
    rg -x "blocklist $blocked" "$root/lib/modules/modules.blocklist" >/dev/null
    strings -a "$root/system/bin/init" | rg -F "$blocked" >/dev/null
done
bash "$component/tests/check-payload.sh" "$root" > "$destination/payload-audit.log"
bash "$component/tests/check-nested-payloads.sh" "$root" > "$destination/nested-payload-audit.log"
bash "$component/tests/check-elf-closure.sh" "$root" > "$destination/elf-closure.log"
# Preserve both original newc streams. Linux initramfs accepts concatenated archives and recovery overlays vendor last.
cat "$destination/vendor.cpio" "$destination/recovery.cpio" | gzip -9 -n -c > "$destination/ramdisk.gz"
gzip -9 -n -c "$kernel" > "$destination/kernel.gz"
cp -- "$vendor/dtb" "$destination/dtb"
dd if="$stock/dtbo.img" of="$destination/recovery-dtbo.img" bs=1M iflag=count_bytes count="$dtbo_bytes" status=none
vendor_cmdline=$(dd if="$stock/vendor_boot.img" bs=1 skip=28 count=2048 status=none | tr -d '\000')
bootconfig=$(tr '\n' ' ' < "$vendor/bootconfig")
cmdline="$vendor_cmdline $bootconfig rdinit=/system/bin/init androidboot.force_normal_boot=0 module_blacklist=charger_partition,ufs_ffu"
[[ ${#cmdline} -lt 1536 && $cmdline != *$'\n'* ]]
printf '%s\n' "$cmdline" > "$destination/CMDLINE.txt"
# Header addresses are the pinned Uke vendor header values; the reference ABL uses its dynamic UEFI allocation.
"$mkboot" --header_version 2 --pagesize 4096 --base 0 \
    --kernel_offset "$(u32 "$stock/vendor_boot.img" 16)" \
    --ramdisk_offset "$(u32 "$stock/vendor_boot.img" 20)" \
    --tags_offset "$(u32 "$stock/vendor_boot.img" 2076)" \
    --dtb_offset "$(od -An -tu8 --endian=little -j2104 -N8 "$stock/vendor_boot.img" | tr -d ' \n')" \
    --kernel "$destination/kernel.gz" --ramdisk "$destination/ramdisk.gz" \
    --recovery_dtbo "$destination/recovery-dtbo.img" --dtb "$destination/dtb" \
    --os_version 16.0.0 --os_patch_level 2025-06 --cmdline "$cmdline" --output "$destination/recovery-v2.img"
align() { echo $(( ($1+4095)/4096*4096 )); }
kernel_gzip_bytes=$(stat -c %s "$destination/kernel.gz")
ramdisk_gzip_bytes=$(stat -c %s "$destination/ramdisk.gz")
dtb_bytes=$(stat -c %s "$destination/dtb")
# This exact reference parser counts the v2 DTB twice; retain padding before the AVB footer for that read extent.
abl_extent=$((4096+$(align "$kernel_gzip_bytes")+$(align "$ramdisk_gzip_bytes")+$(align "$dtbo_bytes")+2*$(align "$dtb_bytes")))
[[ $abl_extent -le $("$avb" add_hash_footer --partition_size 104857600 --calc_max_image_size) ]]
truncate -s "$abl_extent" "$destination/recovery-v2.img"
"$avb" add_hash_footer --image "$destination/recovery-v2.img" --partition_name recovery \
    --partition_size 104857600 --algorithm NONE --salt 97b2c53022e8c0bd0b279c4c592a196126a6eb0a93647c2957b39dcd5f2dc1c9
sha256sum "${inputs[@]}" > "$destination/INPUTS-AFTER.sha256"
cmp "$destination/INPUTS-BEFORE.sha256" "$destination/INPUTS-AFTER.sha256"
jq -n --arg source "$(sha256sum "$recovery" | cut -d' ' -f1)" \
    --arg image "$(sha256sum "$destination/recovery-v2.img" | cut -d' ' -f1)" \
    --argjson kernel_bytes "$kernel_gzip_bytes" --argjson ramdisk_bytes "$ramdisk_gzip_bytes" \
    --argjson dtb_bytes "$dtb_bytes" --argjson dtbo_bytes "$dtbo_bytes" --argjson abl_extent "$abl_extent" \
    '{schema_version:1,date:"2026-10-18",evidence_class:"private-unqualified-recovery-prototype",source_recovery_sha256:$source,
      image_sha256:$image,header_version:2,partition_bytes:104857600,reference_abl_read_extent:$abl_extent,
      firmware_profile:"global-os3.0.303.0",kernel_release:"6.1.138-android14-11-g0c3d559bcd85-ab14529422",
      kernel_gzip_bytes:$kernel_bytes,ramdisk_gzip_bytes:$ramdisk_bytes,dtb_bytes:$dtb_bytes,dtbo_bytes:$dtbo_bytes,
      ramdisk_archive_order:["stock-vendor","pinned-recovery"],avb_algorithm:"NONE",source_inputs_unchanged:true,
      payload_audit:true,elf_closure:true,blocked_modules:["charger_partition","ufs_ffu"],
      kernel_load_address_equivalence:false,reference_gzip_kernel_offset:524288,raw_kernel_text_offset:0,
      existing_touch_kernel_requirement:"6.1.175-android14-11-ga3b9c44908dd-ab13320413",
      validation:{oem_abl_binary_match:false,boot_image_independence:false,kernel_archive_execution:false,
        physical_acceptance:false,physical_touch:false,both_slots_linux:false,installable_release:false}}' > "$destination/PROTOTYPE.json"
printf 'Private v2 prototype assembled; boot, touch and installation acceptance remain unavailable.\n'
