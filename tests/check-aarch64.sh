#!/usr/bin/env bash
# QEMU user-mode fixtures. Synthetic files only; no tablet, mounts or listeners.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
payload=$(realpath -- "${1:?Usage: check-aarch64.sh EXTRACTED_RAMDISK}")
command -v qemu-aarch64 >/dev/null
if [[ ${UKE_QEMU_HOST_KERNEL_BOUND:-0} != 1 ]]; then
    # QEMU's prefix can redirect Root("/") to the extracted ramdisk, whose
    # sys/proc mountpoints are empty on the host. Bind the real host kernel
    # observations read-only so image write gates still exclude live aliases.
    [[ -d $payload/sys && ! -L $payload/sys && -d $payload/proc && ! -L $payload/proc ]]
    exec bwrap --ro-bind / / --dev-bind /dev /dev --bind /tmp /tmp --ro-bind /sys "$payload/sys" --ro-bind /proc "$payload/proc" \
        --setenv UKE_QEMU_HOST_KERNEL_BOUND 1 bash "$component/tests/check-aarch64.sh" "$payload"
fi
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
export UKE_TEST_RAMDISK="$payload"
cat > "$work/recoveryctl" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
exec qemu-aarch64 -L "$UKE_TEST_RAMDISK" \
    -E "LD_LIBRARY_PATH=$UKE_TEST_RAMDISK/system/lib64:$UKE_TEST_RAMDISK/vendor/lib64" \
    "$UKE_TEST_RAMDISK/system/bin/uke-recoveryctl" "$@"
SH
chmod 755 "$work/recoveryctl"
UKE_RECOVERYCTL_BINARY="$work/recoveryctl" bash "$component/tests/check-ure.sh"
UKE_RECOVERYCTL_BINARY="$work/recoveryctl" bash "$component/tests/check-backup.sh"
UKE_RECOVERYCTL_BINARY="$work/recoveryctl" bash "$component/tests/check-tree-backup.sh"
UKE_RECOVERYCTL_BINARY="$work/recoveryctl" bash "$component/tests/check-storage-backup.sh"
UKE_RECOVERYCTL_BINARY="$work/recoveryctl" bash "$component/tests/check-restore.sh"
UKE_RECOVERYCTL_BINARY="$work/recoveryctl" bash "$component/tests/check-stream-restore.sh"
UKE_RECOVERYCTL_BINARY="$work/recoveryctl" bash "$component/tests/check-gpt.sh"
UKE_RECOVERYCTL_BINARY="$work/recoveryctl" bash "$component/tests/check-stock-gpt.sh"
UKE_RECOVERYCTL_BINARY="$work/recoveryctl" bash "$component/tests/check-partition-map.sh"
UKE_RECOVERYCTL_BINARY="$work/recoveryctl" bash "$component/tests/check-layout.sh"
UKE_RECOVERYCTL_BINARY="$work/recoveryctl" bash "$component/tests/check-display.sh"
UKE_RECOVERYCTL_BINARY="$work/recoveryctl" bash "$component/tests/check-boot-audit.sh"
UKE_RECOVERYCTL_BINARY="$work/recoveryctl" bash "$component/tests/check-recoveryctl.sh"
target() {
    local name=$1; shift
    [[ -f $payload/system/bin/$name && ! -L $payload/system/bin/$name ]]
    qemu-aarch64 -L "$payload" -E "LD_LIBRARY_PATH=$payload/system/lib64:$payload/vendor/lib64" \
        -E "MKE2FS_CONFIG=$payload/system/etc/mke2fs.conf" "$payload/system/bin/$name" "$@"
}
target wimlib-imagex --version > "$work/wim-version"
rg -q '1\.14\.5' "$work/wim-version"
target ntfsresize --version > "$work/ntfs-version"
rg -q 'ntfsresize' "$work/ntfs-version"
target dropbear -h > "$work/ssh-help" 2>&1 || [[ $? == 1 ]]
rg -q '2025\.89' "$work/ssh-help"
! rg -q 'Allow.*password|Forwarded connections' "$work/ssh-help"
# Generate an ephemeral key without ever starting the daemon. The key remains
# in the temporary fixture directory and is destroyed on exit.
qemu-aarch64 -0 dropbearkey -L "$payload" -E "LD_LIBRARY_PATH=$payload/system/lib64" \
    "$payload/system/bin/dropbear" -t ed25519 -f "$work/host-key" > "$work/key-output" 2>&1
[[ -s $work/host-key ]]
mkdir "$work/capture" "$work/restore"
printf 'Synthetic recovery WIM fixture\n' > "$work/capture/data.txt"
target wimlib-imagex capture "$work/capture" "$work/fixture.wim" --check --compress=XPRESS --threads=1 > "$work/wim-capture" 2>&1
target wimlib-imagex verify "$work/fixture.wim" > "$work/wim-verify" 2>&1
target wimlib-imagex info "$work/fixture.wim" > "$work/wim-info" 2>&1
target wimlib-imagex apply "$work/fixture.wim" 1 "$work/restore" > "$work/wim-apply" 2>&1
cmp "$work/capture/data.txt" "$work/restore/data.txt"
head -c 512 "$work/fixture.wim" > "$work/truncated.wim"
if target wimlib-imagex verify "$work/truncated.wim" > "$work/wim-negative" 2>&1; then
    echo 'Truncated WIM was accepted' >&2; exit 1
fi
truncate -s 32M "$work/exfat.img"
target mkfs.exfat "$work/exfat.img" > "$work/exfat-mkfs" 2>&1
before=$(sha256sum "$work/exfat.img" | cut -d' ' -f1)
target fsck.exfat -n "$work/exfat.img" > "$work/exfat-check" 2>&1
[[ $before == "$(sha256sum "$work/exfat.img" | cut -d' ' -f1)" ]]
truncate -s 32M "$work/ext4.img"
target mke2fs -t ext4 -F "$work/ext4.img" > "$work/ext4-mkfs" 2>&1
before=$(sha256sum "$work/ext4.img" | cut -d' ' -f1)
target e2fsck -f -n "$work/ext4.img" > "$work/ext4-check" 2>&1
[[ $before == "$(sha256sum "$work/ext4.img" | cut -d' ' -f1)" ]]
truncate -s 32M "$work/ntfs.img"
target mkfs.ntfs -F -Q "$work/ntfs.img" > "$work/ntfs-mkfs" 2>&1
before=$(sha256sum "$work/ntfs.img" | cut -d' ' -f1)
target ntfsresize --info --no-action "$work/ntfs.img" > "$work/ntfs-info" 2>&1
[[ $before == "$(sha256sum "$work/ntfs.img" | cut -d' ' -f1)" ]]
printf 'AArch64 QEMU: file/GPT/raw-image journals, local and host-streamed restore/readback/rollback, six-LUN stock GPT reconstruction, partition maps, display geometry/private settings, verified host receivers and duplex transport mocks, WIM round trip, ext4/exFAT/NTFS no-action checks and SSH key generation passed; no rendering or hardware evidence.\n'
