#!/usr/bin/env bash
# Host-only OrangeFox packaging callback; never installed or run on the tablet.
set -euo pipefail
payload=${1:?Missing staged ramdisk}
phase=${2:?Missing callback phase}
[[ $phase == --first-call ]] || exit 0
[[ $payload == /mnt/out-public/target/product/uke/recovery/root ]] || {
    echo 'Refusing an unexpected ramdisk staging path' >&2; exit 1;
}
[[ -f $payload/system/bin/uke-recoveryctl && -f $payload/system/bin/uke-recovery-install ]]
bash_binary=/mnt/out-public/target/product/uke/system/system_ext/bin/bash
[[ -f $bash_binary && ! -L $bash_binary ]]
readelf -h "$bash_binary" | grep -q 'Machine:.*AArch64'
# The upstream vendor hook can leave a stale prebuilt in an incremental root.
# Always install the locked, source-built shell and its canonical symlink.
cp -- "$bash_binary" "$payload/system/bin/bash"
chmod 755 "$payload/system/bin/bash"
ln -sf /system/bin/bash "$payload/sbin/bash"

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
