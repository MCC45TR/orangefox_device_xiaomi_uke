#!/usr/bin/env bash
# Verify private assembly boundaries and optionally inspect a genuine prototype.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" native bash "${BASH_SOURCE[0]}" "$@"
fi
script="$component/scripts/prepare-independent-recovery.sh"
bash -n "$script"
work=$(mktemp -d "$component/build/independent-recovery-tests-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
expect_refusal() { if bash "$script" "$@" > "$work/refusal.log" 2>&1; then echo 'Unsafe prototype input accepted.' >&2; exit 1; fi; }
expect_refusal "$work/missing.img" test-missing-input
expect_refusal /dev/null test-device-input
expect_refusal "$work/missing.img" /dev/null
for mutation in digest size header; do
    truncate -s 4096 "$work/$mutation.img"
    case $mutation in
        digest) printf 'ANDROID!' | dd of="$work/$mutation.img" conv=notrunc status=none;;
        size) truncate -s 104857601 "$work/$mutation.img";;
        header) printf '\377\377\377\377' | dd of="$work/$mutation.img" bs=1 seek=40 conv=notrunc status=none;;
    esac
    expect_refusal "$work/$mutation.img" "test-$mutation-input"
done
ln -s "$work/digest.img" "$work/image-link"
expect_refusal "$work/image-link" test-symlink-input
if [[ $# == 1 ]]; then
    [[ -d $1 && ! -L $1 ]]
    prototype=$(realpath "$1")
    report="$prototype/PROTOTYPE.json"
    image="$prototype/recovery-v2.img"
    jq -e '.evidence_class=="private-unqualified-recovery-prototype" and .header_version==2 and
        .source_inputs_unchanged and .payload_audit and .elf_closure and
        .raw_kernel_bytes_preserved and (.reference_compressed_kernel_path|not) and (.validation.installable_release|not) and
        (.validation.physical_acceptance|not)' "$report" >/dev/null
    [[ $(sha256sum "$image" | cut -d' ' -f1) == $(jq -r .image_sha256 "$report") && $(stat -c %s "$image") == 104857600 ]]
    cmp "$prototype/INPUTS-BEFORE.sha256" "$prototype/INPUTS-AFTER.sha256"
    u32() { od -An -tu4 --endian=little -j"$1" -N4 "$image" | tr -d ' \n'; }
    align() { echo $(( ($1+4095)/4096*4096 )); }
    kernel_bytes=$(u32 8); ramdisk_bytes=$(u32 16)
    dtbo_bytes=$(u32 1632); dtb_bytes=$(u32 1648)
    ramdisk_offset=$((4096+$(align "$kernel_bytes")))
    dtbo_offset=$((ramdisk_offset+$(align "$ramdisk_bytes")))
    dtb_offset=$((dtbo_offset+$(align "$dtbo_bytes")))
    [[ $(u32 36) == 4096 && $(u32 40) == 2 && $(u32 1644) == 1660 &&
       $(od -An -tu8 --endian=little -j1636 -N8 "$image" | tr -d ' \n') == "$dtbo_offset" ]]
    [[ $(u32 12) == 32768 && $(u32 20) == 16777216 && $(u32 32) == 256 &&
       $(od -An -tu8 --endian=little -j1652 -N8 "$image" | tr -d ' \n') == 32505856 ]]
    cmdline=$({ dd if="$image" bs=1 skip=64 count=512 status=none;
        dd if="$image" bs=1 skip=608 count=1024 status=none; } | tr -d '\000')
    [[ $cmdline == "$(cat "$prototype/CMDLINE.txt")" &&
       $cmdline == *'rdinit=/system/bin/init'* && $cmdline == *'androidboot.force_normal_boot=0'* &&
       $cmdline == *'module_blacklist=charger_partition,ufs_ffu'* ]]
    for section in kernel ramdisk dtbo dtb; do
        case $section in
            kernel) offset=4096; bytes=$kernel_bytes; file=kernel;;
            ramdisk) offset=$ramdisk_offset; bytes=$ramdisk_bytes; file=ramdisk.zst;;
            dtbo) offset=$dtbo_offset; bytes=$dtbo_bytes; file=recovery-dtbo.img;;
            dtb) offset=$dtb_offset; bytes=$dtb_bytes; file=dtb;;
        esac
        [[ $bytes == "$(stat -c %s "$prototype/$file")" ]]
        dd if="$image" bs=1M iflag=skip_bytes,count_bytes skip="$offset" count="$bytes" status=none | cmp "$prototype/$file" -
    done
    avb="$component/src/upstream/orangefox-android16/out-public/host/linux-x86/bin/avbtool"
    "$avb" info_image --image "$image" > "$work/avb.txt"
    rg -q '^Algorithm:.*NONE$' "$work/avb.txt"
    sha256sum "$prototype/kernel" > "$work/kernel.sha256"
    [[ $(cut -d' ' -f1 "$work/kernel.sha256") == 97b2c53022e8c0bd0b279c4c592a196126a6eb0a93647c2957b39dcd5f2dc1c9 ]]
    cat "$prototype/vendor.cpio" "$prototype/recovery.cpio" > "$work/expected.cpio"
    zstd -q -dc "$prototype/ramdisk.zst" | cmp "$work/expected.cpio" -
    rg -x 'CONFIG_RD_ZSTD=y' "$prototype/kernel.config" >/dev/null
    bash "$component/scripts/check-recovery-boot-chain.sh" "$image" "$work/boot-chain.json"
    jq -e '.structural_candidate and .header_version==2' "$work/boot-chain.json" >/dev/null
elif [[ $# != 0 ]]; then
    echo 'Usage: check-independent-recovery.sh [PRIVATE_PROTOTYPE_DIRECTORY]' >&2; exit 2
fi
echo 'Independent recovery prototype input boundaries passed; no device operation was performed.'
