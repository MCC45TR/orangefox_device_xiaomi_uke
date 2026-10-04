#!/usr/bin/env bash
# Host-only source fixture; never reset or overwrite the developer checkout.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source_path="$component/src/upstream/orangefox-android16/bootable/recovery"
scratch=$(mktemp -d "$component/build/text-patch-fixture-XXXXXX")
git clone --shared --no-checkout "$source_path" "$scratch/source" >/dev/null 2>&1
git -C "$scratch/source" checkout --detach 3d733672081bca3af42475a286145f4a8cdce4e7 >/dev/null 2>&1
bash "$component/scripts/prepare-recovery-patches.sh" "$scratch/source"
bash "$component/scripts/prepare-recovery-patches.sh" "$scratch/source" check
printf '\n// unreviewed source sentinel\n' >> "$scratch/source/minuitwrp/truetype.cpp"
before=$(sha256sum "$scratch/source/minuitwrp/truetype.cpp" | cut -d' ' -f1)
if bash "$component/scripts/prepare-recovery-patches.sh" "$scratch/source" > "$scratch/refusal.log" 2>&1; then
    echo 'Unknown source changes were incorrectly accepted' >&2; exit 1
fi
[[ $(sha256sum "$scratch/source/minuitwrp/truetype.cpp" | cut -d' ' -f1) == "$before" ]]
bash "$component/scripts/prepare-recovery-patches.sh" "$source_path" check
echo 'Reviewed text source stack and unknown-change preservation passed; disposable clone retained privately.'
