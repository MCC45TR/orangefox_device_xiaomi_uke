#!/usr/bin/env bash
# Exercise the production selector without a tablet or an executable payload.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
source "$component/scripts/touch-payload-init.sh"
work=$(mktemp -d "$component/build/touch-init-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
root="$work/root"
mkdir -p "$root/system/etc/init/hw"
script="$root/system/etc/init/hw/init.rc"
line='    copy /system/etc/ld.config.txt /linkerconfig/ld.config.txt'
refuse() { if touch_payload_init "$root" >/dev/null 2>&1; then echo "Unexpected init acceptance: $1" >&2; exit 1; fi; }
printf '%s\n' "$line" > "$root/init.rc"
refuse 'legacy-only script is not the pinned default'
printf '%s\n' "$line" > "$script"
[[ $(touch_payload_init "$root") == "$script" ]]
printf 'unrelated legacy script\n' > "$root/init.rc"
[[ $(touch_payload_init "$root") == "$script" ]]
printf '    copy /system/etc/ldXconfigYtxt /linkerconfig/ldXconfigYtxt\n' > "$script"
refuse 'lookalike filenames must not match regex punctuation'
printf '%s\n' "$line" "$line" > "$script"
refuse 'duplicate runtime copy'
printf 'on init\n' > "$script"
refuse 'missing runtime copy'
printf '%s\n' "$line" > "$work/outside.rc"
rm "$script"
ln -s "$work/outside.rc" "$script"
refuse 'script symlink'
rm "$script"
mv "$root/system/etc/init/hw" "$work/alias-target"
printf '%s\n' "$line" > "$work/alias-target/init.rc"
ln -s "$work/alias-target" "$root/system/etc/init/hw"
refuse 'parent symlink'
printf 'Pinned modern init selector: 8 controls passed; no device execution.\n'
# Execute the manifest portion of the actual packaging callback in host fixtures.
awk '/^# Without this declaration/ {copy=1} /^# An incremental relink/ {copy=0} copy' \
    "$component/src/device/xiaomi/uke/prepare-public-ramdisk.sh" > "$work/manifest-hook.inc"
[[ -s $work/manifest-hook.inc ]]
cat > "$work/manifest-controls.sh" <<'CONTROLS'
set -euo pipefail
payload=/tmp/touch-manifest/payload
mkdir -p "$payload"
source /tmp/touch-manifest/manifest-hook.inc
source /tmp/touch-manifest/manifest-hook.inc
[[ $(stat -c %a "$manifest") == 644 ]]
printf 'existing unrelated HAL declaration\n' > "$manifest"
cp "$manifest" /tmp/touch-manifest/sentinel
if bash -euc 'payload=/tmp/touch-manifest/payload; source /tmp/touch-manifest/manifest-hook.inc' > /tmp/touch-manifest/refusal.log 2>&1; then exit 1; fi
cmp "$manifest" /tmp/touch-manifest/sentinel
rm "$manifest"
ln -s /tmp/touch-manifest/sentinel "$manifest"
if bash -euc 'payload=/tmp/touch-manifest/payload; source /tmp/touch-manifest/manifest-hook.inc' > /tmp/touch-manifest/refusal.log 2>&1; then exit 1; fi
[[ -L $manifest && $(cat /tmp/touch-manifest/sentinel) == 'existing unrelated HAL declaration' ]]
CONTROLS
bwrap --ro-bind / / --tmpfs /tmp --tmpfs /mnt --ro-bind "$component/src/device/xiaomi/uke" /mnt/device/xiaomi/uke \
    --bind "$work" /tmp/touch-manifest bash /tmp/touch-manifest/manifest-controls.sh
printf '%s\n' 'Touch AIDL manifest: fresh/repeated packaging and preservation of unrelated/symlinked declarations passed on host fixtures.'
