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
freetype_source="$component/src/upstream/orangefox-android16/external/freetype"
git clone --shared --no-checkout "$freetype_source" "$scratch/freetype" >/dev/null 2>&1
git -C "$scratch/freetype" checkout --detach d968d2541f7158e18ab22680bfa08a538019bf6a >/dev/null 2>&1
bash "$component/scripts/prepare-reviewed-patches.sh" "$scratch/freetype" apply freetype
bash "$component/scripts/prepare-reviewed-patches.sh" "$scratch/freetype" check freetype
printf '\n/* Unreviewed FreeType sentinel */\n' >> "$scratch/freetype/src/truetype/ttgxvar.c"
before=$(sha256sum "$scratch/freetype/src/truetype/ttgxvar.c" | cut -d' ' -f1)
if bash "$component/scripts/prepare-reviewed-patches.sh" "$scratch/freetype" apply freetype > "$scratch/freetype-refusal.log" 2>&1; then
    echo 'Unknown FreeType source changes were incorrectly accepted' >&2; exit 1
fi
[[ $(sha256sum "$scratch/freetype/src/truetype/ttgxvar.c" | cut -d' ' -f1) == "$before" ]]
bash "$component/scripts/prepare-reviewed-patches.sh" "$freetype_source" check freetype
echo 'Reviewed text source stack and unknown-change preservation passed; disposable clone retained privately.'
