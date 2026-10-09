#!/usr/bin/env bash
# Host-only generic ARM64 formatter handoff test. No host block device is opened or attached.
set -euo pipefail
umask 077
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
runner=$(realpath -e -- "${BASH_SOURCE[0]}")
component=$(cd -- "$here/.." && pwd -P)
fixture="$here/vm/formatter-handoff.cpp"
module_stager="$here/vm/strip-debug-module.sh"
header="$component/src/device/xiaomi/uke/recoveryctl/libuke/dualboot_view_handoff.hpp"
[[ $# == 3 || $# == 4 ]] || {
    echo 'Usage: check-formatter-handoff-vm.sh GENERIC_KERNEL DM_MODULE PAYLOAD_ROOT [SEALED_RECOVERY_IMAGE]' >&2
    exit 1
}
kernel=$1
module=$2
payload=$3
image=${4:-}
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" vm /usr/bin/env \
        "UKE_QEMU_SYSTEM_AARCH64=${UKE_QEMU_SYSTEM_AARCH64:-$(type -P qemu-system-aarch64 || true)}" bash "$runner" "$@"
fi
for command in cpio gzip jq sha256sum timeout readelf rg find sort cp realpath awk sed tail cmp install truncate; do command -v "$command" >/dev/null; done
qemu=${UKE_QEMU_SYSTEM_AARCH64:-$(type -P qemu-system-aarch64 || true)}
[[ -f $kernel && ! -L $kernel && -f $module && ! -L $module && -d $payload && ! -L $payload &&
   -f $header && ! -L $header && -x $qemu && -f $fixture && ! -L $fixture &&
   -f $module_stager && ! -L $module_stager ]]
if [[ -n $image ]]; then [[ -f $image && ! -L $image ]]; fi
tree="$component/src/upstream/orangefox-android16"
compiler="$tree/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
[[ $(sha256sum "$compiler" | cut -d' ' -f1) == 55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f ]]
crt="$tree/out-public/soong/.intermediates/bionic/libc"
begin="$crt/crtbegin_dynamic/android_arm64_armv8-a/crtbegin_dynamic.o"
end="$crt/crtend_android/android_arm64_armv8-a/crtend_android.o"
libc="$payload/system/lib64/libc.so"
[[ -f $begin && -f $end && -f $libc ]]
mkdir -p "$component/build/formatter-handoff-vm" "$component/reports/private"
job=$(mktemp -d "$component/build/formatter-handoff-vm/job-XXXXXXXX")
printf '%s\n' "$job" > "$component/build/formatter-handoff-vm/latest-job"
cp -- "$header" "$job/dualboot_view_handoff.hpp"
cp -- "$fixture" "$job/fixture.cpp"
sha256sum -- "$runner" "$module_stager" "$fixture" "$header" "$kernel" "$module" "$compiler" "$begin" "$end" "$libc" > "$job/source-inputs-before.sha256"
if [[ -n $image ]]; then sha256sum -- "$image" >> "$job/source-inputs-before.sha256"; fi
cmp "$header" "$job/dualboot_view_handoff.hpp"
cmp "$fixture" "$job/fixture.cpp"
sealed=false
if [[ -n $image ]]; then
    bash "$component/scripts/build-evidence.sh" payload "$image" "$payload" "$job/compile-evidence.json" > "$job/packed-payload-gate.log" 2>&1
    sealed=true
fi
flags=(-target aarch64-linux-android10000 -nostdlibinc -std=c++20 -fno-exceptions -fno-rtti -fPIC -O2
    -D__ANDROID_RECOVERY__ -D__ANDROID_RAMDISK__ -DANDROID_STRICT
    -D__LIBC_API__=10000 -D__LIBM_API__=10000 -D__LIBDL_API__=10000
    -Wall -Wextra -Werror "-ffile-prefix-map=$component=/workspace/recovery" "-fdebug-prefix-map=$component=/workspace/recovery"
    "-I$job" -isystem "$tree/bionic/libc/include"
    -isystem "$tree/bionic/libc/kernel/uapi/asm-arm64" -isystem "$tree/bionic/libc/kernel/uapi"
    -isystem "$tree/bionic/libc/kernel/android/scsi" -isystem "$tree/bionic/libc/kernel/android/uapi")
printf '%s\n' "${flags[@]}" > "$job/compile-flags.txt"
"$compiler" "${flags[@]}" -M -MF "$job/fixture.pre.d" -MT "$job/fixture.o" "$job/fixture.cpp" > "$job/preprocess.log" 2>&1
awk '{for(i=1;i<=NF;i++)if($i!="\\" && $i!~/:$/)print $i}' "$job/fixture.pre.d" | LC_ALL=C sort -u > "$job/headers-before.list"
while IFS= read -r file; do [[ -f $file && ! -L $file ]]; sha256sum -- "$file"; done < "$job/headers-before.list" > "$job/headers-before.sha256"
"$compiler" "${flags[@]}" -MD -MF "$job/fixture.d" -c "$job/fixture.cpp" -o "$job/fixture.o" > "$job/compile.log" 2>&1
"$compiler" --target=aarch64-linux-android10000 -pie -nostdlib -Wl,--no-undefined \
    -Wl,-dynamic-linker,/system/bin/linker64 "$begin" "$job/fixture.o" "$libc" "$end" \
    -o "$job/ure-formatter-fixture" > "$job/link.log" 2>&1
readelf -h -d "$job/ure-formatter-fixture" > "$job/fixture-elf.txt"
rg -q 'Machine:.*AArch64' "$job/fixture-elf.txt"
[[ $(rg -c 'Shared library:' "$job/fixture-elf.txt") == 1 ]]
rg -q 'Shared library: \[libc.so\]' "$job/fixture-elf.txt"
# Preserve exact compiler header contents as well as the direct harness inputs.
awk '{for(i=1;i<=NF;i++)if($i!="\\" && $i!~/:$/)print $i}' "$job/fixture.d" | LC_ALL=C sort -u > "$job/headers.list"
while IFS= read -r file; do [[ -f $file && ! -L $file ]]; sha256sum -- "$file"; done < "$job/headers.list" > "$job/headers.sha256"
cmp "$job/headers-before.list" "$job/headers.list"
cmp "$job/headers-before.sha256" "$job/headers.sha256"
mkdir "$job/root"
cp -a -- "$payload/." "$job/root/"
install -Dm755 "$job/ure-formatter-fixture" "$job/root/system/bin/ure-formatter-fixture"
mkdir -p "$job/root/ure-vm-modules"
bash "$module_stager" "$component" "$module" "$job/root/ure-vm-modules/dm-mod.ko" "$job"
f2fs=make_f2fs
if [[ ! -f $payload/system/bin/$f2fs ]]; then f2fs=mkfs.f2fs; fi
for file in "$f2fs" fsck.f2fs mke2fs e2fsck mkfs.fat fsck.fat mkfs.ntfs fsck.ntfs toybox linker64; do
    [[ -f $payload/system/bin/$file ]]
    sha256sum -- "$payload/system/bin/$file" >> "$job/tools.sha256"
done
for file in btrfs mkfs.btrfs; do
    if [[ -f $payload/system/bin/$file ]]; then sha256sum -- "$payload/system/bin/$file" >> "$job/tools.sha256"; fi
done
for base in system/lib64 vendor/lib64; do
    if [[ -d $payload/$base ]]; then
        while IFS= read -r -d '' file; do sha256sum -- "$file"; done < <(find "$payload/$base" -type f -print0 | LC_ALL=C sort -z)
    fi
done > "$job/runtime-libraries.sha256"
for base in system/etc vendor/etc; do
    if [[ -d $payload/$base ]]; then
        while IFS= read -r -d '' file; do sha256sum -- "$file"; done < <(find "$payload/$base" -type f -print0 | LC_ALL=C sort -z)
    fi
done > "$job/runtime-configuration.sha256"
mkdir -p "$job/root/run" "$job/root/tmp" "$job/root/proc" "$job/root/sys" "$job/root/dev"
cat > "$job/root/ure-formatter-init" <<'GUEST'
#!/system/bin/sh
export PATH=/system/bin
export LD_LIBRARY_PATH=/system/lib64:/vendor/lib64:/system/lib64/bootstrap
export MKE2FS_CONFIG=/system/etc/mke2fs.conf
/system/bin/toybox mount -t proc proc /proc || exit 100
mount -t sysfs sysfs /sys || exit 101
mount -t devtmpfs devtmpfs /dev || exit 102
mount -t tmpfs tmpfs /run || exit 103
mount -t tmpfs tmpfs /tmp || exit 104
insmod /ure-vm-modules/dm-mod.ko || exit 105
/system/bin/ure-formatter-fixture
result=$?
echo "URE_FORMAT_FIXTURE_EXIT $result"
sync
echo o > /proc/sysrq-trigger
toybox reboot -p -f
exit "$result"
GUEST
chmod 755 "$job/root/ure-formatter-init"
bash "$component/tests/check-payload.sh" "$job/root" > "$job/payload-gate.log"
truncate -s 528M "$job/disposable-media.img"
[[ -f $job/disposable-media.img && ! -L $job/disposable-media.img ]]
(cd "$job/root" && find . -print0 | LC_ALL=C sort -z | cpio --null -o --format=newc --quiet | gzip -n -1) > "$job/initrd.cpio.gz"
"$qemu" --version > "$job/qemu-version.txt"
timeout 1200 "$qemu" -machine virt -cpu cortex-a72 -smp 2 -m 2048 -accel tcg \
    -display none -monitor none -serial stdio -no-reboot -nic none -no-user-config \
    -kernel "$kernel" -initrd "$job/initrd.cpio.gz" \
    -append 'console=ttyAMA0 rdinit=/ure-formatter-init panic=1 ure_formatter_fixture=1' \
    -drive "if=none,id=fixture,format=raw,file=$job/disposable-media.img,cache=none" \
    -device virtio-blk-device,drive=fixture,logical_block_size=4096,physical_block_size=4096 \
    > "$job/console.log" 2>&1
rg -q '^URE_FORMAT_FIXTURE_EXIT 0\r?$' "$job/console.log"
rg -q 'Error: In use by the system!' "$job/console.log"
for check in old-exclusive-real-tool-refused-byte-identical foreign-exclusive-claim-refused foreign-nonexclusive-open-count-refused \
    foreign-mapper-descriptor-refused mapper-UUID-mismatch-refused failed-formatter-stops-sequence \
    mounted-view-handoff-refused mounted-ext4-tool-refused-byte-identical mounted-ntfs-tool-refused-byte-identical \
    f2fs-format-readonly-checker-bounded-neighbors ext4-format-readonly-checker-bounded-neighbors \
    fat32-format-readonly-checker-bounded-neighbors ntfs-format-readonly-checker-bounded-neighbors; do
    rg -q "^URE_FORMAT_CHECK $check 0\r?$" "$job/console.log"
done
awk '/^URE_FORMAT_HANDOFF_RESULT / {sub(/^URE_FORMAT_HANDOFF_RESULT /, ""); sub(/\r$/, ""); print}' "$job/console.log" > "$job/guest-result.json"
jq -e '.passed==true and .logical_sector_bytes==4096 and .fat32_view_bytes==314572800 and .fat32_cluster_geometry_verified==true and
    .physical_device==false and .shipping_cli_positive_execute==false and
    .read_only_checker_sha256_unchanged==true and .gpt_writer_present==false and .production_quarantine_test==false' "$job/guest-result.json" >/dev/null
rg -q 'is mounted; will not make a filesystem here!' "$job/console.log"
rg -q 'Refusing to make a filesystem here!' "$job/console.log"
sha256sum -- "$runner" "$module_stager" "$fixture" "$header" "$kernel" "$module" "$compiler" "$begin" "$end" "$libc" > "$job/source-inputs-after.sha256"
if [[ -n $image ]]; then sha256sum -- "$image" >> "$job/source-inputs-after.sha256"; fi
cmp "$job/source-inputs-before.sha256" "$job/source-inputs-after.sha256"
sha256sum -c "$job/headers.sha256" > "$job/headers-repeat-check.log"
sha256sum -c "$job/tools.sha256" > "$job/tools-repeat-check.log"
sha256sum -c "$job/runtime-libraries.sha256" > "$job/runtime-repeat-check.log"
sha256sum -c "$job/runtime-configuration.sha256" > "$job/configuration-repeat-check.log"
image_sha=null
if [[ -n $image ]]; then image_sha=$(sha256sum "$image" | cut -d' ' -f1); fi
jq --arg header "$(sha256sum "$header" | cut -d' ' -f1)" --arg source "$(sha256sum "$fixture" | cut -d' ' -f1)" \
    --arg runner "$(sha256sum "$runner" | cut -d' ' -f1)" --arg elf "$(sha256sum "$job/ure-formatter-fixture" | cut -d' ' -f1)" \
    --arg kernel "$(sha256sum "$kernel" | cut -d' ' -f1)" --arg module "$(sha256sum "$module" | cut -d' ' -f1)" \
    --arg compiler "$(sha256sum "$compiler" | cut -d' ' -f1)" --arg inputs "$(sha256sum "$job/source-inputs-after.sha256" | cut -d' ' -f1)" \
    --arg tools "$(sha256sum "$job/tools.sha256" | cut -d' ' -f1)" --arg libraries "$(sha256sum "$job/runtime-libraries.sha256" | cut -d' ' -f1)" \
    --arg configuration "$(sha256sum "$job/runtime-configuration.sha256" | cut -d' ' -f1)" --arg image "$image_sha" \
    --arg console "$(sha256sum "$job/console.log" | cut -d' ' -f1)" --argjson sealed "$sealed" \
    '. + {handoff_header_sha256:$header,fixture_source_sha256:$source,runner_sha256:$runner,fixture_elf_sha256:$elf,
      kernel_sha256:$kernel,dm_mod_sha256:$module,compiler_sha256:$compiler,source_inputs_sha256:$inputs,
      tools_manifest_sha256:$tools,runtime_libraries_manifest_sha256:$libraries,runtime_configuration_manifest_sha256:$configuration,
      console_sha256:$console,recovery_image_sha256:(if $image=="null" then null else $image end),
      payload_matches_sealed_recovery_image:$sealed,shipping_kernel_test:false,host_block_attachment:false,network:false,
      mounted_view_handoff_refused:true,mounted_ext4_tool_refused_byte_identical:true,mounted_ntfs_tool_refused_byte_identical:true,
      interrupted_live_transaction_test:false,global_kernel_exclusion_claim:false}' "$job/guest-result.json" > "$job/verification.json"
cp -- "$job/verification.json" "$component/reports/private/formatter-handoff-vm-verification.json"
printf 'Disposable 4K DM formatter/checker handoff passed. Private evidence: %s\n' "$job"
