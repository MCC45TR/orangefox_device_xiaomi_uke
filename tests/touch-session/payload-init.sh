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
