#!/usr/bin/env bash
# Host-only OrangeFox packaging callback; never installed or run on the tablet.
set -euo pipefail
export LC_ALL=C LANG=C
payload=${1:?Missing staged ramdisk}
phase=${2:?Missing callback phase}
[[ $phase == --first-call ]] || exit 0
[[ $payload == /mnt/out-public/target/product/uke/recovery/root ]] || {
    echo 'Refusing an unexpected ramdisk staging path' >&2; exit 1;
}
[[ -f $payload/system/bin/uke-recoveryctl && -f $payload/system/bin/uke-recovery-install ]]
# An incremental relink target can retain yesterday's renderer while the GUI
# executable already imports today's APIs. Install the exact source-built
# renderer and fail packaging if its monitor entry points are missing.
renderer=/mnt/out-public/target/product/uke/system/lib64/libminuitwrp.so
[[ -f $renderer && ! -L $renderer ]]
for symbol in gr_external_select gr_external_update ure_mirror_select; do
    readelf --dyn-syms --wide "$renderer" | grep -F "$symbol" > /dev/null
done
cp -- "$renderer" "$payload/system/lib64/libminuitwrp.so"
# Make recovery modules with a custom install path do not install every shared
# dependency into the ramdisk. Copy the exact linked, source-built codec.
codec=/mnt/out-public/target/product/uke/system/lib64/libzstd.so
[[ -f $codec && ! -L $codec ]]
readelf -h "$codec" | grep -q 'Machine:.*AArch64'
cp -- "$codec" "$payload/system/lib64/libzstd.so"
cp -- /mnt/device/xiaomi/uke/maintainer.xml "$payload/sbin/maintainer.xml"
mkdir -p "$payload/twres/images/URE"
cp -- /mnt/device/xiaomi/uke/ui-icons/*.png "$payload/twres/images/URE/"
mkdir -p "$payload/system/etc/ure/licenses"
cp -- /mnt/device/xiaomi/uke/ui-icons/LICENSE "$payload/system/etc/ure/licenses/lucide.txt"
cp -- /mnt/device/xiaomi/uke/ure-tools.lock.json "$payload/system/etc/ure/tools.lock.json"
cp -- /mnt/external/ure-wimlib/COPYING "$payload/system/etc/ure/licenses/wimlib.txt"
cp -- /mnt/external/ure-wimlib/COPYING.GPLv3 "$payload/system/etc/ure/licenses/wimlib-GPLv3.txt"
cp -- /mnt/external/ure-dropbear/LICENSE "$payload/system/etc/ure/licenses/dropbear.txt"
cp -- /mnt/external/ure-dropbear/libtomcrypt/LICENSE "$payload/system/etc/ure/licenses/libtomcrypt.txt"
cp -- /mnt/external/ure-dropbear/libtommath/LICENSE "$payload/system/etc/ure/licenses/libtommath.txt"
cp -- /mnt/external/ntfs-3g/COPYING "$payload/system/etc/ure/licenses/ntfs-3g.txt"
cp -- /mnt/external/jsoncpp/LICENSE "$payload/system/etc/ure/licenses/jsoncpp.txt"
cp -- /mnt/external/zstd/LICENSE "$payload/system/etc/ure/licenses/zstd.txt"
cp -- /mnt/external/lzma/NOTICE "$payload/system/etc/ure/licenses/lzma-sdk.txt"
cp -- /mnt/device/xiaomi/uke/recoveryctl/LICENSE-APACHE "$payload/system/etc/ure/licenses/native-Apache-2.0.txt"
# This generic vendor helper depends on an unaccepted KeyMint/TEE stack. FBE is
# disabled in this profile; do not ship a dangling prebuilt decryption helper.
rm -f -- "$payload/system/bin/keystore_cli_v2"
# AOSP filesystem utilities select the bootstrap interpreter path. Resolve it
# to the packaged source-built Bionic loader using a relative runtime link.
[[ ! -L $payload/system/bin/bootstrap ]]
mkdir -p "$payload/system/bin/bootstrap"
ln -sf ../linker64 "$payload/system/bin/bootstrap/linker64"
cp -- /mnt/out-public/target/product/uke/system/etc/mke2fs.conf "$payload/system/etc/mke2fs.conf"
# The generic vendor ps prebuilt requests an absent /sbin linker. Keep its
# public command path while using the locked AOSP Toybox implementation.
ln -sf /system/bin/ps "$payload/FFiles/ps"
# Add one exact navigation entry to the generated locked theme. An unexpected
# upstream page shape must fail packaging rather than hide a missing feature.
advanced="$payload/twres/pages/advanced.xml"
if ! grep -q 'page">ure_home<' "$advanced"; then
    [[ $(grep -c '<listitem name="{@mount_btn}">' "$advanced") == 1 ]]
    sed -i '/<listitem name="{@mount_btn}">/i\                <listitem name="Uke Recovery Environment"><icon res="wrench"/><action function="page">ure_home</action></listitem>' "$advanced"
fi
bash_binary=/mnt/out-public/target/product/uke/system/system_ext/bin/bash
[[ -f $bash_binary && ! -L $bash_binary ]]
readelf -h "$bash_binary" | grep -q 'Machine:.*AArch64'
# The upstream vendor hook can leave a stale prebuilt in an incremental root.
# Always install the locked, source-built shell and its canonical symlink.
cp -- "$bash_binary" "$payload/system/bin/bash"
chmod 755 "$payload/system/bin/bash"
ln -sf /system/bin/bash "$payload/sbin/bash"
if [[ -f $payload/system/bin/dropbear ]]; then
    ln -sf /system/bin/dropbear "$payload/system/bin/dropbearkey"
    # AOSP already provides /bin -> /system/bin. Never follow this absolute
    # link on the build host, where it points outside the staged ramdisk.
    if [[ -L $payload/bin ]]; then
        [[ $(readlink -- "$payload/bin") == /system/bin ]]
    else
        mkdir -p "$payload/bin"
        ln -sf /system/bin/sh "$payload/bin/sh"
    fi
fi

# Ship only the license-identified AOSP static Roboto font. Retain resource
# filenames as aliases so upstream themes do not fail to resolve a font.
font_source=/mnt/external/roboto-fonts/RobotoStatic-Regular.ttf
[[ -s $font_source ]]
for font in "$payload/twres/fonts/"*.ttf; do
    [[ -f $font && ! -L $font ]] || exit 1
    cp -- "$font_source" "$font"
done
cp -- /mnt/external/roboto-fonts/NOTICE "$payload/twres/fonts/LICENSE.txt"

# These generic addons write FRP, vbmeta or encryption settings without Uke
# identity/fallback checks. Do not ship their executable recipes in this alpha.
rm -f -- "$payload/FFiles/OF_DelFRP/OF_DelFRP.zip" \
    "$payload/FFiles/OF_avb10/OF_avb10.zip" \
    "$payload/FFiles/OF_avb20/OF_avb20.zip" \
    "$payload/FFiles/OF_avb20/OF_avb20.sh" \
    "$payload/FFiles/OF_verity_crypt/OF_verity_crypt.zip" \
    "$payload/FFiles/OF_DelFiles/OF_DelFiles.zip" \
    "$payload/FFiles/OF_DelThemes/OF_DelThemes.zip" \
    "$payload/FFiles/OF_Del_Survival/OF_Del_Survival.zip" \
    "$payload/FFiles/OF_backup_settings/OF_backup_settings.zip" \
    "$payload/FFiles/OF_bind_internal/OF_bind_internal.zip" \
    "$payload/FFiles/OF_reset/OF_reset.zip" \
    "$payload/FFiles/Tools/system_sar_mount/system_sar_mount.zip"
echo 'Uke public ramdisk: source-built Bash, licensed Roboto aliases; generic firmware/security-write addons omitted.'
