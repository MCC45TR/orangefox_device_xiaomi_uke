#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Exercise exact reviewed additions using a local throwaway pinned checkout.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
work=$(mktemp -d "$component/build/reviewed-additions-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
git clone --quiet --shared --no-hardlinks "$component/src/upstream/orangefox-android16/system/vold" "$work/source"
[[ $(git -C "$work/source" rev-parse HEAD) == 953de9608eb78380b3c4e39e801c2bc0af7dbddc ]]
LC_ALL=tr_TR.UTF-8 LANG=tr_TR.UTF-8 bash "$component/scripts/prepare-reviewed-patches.sh" "$work/source" apply vold
bash "$component/scripts/prepare-reviewed-patches.sh" "$work/source" check vold
[[ -f $work/source/ure-fbe-parser.hpp && -f $work/source/ure-fbe-gcm.hpp ]]
cp "$work/source/ure-fbe-parser.hpp" "$work/original-header"
printf '\n// unreviewed change\n' >> "$work/source/ure-fbe-parser.hpp"
sha256sum "$work/source/Decrypt.cpp" "$work/source/KeyStorage.cpp" "$work/source/ure-fbe-parser.hpp" > "$work/before"
if bash "$component/scripts/prepare-reviewed-patches.sh" "$work/source" apply vold > "$work/refusal" 2>&1; then exit 1; fi
rg -q 'Unknown active source changes' "$work/refusal"
sha256sum --check --quiet "$work/before"
cp "$work/original-header" "$work/source/ure-fbe-parser.hpp"
mv "$work/source/ure-fbe-parser.hpp" "$work/missing-header"
if bash "$component/scripts/prepare-reviewed-patches.sh" "$work/source" check vold > "$work/missing" 2>&1; then exit 1; fi
ln -s "$work/missing-header" "$work/source/ure-fbe-parser.hpp"
if bash "$component/scripts/prepare-reviewed-patches.sh" "$work/source" check vold > "$work/link" 2>&1; then exit 1; fi
rm "$work/source/ure-fbe-parser.hpp"
cp "$work/missing-header" "$work/source/ure-fbe-parser.hpp"
bash "$component/scripts/prepare-reviewed-patches.sh" "$work/source" check vold
printf '%s\n' 'Exact added-header admission, altered/missing/symlink refusals and unknown-source preservation passed.'
