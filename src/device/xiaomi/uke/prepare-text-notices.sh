#!/usr/bin/env bash
# Host-only packaging: retain original library, per-file and Unicode notices.
set -euo pipefail
export LC_ALL=C
source_root=${1:?Android source root is required}
destination=${2:?Staged license directory is required}
device=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
hb="$source_root/external/ure-harfbuzz"
fb="$source_root/external/ure-fribidi"
for path in "$source_root" "$source_root/external" "$hb" "$hb/src" "$hb/src/ms-use" "$fb" "$fb/lib"; do
    [[ -d $path && ! -L $path ]]
done
for path in "$hb/COPYING" "$hb/src/ms-use/COPYING" "$fb/COPYING" "$device/text-notices/Unicode-LICENSE.txt"; do
    [[ -f $path && ! -L $path ]]
done
[[ $(sha256sum "$device/text-notices/Unicode-LICENSE.txt" | cut -d' ' -f1) == e7a93b009565cfce55919a381437ac4db883e9da2126fa28b91d12732bc53d96 ]]
[[ ! -L $destination ]]
mkdir -p "$destination"
scratch=$(mktemp -d "$destination/.text-notices-XXXXXXXX")
trap 'rm -rf -- "$scratch"' EXIT
cp -- "$hb/COPYING" "$scratch/harfbuzz.txt"
cp -- "$hb/src/ms-use/COPYING" "$scratch/harfbuzz-MS-USE.txt"
cp -- "$fb/COPYING" "$scratch/fribidi-LGPL-2.1.txt"
cp -- "$device/text-notices/Unicode-LICENSE.txt" "$scratch/unicode-data.txt"
# Headers participate in different architecture/feature builds. Preserve the
# source tree's leading copyright blocks as a conservative superset instead
# of silently losing an ISC or Microsoft notice behind a root-level COPYING.
notice_bundle() {
    local root=$1 subdirectory=$2 name=$3 bundle=$4 file
    printf 'Original %s source-file copyright and permission notices.\nSource/header notices are retained conservatively; this is not an additional license grant.\n\n' "$name" > "$bundle"
    [[ -z $(find "$root/$subdirectory" -type l -print -quit) ]]
    while IFS= read -r -d '' file; do
    awk '
        BEGIN { block=0 }
        /^[[:space:]]*\/\*/ { block=1 }
        block { print; if ($0 ~ /\*\//) block=0; next }
        /^[[:space:]]*$/ { next }
        { exit }
    ' "$file" > "$scratch/comment"
    if rg -qi 'copyright' "$scratch/comment"; then
        printf '%s\n' "Source: ${file#"$root/"}" >> "$bundle"
        cat "$scratch/comment" >> "$bundle"
        printf '\n' >> "$bundle"
    fi
    done < <(find "$root/$subdirectory" -type f \( -name '*.c' -o -name '*.cc' -o -name '*.h' -o -name '*.hh' -o -name '*.rl' \) -print0 | sort -z)
    [[ $(wc -c < "$bundle") -le 2097152 ]]
}
notice_bundle "$hb" src HarfBuzz "$scratch/harfbuzz-source-notices.txt"
notice_bundle "$fb" lib FriBidi "$scratch/fribidi-source-notices.txt"
rg -q 'Grigori Goronzy' "$scratch/harfbuzz-source-notices.txt"
rg -q 'Dov Grobgeld' "$scratch/fribidi-source-notices.txt"
for file in harfbuzz.txt harfbuzz-MS-USE.txt harfbuzz-source-notices.txt fribidi-LGPL-2.1.txt fribidi-source-notices.txt unicode-data.txt; do
    [[ ! -L $destination/$file && ( ! -e $destination/$file || -f $destination/$file ) ]]
done
for file in harfbuzz.txt harfbuzz-MS-USE.txt harfbuzz-source-notices.txt fribidi-LGPL-2.1.txt fribidi-source-notices.txt unicode-data.txt; do
    cmp -s -- "$scratch/$file" "$destination/$file" || cp -- "$scratch/$file" "$destination/$file"
done
echo 'Original text-library and Unicode notices retained; release source/relink closure is checked separately.'
