#!/usr/bin/env bash
# Replay Android's recorded fs_config conversion instead of equating host
# staging permissions with the permissions encoded by mkbootfs.
set -euo pipefail
export LC_ALL=C
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output=${1:?Build output directory required}
image=${2:?Dedicated recovery image required}
extracted=${3:?Extracted payload root required}
work=${4:?New private working directory required}
[[ -d $output && ! -L $output && -f $image && ! -L $image && -d $extracted && ! -L $extracted && ! -e $work && ! -L $work ]]
# The caller must first authenticate the image and output against the sealed
# receipt, including mkbootfs, lz4, staging and target filesystem configuration.
mkbootfs="$output/host/linux-x86/bin/mkbootfs"
lz4="$output/host/linux-x86/bin/lz4"
staging="$output/target/product/uke/recovery/root"
system="$output/target/product/uke/system"
[[ -x $mkbootfs && ! -L $mkbootfs && -x $lz4 && ! -L $lz4 && -d $staging && ! -L $staging && -d $system && ! -L $system ]]
[[ $(stat -c %s "$image") == 104857600 && $(dd if="$image" bs=1 count=8 status=none) == 'ANDROID!' ]]
[[ $(od -An -tu4 -j8 -N4 "$image" | tr -d ' ') == 0 && $(od -An -tu4 -j40 -N4 "$image" | tr -d ' ') == 4 ]]
bytes=$(od -An -tu4 -j12 -N4 "$image" | tr -d ' ')
[[ $bytes =~ ^[0-9]+$ && $bytes -gt 0 && $bytes -le 65000000 ]]
mkdir -m700 -- "$work"
# fs_config searches these six product siblings. Refuse a configuration that
# resolves outside its recorded product path, including parent symlinks.
output=$(cd -- "$output" && pwd -P)
for partition in system vendor oem odm product system_ext; do
    for kind in files dirs; do
        config="$output/target/product/uke/$partition/etc/fs_config_$kind"
        if [[ -e $config || -L $config ]]; then
            [[ -f $config && ! -L $config && $(realpath -e -- "$config") == "$config" ]] || {
                echo 'Filesystem configuration must be a retained regular product file.' >&2; exit 1;
            }
        fi
    done
done
# A fresh root exposes only the recorded output and the host runtime. Absolute
# /system,/vendor,... fallbacks cannot import unrelated workstation rules.
bwrap --ro-bind /usr /usr --ro-bind /lib64 /lib64 --ro-bind /lib /lib \
    --ro-bind "$output" /mnt --proc /proc --clearenv --setenv LC_ALL C \
    --setenv LD_LIBRARY_PATH /mnt/host/linux-x86/lib64 --chdir /mnt \
    /mnt/host/linux-x86/bin/mkbootfs -d /mnt/target/product/uke/system \
    /mnt/target/product/uke/recovery/root > "$work/canonical.cpio"
[[ $(stat -c %s "$work/canonical.cpio") -le 268435456 ]]
dd if="$image" of="$work/ramdisk.lz4" bs=1M iflag=skip_bytes,count_bytes skip=4096 count="$bytes" status=none
# cmp consumes the complete expected archive. A larger, shorter or malformed
# decompressed stream fails without accepting a partial byte comparison.
"$lz4" -dc "$work/ramdisk.lz4" | cmp -- "$work/canonical.cpio" -
cpio -it --quiet < "$work/canonical.cpio" > "$work/members"
[[ $(wc -l < "$work/members") -le 20000 ]]
while IFS= read -r entry; do
    case "$entry" in /*|../*|*/../*|*/..|*\\*|*$'\r'*) echo 'Unsafe canonical payload path rejected.' >&2; exit 1;; esac
done < "$work/members"
mkdir -m700 -- "$work/root"
bwrap --ro-bind / / --bind "$work/root" /mnt --chdir /mnt \
    cpio -idm --quiet --no-absolute-filenames < "$work/canonical.cpio"
bash "$component/scripts/index-build-tree.sh" "$work/root" "$work/canonical-index" payload
bash "$component/scripts/index-build-tree.sh" "$extracted" "$work/extracted-index" payload
diff -qr -- "$work/canonical-index" "$work/extracted-index" >/dev/null || {
    echo 'Extracted payload content, members, modes or links differ from the canonical archive.' >&2; exit 1;
}
printf 'Packed payload matches exact mkbootfs/fs_config replay, including contents, modes and links.\n'
